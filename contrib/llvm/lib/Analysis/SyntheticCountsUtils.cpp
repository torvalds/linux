//===--- SyntheticCountsUtils.cpp - synthetic counts propagation utils ---===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines utilities for propagating synthetic counts.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/SyntheticCountsUtils.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/ModuleSummaryIndex.h"

using namespace llvm;

// Given an SCC, propagate entry counts along the edge of the SCC nodes.
template <typename CallGraphType>
void SyntheticCountsUtils<CallGraphType>::propagateFromSCC(
    const SccTy &SCC, GetProfCountTy GetProfCount, AddCountTy AddCount) {

  DenseSet<NodeRef> SCCNodes;
  SmallVector<std::pair<NodeRef, EdgeRef>, 8> SCCEdges, NonSCCEdges;

  for (auto &Node : SCC)
    SCCNodes.insert(Node);

  // Partition the edges coming out of the SCC into those whose destination is
  // in the SCC and the rest.
  for (const auto &Node : SCCNodes) {
    for (auto &E : children_edges<CallGraphType>(Node)) {
      if (SCCNodes.count(CGT::edge_dest(E)))
        SCCEdges.emplace_back(Node, E);
      else
        NonSCCEdges.emplace_back(Node, E);
    }
  }

  // For nodes in the same SCC, update the counts in two steps:
  // 1. Compute the additional count for each node by propagating the counts
  // along all incoming edges to the node that originate from within the same
  // SCC and summing them up.
  // 2. Add the additional counts to the nodes in the SCC.
  // This ensures that the order of
  // traversal of nodes within the SCC doesn't affect the final result.

  DenseMap<NodeRef, Scaled64> AdditionalCounts;
  for (auto &E : SCCEdges) {
    auto OptProfCount = GetProfCount(E.first, E.second);
    if (!OptProfCount)
      continue;
    auto Callee = CGT::edge_dest(E.second);
    AdditionalCounts[Callee] += OptProfCount.getValue();
  }

  // Update the counts for the nodes in the SCC.
  for (auto &Entry : AdditionalCounts)
    AddCount(Entry.first, Entry.second);

  // Now update the counts for nodes outside the SCC.
  for (auto &E : NonSCCEdges) {
    auto OptProfCount = GetProfCount(E.first, E.second);
    if (!OptProfCount)
      continue;
    auto Callee = CGT::edge_dest(E.second);
    AddCount(Callee, OptProfCount.getValue());
  }
}

/// Propgate synthetic entry counts on a callgraph \p CG.
///
/// This performs a reverse post-order traversal of the callgraph SCC. For each
/// SCC, it first propagates the entry counts to the nodes within the SCC
/// through call edges and updates them in one shot. Then the entry counts are
/// propagated to nodes outside the SCC. This requires \p GraphTraits
/// to have a specialization for \p CallGraphType.

template <typename CallGraphType>
void SyntheticCountsUtils<CallGraphType>::propagate(const CallGraphType &CG,
                                                    GetProfCountTy GetProfCount,
                                                    AddCountTy AddCount) {
  std::vector<SccTy> SCCs;

  // Collect all the SCCs.
  for (auto I = scc_begin(CG); !I.isAtEnd(); ++I)
    SCCs.push_back(*I);

  // The callgraph-scc needs to be visited in top-down order for propagation.
  // The scc iterator returns the scc in bottom-up order, so reverse the SCCs
  // and call propagateFromSCC.
  for (auto &SCC : reverse(SCCs))
    propagateFromSCC(SCC, GetProfCount, AddCount);
}

template class llvm::SyntheticCountsUtils<const CallGraph *>;
template class llvm::SyntheticCountsUtils<ModuleSummaryIndex *>;
