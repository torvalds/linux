//===- SyncDependenceAnalysis.h - Divergent Branch Dependence -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// \file
// This file defines the SyncDependenceAnalysis class, which computes for
// every divergent branch the set of phi nodes that the branch will make
// divergent.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_SYNC_DEPENDENCE_ANALYSIS_H
#define LLVM_ANALYSIS_SYNC_DEPENDENCE_ANALYSIS_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Analysis/LoopInfo.h"
#include <memory>

namespace llvm {

class BasicBlock;
class DominatorTree;
class Loop;
class PostDominatorTree;

using ConstBlockSet = SmallPtrSet<const BasicBlock *, 4>;

/// \brief Relates points of divergent control to join points in
/// reducible CFGs.
///
/// This analysis relates points of divergent control to points of converging
/// divergent control. The analysis requires all loops to be reducible.
class SyncDependenceAnalysis {
  void visitSuccessor(const BasicBlock &succBlock, const Loop *termLoop,
                      const BasicBlock *defBlock);

public:
  bool inRegion(const BasicBlock &BB) const;

  ~SyncDependenceAnalysis();
  SyncDependenceAnalysis(const DominatorTree &DT, const PostDominatorTree &PDT,
                         const LoopInfo &LI);

  /// \brief Computes divergent join points and loop exits caused by branch
  /// divergence in \p Term.
  ///
  /// The set of blocks which are reachable by disjoint paths from \p Term.
  /// The set also contains loop exits if there two disjoint paths:
  /// one from \p Term to the loop exit and another from \p Term to the loop
  /// header. Those exit blocks are added to the returned set.
  /// If L is the parent loop of \p Term and an exit of L is in the returned
  /// set then L is a divergent loop.
  const ConstBlockSet &join_blocks(const Instruction &Term);

  /// \brief Computes divergent join points and loop exits (in the surrounding
  /// loop) caused by the divergent loop exits of\p Loop.
  ///
  /// The set of blocks which are reachable by disjoint paths from the
  /// loop exits of \p Loop.
  /// This treats the loop as a single node in \p Loop's parent loop.
  /// The returned set has the same properties as for join_blocks(TermInst&).
  const ConstBlockSet &join_blocks(const Loop &Loop);

private:
  static ConstBlockSet EmptyBlockSet;

  ReversePostOrderTraversal<const Function *> FuncRPOT;
  const DominatorTree &DT;
  const PostDominatorTree &PDT;
  const LoopInfo &LI;

  std::map<const Loop *, std::unique_ptr<ConstBlockSet>> CachedLoopExitJoins;
  std::map<const Instruction *, std::unique_ptr<ConstBlockSet>>
      CachedBranchJoins;
};

} // namespace llvm

#endif // LLVM_ANALYSIS_SYNC_DEPENDENCE_ANALYSIS_H
