#ifndef MAP_READ_H_
#define MAP_READ_H_
#include "MMIndex.h"
#include "Genome.h"
#include "Read.h"
#include "Options.h"
#include "CompareLists.h"
#include "Sorting.h"
#include "TupleOps.h"
#include <iostream>
#include <algorithm>
#include <iterator>
#include <ctime>
#include <cmath>	// std::log 

#include <sstream>
#include <thread>


#include "Clustering.h"
#include <thread>

#include "AffineOneGapAlign.h"
#include "GlobalChain.h"
#include "TupleOps.h"
#include "SparseDP.h"
#include "Merge.h"
#include "MergeAnchors.h"
#include "SparseDP_Forward.h"
#include "Chain.h"
#include "overload.h"
#include "LinearExtend.h"
#include "SplitClusters.h"

using namespace std;


void SwapStrand (Read & read, Options & opts, Cluster & cluster) {
	for (int m = 0; m < cluster.matches.size(); m++) {
		cluster.matches[m].first.pos = read.length - (cluster.matches[m].first.pos + opts.globalK);
	}
	GenomePos r = cluster.qStart;
	cluster.qStart = read.length - cluster.qEnd;
	cluster.qEnd = read.length - r;
}


void SwapStrand(Read &read, Options &opts, GenomePairs &matches) {
	for (int m=0; m < matches.size(); m++) {
		matches[m].first.pos = read.length - (matches[m].first.pos + opts.globalK);
	}
}

void SwapStrand(Read &read, Options &opts, GenomePairs &matches, int start, int end) {
	for (int m=start; m<end; m++) {
		matches[m].first.pos = read.length - (matches[m].first.pos + opts.globalK);
	}
}


int Matched(GenomePos qs, GenomePos qe, GenomePos ts, GenomePos te) {
	return min(qe-qs+1, te-ts+1); // TODO(Jingwen): check whether should add 1
}

void SetMatchAndGaps(GenomePos qs, GenomePos qe, GenomePos ts, GenomePos te, int &m, int &qg, int &tg) {
	m=Matched(qs, qe, ts, te);
	qg=qe-qs+1-m; //TODO(Jingwen): check whether should add 1
	tg=te-ts+1-m;
}


void RemoveOverlappingClusters(vector<Cluster> &clusters, Options &opts) {
	int a=0;
	int ovp=a;
	if (clusters.size() == 0) {
		return;
	}
	while (a < clusters.size() - 1) {
		ovp=a+1;
		float num=1.0;
		float denom=1.0;

		while ( ovp < clusters.size() and clusters[a].Overlaps(clusters[ovp], 0.8 ) ) {
			ovp++;
		}
		if (ovp - a > opts.maxCandidates) {
			for (int i=a+opts.maxCandidates; i < ovp; i++) {
				clusters[i].matches.clear();
			}
		}
		a=ovp;
	}
	int c=0;
	for (int i=0; i < clusters.size(); i++) {
		if (clusters[i].matches.size() > 0) {
			clusters[c] = clusters[i];
			c++;
		}
	}
	clusters.resize(c);
}

void SimpleMapQV(vector<SegAlignmentGroup> &alignments) {
	int a=0;
	int ovp=a;
	if (alignments.size() == 0) {
		return;
	}
	if (alignments.size() == 1) {
		alignments[a].mapqv=255;
		alignments[a].SetMapqv(); // give mapq 255 to every segment
		return;
	}
	while (a < alignments.size() - 1) {
		ovp=a+1;
		float num=1.0;
		float denom=1.0;

		while (ovp < alignments.size() and alignments[a].Overlaps(alignments[ovp], 0.9) ) {
			int nmDiff = alignments[a].nm - alignments[ovp].nm;
			if (nmDiff < 10) {
				denom += pow(0.5,nmDiff);
			}
			ovp++;
		}
		if (ovp == a+1){
			alignments[a].mapqv=255;
		}
		else {
			// comparing float ok because it is potentially not set.
			if (denom == 1.0) {
				alignments[a].mapqv=255;
			}
			else {
				alignments[a].mapqv=-10*log10(1-num/denom);
			}
		}
		alignments[a].SetMapqv();
		a=ovp;
	}			
}

int AlignSubstrings(char *qSeq, GenomePos &qStart, GenomePos &qEnd, char *tSeq, GenomePos &tStart, GenomePos &tEnd,
										vector<int> &scoreMat, vector<Arrow> &pathMat, Alignment &aln, Options &options) {
	
	int qLen = qEnd-qStart;
	int tLen = tEnd-tStart;
	int drift = abs(qLen - tLen);
	int k = max(7, drift+1);
	

	
	/*
	int score = KBandAlign(&qSeq[qStart], qEnd-qStart, &tSeq[tStart], tEnd-tStart, 
												 -5,3,2,2, k, // make these smart later.
												 scoreMat, pathMat, aln);*/

	string readSeq(&qSeq[qStart], qEnd-qStart);
	string chromSeq(&tSeq[tStart],tEnd-tStart);

	int score = AffineOneGapAlign(readSeq, chromSeq, options.localMatch, options.localMismatch, options.localIndel, options.localBand, aln);
	return score;
}

class SortClusterBySize {
public:
	bool operator()(Cluster &a, Cluster &b) {
		return a.matches.size() > b.matches.size();
	}
};

class SortAlignmentsByMatches {
public:
	bool operator()(const SegAlignmentGroup a, const SegAlignmentGroup b) const {
		return a.nm > b.nm;
	}
};


void RankClustersByScore(vector<Cluster> &clusters) {
	sort(clusters.begin(), clusters.end(), SortClusterBySize());
}

int SetStrand(Read &read, Genome &genome, Options &opts, GenomePairs &matches) { 
	int nSame=0;
	int nDifferent=0;
	for (int m=0; m< matches.size(); m++) {
		int chromIndex = genome.header.Find(matches[m].second.pos);
		char *chrom=genome.seqs[chromIndex];
		int chromPos = matches[m].second.pos - genome.header.pos[chromIndex];
		GenomeTuple readTup, genomeTup;
		StoreTuple(read.seq, matches[m].first.pos, opts.globalK, readTup);
		StoreTuple(chrom, chromPos, opts.globalK, genomeTup);
		if (readTup.t == genomeTup.t) {
			nSame++;
		}
		else {
			nDifferent++;
		}
	}
	if (nSame > nDifferent) {
		return 0;
	}
	else {
		return 1;
	}
}
template<typename T>
void SwapReadCoordinates(vector<T> &matches,
												 GenomePos readLength, GenomePos kmer){

	for (int i=0; i < matches.size(); i++) {
		matches[i].first.pos = readLength - (matches[i].first.pos+ kmer);
	}
}

void ReverseClusterStrand(Read &read, Genome &genome, Options &opts, 
											vector<Cluster> &clusters) {
	for (int c = 0; c < clusters.size(); c++) {
			SwapStrand(read, opts, clusters[c].matches);
			clusters[c].strand = 1;
	}
}


void SetClusterStrand(Read &read, Genome &genome, Options &opts, 
											vector<Cluster> &clusters) {
	for (int c = 0; c < clusters.size(); c++) {
		clusters[c].strand = SetStrand(read, genome, opts, clusters[c].matches);
		if (clusters[c].strand == 1) {
			SwapStrand(read, opts, clusters[c].matches);
		}
	}
}
template<typename T>
void UpdateBoundaries(T &matches, 
											GenomePos &qStart, GenomePos &qEnd, 
											GenomePos &tStart, GenomePos &tEnd) {
	for (int i =0; i< matches.size(); i++) {
		qStart=min(qStart, matches[i].first.pos);
		qEnd=max(qEnd, matches[i].first.pos);
		tStart=min(tStart, matches[i].second.pos);
		tEnd=max(tEnd, matches[i].second.pos);
	}
}
void RemoveEmptyClusters(vector<Cluster> &clusters, int minSize=1) {
	int cCur=0;
	for(int c=0; c<clusters.size(); c++) {
		if (clusters[c].tEnd== 0 or clusters[c].matches.size() < minSize ) {
			continue;
		}
		else {
					clusters[cCur] = clusters[c];
					cCur++;
		}
	}
	if (cCur < clusters.size() ) {
		clusters.resize(cCur);
	}
}

void MergeAdjacentClusters(ClusterOrder &order, Genome &genome, Options &opts) {
	int c=0;
	int cn=0;
	c=0;
	//cerr << "merging " << order.size() << " clusters" << endl;
	while(c < order.size()) {
		//cerr << "c: " << c << endl;
		//cerr << "Order[c]: " << order[c].qStart << "\t" << order[c].qEnd << "\t" << order[c].tStart << "\t" << order[c].tEnd << endl;
  		cn=c+1;
		int curEndChrom = genome.header.Find(order[c].tEnd);
		while (cn < order.size()) {
			//cerr <<"cn: " << cn<< endl;
			//cerr << "Order[cn]: " << order[cn].qStart << "\t" << order[cn].qEnd << "\t" << order[cn].tStart << "\t" << order[cn].tEnd << endl;
			int nextStartChrom = genome.header.Find(order[cn].tStart);
			int gap;
			//cerr << "(int)(order[cn].tStart - order[cn].qStart): " << (int)(order[cn].tStart - order[cn].qStart) << endl;
			//cerr << "(int)(order[c].tEnd-order[c].qEnd): " << (int)(order[c].tEnd-order[c].qEnd) << endl;
			gap = abs((int)((int)(order[cn].tStart - order[cn].qStart) - (int)(order[c].tEnd-order[c].qEnd)));

			// TODO(Jingwen): (gap < opts.maxDiag or order[c].Encompasses(order[cn],0.5)) or gap < opts.maxDiag???
			// (gap < opts.maxDiag or order[c].Encompasses(order[cn],0.5)) --> repetitive region will be merged into one cluster
			if (nextStartChrom == curEndChrom and order[c].strand == order[cn].strand and (gap < opts.maxDiag or order[c].Encompasses(order[cn],0.5))) {
				//cerr << "if happened " << endl;
				order[c].matches.insert(order[c].matches.end(), order[cn].matches.begin(), order[cn].matches.end());
				order[c].qEnd = order[cn].qEnd;
				order[c].tEnd = order[cn].tEnd;
				order[cn].tEnd=0;
				order[cn].tStart=0;
				order[cn].matches.clear();
				cn++;
			}
			else {

				int cn2=cn;
				int MAX_AHEAD=10;
				while (cn2 < order.size() and 
							 cn2-cn < MAX_AHEAD and 
							 nextStartChrom == curEndChrom and 
							 order[c].strand == order[cn2].strand and 
							 order[c].Encompasses(order[cn2],0.5) == false) {
					gap = abs((int)((int)(order[cn2].tStart - order[cn2].qStart) - (int)(order[c].tEnd-order[c].qEnd)));
					nextStartChrom = genome.header.Find(order[cn2].tStart);
					if (gap < opts.maxGap) {
						break;
					}
					cn2++;
				}
				if (cn2 < order.size() and
						cn2 - cn < MAX_AHEAD and
						cn2 > cn and gap < opts.maxGap) {
					cn=cn2;
					order[c].matches.insert(order[c].matches.end(), order[cn].matches.begin(), order[cn].matches.end());
					order[c].qEnd = order[cn].qEnd;
					order[c].tEnd = order[cn].tEnd;
					order[cn].tEnd=0;
					order[cn].matches.clear();
					cn++;
				}
				else {
					break;
				}
			}
		}
		c=cn;
	}
}

void MergeOverlappingClusters(ClusterOrder &order) {
	int cCur = 0;
	while(cCur < order.size()){
		int cNext;
		
		cNext = cCur + 1;
		while ( cNext < order.size() and
						order[cNext].OverlapsPrevious(order[cCur])) {
			order[cCur].matches.insert(order[cCur].matches.end(),
															order[cNext].matches.begin(),
															order[cNext].matches.end());
			order[cCur].UpdateBoundaries(order[cNext]);
			//
			// Signal to remove cm;
			//
			order[cNext].start=0;
			order[cNext].end=0;
			cNext+=1;
		}
		cCur=cNext;
	}
	//
	// Remove merged clusters.
	//
	RemoveEmptyClusters(*order.clusters);
}

void RefineSubstrings(char *read, GenomePos readSubStart, GenomePos readSubEnd, char *genome, GenomePos genomeSubStart, GenomePos genomeSubEnd, 
											vector<int> &scoreMat, vector<Arrow> &pathMat, Alignment &aln, Options &opts) {

	aln.blocks.clear();
	AlignSubstrings(read, readSubStart, readSubEnd, genome, genomeSubStart, genomeSubEnd, scoreMat, pathMat, aln, opts);
	for (int b = 0; b < aln.blocks.size(); b++) {
		aln.blocks[b].qPos += readSubStart;
		aln.blocks[b].tPos += genomeSubStart;

	}
	
}

void SeparateMatchesByStrand(Read &read, Genome &genome, int k, vector<pair<GenomeTuple, GenomeTuple> > &allMatches,  vector<pair<GenomeTuple, GenomeTuple> > &forMatches,
								vector<pair<GenomeTuple, GenomeTuple> > &revMatches) {
	//
	// A value of 0 implies forward strand match.
	//
	vector<bool> strand(allMatches.size(), 0);
	int nForward=0;
	for (int i=0; i < allMatches.size(); i++) {
		int readPos = allMatches[i].first.pos;
		uint64_t refPos = allMatches[i].second.pos;
		char *genomePtr=genome.GlobalIndexToSeq(refPos);
		//
		// Read and genome are identical, the match is in the forward strand
		if (strncmp(&read.seq[readPos], genomePtr, k) == 0) {
			nForward++;
		}
		else {
			//
			// The k-mers are not identical, but a match was stored between
			// readPos and *genomePtr, therefore the match must be reverse.
			//
			strand[i] = true;
		}
	}
	//
	// Populate two lists, one for forward matches one for reverse.
	//
	forMatches.resize(nForward);
	revMatches.resize(allMatches.size()-nForward);
	int i=0,r=0,f=0;
	for (i=0,r=0,f=0; i < allMatches.size(); i++) {
		if (strand[i] == 0) {
			forMatches[f] = allMatches[i];
			f++;
		}
		else {
			revMatches[r] = allMatches[i];
			r++;
		}
	}
}

// Revised: output strand for each matches
void SeparateMatchesByStrand(Read &read, Genome &genome, int k, vector<pair<GenomeTuple, GenomeTuple> > &allMatches, vector<bool> & strand) {
	//
	// A value of 0 implies forward strand match.
	//
	int nForward=0;
	for (int i=0; i < allMatches.size(); i++) {
		int readPos = allMatches[i].first.pos;
		uint64_t refPos = allMatches[i].second.pos;
		char *genomePtr=genome.GlobalIndexToSeq(refPos);
		//
		// Read and genome are identical, the match is in the forward strand
		if (strncmp(&read.seq[readPos], genomePtr, k) == 0) {
			nForward++;
		}
		else {
			//
			// The k-mers are not identical, but a match was stored between
			// readPos and *genomePtr, therefore the match must be reverse.
			//
			strand[i] = true;
		}
	}
}

void traceback(vector<int> &clusterchain, int &i, vector<int> &clusters_predecessor, vector<bool> &used) {

	if (used[i] == 0) {
		clusterchain.push_back(i);	
		used[i] = 1;	
		if (clusters_predecessor[i] != -1) {
			if (used[clusters_predecessor[i]] == 0) {
				traceback(clusterchain, clusters_predecessor[i], clusters_predecessor, used);			
			}
			else {
				for (int lr = 0; lr < clusterchain.size(); ++lr) {
					used[clusterchain[lr]] = 0;
				}	
				clusterchain.clear();		
			}
		}
	}
}

// TODO(Jingwen): delete this function
void traceback (vector<int> &onechain, int &i, vector<int> &clusters_predecessor) {

	onechain.push_back(i);
	if (clusters_predecessor[i] != -1) {
		traceback(onechain, clusters_predecessor[i], clusters_predecessor);
	}
}


// This function removes spurious MERGED anchors in chain after 1st SDP
void RemoveSpuriousAnchors (vector<unsigned int> &chain, Options &opts, const vector<ClusterCoordinates> &Anchors, 
								const LogCluster &logcluster) {
	int cs = 0, ce = 1;
	vector<bool> remove(chain.size(), false);

	while (ce < chain.size()) {

		int dist = 0;
		int diffstrand = 0;
		if (Anchors[chain[ce-1]].strand == Anchors[chain[ce]].strand and Anchors[chain[ce-1]].strand == 0) { // forward stranded
			dist = max((int)Anchors[chain[ce]].qStart - Anchors[chain[ce-1]].qEnd, (int)Anchors[chain[ce]].tStart - Anchors[chain[ce-1]].tEnd);
		}
		else if (Anchors[chain[ce-1]].strand == Anchors[chain[ce]].strand and Anchors[chain[ce-1]].strand == 1) { // reverse stranded
			dist = max((int)Anchors[chain[ce]].qStart - Anchors[chain[ce-1]].qEnd, (int)Anchors[chain[ce-1]].tStart - Anchors[chain[ce]].tEnd);
		}
		else diffstrand = 1;

		//cerr << "ce-1: " << ce-1 << "  ce: " << ce << "  abs(dist): " << abs(dist) << "  diffstrand: " << diffstrand <<  endl;
		if (diffstrand == 0 and abs(dist) <= opts.maxRemoveSpuriousAnchorsDist) ce++;
		else {
			//cerr << "else" << endl;
			int anchorNum = 0, Length = 0;
			for (int i = cs; i < ce; i++) {
				anchorNum += Anchors[chain[i]].end - Anchors[chain[i]].start;
			}
			Length = min((int)Anchors[chain[ce - 1]].qEnd - Anchors[chain[cs]].qStart, (int)Anchors[chain[ce - 1]].tEnd - Anchors[chain[cs]].tStart);
			int coarse = Anchors[chain[ce-1]].coarseSubCluster; // Assume chain[cs]... chain[ce-1] are in the same SubCluster
			if ((anchorNum < opts.minRemoveSpuriousAnchorsNum or abs(Length) < opts.minRemoveSpuriousAnchorsLength) and 
				       (float)anchorNum/(logcluster.SubCluster[coarse].end - logcluster.SubCluster[coarse].start) < 0.05) { 
				//
				// The third condition enables small inversion or small stretches of matches 

				//cerr << "cs: " << cs << "  ce: " << ce << endl;
				//cerr << "anchorNum: " << anchorNum << " Length: " << Length << endl;
				//cerr << "ce - cs < min" << endl;
				for (int i = cs; i < ce; i++) {
					remove[i] = true;
				}
			}
			cs = ce;
			ce++;
		}
		//cerr << "cs: " << cs << "  ce: " << ce << endl;
	}
	if (ce == chain.size() and cs < chain.size()) {
		int anchorNum = 0, Length = 0;
		for (int i = cs; i < ce; i++) {
			anchorNum += Anchors[chain[i]].end - Anchors[chain[i]].start;
		}
		Length = min((int)Anchors[chain[ce - 1]].qEnd - Anchors[chain[cs]].qStart, (int)Anchors[chain[ce - 1]].tEnd - Anchors[chain[cs]].tStart);
		int coarse = Anchors[chain[ce-1]].coarseSubCluster;
		if ((anchorNum < opts.minRemoveSpuriousAnchorsNum or abs(Length) < opts.minRemoveSpuriousAnchorsLength) and 
			 (float)anchorNum/(logcluster.SubCluster[coarse].end - logcluster.SubCluster[coarse].start) < 0.05) {
			for (int i = cs; i < ce; i++) {
				remove[i] = true;
			}
		}		
	}

	int m = 0;
	for (int s = 0; s < chain.size(); s++) {
		if (remove[s] == false) {
			chain[m] = chain[s];
			m++;
		}
	}
	chain.resize(m);
}



// This function removes spurious UNMERGED anchors in chain after 1st SDP
void RemoveSpuriousAnchors (vector<unsigned int> &chain, Options &opts, const Cluster &Anchors, const LogCluster &logcluster) {
	int cs = 0, ce = 1;
	vector<bool> remove(chain.size(), false);

	while (ce < chain.size()) {

		int dist = 0;
		int diffstrand = 0;
		if (Anchors.strands[chain[ce-1]] == Anchors.strands[chain[ce]] and Anchors.strands[chain[ce-1]] == 0) { // forward stranded
			dist = max((int)Anchors.matches[chain[ce]].first.pos - Anchors.matches[chain[ce-1]].first.pos - opts.globalK, 
					   (int)Anchors.matches[chain[ce]].second.pos - Anchors.matches[chain[ce-1]].second.pos - opts.globalK);
		}
		else if (Anchors.strands[chain[ce-1]] == Anchors.strands[chain[ce]] and Anchors.strands[chain[ce-1]] == 1) { // reverse stranded
			dist = max((int)Anchors.matches[chain[ce]].first.pos - Anchors.matches[chain[ce-1]].first.pos - opts.globalK, 
				       (int)Anchors.matches[chain[ce-1]].second.pos - Anchors.matches[chain[ce]].second.pos - opts.globalK);
		}
		else diffstrand = 1;

		//cerr << "ce-1: " << ce-1 << "  ce: " << ce << "  dist: " << dist << endl;
		if (diffstrand == 0 and abs(dist) <= opts.maxRemoveSpuriousAnchorsDist) ce++;
		else {
			int coarse = Anchors.coarseSubCluster[chain[ce-1]];
			if (ce - cs < opts.minRemoveSpuriousAnchorsNum and 
					(float)(ce - cs)/(logcluster.SubCluster[coarse].end - logcluster.SubCluster[coarse].start) < 0.05) {
				//cerr << "cs: " << cs << "  ce: " << ce << endl;
				//cerr << "ce - cs: " << ce- cs << " logcluster.SubCluster[coarse].end - logcluster.SubCluster[coarse].start: " << 
				//		logcluster.SubCluster[coarse].end - logcluster.SubCluster[coarse].start << endl;
				//cerr << "ce - cs < min" << endl;
				for (int i = cs; i < ce; i++) {
					remove[i] = true;
				}
			}
			cs = ce;
			ce++;
		}
	}
	if (ce == chain.size() and cs < chain.size()) {
		int coarse = Anchors.coarseSubCluster[chain[ce-1]];
		if (ce - cs < opts.minRemoveSpuriousAnchorsNum and 
				(float)(ce - cs)/(logcluster.SubCluster[coarse].end - logcluster.SubCluster[coarse].start) < 0.05) {
				//cerr << "cs: " << cs << "  ce: " << ce << endl;
				//cerr << "ce - cs: " << ce - cs << " logcluster.SubCluster[coarse].end - logcluster.SubCluster[coarse].start: " << 
				//	logcluster.SubCluster[coarse].end - logcluster.SubCluster[coarse].start << endl;
			for (int i = cs; i < ce; i++) {
				remove[i] = true;
			}
		}		
	}

	int m = 0;
	for (int s = 0; s < chain.size(); s++) {
		if (remove[s] == false) {
			chain[m] = chain[s];
			m++;
		}
	}
	chain.resize(m);
}


// This function removes spurious UNMERGED anchors in chain after 2nd SDP
void RemoveSpuriousAnchors(vector<unsigned int> &chain, Options &opts, const GenomePairs &Anchors) {
	int cs = 0, ce = 1;
	vector<bool> remove(chain.size(), false);

	while (ce < chain.size()) {

		int dist = 0;
		dist = max((int)Anchors[chain[ce]].first.pos - Anchors[chain[ce-1]].first.pos - opts.globalK, 
				   (int)Anchors[chain[ce]].second.pos - Anchors[chain[ce-1]].second.pos - opts.globalK);

		//cerr << "ce-1: " << ce-1 << "  ce: " << ce << "  dist: " << dist << endl;
		if (abs(dist) <= opts.maxRemoveSpuriousAnchorsDist) ce++;
		else {
			if (ce - cs < opts.minRemoveSpuriousAnchorsNum) {
				//cerr << "cs: " << cs << "  ce: " << ce << endl;
				//cerr << "anchorNum: " << anchorNum << " Length: " << Length << endl;
				//cerr << "ce - cs < min" << endl;
				for (int i = cs; i < ce; i++) {
					remove[i] = true;
				}
			}
			cs = ce;
			ce++;
		}
	}
	if (ce == chain.size() and cs < chain.size()) {
		if (ce - cs < opts.minRemoveSpuriousAnchorsNum) {
			for (int i = cs; i < ce; i++) {
				remove[i] = true;
			}
		}		
	}

	int m = 0;
	for (int s = 0; s < chain.size(); s++) {
		if (remove[s] == false) {
			chain[m] = chain[s];
			m++;
		}
	}
	chain.resize(m);	
}


//
// This function switches index in splitclusters back 
//
void 
switchindex (vector<Cluster> & splitclusters, vector<Primary_chain> & Primary_chains) {
	for (int p = 0; p < Primary_chains.size(); p++) {
		for (int h = 0; h < Primary_chains[p].chains.size(); h++) {
			for (int c = 0; c < Primary_chains[p].chains[h].ch.size(); c++) {
				Primary_chains[p].chains[h].ch[c] = splitclusters[Primary_chains[p].chains[h].ch[c]].coarse;
			}
		}
	}

	// Remove the dupplicates 
	for (int p = 0; p < Primary_chains.size(); p++) {
		for (int h = 0; h < Primary_chains[p].chains.size(); h++) {
	  		vector<unsigned int>::iterator itp;
	  		itp = unique(Primary_chains[p].chains[h].ch.begin(), Primary_chains[p].chains[h].ch.end()); 
	  		Primary_chains[p].chains[h].ch.resize(distance(Primary_chains[p].chains[h].ch.begin(), itp));			
		}
	}

}


//
// This function decide the chromIndex
//
int
CHROMIndex(Cluster & cluster, Genome & genome) {

	GenomePos tPos = cluster.tStart;
	int firstChromIndex = genome.header.Find(tPos);
	int lastChromIndex;
	tPos = cluster.tEnd;
	lastChromIndex = genome.header.Find(tPos);
	if (firstChromIndex != lastChromIndex ) {
		return 1;
	}
	cluster.chromIndex = firstChromIndex;  
	return 0;
}


//
// This function refines the Clusters in chain and store refined anchors in refinedClusters
// NOTICE: Inside this function, we need to flip reversed Cluster into forward direction to find refined matches;
// And flip them back after the refining step;
//
void 
REFINEclusters(vector<Cluster> & clusters, vector<Cluster> & refinedclusters, Genome & genome, Read & read,  LocalIndex & glIndex, LocalIndex *localIndexes[2], Options & smallOpts, Options & opts) {


	for (int ph = 0; ph < clusters.size(); ph++) {

		//
		// Get the boundaries of the cluster in genome sequence.
		//
		if (clusters[ph].matches.size() == 0) continue;
		if (clusters[ph].refined == 1) continue; // this Cluster has been refined;

		int pass = CHROMIndex(clusters[ph], genome);
		if (pass == 1) {
			clusters[ph].matches.clear();
			continue;			
		}
		refinedclusters[ph].chromIndex = clusters[ph].chromIndex;
		clusters[ph].refined = 1;


		//
		// Make the anchors reference this chromosome for easier bookkeeping 
		// NOTICE: Remember to add chromOffset back in refinedclusters
		//
		GenomePos chromOffset = genome.header.pos[clusters[ph].chromIndex];
		for (int m = 0; m < clusters[ph].matches.size(); m++) {
			clusters[ph].matches[m].second.pos -= chromOffset;
		}
		GenomePos GenomeClusterEnd = clusters[ph].tEnd;
		GenomePos chromEndOffset = genome.header.GetNextOffset(GenomeClusterEnd);


		//
		// If the current Cluster is reversed stranded, swap the anchors and GenomePos to forward strand; This is for finding refined anchors;
		// NOTICE: Need to flip such Cluster back into reversed strand;
		//
		if (clusters[ph].strand == 1) SwapStrand(read, opts, clusters[ph]);

		// 
		// Decide the diagonal band for each clusters[ph]
		// Find the digonal band that each clusters[ph] is in; NOTICE: here every diagnoal have already subtracted chromOffset, so it's in the same scale with local matches
		// 
		long long int maxDN, minDN;
		maxDN = (long long int) clusters[ph].matches[0].first.pos - (long long int) clusters[ph].matches[0].second.pos;
		minDN = maxDN;
		for (int db = 0; db < clusters[ph].matches.size(); db++) {
			maxDN = max(maxDN, (long long int)clusters[ph].matches[db].first.pos - (long long int)clusters[ph].matches[db].second.pos);
			minDN = min(minDN, (long long int)clusters[ph].matches[db].first.pos - (long long int)clusters[ph].matches[db].second.pos);
		}						
		clusters[ph].maxDiagNum = maxDN + 20;
		clusters[ph].minDiagNum = minDN - 20;


		//
		// Get shorthand access to alignment boundaries.
		//
		CartesianTargetSort<GenomeTuple>(clusters[ph].matches.begin(), clusters[ph].matches.end()); // sorted by second.pos and then first.pos
		GenomePos genomeClusterSegStart, genomeClusterSegEnd;
		genomeClusterSegStart = clusters[ph].tStart;
		genomeClusterSegEnd = clusters[ph].tEnd;


		//
		// Search region starts in window, or beginning of chromosome
		//
		int ls, le;
		GenomePos wts, wte;
		if (chromOffset + smallOpts.window > genomeClusterSegStart) {
			wts = chromOffset;
		}
		else {
			wts = genomeClusterSegStart - smallOpts.window;
		}
				
		if (genomeClusterSegEnd + smallOpts.window > chromEndOffset) {
			wte = chromEndOffset-1;
		}
		else {
			wte = genomeClusterSegEnd + smallOpts.window;
		}
			
		ls = glIndex.LookupIndex(wts);
		le = glIndex.LookupIndex(wte);

		
		// 
		// Get quick access to the local index
		//
		LocalIndex *readIndex;
		readIndex = localIndexes[clusters[ph].strand];


		for (int lsi = ls; lsi <= le; lsi++) {
			//
			// Find the coordinates in the cluster fragment that start in this local index.
			//
			GenomePos genomeLocalIndexStart = glIndex.seqOffsets[lsi]  - chromOffset;
			GenomePos genomeLocalIndexEnd   = glIndex.seqOffsets[lsi+1] - 1 - chromOffset;

			int matchStart = CartesianTargetLowerBound<GenomeTuple>(clusters[ph].matches.begin(), clusters[ph].matches.end(), genomeLocalIndexStart);

			int matchEnd = CartesianTargetUpperBound<GenomeTuple>(clusters[ph].matches.begin(), clusters[ph].matches.end(), genomeLocalIndexEnd);

			//
			// If there is no overlap with this cluster
			if (matchStart >= clusters[ph].end) {
				continue;
			}
			GenomePos readStart = clusters[ph].matches[matchStart].first.pos;
			if (lsi == ls) {
				if (readStart < smallOpts.window) {
					readStart = 0;
				}
				else {
					readStart -= smallOpts.window;
				}
			}
			GenomePos readEnd;
			if (matchEnd > matchStart) {
				readEnd = clusters[ph].matches[matchEnd - 1].first.pos;
			}
			else {
				readEnd = clusters[ph].matches[matchStart].first.pos + opts.globalK;
			}

			//
			// Expand boundaries of read to match.
			if (lsi == le) {
				if (readEnd + smallOpts.window > read.length) {
					readEnd = read.length; 
				}
				else { 
					readEnd += smallOpts.window;	
				}
			}			
				
			//
			// Find the boundaries where in the query the matches should be added.
			//
			int queryIndexStart = readIndex->LookupIndex(readStart);
			int queryIndexEnd = readIndex->LookupIndex(min(readEnd, (GenomePos) read.length-1));
			assert(queryIndexEnd < readIndex->seqOffsets.size()+1);

			for (int qi = queryIndexStart; qi <= queryIndexEnd; ++qi){ 
				
				LocalPairs smallMatches;
				GenomePos qStartBoundary = readIndex->tupleBoundaries[qi];
				GenomePos qEndBoundary   = readIndex->tupleBoundaries[qi+1];
				GenomePos readSegmentStart= readIndex->seqOffsets[qi];
				GenomePos readSegmentEnd  = readIndex->seqOffsets[qi+1];

				CompareLists<LocalTuple>(readIndex->minimizers.begin() + qStartBoundary, readIndex->minimizers.begin() + qEndBoundary, 
												glIndex.minimizers.begin()+ glIndex.tupleBoundaries[lsi], glIndex.minimizers.begin()+ glIndex.tupleBoundaries[lsi+1], 
														smallMatches, smallOpts);
				//
				// Add refined anchors if they fall into the diagonal band and cluster box
				//
				AppendValues<LocalPairs>(refinedclusters[ph].matches, smallMatches.begin(), smallMatches.end(), readSegmentStart, genomeLocalIndexStart,
							 clusters[ph].maxDiagNum, clusters[ph].minDiagNum, clusters[ph].qStart, clusters[ph].qEnd, 
							 		clusters[ph].tStart - chromOffset, clusters[ph].tEnd- chromOffset);
			}
		}
		if (clusters[ph].strand == 1) SwapStrand(read, smallOpts, refinedclusters[ph]);
		refinedclusters[ph].SetClusterBoundariesFromMatches(smallOpts);
		refinedclusters[ph].strand = clusters[ph].strand;
		//refinedclusters[ph].minDiagNum = clusters[ph].minDiagNum;
		//refinedclusters[ph].maxDiagNum = clusters[ph].maxDiagNum;
		refinedclusters[ph].coarse = -1;
		refinedclusters[ph].refinespace = 0;
	}
}


//
// This function find anchors btwn two adjacent Clusters;
//
int 			
RefineBtwnSpace(Cluster * cluster, Options & opts, Genome & genome, Read & read, char *strands[2], GenomePos qe, GenomePos qs, 
							GenomePos te, GenomePos ts, GenomePos st, int cur) {

	int ChromIndex = cluster->chromIndex;

	// 
	// If st == 1, then we need to flip this Cluster, since the following code of fining matches requiers that;
	//
	if (st == 1) {
		GenomePos t = qs;
		qs = read.length - qe;
		qe = read.length - t;
	}

	//
	// Decide the diagonal band for this space
	//
	long long int minDiagNum, maxDiagNum; 
	long long int diag1, diag2;
	diag1 = 0;
	diag2 = (long long int) (qe - qs) - (long long int) (te - ts); // scale diag1 and diag2 to the local coordinates
	minDiagNum = min(diag1, diag2) - 50;
	maxDiagNum = max(diag1, diag2) + 50;
 
	//
	// Find matches in read and reference 
	//
	vector<GenomeTuple> EndReadTup, EndGenomeTup;
	GenomePairs EndPairs;
	StoreMinimizers<GenomeTuple, Tuple>(genome.seqs[ChromIndex] + ts , te - ts, opts.globalK, opts.globalW, EndGenomeTup, false);
	sort(EndGenomeTup.begin(), EndGenomeTup.end());
	StoreMinimizers<GenomeTuple, Tuple>(strands[st] + qs, qe - qs, opts.globalK, opts.globalW, EndReadTup, false);
	sort(EndReadTup.begin(), EndReadTup.end());
	CompareLists(EndReadTup.begin(), EndReadTup.end(), EndGenomeTup.begin(), EndGenomeTup.end(), EndPairs, opts, maxDiagNum, minDiagNum); // By passing maxDiagNum and minDiagNum, this function 																															// filters out anchors that are outside the diagonal band;

	for (int rm = 0; rm < EndPairs.size(); rm++) {
		EndPairs[rm].first.pos  += qs;
		EndPairs[rm].second.pos += ts;
		assert(EndPairs[rm].first.pos < read.length);
		if (st == 1) EndPairs[rm].first.pos  = read.length - EndPairs[rm].first.pos;
		
	}	

	if (EndPairs.size() > 0) {
		cluster->matches.insert(cluster->matches.end(), EndPairs.begin(), EndPairs.end());  // TODO(Jingwen): Time consuming???????
		cluster->SetClusterBoundariesFromMatches(opts);
		cluster->refinespace = 1;
	}

	return 0;
}


//
// This function splits the chain if Clusters on the chain are mapped to different chromosomes;
//
void
SPLITChain(CHain & chain, vector<Cluster*> RefinedClusters, vector<vector<unsigned int>> & splitchains) {

	vector<int> Index; // Index stores the chromIndex of Clusters on chain
	vector<int> chainIndex(chain.ch.size(), 0); // chainIndex[i] means Cluster(chain.ch[i]) has chromIndex Index[chainIndex[i]];

	//
	// Get Index and chainIndex;
	//
	for (int im = 0; im < chain.ch.size(); im++) {

		int curChromIndex = RefinedClusters[chain.ch[im]]->chromIndex;
		if (Index.empty()) {
			Index.push_back(curChromIndex);
			chainIndex[im] = Index.size() - 1;
		}
		else {
			int ex = 0;
			while (ex < Index.size()) {
				if (Index[ex] == curChromIndex) {
					chainIndex[im] = ex;
					break;
				}
				ex++;
			}
			if (ex == Index.size()) {
				Index.push_back(curChromIndex);
				chainIndex[im] = Index.size() - 1;
			}
		}
	}

	//
	// Get sptchain based on Index and chainIndex;
	//
	vector<vector<unsigned int>> sptchain(Index.size()); //// TODO(Jingwen): make sure this initialization is right!
	for (int im = 0; im < chainIndex.size(); im++) {
		sptchain[chainIndex[im]].push_back(chain.ch[im]);
	}
	splitchain = sptchain;
}


int MapRead(const vector<float> & LookUpTable, Read &read, Genome &genome, vector<GenomeTuple> &genomemm, LocalIndex &glIndex, Options &opts, ostream *output, pthread_mutex_t *semaphore=NULL) {
	
	string baseName = read.name;

	for (int i=0; i < baseName.size(); i++) {	
		if (baseName[i] == '/') baseName[i] = '_';	
		if (baseName[i] == '|') baseName[i] = '_';
	}


	vector<GenomeTuple> readmm; // readmm stores minimizers
	vector<pair<GenomeTuple, GenomeTuple> > allMatches, forMatches, revMatches, matches;

	if (opts.storeAll) {
		Options allOpts = opts;
		allOpts.globalW=1;
		StoreMinimizers<GenomeTuple, Tuple>(read.seq, read.length, allOpts.globalK, allOpts.globalW, readmm);			
	}
	else {
		StoreMinimizers<GenomeTuple, Tuple>(read.seq, read.length, opts.globalK, opts.globalW, readmm);
	}
	sort(readmm.begin(), readmm.end()); //sort kmers in readmm(minimizers)
	//
	// Add matches between the read and the genome.
	//
	CompareLists(readmm, genomemm, allMatches, opts);
	DiagonalSort<GenomeTuple>(allMatches); // sort fragments in allMatches by forward diagonal, then by first.pos(read)


	// TODO(Jinwen): delete this after debug
	if (opts.dotPlot) {
		ofstream clust("all-matches.dots");
		for (int m=0; m < allMatches.size(); m++) {
			clust << allMatches[m].first.pos << "\t" << allMatches[m].second.pos << "\t" << allMatches[m].first.pos+ opts.globalK << "\t" 
				<< allMatches[m].second.pos + opts.globalK << "\t0\t0"<<endl;
		}
		clust.close();
	}


	SeparateMatchesByStrand(read, genome, opts.globalK, allMatches, forMatches, revMatches);


	// TODO(Jingwen): only for debug, delete later
	if (opts.dotPlot) {
		ofstream clust("for-matches0.dots");
		for (int m=0; m < forMatches.size(); m++) {
			clust << forMatches[m].first.pos << "\t" << forMatches[m].second.pos << "\t" << opts.globalK + forMatches[m].first.pos << "\t"
					<< forMatches[m].second.pos + opts.globalK << "\t0\t0"<<endl;
		}
		clust.close();
		ofstream rclust("rev-matches0.dots");
		for (int m=0; m < revMatches.size(); m++) {			
			rclust << revMatches[m].first.pos << "\t" << revMatches[m].second.pos + opts.globalK << "\t" << opts.globalK + revMatches[m].first.pos << "\t"
					<< revMatches[m].second.pos << "\t0\t0"<<endl;
		}
		rclust.close();
	}


	int minDiagCluster = 0; // This parameter will be set inside function CleanOffDiagonal, according to anchors density
	CleanOffDiagonal(forMatches, opts, minDiagCluster);

	// TODO(Jingwen): only for debug, delete later
	if (opts.dotPlot) {
		ofstream clust("for-matches1.dots");
		for (int m=0; m < forMatches.size(); m++) {
			clust << forMatches[m].first.pos << "\t" << forMatches[m].second.pos << "\t" << opts.globalK + forMatches[m].first.pos << "\t"
					<< forMatches[m].second.pos + opts.globalK << "\t0\t0"<<endl;
		}
		clust.close();
	}

	vector<Cluster> clusters;
	vector<Cluster> roughclusters;
	int forwardStrand=0;
	// maxDiag must be large enough for the following function "StoreDiagonalClusters". 
	// Otherwise fragments on the same line (line with little curve) might end up in several clusters[i], instead of one clusters[i]

	// The strategy we are taking: 
	// first let maxDiag be a small number but not too small(like 500), which also alleviate the cases where anchors are on a curvy line.
	// Then break the clusters[i] into two if any two anchors are father than maxGap. 
	StoreDiagonalClusters(forMatches, roughclusters, opts, 0, forMatches.size(), true, false, forwardStrand); // rough == true means only storing "start and end" in every clusters[i]
	for (int c = 0; c < roughclusters.size(); c++) {
		CartesianSort(forMatches, roughclusters[c].start, roughclusters[c].end);
		StoreDiagonalClusters(forMatches, clusters, opts, roughclusters[c].start, roughclusters[c].end, false, false, forwardStrand);
	}


	AntiDiagonalSort<GenomeTuple>(revMatches, genome.GetSize());
	minDiagCluster = 0; // This parameter will be set inside function CleanOffDiagonal, according to anchors density
	CleanOffDiagonal(revMatches, opts, minDiagCluster, 1);

	// TODO(Jingwen): Only for debug
	if (opts.dotPlot) {
		ofstream rclust("rev-matches1.dots");
		for (int m=0; m < revMatches.size(); m++) {			
			rclust << revMatches[m].first.pos << "\t" << revMatches[m].second.pos + opts.globalK << "\t" << opts.globalK + revMatches[m].first.pos << "\t"
					<< revMatches[m].second.pos << "\t0\t0"<<endl;
		}
		rclust.close();
	}


	vector<Cluster> revroughClusters;
	int reverseStrand=1;
	StoreDiagonalClusters(revMatches, revroughClusters, opts, 0, revMatches.size(), true, false, reverseStrand);

	for (int c = 0; c < revroughClusters.size(); c++) {
		CartesianSort(revMatches, revroughClusters[c].start, revroughClusters[c].end);
		StoreDiagonalClusters(revMatches, clusters, opts, revroughClusters[c].start, revroughClusters[c].end, false, false, reverseStrand);
	}

	if (opts.dotPlot) {
		ofstream clust("for-matches.dots");
		for (int m=0; m < forMatches.size(); m++) {
			clust << forMatches[m].first.pos << "\t" << forMatches[m].second.pos << "\t" << opts.globalK + forMatches[m].first.pos << "\t"
					<< forMatches[m].second.pos + opts.globalK << "\t0\t0"<<endl;
		}
		clust.close();
		ofstream rclust("rev-matches.dots");
		for (int m=0; m < revMatches.size(); m++) {			
			rclust << revMatches[m].first.pos << "\t" << revMatches[m].second.pos + opts.globalK << "\t" << opts.globalK + revMatches[m].first.pos << "\t"
					<< revMatches[m].second.pos << "\t0\t0"<<endl;
		}
		rclust.close();

		ofstream wclust("roughclusters-matches.dots");
		for (int m=0; m < roughclusters.size(); m++) {
			for (int c = roughclusters[m].start; c < roughclusters[m].end; ++c) {
				wclust << forMatches[c].first.pos << "\t" << forMatches[c].second.pos << "\t" << opts.globalK + forMatches[c].first.pos << "\t"
					<< forMatches[c].second.pos + opts.globalK << "\t" << m << "\t0"<<endl;				
			}
		}
		wclust.close();

		ofstream revclust("revroughClusters-matches.dots");
		for (int m=0; m < revroughClusters.size(); m++) {
			for (int c = revroughClusters[m].start; c < revroughClusters[m].end; ++c) {
				revclust << revMatches[c].first.pos << "\t" << revMatches[c].second.pos + opts.globalK << "\t" << opts.globalK + revMatches[c].first.pos << "\t"
					 << revMatches[c].second.pos<< "\t" << m << "\t0"<<endl;				
			}
		}
		revclust.close();
	}



/*
	//
	// Save the top diagonal bands with high number of anchors
	// 
	CleanOffDiagonal(allMatches, strands, genome, read, opts);
	DiagonalSort<GenomeTuple>(allMatches); // sort fragments in allMatches by forward diagonal, then by first.pos(read)
	
	vector<bool> strands(allMatches.size(), 0);
	SeparateMatchesByStrand(read, genome, opts.globalK, allMatches, strands);
	vector<Cluster> clusters;
	StoreDiagonalClusters(allMatches, clusters, strands, opts);

	*/

	//
	// Split clusters on x and y coordinates, vector<Cluster> splitclusters, add a member for each splitcluster to specify the original cluster it comes from
	//
	// INPUT: vector<Cluster> clusters   OUTPUT: vector<Cluster> splitclusters with member--"coarse" specify the index of the original cluster splitcluster comes from

	vector<Cluster> splitclusters;
	SplitClusters(clusters, splitclusters);


	if (opts.dotPlot) {
		ofstream clust("clusters-coarse.tab");
		for (int m = 0; m < clusters.size(); m++) {
			if (clusters[m].strand == 0) {
				clust << clusters[m].qStart << "\t" 
					  << clusters[m].tStart << "\t"
					  << clusters[m].qEnd   << "\t"
					  << clusters[m].tEnd   << "\t"
					  << m << "\t"
					  << clusters[m].strand << endl;
			}
			else {
				clust << clusters[m].qStart << "\t" 
					  << clusters[m].tEnd << "\t"
					  << clusters[m].qEnd   << "\t"
					  << clusters[m].tStart   << "\t"
					  << m << "\t"
					  << clusters[m].strand << endl;
			}
		}
		clust.close();
	}

	if (opts.dotPlot) {
		ofstream clust("splitclusters-coarse.tab");
		for (int m = 0; m < splitclusters.size(); m++) {
			if (splitclusters[m].strand == 0) {
				clust << splitclusters[m].qStart << "\t" 
					  << splitclusters[m].tStart << "\t"
					  << splitclusters[m].qEnd   << "\t"
					  << splitclusters[m].tEnd   << "\t"
					  << m << "\t"
					  << splitclusters[m].strand << endl;
			}
			else {
				clust << splitclusters[m].qStart << "\t" 
					  << splitclusters[m].tEnd << "\t"
					  << splitclusters[m].qEnd   << "\t"
					  << splitclusters[m].tStart   << "\t"
					  << m << "\t"
					  << splitclusters[m].strand << endl;
			}
		}
		clust.close();
	}



	//
	// Apply SDP on splitclusters. Based on the chain, clean clusters to make it only contain clusters that are on the chain.   --- vector<Cluster> clusters
	// class: chains: vector<chain> chain: vector<vector<int>>     Need parameters: PrimaryAlgnNum, SecondaryAlnNum
	// NOTICE: chains in Primary_chains do not overlap on Cluster
	//

	////// TODO(Jingwen): customize a rate fro SparseDP
	vector<Primary_chain> Primary_chains;
	SparseDP (splitclusters, Primary_chains, opts, LookUpTable, read);
	switchindex(splitclusters, Primary_chains);

	//
	// Remove Clusters in "clusters" that are not on the chains;
	//
	int ChainNum = 0;
	for (int p = 0; p < Primary_chains.size(); p++) {
		ChainNum += Primary_chains[p].chains.size();
	}

	vector<bool> Remove(clusters.size(), 1);
	for (int p = 0; p < Primary_chains.size(); p++) {
		for (int h = 0; h < Primary_chains[p].chains.size(); h++){
			for (int c = 0; c < Primary_chains[p].chains[h].ch.size(); c++) {
				Remove[Primary_chains[p].chains[h].ch[c]] = 0;
			}
		}
	}

	int lm = 0;
	for (int s = 0; s < clusters.size(); s++) {
		if (Remove[s] == 0) {
			clusters[lm] = clusters[s];
			lm++;
		}
	}
	clusters.resize(lm);	


	//
	// Change the index stored in Primary_chains, since we remove some Clusters in "clusters";
	//
	vector<int> NumOfZeros(Remove.size(), 0);
	int num = 0;
	for (int s = 0; s < Remove.size(); s++) {
		if (Remove[s] == 0) {
			num++;
			NumOfZeros[s] = num;
		}
	}

	for (int p = 0; p < Primary_chains.size(); p++) {
		for (int h = 0; h < Primary_chains[p].chains.size(); h++){
			for (int c = 0; c < Primary_chains[p].chains[h].ch.size(); c++) {
				Primary_chains[p].chains[h].ch[c] = NumOfZeros[Primary_chains[p].chains[h].ch[c]] - 1;
			}
		}
	}


	if (opts.dotPlot) {
		ofstream clust("CoarseChains.tab");

		for (int p = 0; p < Primary_chains.size(); p++) {
			for (int h = 0; h < Primary_chains[p].chains.size(); h++){
				for (int c = 0; c < Primary_chains[p].chains[h].ch.size(); c++) {
					int ph = Primary_chains[p].chains[h].ch[c];
					if (clusters[ph].strand == 0) {
						clust << clusters[ph].qStart << "\t" 
							  << clusters[ph].tStart << "\t"
							  << clusters[ph].qEnd   << "\t"
							  << clusters[ph].tEnd   << "\t"
							  << p << "\t"
							  << h << "\t"
							  << Primary_chains[p].chains[h].ch[c] << "\t"
							  << clusters[ph].strand << endl;
					} 
					else {
						clust << clusters[ph].qStart << "\t" 
							  << clusters[ph].tEnd << "\t"
							  << clusters[ph].qEnd   << "\t"
							  << clusters[ph].tStart  << "\t"
							  << p << "\t"
							  << h << "\t"
							  << Primary_chains[p].chains[h].ch[c] << "\t"
							  << clusters[ph].strand << endl;
					}
				}
			}

		}
		clust.close();
	}	


	//
	// Add pointers to seq that make code more readable.
	//
	char *readRC;
	CreateRC(read.seq, read.length, readRC);
	char *strands[2] = { read.seq, readRC };


	//
	// Build local index for refining alignments.
	//
	LocalIndex forwardIndex(glIndex);
	LocalIndex reverseIndex(glIndex);

	LocalIndex *localIndexes[2] = {&forwardIndex, &reverseIndex};
	forwardIndex.IndexSeq(read.seq, read.length);
	reverseIndex.IndexSeq(readRC, read.length); 


	// Set the parameters for merging anchors and 1st SDP
	Options smallOpts = opts;
	Options tinyOpts = smallOpts;
	tinyOpts.globalMaxFreq=3;
	tinyOpts.maxDiag=5;
	tinyOpts.minDiagCluster=2;
	tinyOpts.globalK=smallOpts.globalK-3;
	tinyOpts.minRemoveSpuriousAnchorsNum=5;
	tinyOpts.maxRemoveSpuriousAnchorsDist=50;


	//
	// Refining each cluster in "clusters" needed if CLR reads are aligned. Otherwise, skip this step
	// After this step, the t coordinates in clusters and refinedclusters have been substract chromOffSet. 
	//
	vector<Cluster> refinedclusters(clusters.size());
 	vector<Cluster*> RefinedClusters(clusters.size());

	if (opts.HighlyAccurate == false) {
			
		smallOpts.globalK=glIndex.k;
		smallOpts.globalW=glIndex.w;
		smallOpts.globalMaxFreq=6;
		smallOpts.cleanMaxDiag=10;// used to be 25
		smallOpts.maxDiag=50;
		smallOpts.maxGapBtwnAnchors=100; // used to be 200 // 200 seems a little bit large
		smallOpts.minDiagCluster=3; // used to be 3

		REFINEclusters(clusters, refinedclusters, genome, read,  glIndex, localIndexes, smallOpts, opts);
		// refinedclusters have GenomePos, chromIndex, coarse, matches, strand, refinespace;
		for (int s = 0; s < clusters.size(); s++) {
			RefinedClusters[s] = &refinedclusters[s];
		}
		clusters.clear();
	}
	else {

		//// TODO(Jingwen): write a function to determine the chromIndex and subtract chromOffSet from t coord.
		for (int s = 0; s < clusters.size(); s++) {	

			// Determine the chromIndex of each Cluster;
			int pass = CHROMIndex(clusters[s], genome);
			if (pass == 1) {
				clusters[s].matches.clear();
				continue;				
			}

			// Subtract chromOffSet from t coord.
			GenomePos chromOffset = genome.header.pos[clusters[s].chromIndex];
			for (int m = 0; m < clusters[s].matches.size(); m++) {
				clusters[s].matches[m].second.pos -= chromOffset;
			}
			clusters[s].tStart -= chromOffset;
			clusters[s].tEnd -= chromOffset;
			RefinedClusters[s] = &clusters[s];
		}
	}

	int SizeRefinedClusters = 0;
	if (opts.dotPlot) {
		ofstream clust("RefinedClusters.tab");

		for (int p = 0; p < RefinedClusters.size(); p++) {

			SizeRefinedClusters += RefinedClusters[p]->matches.size();
			for (int h = 0; h < RefinedClusters[p]->matches.size(); h++) {

				if (RefinedClusters[p]->strand == 0) {
					clust << RefinedClusters[p]->matches[h].first.pos << "\t"
						  << RefinedClusters[p]->matches[h].second.pos << "\t"
						  << RefinedClusters[p]->matches[h].first.pos + smallOpts.globalK << "\t"
						  << RefinedClusters[p]->matches[h].second.pos + smallOpts.globalK << "\t"
						  << p << "\t"
						  << RefinedClusters[p]->strand << endl;
				}
				else {
					clust << RefinedClusters[p]->matches[h].first.pos << "\t"
						  << RefinedClusters[p]->matches[h].second.pos + smallOpts.globalK << "\t"
						  << RefinedClusters[p]->matches[h].first.pos + smallOpts.globalK << "\t"
						  << RefinedClusters[p]->matches[h].second.pos<< "\t"
						  << p << "\t"
						  << RefinedClusters[p]->strand << endl;					
				}
			}
		}
		clust.close();
	}	


	//
	// For each chain, check the two ends and spaces between adjacent clusters. If the spaces are too wide, go to find anchors in the banded region.
	// For each chain, we have vector<Cluster> btwnClusters to store anchors;
	//
	for (int p = 0; p < Primary_chains.size(); p++) {
		
		for (int h = 0; h < Primary_chains[p].chains.size(); h++) {
		
			//
			// Find matches btwn every two adjacent Clusters;
			//
			int c = 1;
			GenomePos qe, qs, te, ts;
			bool st; GenomePos SpaceLength;
			while (c < Primary_chains[p].chains[h].ch.size()) {

				int cur = Primary_chains[p].chains[h].ch[c];
				int prev = Primary_chains[p].chains[h].ch[c - 1];

				//
				// Decide the boudaries of space and strand direction btwn RefinedClusters[cur] and RefinedClusters[prev]
				//
				qs = RefinedClusters[cur]->qEnd; 
				qe = RefinedClusters[prev]->qStart;

				if (RefinedClusters[cur]->tEnd <= RefinedClusters[prev]->tStart) {
					ts = RefinedClusters[cur]->tEnd;
					te = RefinedClusters[prev]->tStart;
					st = 0;
				}
				else if (RefinedClusters[cur]->tStart > RefinedClusters[prev]->tEnd) {
					ts = RefinedClusters[prev]->tEnd;
					te = RefinedClusters[cur]->tStart;
					st = 1;
				}

				if (qe > qs and te > ts) {
					SpaceLength = min(qe - qs, te - ts);
					if (SpaceLength > 1000 and SpaceLength < 10000 and RefinedClusters[cur]->chromIndex == RefinedClusters[prev]->chromIndex) {
						// btwnClusters have GenomePos, st, matches, coarse
						// This function also set the "coarse" flag for RefinedClusters[cur]
						RefineBtwnSpace(RefinedClusters[cur], smallOpts, genome, read, strands, qe, qs, te, ts, st, cur);
					}
				}
				c++;

			}

			//
			// Find matches at the right end;
			//
			int rh = Primary_chains[p].chains[h].ch[0];
			st = RefinedClusters[rh]->strand;
			qs = RefinedClusters[rh]->qEnd;
			qe = read.length;
			if (st == 0) {
				ts = RefinedClusters[rh]->tEnd;
				te = ts + qe - qs;				
			}
			else {
				te = RefinedClusters[rh]->tStart;
				if (te > qe - qs) ts = te - (qe - qs);
				else te = 0;
			}
			if (qe > qs and te > ts) {
				SpaceLength = min(qe - qs, te - ts); 
				if (SpaceLength > 500 and SpaceLength < 2000 and te < genome.lengths[RefinedClusters[rh]->chromIndex]) {
					RefineBtwnSpace(RefinedClusters[rh], smallOpts, genome, read, strands, qe, qs, te, ts, st, rh);
				}				
			}


			//
			// Find matches at the left end
			//		
			int lh = Primary_chains[p].chains[h].ch.back();
			qs = 0;
			qe = RefinedClusters[lh]->qStart;
			st = RefinedClusters[lh]->strand;
			if (st == 0) {
				te = RefinedClusters[lh]->tStart;
				if (te > qe - qs) ts = te - (qe - qs);
				else ts = 0;
			}
			else {
				ts = RefinedClusters[lh]->tEnd;
				te = ts + (qe - qs);
			}
			if (qe > qs and te > ts) {
				SpaceLength = min(qe - qs, te - ts);
				if (SpaceLength > 500 and SpaceLength < 2000 and te < genome.lengths[RefinedClusters[lh]->chromIndex]) {
					RefineBtwnSpace(RefinedClusters[lh], smallOpts, genome, read, strands, qe, qs, te, ts, st, lh);
				}				
			}


			//
			// Do linear extension for each anchors and avoid overlapping locations;
			// INPUT: RefinedClusters; OUTPUT: ExtendClusters;
			// NOTICE: ExtendClusters have members: strand, matches, matchesLengths, GenomePos, chromIndex;
			//
			vector<Cluster> ExtendClusters(Primary_chains[p].chains[h].ch.size());
			LinearExtend(RefinedClusters, ExtendClusters, Primary_chains[p].chains[h].ch, smallOpts, genome, read);


			int SizeExtendClusters = 0;
			if (opts.dotPlot) {
				ofstream clust("ExtendClusters.tab");

				for (int p = 0; p < ExtendClusters.size(); p++) {

					SizeExtendClusters += ExtendClusters[p].matches.size();
					for (int h = 0; h < ExtendClusters[p].matches.size(); h++) {
						
						if (ExtendClusters[p].strand == 0) {
							clust << ExtendClusters[p].matches[h].first.pos << "\t"
								  << ExtendClusters[p].matches[h].second.pos << "\t"
								  << ExtendClusters[p].matches[h].first.pos + ExtendClusters[p].matchesLengths[h] << "\t"
								  << ExtendClusters[p].matches[h].second.pos + ExtendClusters[p].matchesLengths[h] << "\t"
								  << p << "\t"
								  << ExtendClusters[p].strand << endl;
						}
						else {
							clust << ExtendClusters[p].matches[h].first.pos << "\t"
								  << ExtendClusters[p].matches[h].second.pos + ExtendClusters[p].matchesLengths[h] << "\t"
								  << ExtendClusters[p].matches[h].first.pos + ExtendClusters[p].matchesLengths[h] << "\t"
								  << ExtendClusters[p].matches[h].second.pos<< "\t"
								  << p << "\t"
								  << ExtendClusters[p].strand << endl;					
						}
					}
				}
				clust.close();
			}	

			cerr << "LinearExtend efficiency: " << (float)SizeExtendClusters/(float)SizeRefinedClusters << endl;


			//
			// Split the chain Primary_chains[p].chains[h] if clusters are aligned to different chromosomes; SplitAlignment is class that vector<* vector<Cluster>>
			// INPUT: vector<Cluster> ExtendClusters; OUTPUT:  vector<vector<unsigned int>> splitchain;
			//
			vector<vector<unsigned int>> splitchains;
			SPLITChain(Primary_chains[p].chains[h], ExtendClusters, splitchains);


			//
			// Apply SDP on all splitchains to get the final rough alignment path;
			// store the result in GenomePairs tupChain; We need vector<Cluster> tupClusters for tackling anchors of different strands
			//
			// NOTICE: should we insert 4 points only for reversed anchors? 
			//
			for (int st = 0; st < splitchains.size(); st++) {

				SparseDP(splitchains[st], ExtendClusters, smallOpts, LookUpTable, read, rate);

			}






			return 0;

		}
	}	



	//
	// Apply SDP on all the anchors to get the final chain; ---- GenomePairs tupChain   vector<Cluster>tupClusters for different strands
	//


	//
	// If there is an inversion in the path, ---- split in vector<Cluster> Chainclusters with start and end specify the positions in tupChain
	//


	//
	// Use normal DP to fill in the spaces between anchors on the chain;
	//








































	if (clusters.size() != 0) {

		if (opts.dotPlot) {
			ofstream clust("clusters-pre-merge.tab");
			for (int c = 0; c < clusters.size(); c++) {
				for (int m = 0; m < clusters[c].matches.size(); m++) {
					clust << clusters[c].matches[m].first.pos << "\t" 
						  << clusters[c].matches[m].second.pos << "\t" 
						  << clusters[c].matches[m].first.pos + opts.globalK << "\t"
						  << clusters[c].matches[m].second.pos + opts.globalK << "\t" 
						  << c << "\t" 
						  << clusters[c].strand << endl;
				}
			}
			clust.close();
		}


		// Decide the rate to raise the cluster value 
		ClusterOrder clusterOrder(&clusters);
		float valuerate = 1;
		float maxvalue = 0;
		for (int cv = 0; cv < clusterOrder.size(); cv++) {
			maxvalue = max(maxvalue, (float) clusterOrder[cv].qEnd - clusterOrder[cv].qStart);
		}
		if (maxvalue < (float) read.length/6) valuerate = 2; 


		//
		// Apply SDP on vector<Cluster> clusters to get primary chains
		//
		vector<Primary_chain> Primary_chains;
		//
		// This SDP needs to insert 4 points for any anchors. Otherwise SDP cannot pick up inversion when the read is reversedly aligned to reference
		//
		//SparseDP (SplitClusters, Primary_chains, opts, LookUpTable, read, valuerate);
		//SparseDP (clusters, Primary_chains, opts, LookUpTable, read, valuerate);


		// Output the primary chains
		if (opts.dotPlot) {
			for (int c = 0; c < Primary_chains.size(); c++) {
				stringstream outNameStrm;
				outNameStrm << "clusters-sdp." << c << ".dots";
				ofstream baseDots(outNameStrm.str().c_str());
				for (int s = 0; s < Primary_chains[c].chains[0].ch.size(); ++s) {
					for (int m = 0; m < clusters[Primary_chains[c].chains[0].ch[s]].matches.size(); ++m) {
						baseDots << clusters[Primary_chains[c].chains[0].ch[s]].matches[m].first.pos << "\t" 
								 << clusters[Primary_chains[c].chains[0].ch[s]].matches[m].second.pos << "\t" 
								 << clusters[Primary_chains[c].chains[0].ch[s]].matches[m].first.pos + opts.globalK << "\t"
								 << clusters[Primary_chains[c].chains[0].ch[s]].matches[m].second.pos + opts.globalK << "\t"
								 << Primary_chains[c].chains[0].ch[s] << "\t" 
								 << clusters[Primary_chains[c].chains[0].ch[s]].strand << endl;					
					}				
				}
				baseDots.close();
			}
		}

		//
		// Check the subCluster in each chain, split the read when necessary;
		// For main chain, main = -1; for split part, main store the index of the main chain; 
		//
		unsigned int seperator = clusters.size() + 1000;
		for (int c = 0; c < Primary_chains.size(); c++) {
			vector<CHain> newchains;
			int wc = 0;

			for (int p = 0; p < Primary_chains[c].chains.size(); p++) {
				vector<vector<unsigned int>> splitchains;
				vector<unsigned int> Sc;
				Sc.push_back(Primary_chains[c].chains[p].ch[0]);
				splitchains.push_back(Sc);
				vector<int> traceIndex(Primary_chains[c].chains[p].ch.size());

				for (int s = 1; s < Primary_chains[c].chains[p].ch.size(); s++) {
					int curC = Primary_chains[c].chains[p].ch[s];
					bool is = 1; // is == 0 means that Primary_chains[c].chains[p].ch[s] won't be split out

					for (int x = s - 1; x >= 0; x--) {
						bool dir = 0; 
						int prevC = Primary_chains[c].chains[p].ch[x];
						if (clusters[curC].tStart >= clusters[prevC].tStart) { // these two subClusters are reversely mapped
							dir = 1;
						}
						long long int prevDiag = 0, curDiag = 0;
						if (dir == 0) {
							prevDiag = (long long int) clusters[prevC].tStart - (long long int) clusters[prevC].qEnd;
							curDiag = (long long int) clusters[curC].tEnd - (long long int) clusters[curC].qStart;
						}
						else {
							prevDiag = (long long int) clusters[prevC].tStart + (long long int) clusters[prevC].qEnd;
							curDiag = (long long int) clusters[curC].tEnd + (long long int) clusters[curC].qStart;
						}

						if (prevDiag > 30000 + curDiag or curDiag > 30000 + prevDiag) { // gap > 30000
							if (x == s-1) {
								splitchains[traceIndex[s-1]].push_back(seperator);
							}
						}
						else { // gap <= 30000
							traceIndex[s] = traceIndex[x];
							splitchains[traceIndex[x]].push_back(Primary_chains[c].chains[p].ch[s]);
							is = 0;
							break;
						}
					}
					if (is == 1) {
						vector<unsigned int> Iso; 
						Iso.push_back(seperator);
						Iso.push_back(Primary_chains[c].chains[p].ch[s]);
						splitchains.push_back(Iso);
						traceIndex[s] = splitchains.size() - 1;	
					}
				}

				int mainChain = 0;
				for (int ss = 1; ss < splitchains.size(); ss++) {
					if (splitchains[ss].size() > splitchains[mainChain].size()) {
						mainChain = ss;
					}
				}
				newchains.push_back(CHain(splitchains[mainChain]));
				for (int ss = 0; ss < splitchains.size(); ss++) {
					if (ss != mainChain) {
						newchains.push_back(CHain(splitchains[ss]));
						newchains.back().main = wc;
					}
				}
				//
				// Decide the tStart, qStart, tEnd, qEnd for each chain
				for (int ss = 0; ss < newchains.size(); ss++) {
					newchains[ss].tStart = clusters[newchains[ss].ch[0]].tStart;
					newchains[ss].qStart = clusters[newchains[ss].ch[0]].qStart;
					newchains[ss].tEnd = clusters[newchains[ss].ch[0]].tEnd;
					newchains[ss].qEnd = clusters[newchains[ss].ch[0]].qEnd;


					for (int nc = 1; nc < newchains[ss].ch.size(); nc++) {
						newchains[ss].tStart = min(newchains[ss].tStart, clusters[newchains[ss].ch[nc]].tStart);
						newchains[ss].qStart = min(newchains[ss].qStart, clusters[newchains[ss].ch[nc]].qStart);
						newchains[ss].tEnd = max(newchains[ss].tEnd, clusters[newchains[ss].ch[nc]].tEnd);
						newchains[ss].qEnd = max(newchains[ss].qEnd, clusters[newchains[ss].ch[nc]].qEnd);
					}
				}
				wc = newchains.size();
			}
			Primary_chains[c].chains = newchains;
		}


		//
		// Decide the direction of each chain in Primary_chains; Only check the first and the last subClusters in each chain
		//
		int chainNum = 0;
		for (int c = 0; c < Primary_chains.size(); c++) {
			chainNum += Primary_chains[c].chains.size();

			for (int p = 0; p < Primary_chains[c].chains.size(); p++) {
				int first, last;
				for (int ss = 0; ss < Primary_chains[c].chains[p].ch[ss]; ss++) {
					if (Primary_chains[c].chains[p].ch[ss] != seperator) {
						first = Primary_chains[c].chains[p].ch[ss];
						break;
					}
				}
				for (int ss = Primary_chains[c].chains[p].ch.size() - 1; ss >= 0; ss--) {
					if (Primary_chains[c].chains[p].ch[ss] != seperator) {
						last = Primary_chains[c].chains[p].ch[ss];
						break;
					}
				}
				//first = Primary_chains[c].chains[p].ch[0];
				//last = Primary_chains[c].chains[p].ch.back();
				GenomePos firstGenomeStart = clusters[first].tStart;
				GenomePos lastGenomeStart = clusters[last].tStart;
				if (firstGenomeStart <= lastGenomeStart) {
					Primary_chains[c].chains[p].direction = 1;
				}
				else {
					Primary_chains[c].chains[p].direction = 0;
				}
			}
		}


		// TODO(Jingwen): only for debug and delete this later. Output the 1st every chain subject to the 1st primary chain
		if (opts.dotPlot) {
			for (int c = 0; c < Primary_chains.size(); c++) {

				for (int ss = 0; ss < Primary_chains[c].chains.size(); ++ss) {
					stringstream outNameStrm;
					outNameStrm << "clusters-dp." << ss << ".dots";
					ofstream baseDots(outNameStrm.str().c_str());
					for (int s = 0; s < Primary_chains[c].chains[ss].ch.size(); ++s) {
						if (Primary_chains[c].chains[ss].ch[s] != seperator) {
							for (int m = 0; m < clusters[Primary_chains[c].chains[ss].ch[s]].matches.size(); ++m) {
								baseDots << clusters[Primary_chains[c].chains[ss].ch[s]].matches[m].first.pos << "\t" 
										 << clusters[Primary_chains[c].chains[ss].ch[s]].matches[m].second.pos << "\t" 
										 << clusters[Primary_chains[c].chains[ss].ch[s]].matches[m].first.pos + opts.globalK << "\t"
										 << clusters[Primary_chains[c].chains[ss].ch[s]].matches[m].second.pos + opts.globalK << "\t"
										 << s << "\t" 
										 << clusters[Primary_chains[c].chains[ss].ch[s]].strand << endl;					
							}								
						}
					}					
					baseDots.close();
				}
			}
		}

		//
		// Record every chain's information in logClusters
		//
		vector<LogCluster> logClusters(chainNum);
		vector<bool> Dele(chainNum, 0);
		vector<int> ind(clusters.size(), 0); // ind[i] == 1 means clusters[ind[i]] should be kept
		int num = 0;
		int Psize = 0;
		int sed = 0;
		for (int c = 0; c < Primary_chains.size(); ++c) {

			int pr = num;

			for (int p = 0; p < Primary_chains[c].chains.size(); ++p) {
			
				logClusters[num].direction = Primary_chains[c].chains[p].direction;    

				if (p != 0 and Primary_chains[c].chains[p].main == -1) { // This is a secondary alignment
					logClusters[num].ISsecondary = 1; 
					logClusters[num].primary = pr;
					logClusters[pr].secondary.push_back(num);
					logClusters[num].main = -1; // this is main part of the secondary alignment
					++sed;
				}
				else if (p == 0 and Primary_chains[c].chains[p].main == -1) {
					logClusters[num].main = -1;
					++sed;
				}
				else {
					logClusters[num].main = Psize + Primary_chains[c].chains[p].main;

					if (sed > 1) {
					logClusters[num].ISsecondary = 1; 
					logClusters[num].primary = pr;
					logClusters[pr].secondary.push_back(num);
					}
				}

				int id = Primary_chains[c].chains[p].ch[0];
				GenomePos tPos; int ChromIndex;
				GenomePos qStart, qEnd, tStart, tEnd;
				if (id != seperator) {
					tPos = clusters[id].tStart;
					int firstChromIndex = genome.header.Find(tPos);
					tPos = clusters[id].tEnd;
					int lastChromIndex = genome.header.Find(tPos);
					if (firstChromIndex != lastChromIndex ) {
						Dele[num] = 1;
						num++;
						continue;
					}
					ChromIndex = genome.header.Find(tPos); 
					qStart = read.length; qEnd = 0; tStart = genome.header.pos[ChromIndex + 1]; tEnd = genome.header.pos[ChromIndex]; // genome.lengths[ChromIndex]
					ind[id] = 1;					
				}

				// insert anchors to logClusters[num].SubCluster[0] 
				// insert into logClusters[num].SubCluster one of the end of the alignment
				//
				GenomePos curGenomeEnd = 0, curReadEnd = 0, nextGenomeStart = 0, nextReadStart   = 0;
				vector<GenomeTuple> EndReadTup, EndGenomeTup;
				GenomePairs EndPairs;
				GenomePos subreadLength, subgenomeLength;

				if (id == seperator) {
					logClusters[num].split = 1;
				}
				else {
					if (clusters[id].strand  == 0) { // the first subcluster is in the forward direction

						if (clusters[id].tEnd + read.length + 300 < genome.header.pos[ChromIndex + 1] + clusters[id].qEnd) {	
							nextGenomeStart = clusters[id].tEnd + (read.length - clusters[id].qEnd) + 300 ;
						}
						else {nextGenomeStart = genome.header.pos[ChromIndex + 1];}
						nextReadStart = read.length;
						curGenomeEnd = clusters[id].tEnd;
						curReadEnd = clusters[id].qEnd;
					} 
					else { // the first subcluster is in the reverse direction
						if (clusters[id].tEnd + clusters[id].qStart + 300 < genome.header.pos[ChromIndex + 1]) {
							nextGenomeStart = clusters[id].tEnd + clusters[id].qStart + 300;
						}
						else {nextGenomeStart = genome.header.pos[ChromIndex + 1];}
						nextReadStart = read.length;
						curReadEnd = read.length - clusters[id].qStart; // flip the box into forward direction
						curGenomeEnd = clusters[id].tEnd;
					}	
					subreadLength = nextReadStart - curReadEnd;
					subgenomeLength = nextGenomeStart - curGenomeEnd;
					assert(nextReadStart >= curReadEnd);
					assert(nextGenomeStart >= curGenomeEnd);
					

					if (subreadLength > 500 and subreadLength < 1000) {
						StoreMinimizers<GenomeTuple, Tuple>(genome.seqs[ChromIndex] + (curGenomeEnd - genome.header.pos[ChromIndex]), subgenomeLength, opts.globalK, 1, EndGenomeTup, false);
						sort(EndGenomeTup.begin(), EndGenomeTup.end());
						StoreMinimizers<GenomeTuple, Tuple>(strands[clusters[id].strand] + curReadEnd, subreadLength, opts.globalK, 1, EndReadTup, false);
						sort(EndReadTup.begin(), EndReadTup.end());
						CompareLists(EndReadTup.begin(), EndReadTup.end(), EndGenomeTup.begin(), EndGenomeTup.end(), EndPairs, opts);
						
						for(int rm=0; rm < EndPairs.size(); rm++) {
							EndPairs[rm].first.pos  += curReadEnd;
							EndPairs[rm].second.pos += curGenomeEnd;
							assert(EndPairs[rm].first.pos < read.length);
						}
						// TODO(Jingwen): add a clean off diagonal function here to remove noisy anchors
						// Set boundaries for EndPairs
						for (int rm = 0; rm < EndPairs.size(); rm++) {
							// Here qStart qEnd are all in the forward direction
							qStart = min(EndPairs[rm].first.pos, qStart);
							qEnd = max(EndPairs[rm].first.pos + opts.globalK, qEnd);
							tStart = min(EndPairs[rm].second.pos, tStart);
							tEnd = max(EndPairs[rm].second.pos + opts.globalK, tEnd);					
						}

						if (EndPairs.size() != 0) {
							clusters[id].matches.insert(clusters[id].matches.end(), EndPairs.begin(), EndPairs.end());
						}
					}	

					//
					// insert the first cluster into logClusters[num].SubCluster
					//
					if (clusters[id].strand == 1) {
						logClusters[num].SubCluster.push_back(Cluster(0, clusters[id].matches.size(), read.length - clusters[id].qEnd, 
																		read.length - clusters[id].qStart, clusters[id].tStart, 
																			clusters[id].tEnd, clusters[id].strand, id));	
						// Swap rev anchors	
						for (int m = 0; m < clusters[id].matches.size() - EndPairs.size(); m++) { // do not need to flip anchors in EndPairs
							clusters[id].matches[m].first.pos = read.length - (clusters[id].matches[m].first.pos + opts.globalK);
						}						
					}
					else {
						logClusters[num].SubCluster.push_back(Cluster(0, clusters[id].matches.size(), clusters[id].qStart, clusters[id].qEnd,
																	clusters[id].tStart, clusters[id].tEnd, clusters[id].strand, id));
					}

					if (EndPairs.size() != 0) {
						clusters[id].qEnd = max(clusters[id].qEnd, qEnd);
						clusters[id].qStart = min(clusters[id].qStart, qStart);
						clusters[id].tEnd = max(clusters[id].tEnd, tEnd);
						clusters[id].tStart = min(clusters[id].tStart, tStart);	
						logClusters[num].SubCluster.back().qStart = clusters[id].qStart;
						logClusters[num].SubCluster.back().qEnd = clusters[id].qEnd;
						logClusters[num].SubCluster.back().tStart = clusters[id].tStart;
						logClusters[num].SubCluster.back().tEnd = clusters[id].tEnd;

					}
				}


				//
				// insert the rest clusters
				//
				if (Primary_chains[c].chains[p].ch.size() > 1) {

					//
					// Get some parameters for splitted read part
					if (id == seperator and Primary_chains[c].chains[p].ch[1] != seperator) {
						int rs = Primary_chains[c].chains[p].ch[1];
						tPos = clusters[rs].tStart;
						int firstChromIndex = genome.header.Find(tPos);
						tPos = clusters[rs].tEnd;
						int lastChromIndex = genome.header.Find(tPos);
						if (firstChromIndex != lastChromIndex ) {
							Dele[num] = 1;
							num++;
							continue;
						}
						ChromIndex = genome.header.Find(tPos); 
						qStart = read.length; qEnd = 0; tStart = genome.header.pos[ChromIndex + 1]; tEnd = genome.header.pos[ChromIndex]; // genome.lengths[ChromIndex]
						ind[rs] = 1;	
						id = rs;
					}

					for (int s = 1; s < Primary_chains[c].chains[p].ch.size(); ++s) {

						int lc = clusters[id].matches.size();
						int ids = Primary_chains[c].chains[p].ch[s];

						if (logClusters[num].split == 1 and s == 1 and ids != seperator) {
							lc = 0;
						}

						if ((logClusters[num].split != 1 and ids != seperator) or (logClusters[num].split == 1 and ids != seperator and s != 1)) {

							clusters[id].matches.insert(clusters[id].matches.end(), clusters[ids].matches.begin(), clusters[ids].matches.end());
							clusters[id].qStart = min(clusters[ids].qStart, clusters[id].qStart);
							clusters[id].tStart = min(clusters[ids].tStart, clusters[id].tStart);
							clusters[id].qEnd = max(clusters[ids].qEnd, clusters[id].qEnd);
							clusters[id].tEnd = max(clusters[ids].tEnd, clusters[id].tEnd);
						}

						if ((logClusters[num].split != 1 and ids != seperator) or (logClusters[num].split == 1 and ids != seperator)) {
							// If this is a reverse strand, then swap it, which will make the later step "refine clusters" easier
							// and also swap the qStart, qEnd
							if (clusters[ids].strand == 1) {
								logClusters[num].SubCluster.push_back(Cluster(lc, clusters[id].matches.size(), read.length - clusters[ids].qEnd,
																			 read.length - clusters[ids].qStart, clusters[ids].tStart, 
																			 clusters[ids].tEnd, clusters[ids].strand, id));

								for (int m = lc; m < clusters[id].matches.size(); m++) {
									clusters[id].matches[m].first.pos = read.length - (clusters[id].matches[m].first.pos + opts.globalK);
								}				
							}
							else {
								logClusters[num].SubCluster.push_back(Cluster(lc, clusters[id].matches.size(), clusters[ids].qStart, clusters[ids].qEnd,
													clusters[ids].tStart, clusters[ids].tEnd, clusters[ids].strand, id));
							}							
						}
					}			
				}


				//
				// insert anchors into logClusters[num].SubCluster.back()
				//
				int idx = Primary_chains[c].chains[p].ch.back();
				if (idx != seperator) {
					tPos = clusters[idx].tStart;
					ChromIndex = genome.header.Find(tPos); 
					qStart = read.length; qEnd = 0; tStart = genome.header.pos[ChromIndex + 1]; tEnd = genome.header.pos[ChromIndex]; // genome.lengths[ChromIndex]
					EndReadTup.clear();
					EndGenomeTup.clear();
					EndPairs.clear();

					if (clusters[idx].strand == 0) {

						if (clusters[idx].tStart > clusters[idx].qStart + 300 + genome.header.pos[ChromIndex]) { 
							curGenomeEnd = clusters[idx].tStart - (clusters[idx].qStart + 300);
						}
						else {curGenomeEnd = genome.header.pos[ChromIndex];}
						curReadEnd = 0;
						nextGenomeStart = clusters[idx].tStart;
						nextReadStart = clusters[idx].qStart;
					} 
					else { // the chain is in the reverse direction
						if (clusters[idx].tStart > read.length - clusters[idx].qEnd + 300 + genome.header.pos[ChromIndex]) {
							curGenomeEnd = clusters[idx].tStart - (read.length - clusters[idx].qEnd + 300);
						}
						else {curGenomeEnd = genome.header.pos[ChromIndex];}
						curReadEnd = 0;
						nextGenomeStart = clusters[idx].tStart; // flip the box into forward direction
						nextReadStart = read.length - clusters[idx].qEnd;
					}	
					subreadLength = nextReadStart - curReadEnd;
					subgenomeLength = nextGenomeStart - curGenomeEnd;
					assert(nextReadStart >= curReadEnd);
					assert(nextGenomeStart >= curGenomeEnd);
					

					if (subreadLength > 500 and subreadLength < 1000) { // TODO(Jingwen): change the way to store minimizers (Allopts) check the begining
						StoreMinimizers<GenomeTuple, Tuple>(genome.seqs[ChromIndex] + (curGenomeEnd - genome.header.pos[ChromIndex]), subgenomeLength, opts.globalK, 1, EndGenomeTup, false);
						sort(EndGenomeTup.begin(), EndGenomeTup.end());
						StoreMinimizers<GenomeTuple, Tuple>(strands[clusters[idx].strand] + curReadEnd, subreadLength, opts.globalK, 1, EndReadTup, false);
						sort(EndReadTup.begin(), EndReadTup.end());
						CompareLists(EndReadTup.begin(), EndReadTup.end(), EndGenomeTup.begin(), EndGenomeTup.end(), EndPairs, opts);
						
						for(int rm=0; rm < EndPairs.size(); rm++) {
							EndPairs[rm].first.pos  += curReadEnd;
							EndPairs[rm].second.pos += curGenomeEnd;
							assert(EndPairs[rm].first.pos < read.length);
						}
						// TODO(Jingwen): add a clean off diagonal function here to remove noisy anchors
						// Set boundaries for EndPairs
						qStart = read.length; qEnd = 0; tStart = genome.header.pos[ChromIndex + 1]; tEnd = genome.header.pos[ChromIndex];
						for (int rm = 0; rm < EndPairs.size(); rm++) {
							// Here qStart and qEnd are all in forward direction
							qStart = min(EndPairs[rm].first.pos, qStart);
							qEnd = max(EndPairs[rm].first.pos + opts.globalK, qEnd);
							tStart = min(EndPairs[rm].second.pos, tStart);
							tEnd = max(EndPairs[rm].second.pos + opts.globalK, tEnd);
						}

						if (EndPairs.size() != 0) {
							clusters[id].matches.insert(clusters[id].matches.end(), EndPairs.begin(), EndPairs.end());
							logClusters[num].SubCluster.back().end = clusters[id].matches.size();
							clusters[id].qEnd = max(clusters[id].qEnd, qEnd);
							clusters[id].qStart = min(clusters[id].qStart, qStart);
							clusters[id].tEnd = max(clusters[id].tEnd, tEnd);
							clusters[id].tStart = min(clusters[id].tStart, tStart);	
							logClusters[num].SubCluster.back().qStart = min(logClusters[num].SubCluster.back().qStart, qStart);
							logClusters[num].SubCluster.back().qEnd = max(logClusters[num].SubCluster.back().qEnd, qEnd);
							logClusters[num].SubCluster.back().tStart = min(logClusters[num].SubCluster.back().tStart, tStart);
							logClusters[num].SubCluster.back().tEnd = max(logClusters[num].SubCluster.back().tEnd, tEnd);
						}
					}									
				}

				logClusters[num].SetCoarse();	
				++num;			
			}
			Psize += Primary_chains[c].chains.size();
		}		

		//
		// Remove unnecessary clusters[i]
		//
		vector<int> clustersempty = ind;
		if (clusters.size() > 0) {
			for (int c = 1; c < clusters.size(); ++c) {
				clustersempty[c] += clustersempty[c-1];
			}				
		}
		//
		// Delete logClusters[i] (the firstChromIndex != lastChromIndex)
		//
		int cd = 0;
		for (int c = 0; c < logClusters.size(); c++) {
			if (Dele[c] == 0) {
				logClusters[cd] = logClusters[c];
				cd++;
			}
		}
		logClusters.resize(cd);
		//
		//logClusters[c] store information for anchors in cluster[logClusters[c].coarse]
		//
		for (int c = 0; c < logClusters.size(); ++c) {
			logClusters[c].coarse = clustersempty[logClusters[c].coarse] - 1; 
		}
		
		int cl=0;
		int cn;
		for(cl=0, cn=0; cn < clusters.size(); cn++) {
			if (ind[cn] == 1) {
				clusters[cl] = clusters[cn];
				cl++;
			}
		}
		clusters.resize(cl);
		//Primary_chains.clear();


		if (opts.dotPlot) {
			ofstream matchfile("long_matches.tab");
			for (int m = 0; m < matches.size(); m++) {
				matchfile << matches[m].first.pos << "\t" << matches[m].second.pos << "\t" << opts.globalK << "\t0\t0" << endl;
			}
			matchfile.close();
			ofstream clust("clusters.tab");
			for (int c = 0; c < logClusters.size(); c++) {
		
				for (int n=0; n<logClusters[c].SubCluster.size();n++) {

					for (int m=logClusters[c].SubCluster[n].start; m<logClusters[c].SubCluster[n].end; m++) {

						if (logClusters[c].SubCluster[n].strand == 0) {
							clust << clusters[logClusters[c].coarse].matches[m].first.pos << "\t"
								  << clusters[logClusters[c].coarse].matches[m].second.pos << "\t" 
								  << clusters[logClusters[c].coarse].matches[m].first.pos + opts.globalK << "\t" 
								  << clusters[logClusters[c].coarse].matches[m].second.pos + opts.globalK  << "\t" 
								 << n << "\t" 
								 << logClusters[c].SubCluster[n].strand << endl;
						}
						else {
							clust 	<< read.length - clusters[logClusters[c].coarse].matches[m].first.pos - 1 << "\t" 
									<< clusters[logClusters[c].coarse].matches[m].second.pos << "\t" 
									<< read.length - clusters[logClusters[c].coarse].matches[m].first.pos - 1 - opts.globalK << "\t"
									<< clusters[logClusters[c].coarse].matches[m].second.pos + opts.globalK << "\t"
									<<  n << "\t" 
									<< logClusters[c].SubCluster[n].strand << endl;
						}
					}
				}
			}
			clust.close();	
		}

		//RemoveOverlappingClusters(clusters, opts); //TODO(Jingwen): check whether need to keep this

		// Merge overlapping clusters
		//
		//RemoveEmptyClusters(clusters, opts.minClusterSize);
		if (opts.mergeGapped) {
			ClusterOrder clusterOrder(&clusters);
			clusterOrder.Sort();
			MergeOverlappingClusters(clusterOrder);
		}


		//
		// Build local index for refining alignments.
		//
		LocalIndex forwardIndex(glIndex);
		LocalIndex reverseIndex(glIndex);

		LocalIndex *localIndexes[2] = {&forwardIndex, &reverseIndex};
		forwardIndex.IndexSeq(read.seq, read.length);
		reverseIndex.IndexSeq(readRC, read.length); 

		// Set the parameters for merging anchors and 1st SDP
		Options smallOpts = opts;
		//smallOpts.maxDiag=2;

		/*
		smallOpts.globalK=glIndex.k;
		smallOpts.globalW=glIndex.w;
		smallOpts.globalMaxFreq=6;
		smallOpts.cleanMaxDiag=10;// used to be 25
		//smallOpts.maxGapBtwnAnchors=100; // used to be 200 // 200 seems a little bit large
		smallOpts.maxDiag=50;
		smallOpts.maxGapBtwnAnchors=2000; // used to be 200 // 200 seems a little bit large
		smallOpts.minDiagCluster=50; // used to be 3
	 	*/
		Options tinyOpts = smallOpts;
		tinyOpts.globalMaxFreq=3;
		tinyOpts.maxDiag=5;
		tinyOpts.minDiagCluster=20;
		tinyOpts.globalK=smallOpts.globalK-3;
		tinyOpts.minRemoveSpuriousAnchorsNum=20;
		tinyOpts.maxRemoveSpuriousAnchorsDist=100;


		for (int c = 0; c < logClusters.size(); c++) {
			
			if (clusters[logClusters[c].coarse].matches.size() == 0) {
				continue;
			}			

			//
			// Get the boundaries of the cluster in genome sequence.
			//
			int nMatch = clusters[logClusters[c].coarse].matches.size();
			GenomePos tPos = clusters[logClusters[c].coarse].tStart;
			int firstChromIndex = genome.header.Find(tPos);
			int lastChromIndex;
			if (nMatch > 1 ) {
				tPos = clusters[logClusters[c].coarse].tEnd;
				lastChromIndex = genome.header.Find(tPos);
			} else { 
				lastChromIndex = firstChromIndex; 
			}
			clusters[logClusters[c].coarse].chromIndex = firstChromIndex;  
			if (firstChromIndex != lastChromIndex ) {
				clusters[logClusters[c].coarse].matches.clear();
				continue;
			}
		}

		//
		// need to flip rev matches back to rev direction, since there is no refining step here
		//
		for (int c = 0; c < logClusters.size(); c++) {
			for (int sc = 0; sc < logClusters[c].SubCluster.size(); sc++) {
				if (logClusters[c].SubCluster[sc].strand == 1) {
					for (int m=logClusters[c].SubCluster[sc].start; m<logClusters[c].SubCluster[sc].end; m++) {
						clusters[logClusters[c].coarse].matches[m].first.pos = read.length - (clusters[logClusters[c].coarse].matches[m].first.pos + opts.globalK);
					}		
					GenomePos E = logClusters[c].SubCluster[sc].qEnd;
					GenomePos S = logClusters[c].SubCluster[sc].qStart;
					logClusters[c].SubCluster[sc].qStart = 	read.length - E;
					logClusters[c].SubCluster[sc].qEnd = read.length - S;
				}
			}
			if (logClusters[c].direction == 1) {
				reverse(logClusters[c].SubCluster.begin(), logClusters[c].SubCluster.end()); // Merge anchors step requires this order
			}
		}


		//
		// Remove clusters under some minimal number of anchors. By default this is one. 
		//
		//RemoveEmptyClusters(clusters);
		vector<SegAlignmentGroup> alignments(clusters.size()); // alignments[i] stores the alignment for one chain
		for (int r = 0; r < logClusters.size(); ++r) {   

			if (clusters[logClusters[r].coarse].matches.size() < opts.minRefinedClusterSize) {
				continue;
			}

			ofstream dotFile;
			if (opts.dotPlot) {
				stringstream outName;
				outName << baseName << "." << r << ".dots";
				dotFile.open(outName.str().c_str());
			}

			//
			// Clean local matches to reduce chaining burden.
			//
			// TODO(Jingwen): already did the above DiagonalSort and CleanoffDiagonal above
			/*
			DiagonalSort<GenomeTuple>(clusters[r].matches); // sort first by forward diagonal and then by first.pos
			CleanOffDiagonal(clusters[r].matches, smallOpts);
			*/

			if (logClusters[r].SubCluster.size() == 0) {
				continue;
			}
			//if (clusters[r].matches.size() > read.length or opts.refineLevel & REF_LOC == 0) {
			//	clusters[r].matches = clusters[clusters[r].coarse].matches;
			//	continue;
			//}

			bool ReverseOnly = 1; // ReverseOnly == 1 means there are only reverse matches
								  // If ReverseOnly == 1, then only inserting two points s2, e2 for every reversed match in 1-st SDP.
			for (int m = 0; m < logClusters[r].SubCluster.size(); ++m) {
				if (logClusters[r].SubCluster[m].strand != 1) ReverseOnly = 0;
			} 

			//
			// At this point in the code, clusters[r].matches is a list
			// of k-mer matches between the read and the genome. 
			
			//
			// This is where the code should go for merging colinear matches

			// Jingwen: Instead of copying directly from clusters[r].matches into the seed set, you can use your code to:
			//  1. Merge adjacent anchors (using "merge" from MergeSplit.h)
			//  2. Split overlapping anchors.
			// 
			// The container of matches is 
			//	vector<Cluster> clusters(clusters.size());
			// Each of these has a vector 'matches', clusters[r].matches
			// 'matches' is defined as 	GenomePairs matches;
			// GenomePairs is a list of GenomePair:
			// typedef vector<GenomePair > GenomePairs;
			// a GenomePair is a pair of indexes into a genome or a read (both called genomes)
			// it is defined as:
			// typedef pair<GenomeTuple, GenomeTuple> GenomePair;
			// You access the first using 'first', as in:
			// GenomePair p;
			// p.first.pos = 1000;
			// p.second.pos=2000;
			//   Each GenomeTuple contains a 'pos' , or the position of a k-mer in a genome (or read), and a tuple, which is just a k-mer. 
			//  When there is a GenomePair, the two tuples are the same!
			//

			// The k-mer that is used in the refined matches is store in 
			// smallOpts.globalK


			vector<ClusterCoordinates> mergedAnchors;
			//bool WholeReverseDirection = 0; 
			// 
			bool Mix = 0; // Mix == 1 means that there are both anchors in forward and reverse direction
			int wr = 0;
			GenomePos prev_qEnd = 0, cur_qEnd = 0;
			if (logClusters[r].SubCluster.size() == 1) {Mix = 0;} 
			else {
				for (int m = 1; m < logClusters[r].SubCluster.size(); ++m) {
					prev_qEnd = logClusters[r].SubCluster[m-1].qEnd;
					cur_qEnd = logClusters[r].SubCluster[m].qEnd;
					if (prev_qEnd >= cur_qEnd) {++wr;}
					if (logClusters[r].SubCluster[m].strand != logClusters[r].SubCluster[m - 1].strand) {Mix = 1;}
				}
				if (wr != logClusters[r].SubCluster.size() - 1 and wr != 0) {continue;} // the whole chain is in forward direction
				//else if (wr == 0) {WholeReverseDirection = 1;} // the whole chain is in reverse direction
				//else {continue;}				
			}


			//
			// If the chain is in reverse direction and there exist both forward and reverse anchors, flip it into a forward direction, so that SDP can work 
			//		
			bool WholeReverseDirection = logClusters[r].direction; // WholeReverseDirection == 1 means the read is reversely mapped to reference;
											// This is important when deciding the boundary in MergeAnchor function
			if (logClusters[r].direction == 1 and Mix == 1) {
				WholeReverseDirection = 0;
				GenomePos qs, qe;
				for (int m = 0; m < logClusters[r].SubCluster.size(); m++) {
					if (logClusters[r].SubCluster[m].strand == 0) {logClusters[r].SubCluster[m].strand = 1;}
					else {logClusters[r].SubCluster[m].strand = 0;}

					qs = logClusters[r].SubCluster[m].qStart;
					qe = logClusters[r].SubCluster[m].qEnd;
					logClusters[r].SubCluster[m].qStart = read.length - qe;
					logClusters[r].SubCluster[m].qEnd = read.length - qs;

					for (int n = logClusters[r].SubCluster[m].start; n < logClusters[r].SubCluster[m].end; n++) {
						clusters[logClusters[r].coarse].matches[n].first.pos = read.length - clusters[logClusters[r].coarse].matches[n].first.pos - smallOpts.globalK;
					}
					qs = clusters[logClusters[r].coarse].qStart;
					qe = clusters[logClusters[r].coarse].qEnd;
					clusters[logClusters[r].coarse].qStart = read.length - qe;
					clusters[logClusters[r].coarse].qEnd = read.length - qs;
				}
			}


			if (opts.dotPlot) {
				stringstream outNameStrm;
				outNameStrm << baseName + "." << r << ".orig.dots";
				ofstream baseDots(outNameStrm.str().c_str());
				for (int m=0; m<logClusters[r].SubCluster.size(); m++) {

					for (int n=logClusters[r].SubCluster[m].start; n<logClusters[r].SubCluster[m].end; n++) {
						if (logClusters[r].SubCluster[m].strand == 0) {
							baseDots << clusters[logClusters[r].coarse].matches[n].first.pos << "\t" << clusters[logClusters[r].coarse].matches[n].second.pos << "\t" 
										<< clusters[logClusters[r].coarse].matches[n].first.pos + smallOpts.globalK << "\t" 
										<< clusters[logClusters[r].coarse].matches[n].second.pos + smallOpts.globalK << "\t"  
										<< m << "\t"
										<< logClusters[r].SubCluster[m].strand << endl;
						}
						else {
							baseDots << clusters[logClusters[r].coarse].matches[n].first.pos << "\t" << clusters[logClusters[r].coarse].matches[n].second.pos + smallOpts.globalK << "\t"
										<< clusters[logClusters[r].coarse].matches[n].first.pos + smallOpts.globalK << "\t" 
										<< clusters[logClusters[r].coarse].matches[n].second.pos  << "\t"  
										<< m << "\t"
										<< logClusters[r].SubCluster[m].strand << endl;							
						}
					}
				}
				baseDots.close();
			}

			if (opts.dotPlot) {
				stringstream outNameStrm;
				outNameStrm << baseName + "." << r << ".clean.dots";
				ofstream baseDots(outNameStrm.str().c_str());
				for (int m=0; m<logClusters[r].SubCluster.size(); ++m) {

					for (int n=logClusters[r].SubCluster[m].start; n<logClusters[r].SubCluster[m].end; n++) {
						if (logClusters[r].SubCluster[m].strand == 0) {
							baseDots << clusters[logClusters[r].coarse].matches[n].first.pos << "\t" << clusters[logClusters[r].coarse].matches[n].second.pos << "\t" 
										<< clusters[logClusters[r].coarse].matches[n].first.pos + smallOpts.globalK << "\t" 
										<< clusters[logClusters[r].coarse].matches[n].second.pos + smallOpts.globalK << "\t"  
										<< m << "\t"
										<< logClusters[r].SubCluster[m].strand << endl;
						}
						else {
							baseDots << clusters[logClusters[r].coarse].matches[n].first.pos << "\t" << clusters[logClusters[r].coarse].matches[n].second.pos + smallOpts.globalK << "\t"
										<< clusters[logClusters[r].coarse].matches[n].first.pos + smallOpts.globalK << "\t" 
										<< clusters[logClusters[r].coarse].matches[n].second.pos  << "\t"  
										<< m << "\t"
										<< logClusters[r].SubCluster[m].strand << endl;							
						}
					}
				}
				baseDots.close();
			}


			if (opts.mergeClusters) {

				mergedAnchors.clear();
				//MergeClusters(smallOpts, clusters, vt, r);
				//mergeClusters (smallOpts, clusters[r].matches, vt, r, baseName);
				MergeAnchors (smallOpts, clusters, logClusters, r, mergedAnchors, WholeReverseDirection);
				//MergeAnchors (smallOpts, clusters, logClusters, r, mergedAnchors, 0);
				int MergeAnchorsNum = 0;
				for (int m = 0; m < mergedAnchors.size(); m++) {
					MergeAnchorsNum += mergedAnchors[m].end - mergedAnchors[m].start;
				}
				// TODO(Jingwen): only for debug the new MergeAnchors function from MergeAnchors.h
				if (opts.dotPlot) {
					stringstream outNameStrm;
					outNameStrm << baseName + "." << r << ".merged.dots";
					ofstream baseDots(outNameStrm.str().c_str());
					for (int m=0; m < mergedAnchors.size(); m++) {

						if (mergedAnchors[m].strand == 0) {
							// chain stores indices which refer to elments in vt
							baseDots << mergedAnchors[m].qStart << "\t" 
									 << mergedAnchors[m].tStart << "\t" 
									 << mergedAnchors[m].qEnd << "\t" 
									 << mergedAnchors[m].tEnd << "\t"
									 << r << "\t"
									 << mergedAnchors[m].strand << endl;									
						}		
						else {
							baseDots << mergedAnchors[m].qStart << "\t" 
									 << mergedAnchors[m].tEnd << "\t" 
									 << mergedAnchors[m].qEnd << "\t" 
									 << mergedAnchors[m].tStart << "\t"
									 << r << "\t"
									 << mergedAnchors[m].strand << endl;								
						}	

					}
					baseDots.close();
				}
				//
				// Output the anchor efficiency file TODO(Jingwen): delete this later
				/*				
				if (clusters[r].matches.size() != 0) {
					
					stringstream outNameStrm;
					outNameStrm << "AnchorEfficiency.tab";
					ofstream baseDots;
					baseDots.open(outNameStrm.str().c_str(), std::ios::app);
					baseDots << mergedAnchors.size() << "\t" << clusters[r].matches.size() << "\t" 
								<< read.name << endl;
					baseDots.close();						
				}
				*/
				
			}
			if (mergedAnchors.size() == 0) continue;


			// Perform sparse chaining, uses time O(n (log n)^2).
			//
			// Merge anchors that are not overlapping
			//
			// The input are a buntch of fragments which are either stored in vector<Cluster> vt or clusters[r].matches
			// 
			vector<unsigned int> chain; // chain contains the index of the fragments involved in the result of SparseDP.
			if (opts.SparseDP) {
				chain.clear();
				if (opts.mergeClusters and mergedAnchors.size() < 1000000 and mergedAnchors.size() > 0) {
					if (clusters[logClusters[r].coarse].matches.size()/((float)(min(clusters[logClusters[r].coarse].qEnd - clusters[logClusters[r].coarse].qStart, clusters[logClusters[r].coarse].tEnd - clusters[logClusters[r].coarse].tStart))) < 0.1) {
						//SparseDP(mergedAnchors, chain, smallOpts, LookUpTable, ReverseOnly, 5); 
					}
					else {
						//SparseDP(mergedAnchors, chain, smallOpts, LookUpTable, ReverseOnly);
					}
				}
				else if (clusters[logClusters[r].coarse].matches.size() < 1000000) {
					if (clusters[logClusters[r].coarse].matches.size()/((float)(min(clusters[logClusters[r].coarse].qEnd - clusters[logClusters[r].coarse].qStart, clusters[logClusters[r].coarse].tEnd - clusters[logClusters[r].coarse].tStart))) < 0.1) {
						//SparseDP(clusters[logClusters[r].coarse].matches, chain, smallOpts, LookUpTable, logClusters[r], clusters[logClusters[r].coarse].strands, ReverseOnly, 10);
					}
					else { 
						// If anchors are unmerged, then we need to give a higher anchor value to every anchor
						// Since gap cost of chaining is higher.
						//SparseDP(clusters[logClusters[r].coarse].matches, chain, smallOpts, LookUpTable, logClusters[r], clusters[logClusters[r].coarse].strands, ReverseOnly, 5);
					}
				}
			}

			if (chain.size() == 0) continue;

			//
			// If the chain is in reverse direction and there exist both forward and reverse anchors, flip it back since now it's in forward direction
			//
			/*
			if (WholeReverseDirection == 1 and Mix == 1) {
				GenomePos qs, qe;
				for (int m = 0; m < logClusters[r].SubCluster.size(); m++) {
					if (logClusters[r].SubCluster[m].strand == 0) {logClusters[r].SubCluster[m].strand = 1;}
					else {logClusters[r].SubCluster[m].strand = 0;}

					qs = logClusters[r].SubCluster[m].qStart;
					qe = logClusters[r].SubCluster[m].qEnd;
					logClusters[r].SubCluster[m].qStart = read.length - qe;
					logClusters[r].SubCluster[m].qEnd = read.length - qs;

					for (int n = logClusters[r].SubCluster[m].start; n < logClusters[r].SubCluster[m].end; n++) {
						clusters[r].matches[n].first.pos = read.length - clusters[r].matches[n].first.pos - smallOpts.globalK;
					}
					qs = clusters[r].qStart;
					qe = clusters[r].qEnd;
					clusters[r].qStart = read.length - qe;
					clusters[r].qEnd = read.length - qs;
				}

				if (mergeClusters) {
					for (int m = 0; m < mergedAnchors.size(); m++) {
						qs = mergedAnchors[m].qStart;
						qe = mergedAnchors[m].qEnd;
						mergedAnchors[m].qStart = read.length - qe;
						mergedAnchors[m].qEnd = read.length - qs;
						if (mergedAnchors[m].strand == 0) {mergedAnchors[m].strand = 1;}
						else {mergedAnchors[m].strand = 0;}
					}
				}
			}
			*/
				

			// TODO(Jingwen): Only for debug
			if (opts.dotPlot) {
				stringstream outNameStrm;
				outNameStrm << baseName + "." << r << ".first-sdp.dots";
				ofstream baseDots(outNameStrm.str().c_str());
				for (int c = 0; c < chain.size(); c++) {
					if (opts.mergeClusters) {

						if (mergedAnchors[chain[c]].strand == 0) {
							// chain stores indices which refer to elments in vt
							baseDots << mergedAnchors[chain[c]].qStart << "\t" 
									 << mergedAnchors[chain[c]].tStart << "\t" 
									 << mergedAnchors[chain[c]].qEnd << "\t" 
									 << mergedAnchors[chain[c]].tEnd << "\t"
									 << r << "\t"
									 << mergedAnchors[chain[c]].strand << endl;									
						}		
						else {
							baseDots << mergedAnchors[chain[c]].qStart << "\t" 
									 << mergedAnchors[chain[c]].tEnd << "\t" 
									 << mergedAnchors[chain[c]].qEnd << "\t" 
									 << mergedAnchors[chain[c]].tStart << "\t"
									 << r << "\t"
									 << mergedAnchors[chain[c]].strand << endl;								
						}	
						//TODO(Jingwen): delete the following code later
						if (c != chain.size() - 1) {
							assert(mergedAnchors[chain[c]].qStart < mergedAnchors[chain[c+1]].qStart);
							assert(mergedAnchors[chain[c]].qStart < mergedAnchors[chain[c+1]].qStart);
						}			
					}
					else {
						// chain stores indices which refer to elements in clusters[r].matches
						if (clusters[logClusters[r].coarse].strands[chain[c]] == 0) {
							baseDots << clusters[logClusters[r].coarse].matches[chain[c]].first.pos << "\t" 
									 << clusters[logClusters[r].coarse].matches[chain[c]].second.pos << "\t" 
									 << clusters[logClusters[r].coarse].matches[chain[c]].first.pos + smallOpts.globalK << "\t"
									 << clusters[logClusters[r].coarse].matches[chain[c]].second.pos + smallOpts.globalK << "\t"
									 << r << "\t"
									 << clusters[logClusters[r].coarse].strands[chain[c]] << endl;								
						}
						else {
							baseDots << clusters[logClusters[r].coarse].matches[chain[c]].first.pos << "\t" 
									 << clusters[logClusters[r].coarse].matches[chain[c]].second.pos + smallOpts.globalK << "\t" 
									 << clusters[logClusters[r].coarse].matches[chain[c]].first.pos + smallOpts.globalK << "\t"
									 << clusters[logClusters[r].coarse].matches[chain[c]].second.pos << "\t"
									 << r << "\t"
									 << clusters[logClusters[r].coarse].strands[chain[c]] << endl;	
						}		
						//TODO(Jingwen): delete the following code later
						if (c != chain.size() - 1) {
							assert(clusters[logClusters[r].coarse].matches[chain[c]].second.pos >= clusters[logClusters[r].coarse].matches[chain[c+1]].second.pos + smallOpts.globalK);
						}				
					}	
				}
				baseDots.close();
			}
	

			GenomePairs tupChain;
			vector<Cluster> tupChainClusters;
			vector<int> Tupchain; // stores the index of the genomepair in clusters[r].matches
			Tupchain.clear();
			tupChain.clear();
			tupChainClusters.clear();
			GenomePos alignReadStart = 0, alignReadEnd = 0, alignGenomeStart = 0, alignGenomeEnd = 0;

			if (opts.mergeClusters and mergedAnchors.size() > 0) {
				//
				//RemovePairedIndels(chain, mergedAnchors, opts);	
				//
				RemoveSpuriousAnchors(chain, opts, mergedAnchors, logClusters[r]);

				//
				// Add small anchors to Tupchain. (Use greedy algorithm to make small anchors not overlap with each other)
				// 
				vector<int> tupChainStrand;
				tupChainStrand.clear();
				for (int ch=0; ch < chain.size(); ch++) { 

					//cerr << "ch: "<< ch<< endl<<endl;
					//cerr << "chain[" << ch <<"]   " << chain[ch] << endl; 
					//
					// Use greedy algorithm to make small anchors not overlap with each other
					int id = mergedAnchors[chain[ch]].start;
					int c = 0;
					//cerr << "ch: " << ch << endl;
					//cerr << "id: " << id << endl;
					//cerr << "mergedAnchors[chain[ch]].strand: " << mergedAnchors[chain[ch]].strand << endl;
					if (mergedAnchors[chain[ch]].strand == 1) { // rev strand
						//cerr << "id: " << id << endl;
						Tupchain.push_back(id);
						tupChainStrand.push_back(mergedAnchors[chain[ch]].strand);
						c++;

						GenomePos qEnd = clusters[logClusters[r].coarse].matches[id].first.pos + smallOpts.globalK;
						GenomePos tEnd = clusters[logClusters[r].coarse].matches[id].second.pos; // should not use -1 here, because clusters[r].matches[id].second.pos might be 0
						//cerr << "qEnd: " << qEnd << endl;
						//cerr << "tEnd: " << tEnd << endl;
						int ce = id + 1;
						while (ce < mergedAnchors[chain[ch]].end) {

							while (ce < mergedAnchors[chain[ch]].end and 
									(clusters[logClusters[r].coarse].matches[ce].first.pos < qEnd or clusters[logClusters[r].coarse].matches[ce].second.pos + smallOpts.globalK >= tEnd)) { 
								++ce;
							}
							if (ce < mergedAnchors[chain[ch]].end) {

								Tupchain.push_back(ce);
								tupChainStrand.push_back(mergedAnchors[chain[ch]].strand);	
								c++;

								//
								// TODO(Jingwen): only for deubg and delete later
								int s = Tupchain.size() - 1;
								assert(clusters[logClusters[r].coarse].matches[Tupchain[s-1]].first.pos + smallOpts.globalK <= 
										clusters[logClusters[r].coarse].matches[Tupchain[s]].first.pos);
								assert(clusters[logClusters[r].coarse].matches[Tupchain[s]].second.pos + smallOpts.globalK <= 
										clusters[logClusters[r].coarse].matches[Tupchain[s-1]].second.pos);

								id = ce;
								qEnd = clusters[logClusters[r].coarse].matches[id].first.pos + smallOpts.globalK;
								tEnd = clusters[logClusters[r].coarse].matches[id].second.pos;
								++ce;
							}
						}

					}
					else { // forward strand
						Tupchain.push_back(id);
						tupChainStrand.push_back(mergedAnchors[chain[ch]].strand);
						c++;
						//cerr << "push back: (" << clusters[r].matches[id].first.pos << ", " <<  clusters[r].matches[id].second.pos << ")" << endl;
						//cerr << "id: " << id << endl;

						GenomePos qEnd = clusters[logClusters[r].coarse].matches[id].first.pos + smallOpts.globalK;
						GenomePos tEnd = clusters[logClusters[r].coarse].matches[id].second.pos + smallOpts.globalK;	

						int ce = id + 1;
						while (ce < mergedAnchors[chain[ch]].end) {
							
							while (ce < mergedAnchors[chain[ch]].end and 
									(clusters[logClusters[r].coarse].matches[ce].first.pos < qEnd or clusters[logClusters[r].coarse].matches[ce].second.pos < tEnd)) { 
								++ce;
							}
							if (ce < mergedAnchors[chain[ch]].end) {

								Tupchain.push_back(ce);
								tupChainStrand.push_back(mergedAnchors[chain[ch]].strand);
								c++;
								//cerr << "push back: (" << clusters[r].matches[ce].first.pos << ", " <<  clusters[r].matches[ce].second.pos << ")" << endl;
								//cerr << "ce: " << ce << endl;

								//
								// TODO(Jingwen): Only for debug and delete this later
								int s = Tupchain.size() - 1;
								assert(clusters[logClusters[r].coarse].matches[Tupchain[s-1]].first.pos + smallOpts.globalK <= 
										clusters[logClusters[r].coarse].matches[Tupchain[s]].first.pos);
								assert(clusters[logClusters[r].coarse].matches[Tupchain[s-1]].second.pos + smallOpts.globalK <= 
										clusters[logClusters[r].coarse].matches[Tupchain[s]].second.pos);
	
								id = ce;
								qEnd = clusters[logClusters[r].coarse].matches[id].first.pos + smallOpts.globalK;
								tEnd = clusters[logClusters[r].coarse].matches[id].second.pos + smallOpts.globalK;	
								++ce;
							}
						}
					}
				}				

				assert(Tupchain.size() == tupChainStrand.size());// TODO(Jingwen): only for debug and delete this later
				// 
				// Remove paired indels on Tupchain
				// And remove spurious anchors of different strand inside tupChainClusters[i]
				RemovePairedIndels(Tupchain, tupChainStrand, clusters[logClusters[r].coarse].matches, read.length, smallOpts);

				//
				// Add anchors to tupChain
				for (int at = 0; at < Tupchain.size(); ++at) {
					if (tupChainStrand[at] == 0) {
						tupChain.push_back(GenomePair(GenomeTuple(0, clusters[logClusters[r].coarse].matches[Tupchain[at]].first.pos), 
													GenomeTuple(0, clusters[logClusters[r].coarse].matches[Tupchain[at]].second.pos)));
					}
					else { // swap the rev anchors to forward direction
						tupChain.push_back(GenomePair(GenomeTuple(0, read.length - (clusters[logClusters[r].coarse].matches[Tupchain[at]].first.pos + smallOpts.globalK)), 
												GenomeTuple(0, clusters[logClusters[r].coarse].matches[Tupchain[at]].second.pos)));
					}
				}

				int cs = 0, ce = 0;
				while (ce < tupChainStrand.size()) {

					if (tupChainStrand[cs] == tupChainStrand[ce]) ce++;
					else {
						tupChainClusters.push_back(Cluster(cs, ce, tupChainStrand[cs]));
						cs = ce;
					}
				}
				if (ce == tupChainStrand.size() and cs < tupChainStrand.size()) {
						tupChainClusters.push_back(Cluster(cs, ce, tupChainStrand[cs]));
				}
			}
			else {
				//
				//
				RemoveSpuriousAnchors(chain, smallOpts, clusters[logClusters[r].coarse], logClusters[r]);

				// Remove paired indels
				// Also remove spirious anchors of different strand inside tupChainClusters[i]
				RemovePairedIndels(chain, clusters[logClusters[r].coarse].strands, clusters[logClusters[r].coarse].matches, read.length, smallOpts);

				//
				// chain stores indices which refer to elements in clusters[r].matches
				for (int ch=0; ch < chain.size(); ch++) {

					assert(clusters[logClusters[r].coarse].matches[chain[ch]].first.pos <= read.length); // TODO(Jingwen): delete this after debug
					// Swap anchors of reverse strand back to reverse strand coordinates, making it easier to fill up the gap
					if (clusters[logClusters[r].coarse].strands[chain[ch]] == 1) {
						tupChain.push_back(GenomePair(GenomeTuple(0, read.length - (clusters[logClusters[r].coarse].matches[chain[ch]].first.pos + smallOpts.globalK)), 
													GenomeTuple(0, clusters[logClusters[r].coarse].matches[chain[ch]].second.pos)));
					}
					else {
						tupChain.push_back(GenomePair(GenomeTuple(0, clusters[logClusters[r].coarse].matches[chain[ch]].first.pos), 
															GenomeTuple(0, clusters[logClusters[r].coarse].matches[chain[ch]].second.pos)));						
					}
				}


				int cs = 0, ce = 0;
				tupChainClusters.push_back(Cluster(0, 0, clusters[logClusters[r].coarse].strands[chain[0]]));
				while (ce < chain.size()) {
					if (clusters[logClusters[r].coarse].strands[chain[cs]] == clusters[logClusters[r].coarse].strands[chain[ce]]) ce++;
					else {
						tupChainClusters.push_back(Cluster(cs, ce, clusters[logClusters[r].coarse].strands[chain[cs]]));
						//if (ce - cs >= smallOpts.mintupChainClustersize) tupChainClusters.push_back(Cluster(cs, ce, clusters[r].strands[chain[cs]]));
						cs = ce;
					}
				}
				if (ce == chain.size() and cs < chain.size()) {
						tupChainClusters.push_back(Cluster(cs, ce, clusters[logClusters[r].coarse].strands[chain[cs]]));
				}
				tupChainClusters.push_back(Cluster(0, 0, clusters[logClusters[r].coarse].strands[chain.back()]));
			}


			int chromIndex = clusters[logClusters[r].coarse].chromIndex;
			//
			// scale the second.pos to certain chromosome instead of the whole genome;
			//
			for (int s = 0; s < tupChain.size(); s++) {
				tupChain[s].second.pos -= genome.header.pos[chromIndex];
			}

			//(TODO)Jingwen: For Debug(remove this later)
			if (opts.dotPlot) {
				stringstream outNameStrm;
				outNameStrm << baseName + "." << r << ".first-sdp-clean.dots";
				ofstream baseDots(outNameStrm.str().c_str());
				for (int m = 0; m < tupChainClusters.size(); m++) {
					for (int c = tupChainClusters[m].start; c < tupChainClusters[m].end; c++) {
	
						if (tupChainClusters[m].strand == 0) {
							baseDots << tupChain[c].first.pos << "\t" 
									 << tupChain[c].second.pos << "\t" 
									 << tupChain[c].first.pos + smallOpts.globalK << "\t" 
									 << tupChain[c].second.pos + smallOpts.globalK << "\t"
									 << m << "\t"
									 << tupChainClusters[m].strand << endl;								
						}
						else {
							baseDots << read.length - tupChain[c].first.pos - smallOpts.globalK << "\t" 
									 << tupChain[c].second.pos + smallOpts.globalK<< "\t" 
									 << read.length - tupChain[c].first.pos << "\t" 
									 << tupChain[c].second.pos << "\t"
									 << m << "\t"
									 << tupChainClusters[m].strand << endl;	

						}	
						//TODO(Jingwe): the following code is only for debug
						if (tupChainClusters[m].strand == 1 and c != tupChainClusters[m].end - 1) {
							//assert(tupChain[c].first.pos > tupChain[c+1].first.pos);
							//assert(tupChain[c].second.pos >= tupChain[c+1].second.pos + smallOpts.globalK);
						}
					}
				}
			}



			if (tupChain.size() == 0) {
				clusters[logClusters[r].coarse].matches.clear();
				continue;
			}

			// TODO(Jingwen): the following code is for removepairedIndels, check later how to modify it
			vector<Cluster> chainClust;
			Options diagOpts;
			diagOpts = smallOpts;
			diagOpts.maxDiag=15;
			diagOpts.minClusterSize=1;

			//
			// Create subsequences that will be used to generate the alignment.  Gaps should be inserted 
			// with respect to an offset from chainGenomeStart and chainReadStart
			//
			for (int s = 0; s < tupChainClusters.size(); s++) {

				// TODO(Jingwen): gapOpts should be replaced by tinyOpts
				Options gapOpts=opts;
				gapOpts.globalMaxFreq=5;
				gapOpts.globalK=7;
				vector<GenomeTuple> gapReadTup, gapGenomeTup;
				GenomePairs gapPairs;
				vector<GenomePairs> refinedChains(tupChainClusters[s].end - tupChainClusters[s].start - 1); // Note: refinedChains[i] stores the gap-fragments which locate after chain[i]
				vector<int> refinedChainsLength(tupChainClusters[s].end - tupChainClusters[s].start - 1, -1); // refinedChainsLength[i] stores the tinyOpts.globalK of the gap-fragments which locate after chain[i]
				vector<int> scoreMat;
				vector<Arrow> pathMat;
				int chainLength = tupChainClusters[s].end - tupChainClusters[s].start;
				

				if (tupChainClusters[s].strand == 1) reverse(tupChain.begin() + tupChainClusters[s].start, tupChain.begin() + tupChainClusters[s].end);
				for (int c = tupChainClusters[s].start; chainLength > 0 and c < tupChainClusters[s].end - 1; c++) {
					//cerr << "tupChainClusters[s].start: " << tupChainClusters[s].start << "  tupChainClusters[s].end: " << tupChainClusters[s].end << endl;
					//cerr << "c: " << c << endl;

					GenomePos curGenomeEnd = tupChain[c].second.pos + smallOpts.globalK;
					GenomePos curReadEnd = tupChain[c].first.pos + smallOpts.globalK;

					GenomePos nextGenomeStart = tupChain[c+1].second.pos;
					GenomePos nextReadStart = tupChain[c+1].first.pos;

					assert(nextReadStart >= curReadEnd);
					GenomePos subreadLength = nextReadStart - curReadEnd; 
					assert(nextGenomeStart >= curGenomeEnd);
					GenomePos subgenomeLength = nextGenomeStart - curGenomeEnd;


					if (nextReadStart > curReadEnd and nextGenomeStart > curGenomeEnd) {

						if (subreadLength > 50 and subgenomeLength > 50 and opts.refineLevel & REF_DYN) {

							// TODO(Jingwen): should we only consider minLen? because min(subreadLength, subgenomeLength) is the length of possible matches
							GenomePos maxLen = min(subreadLength, subgenomeLength);
							//GenomePos maxLen = min(subreadLength, subgenomeLength);
							if (maxLen < 500) {
								tinyOpts.globalK=5;
							}
							else if (maxLen < 2000) {
								tinyOpts.globalK=7;
							}
							else {
								tinyOpts.globalK=9;
							}
							gapGenomeTup.clear();
							gapReadTup.clear();
							gapPairs.clear();

							//
							// Find matches between read and reference in the coordinate space of read and chromosome
							//
							assert(curGenomeEnd < genome.lengths[chromIndex]);
							assert(curGenomeEnd + subgenomeLength < genome.lengths[chromIndex]);
							StoreMinimizers<GenomeTuple, Tuple>(genome.seqs[chromIndex] + curGenomeEnd, subgenomeLength, tinyOpts.globalK, 1, gapGenomeTup, false);

							sort(gapGenomeTup.begin(), gapGenomeTup.end());
							StoreMinimizers<GenomeTuple, Tuple>(strands[tupChainClusters[s].strand] + curReadEnd, subreadLength, tinyOpts.globalK, 1, gapReadTup, false);
							sort(gapReadTup.begin(), gapReadTup.end());
							CompareLists(gapReadTup.begin(), gapReadTup.end(), gapGenomeTup.begin(), gapGenomeTup.end(), gapPairs, tinyOpts);

							//
							// Remove egregious off diagonal seeds
							//
							DiagonalSort<GenomeTuple>(gapPairs); // sort gapPairs by forward diagonals and then by first.pos
							
							int tinyDiagStart = curReadEnd - curGenomeEnd;
							int tinyDiagEnd =  nextReadStart - nextGenomeStart;
							int diagDiff = abs((int) tinyDiagStart - (int) tinyDiagEnd);

							// TODO(Jingwen): after applying this CleanOffDiagonal, no gapPairs left
							// CleanOffDiagonal(gapPairs, tinyOpts, 0, diagDiff);
							//CartesianTargetSort<GenomeTuple>(gapPairs);

							for(int rm=0; rm < gapPairs.size(); rm++) {
								gapPairs[rm].first.pos  += curReadEnd;
								gapPairs[rm].second.pos += curGenomeEnd;
								assert(gapPairs[rm].first.pos < read.length); // TODO(Jingwen): delete this after debug
							}

							// gapPairs stores all the fragments in the gap. the length of the fragment here is tinyOpts.globalK
							// gapChain stores the index of fragments that are involved in the result of sdp
							vector<unsigned int> gapChain; 
							if (gapPairs.size() > 0) {
								if (opts.SparseDP) {
									gapChain.clear();
									if (gapPairs.size() < 100000) {
										if (gapPairs.size()/((float)min(subreadLength, subgenomeLength)) < 0.1) {
											SparseDP_ForwardOnly(gapPairs, gapChain, tinyOpts, LookUpTable, 5); // TODO(Jingwen): change the rate to customized number
										}
										else { 
											SparseDP_ForwardOnly(gapPairs, gapChain, tinyOpts, LookUpTable); 
										}
									}
								}
							}

							RemovePairedIndels(curReadEnd, curGenomeEnd, nextReadStart, nextGenomeStart, gapChain, gapPairs, tinyOpts);
							RemoveSpuriousAnchors(gapChain, tinyOpts, gapPairs);
							//cerr << "gapChain.size()/((float)min(subreadLength, subgenomeLength)): " << gapChain.size()/((float)min(subreadLength, subgenomeLength)) << endl;
							if (gapChain.size()/((float)min(subreadLength, subgenomeLength)) < 0.02) gapChain.clear();

							if (opts.dotPlot) {
								stringstream outNameStrm;
								outNameStrm << baseName + "." << r << ".second-sdp.dots";
								ofstream baseDots;
								baseDots.open(outNameStrm.str().c_str(), std::ios::app);
								for (int c = 0; c < gapChain.size(); c++) {
									// chain stores indices which refer to elements in clusters[r].matches
									if (tupChainClusters[s].strand == 0) {
										baseDots << gapPairs[gapChain[c]].first.pos << "\t" 
												 << gapPairs[gapChain[c]].second.pos << "\t" 
												 << tinyOpts.globalK + gapPairs[gapChain[c]].first.pos << "\t"
												 << tinyOpts.globalK + gapPairs[gapChain[c]].second.pos << "\t"
												 << r << "\t"
												 << tupChainClusters[s].strand << endl;														
									}
									else {	
										baseDots << read.length - gapPairs[gapChain[c]].first.pos - tinyOpts.globalK << "\t" 
												 << gapPairs[gapChain[c]].second.pos + tinyOpts.globalK << "\t" 
												 << read.length - gapPairs[gapChain[c]].first.pos << "\t"
												 << gapPairs[gapChain[c]].second.pos << "\t"
												 << r << "\t"
												 << tupChainClusters[s].strand << endl;												
									}
								}
								baseDots.close();
							}

							for (unsigned int u = 0; u < gapChain.size(); ++u) {
								refinedChains[c - tupChainClusters[s].start].push_back(gapPairs[gapChain[u]]);
								assert(refinedChains[c - tupChainClusters[s].start].back().first.pos <= read.length);

								// TODO(Jingwen): delete the debug code
								if (refinedChains[c - tupChainClusters[s].start].size() > 1) { 
									int last = refinedChains[c - tupChainClusters[s].start].size();
									assert(refinedChains[c - tupChainClusters[s].start][last -2].first.pos + tinyOpts.globalK <= refinedChains[c - tupChainClusters[s].start][last -1].first.pos);
									assert(refinedChains[c - tupChainClusters[s].start][last -2].second.pos + tinyOpts.globalK <= refinedChains[c - tupChainClusters[s].start][last -1].second.pos);						
								}
							}
							refinedChainsLength[c - tupChainClusters[s].start] = tinyOpts.globalK;	
							//cerr<< "s: " << s << "  c: " << c << "  gapPairs.size(): " << gapPairs.size() << "  refinedChains[c].size(): "<< refinedChains[c].size() << endl;
						}
					}	
				}					
				

				//
				// Refine and store the alignment
				//
				//
				// The alignment is on a substring that starts at the beginning of the first chain.
				//

				Alignment *alignment = new Alignment(strands[tupChainClusters[s].strand], read.seq, read.length, read.name, tupChainClusters[s].strand, genome.seqs[chromIndex],  
													genome.lengths[chromIndex], genome.header.names[chromIndex], chromIndex);	

				alignments[r].SegAlignment.push_back(alignment);
				if (logClusters[r].ISsecondary == 0) alignments[r].secondary = logClusters[r].secondary;
				else {
					alignments[r].ISsecondary = logClusters[r].ISsecondary;
					alignments[r].primary = logClusters[r].primary;					
				}
				if (logClusters[r].main != -1) {alignments[r].split = 1;}

				for (int c = tupChainClusters[s].start; chainLength > 0 and c < tupChainClusters[s].end - 1; c++) {
					//
					// Chain is with respect to full sequence
					//
					GenomePos curGenomeEnd     = tupChain[c].second.pos + smallOpts.globalK;
					GenomePos curReadEnd       = tupChain[c].first.pos + smallOpts.globalK;
					GenomePos nextGenomeStart  = tupChain[c+1].second.pos;
					GenomePos nextReadStart    = tupChain[c+1].first.pos;
					int curRefinedReadEnd      = curReadEnd;
					int curRefinedGenomeEnd    = curGenomeEnd;
					int nextRefinedReadStart   = nextReadStart;
					int nextRefinedGenomeStart = nextGenomeStart;

					if (opts.dotPlot) {
						if (tupChainClusters[s].strand == 0) {
							dotFile << tupChain[c].first.pos << "\t" 
									<< tupChain[c].second.pos << "\t" 
									<< tupChain[c].first.pos + smallOpts.globalK << "\t"
									<< tupChain[c].second.pos + smallOpts.globalK << "\t"
									<< r << "\t"
									<< tupChainClusters[s].strand << endl;					
						}
						else {
							dotFile << read.length - tupChain[c].first.pos - smallOpts.globalK << "\t" 
									<< tupChain[c].second.pos  + smallOpts.globalK << "\t" 
									<< read.length - tupChain[c].first.pos << "\t"
									<< tupChain[c].second.pos << "\t" 
									<< r << "\t"
									<< tupChainClusters[s].strand << endl;							
						}
					}
					/*
					if (WholeReverseDirection == 1 and Mix == 1) { // need to flip anchors for this
						alignment->blocks.push_back(Block(read.length - tupChain[c].first.pos + smallOpts.globalK, tupChain[c].second.pos, smallOpts.globalK)); 
					}
					else {alignment->blocks.push_back(Block(tupChain[c].first.pos, tupChain[c].second.pos, smallOpts.globalK)); }
					*/
					alignment->blocks.push_back(Block(tupChain[c].first.pos, tupChain[c].second.pos, smallOpts.globalK));

					if (alignment->blocks.size() > 1) {
						int last=alignment->blocks.size();
						/*
						if (WholeReverseDirection == 1 and Mix == 1) {
							assert((read.length - alignment->blocks[last-2].qPos)
									<= (read.length - alignment->blocks[last-1].qPos - alignment->blocks[last-1].length));
							assert((alignment->blocks[last-2].tPos + alignment->blocks[last-2].length) <= alignment->blocks[last-1].tPos);							
						}
						else {
							assert(alignment->blocks[last-2].qPos + alignment->blocks[last-2].length <= alignment->blocks[last-1].qPos);
							assert(alignment->blocks[last-2].tPos + alignment->blocks[last-2].length <= alignment->blocks[last-1].tPos);							
						}
						*/
						assert(alignment->blocks[last-2].qPos + alignment->blocks[last-2].length <= alignment->blocks[last-1].qPos);
						assert(alignment->blocks[last-2].tPos + alignment->blocks[last-2].length <= alignment->blocks[last-1].tPos);
					}

					for (int cs = 0; cs < refinedChains[c - tupChainClusters[s].start].size(); cs++) {
						//
						// Refined anchors are with respect to the chained sequence

						nextRefinedReadStart = refinedChains[c - tupChainClusters[s].start][cs].first.pos;
						nextRefinedGenomeStart = refinedChains[c - tupChainClusters[s].start][cs].second.pos;
						
						if (opts.dotPlot) {
							if (tupChainClusters[s].strand == 0) {
								dotFile << refinedChains[c - tupChainClusters[s].start][cs].first.pos << "\t" 
										<< refinedChains[c - tupChainClusters[s].start][cs].second.pos << "\t" 
										<< refinedChains[c - tupChainClusters[s].start][cs].first.pos + refinedChainsLength[c - tupChainClusters[s].start] << "\t"
										<< refinedChains[c - tupChainClusters[s].start][cs].second.pos + refinedChainsLength[c - tupChainClusters[s].start] << "\t" 
										<< r << "\t"
										<< tupChainClusters[s].strand << endl;								
							}
							else {
								dotFile << read.length - refinedChains[c - tupChainClusters[s].start][cs].first.pos - refinedChainsLength[c - tupChainClusters[s].start] << "\t" 
										<< refinedChains[c - tupChainClusters[s].start][cs].second.pos + refinedChainsLength[c - tupChainClusters[s].start]<< "\t" 
										<< read.length - refinedChains[c - tupChainClusters[s].start][cs].first.pos << "\t"
										<< refinedChains[c - tupChainClusters[s].start][cs].second.pos << "\t" 
										<< r << "\t"
										<< tupChainClusters[s].strand << endl;								
							}

						}

						// find small matches between fragments in gapChain
						int m, rg, gg;
						SetMatchAndGaps(curRefinedReadEnd, nextRefinedReadStart, curRefinedGenomeEnd, nextRefinedGenomeStart, m, rg, gg);
						if (m > 0) {
							Alignment betweenAnchorAlignment;
							if (opts.refineLevel & REF_DP) {						
								RefineSubstrings(strands[tupChainClusters[s].strand], curRefinedReadEnd, nextRefinedReadStart, genome.seqs[chromIndex], 
																 curRefinedGenomeEnd, nextRefinedGenomeStart, scoreMat, pathMat, betweenAnchorAlignment, opts);
								// TODO(Jingwen): check what betweenAnchorAlignment looks like
								// and flip it if necessary
								alignment->blocks.insert(alignment->blocks.end(), betweenAnchorAlignment.blocks.begin(), betweenAnchorAlignment.blocks.end());
								int b;
								for (b = 1; b < betweenAnchorAlignment.blocks.size(); b++) {
									assert(betweenAnchorAlignment.blocks[b-1].qPos + betweenAnchorAlignment.blocks[b-1].length <= betweenAnchorAlignment.blocks[b].qPos);
									assert(betweenAnchorAlignment.blocks[b-1].tPos + betweenAnchorAlignment.blocks[b-1].length <= betweenAnchorAlignment.blocks[b].tPos);						
								}
								betweenAnchorAlignment.blocks.clear();
							}
						}

						curRefinedReadEnd = refinedChains[c - tupChainClusters[s].start][cs].first.pos + refinedChainsLength[c - tupChainClusters[s].start];
						curRefinedGenomeEnd = refinedChains[c - tupChainClusters[s].start][cs].second.pos + refinedChainsLength[c - tupChainClusters[s].start];
						alignment->blocks.push_back(Block(nextRefinedReadStart, nextRefinedGenomeStart, curRefinedReadEnd - nextRefinedReadStart));

						if (alignment->blocks.size() > 1) {
							int last=alignment->blocks.size();
							assert(alignment->blocks[last-2].qPos + alignment->blocks[last-2].length <= alignment->blocks[last-1].qPos);
							assert(alignment->blocks[last-2].tPos + alignment->blocks[last-2].length <= alignment->blocks[last-1].tPos);
						}

					}
					// Add the last gap, or the only one if no refinements happened here.				 
					int match, readGap, genomeGap;
					SetMatchAndGaps(curRefinedReadEnd, nextReadStart, curRefinedGenomeEnd, nextGenomeStart, match, readGap, genomeGap);
					if (match > 0) {
						if (opts.refineLevel & REF_DP) {
							Alignment aln;
							assert(curRefinedReadEnd < read.length);
							assert(nextReadStart <read.length);
							assert(curRefinedGenomeEnd < genome.lengths[chromIndex]);
							assert(nextGenomeStart < genome.lengths[chromIndex]);
							RefineSubstrings(strands[tupChainClusters[s].strand], curRefinedReadEnd, nextReadStart, genome.seqs[chromIndex], 
															 curRefinedGenomeEnd, nextGenomeStart, scoreMat, pathMat, aln, opts);
							alignment->blocks.insert(alignment->blocks.end(), aln.blocks.begin(), aln.blocks.end());
							aln.blocks.clear();			
						}		
					}
				}
				alignment->blocks.push_back(Block(tupChain[tupChainClusters[s].end - 1].first.pos, tupChain[tupChainClusters[s].end - 1].second.pos, smallOpts.globalK));
			

				int nm=0;
				for(int b=0; b < alignment->blocks.size(); b++) {
					nm+= alignment->blocks[b].length;
				}
				alignment->nblocks = tupChainClusters[s].end - tupChainClusters[s].start;
				if (opts.dotPlot) {
					dotFile.close();
				}
				
				if (logClusters[r].direction == 1 and Mix == 1) {
					// flip the strand direction
					if (tupChainClusters[s].strand == 0) {
						(alignments[r].SegAlignment.back())->read = strands[1];
						(alignments[r].SegAlignment.back())->strand = 1;
					}
					else {
						(alignments[r].SegAlignment.back())->read = strands[0];
						(alignments[r].SegAlignment.back())->strand = 0;
					}
					/*
					for (int b = 0; b < (alignments[r].SegAlignment.back())->blocks.size(); b++) {
						(alignments[r].SegAlignment.back())->blocks[b].qPos = read.length - 
							((alignments[r].SegAlignment.back())->blocks[b].qPos + (alignments[r].SegAlignment.back())->blocks[b].length);
					}
					*/
				}

			}
		} 


		if (opts.dotPlot) {
			stringstream outNameStrm;
			//outNameStrm << baseName + "." << a << ".alignment.dots";
			ofstream baseDots;
			//baseDots.open(outNameStrm.str().c_str());
		

			for (int a=0; a < (int) alignments.size(); a++){

				outNameStrm << baseName + "." << a << ".alignment.dots";
				baseDots.open(outNameStrm.str().c_str());

				for (int s = 0; s < alignments[a].SegAlignment.size(); s++) {

					// for debug. TODO(Jingwen): delete this later fix the following code!!!
					for (int c = 0; c < alignments[a].SegAlignment[s]->blocks.size(); c++) {				
					
						if (alignments[a].SegAlignment[s]->strand == 0) {
							baseDots << alignments[a].SegAlignment[s]->blocks[c].qPos << "\t" 
									 << alignments[a].SegAlignment[s]->blocks[c].tPos << "\t" 
									 << alignments[a].SegAlignment[s]->blocks[c].qPos + alignments[a].SegAlignment[s]->blocks[c].length << "\t" 
									 << alignments[a].SegAlignment[s]->blocks[c].tPos + alignments[a].SegAlignment[s]->blocks[c].length << "\t"
									 << a << "\t"
									 << s << "\t"
									 << alignments[a].SegAlignment[s]->strand << endl;							
						} 
						else {
							baseDots << read.length - alignments[a].SegAlignment[s]->blocks[c].qPos - alignments[a].SegAlignment[s]->blocks[c].length << "\t" 
									 << alignments[a].SegAlignment[s]->blocks[c].tPos + alignments[a].SegAlignment[s]->blocks[c].length << "\t" 
									 << read.length - alignments[a].SegAlignment[s]->blocks[c].qPos << "\t" 
									 << alignments[a].SegAlignment[s]->blocks[c].tPos << "\t"
									 << a << "\t"
									 << s << "\t"
									 << alignments[a].SegAlignment[s]->strand << endl;
						}
					}		
				}
			}
			
			baseDots.close();
		}

		// 
		// Flip back the reverse matches
		for (int a=0; a < (int) alignments.size(); a++){

			for (int s = 0; s < alignments[a].SegAlignment.size(); s++) {
				
				for (int c = 0; c < alignments[a].SegAlignment[s]->blocks.size(); c++) {
				
					if (alignments[a].SegAlignment[s]->strand == 1) {
						GenomePos E = alignments[a].SegAlignment[s]->blocks[c].qPos + alignments[a].SegAlignment[s]->blocks[c].length;
						alignments[a].SegAlignment[s]->blocks[c].qPos = read.length - E;
					}
				}
			}
		}

		int cm = 0;
		for (int a = 0; a < alignments.size(); a++) {
			if (alignments[a].SegAlignment.size() != 0) {
				alignments[cm] = alignments[a];
				for (int b = 0; b < alignments[cm].SegAlignment.size(); b++) {
					alignments[cm].SegAlignment[b]->CalculateStatistics(alignments[cm].SegAlignment.size(), alignments[cm].ISsecondary, alignments[cm].split);	
				}
				alignments[cm].SetBoundariesFromSegAlignmentAndnm(read);
				cm++;
			} 
		}
		alignments.resize(cm);
		//alignments.SetBoundariesFromSegAlignmentAndnm(read);

		sort(alignments.begin(), alignments.end(), SortAlignmentsByMatches());

		SimpleMapQV(alignments);

/*
		for (int a=0; a < (int) alignments.size(); a++){

			for (int s = 0; s < alignments[a].SegAlignment.size(); s++) {
				
				for (int c = 0; c < alignments[a].SegAlignment[s]->blocks.size(); c++) {
				
					if (alignments[a].SegAlignment[s]->strand == 1) {
						GenomePos E = alignments[a].SegAlignment[s]->blocks[c].qPos + alignments[a].SegAlignment[s]->blocks[c].length;
						alignments[a].SegAlignment[s]->blocks[c].qPos = read.length - E;
					}
				}
			}
		}
*/

		for (int a=0; a < (int) alignments.size(); a++){

			for (int s = 0; s < alignments[a].SegAlignment.size(); s++) {

				if (opts.printFormat == 'b') {
					alignments[a].SegAlignment[s]->PrintBed(*output);
				}
				else if (opts.printFormat == 's') {
					alignments[a].SegAlignment[s]->PrintSAM(*output, opts);
				}
				else if (opts.printFormat == 'p') {
					alignments[a].SegAlignment[s]->PrintPairwise(*output);
				}
			}
		}

		/*
		if (semaphore != NULL ) {
			pthread_mutex_unlock(semaphore);
		}
		*/

		//
		// Done with one read. Clean memory.
		//
		delete[] readRC;
		for (int a = 0; a < alignments.size(); a++) {
			for (int s = 0; s < alignments[a].SegAlignment.size(); s++) {
				delete alignments[a].SegAlignment[s];
			}
		}
		
		//read.Clear();

		/*
		// get the time for the program
		clock_t end = std::clock();
		double elapsed_secs = double(end - begin) / CLOCKS_PER_SEC;
		cerr << "Time: " << elapsed_secs << endl;
		*/
		if (alignments.size() > 0 ) {
			return 1;
		}
		else {
			return 0;
		}
	}
	return 0;
}

#endif
