//===-- Analysis/CFG.h - BasicBlock Analyses --------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This family of functions performs analyses on basic blocks, and instructions
// contained within basic blocks.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_CFG_H
#define LLVM_ANALYSIS_CFG_H

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"

namespace llvm {

class BasicBlock;
class DominatorTree;
class Function;
class Instruction;
class LoopInfo;

/// Analyze the specified function to find all of the loop backedges in the
/// function and return them.  This is a relatively cheap (compared to
/// computing dominators and loop info) analysis.
///
/// The output is added to Result, as pairs of <from,to> edge info.
void FindFunctionBackedges(
    const Function &F,
    SmallVectorImpl<std::pair<const BasicBlock *, const BasicBlock *> > &
        Result);

/// Search for the specified successor of basic block BB and return its position
/// in the terminator instruction's list of successors.  It is an error to call
/// this with a block that is not a successor.
unsigned GetSuccessorNumber(const BasicBlock *BB, const BasicBlock *Succ);

/// Return true if the specified edge is a critical edge. Critical edges are
/// edges from a block with multiple successors to a block with multiple
/// predecessors.
///
bool isCriticalEdge(const Instruction *TI, unsigned SuccNum,
                    bool AllowIdenticalEdges = false);

/// Determine whether instruction 'To' is reachable from 'From',
/// returning true if uncertain.
///
/// Determine whether there is a path from From to To within a single function.
/// Returns false only if we can prove that once 'From' has been executed then
/// 'To' can not be executed. Conservatively returns true.
///
/// This function is linear with respect to the number of blocks in the CFG,
/// walking down successors from From to reach To, with a fixed threshold.
/// Using DT or LI allows us to answer more quickly. LI reduces the cost of
/// an entire loop of any number of blocks to be the same as the cost of a
/// single block. DT reduces the cost by allowing the search to terminate when
/// we find a block that dominates the block containing 'To'. DT is most useful
/// on branchy code but not loops, and LI is most useful on code with loops but
/// does not help on branchy code outside loops.
bool isPotentiallyReachable(const Instruction *From, const Instruction *To,
                            const DominatorTree *DT = nullptr,
                            const LoopInfo *LI = nullptr);

/// Determine whether block 'To' is reachable from 'From', returning
/// true if uncertain.
///
/// Determine whether there is a path from From to To within a single function.
/// Returns false only if we can prove that once 'From' has been reached then
/// 'To' can not be executed. Conservatively returns true.
bool isPotentiallyReachable(const BasicBlock *From, const BasicBlock *To,
                            const DominatorTree *DT = nullptr,
                            const LoopInfo *LI = nullptr);

/// Determine whether there is at least one path from a block in
/// 'Worklist' to 'StopBB', returning true if uncertain.
///
/// Determine whether there is a path from at least one block in Worklist to
/// StopBB within a single function. Returns false only if we can prove that
/// once any block in 'Worklist' has been reached then 'StopBB' can not be
/// executed. Conservatively returns true.
bool isPotentiallyReachableFromMany(SmallVectorImpl<BasicBlock *> &Worklist,
                                    BasicBlock *StopBB,
                                    const DominatorTree *DT = nullptr,
                                    const LoopInfo *LI = nullptr);

/// Return true if the control flow in \p RPOTraversal is irreducible.
///
/// This is a generic implementation to detect CFG irreducibility based on loop
/// info analysis. It can be used for any kind of CFG (Loop, MachineLoop,
/// Function, MachineFunction, etc.) by providing an RPO traversal (\p
/// RPOTraversal) and the loop info analysis (\p LI) of the CFG. This utility
/// function is only recommended when loop info analysis is available. If loop
/// info analysis isn't available, please, don't compute it explicitly for this
/// purpose. There are more efficient ways to detect CFG irreducibility that
/// don't require recomputing loop info analysis (e.g., T1/T2 or Tarjan's
/// algorithm).
///
/// Requirements:
///   1) GraphTraits must be implemented for NodeT type. It is used to access
///      NodeT successors.
//    2) \p RPOTraversal must be a valid reverse post-order traversal of the
///      target CFG with begin()/end() iterator interfaces.
///   3) \p LI must be a valid LoopInfoBase that contains up-to-date loop
///      analysis information of the CFG.
///
/// This algorithm uses the information about reducible loop back-edges already
/// computed in \p LI. When a back-edge is found during the RPO traversal, the
/// algorithm checks whether the back-edge is one of the reducible back-edges in
/// loop info. If it isn't, the CFG is irreducible. For example, for the CFG
/// below (canonical irreducible graph) loop info won't contain any loop, so the
/// algorithm will return that the CFG is irreducible when checking the B <-
/// -> C back-edge.
///
/// (A->B, A->C, B->C, C->B, C->D)
///    A
///  /   \
/// B<- ->C
///       |
///       D
///
template <class NodeT, class RPOTraversalT, class LoopInfoT,
          class GT = GraphTraits<NodeT>>
bool containsIrreducibleCFG(RPOTraversalT &RPOTraversal, const LoopInfoT &LI) {
  /// Check whether the edge (\p Src, \p Dst) is a reducible loop backedge
  /// according to LI. I.e., check if there exists a loop that contains Src and
  /// where Dst is the loop header.
  auto isProperBackedge = [&](NodeT Src, NodeT Dst) {
    for (const auto *Lp = LI.getLoopFor(Src); Lp; Lp = Lp->getParentLoop()) {
      if (Lp->getHeader() == Dst)
        return true;
    }
    return false;
  };

  SmallPtrSet<NodeT, 32> Visited;
  for (NodeT Node : RPOTraversal) {
    Visited.insert(Node);
    for (NodeT Succ : make_range(GT::child_begin(Node), GT::child_end(Node))) {
      // Succ hasn't been visited yet
      if (!Visited.count(Succ))
        continue;
      // We already visited Succ, thus Node->Succ must be a backedge. Check that
      // the head matches what we have in the loop information. Otherwise, we
      // have an irreducible graph.
      if (!isProperBackedge(Node, Succ))
        return true;
    }
  }

  return false;
}
} // End llvm namespace

#endif
