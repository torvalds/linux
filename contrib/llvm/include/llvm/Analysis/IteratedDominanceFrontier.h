//===- IteratedDominanceFrontier.h - Calculate IDF --------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
/// Compute iterated dominance frontiers using a linear time algorithm.
///
/// The algorithm used here is based on:
///
///   Sreedhar and Gao. A linear time algorithm for placing phi-nodes.
///   In Proceedings of the 22nd ACM SIGPLAN-SIGACT Symposium on Principles of
///   Programming Languages
///   POPL '95. ACM, New York, NY, 62-73.
///
/// It has been modified to not explicitly use the DJ graph data structure and
/// to directly compute pruned SSA using per-variable liveness information.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_IDF_H
#define LLVM_ANALYSIS_IDF_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFGDiff.h"
#include "llvm/IR/Dominators.h"

namespace llvm {

/// Determine the iterated dominance frontier, given a set of defining
/// blocks, and optionally, a set of live-in blocks.
///
/// In turn, the results can be used to place phi nodes.
///
/// This algorithm is a linear time computation of Iterated Dominance Frontiers,
/// pruned using the live-in set.
/// By default, liveness is not used to prune the IDF computation.
/// The template parameters should be either BasicBlock* or Inverse<BasicBlock
/// *>, depending on if you want the forward or reverse IDF.
template <class NodeTy, bool IsPostDom>
class IDFCalculator {
 public:
   IDFCalculator(DominatorTreeBase<BasicBlock, IsPostDom> &DT)
       : DT(DT), GD(nullptr), useLiveIn(false) {}

   IDFCalculator(DominatorTreeBase<BasicBlock, IsPostDom> &DT,
                 const GraphDiff<BasicBlock *, IsPostDom> *GD)
       : DT(DT), GD(GD), useLiveIn(false) {}

   /// Give the IDF calculator the set of blocks in which the value is
   /// defined.  This is equivalent to the set of starting blocks it should be
   /// calculating the IDF for (though later gets pruned based on liveness).
   ///
   /// Note: This set *must* live for the entire lifetime of the IDF calculator.
   void setDefiningBlocks(const SmallPtrSetImpl<BasicBlock *> &Blocks) {
     DefBlocks = &Blocks;
   }

  /// Give the IDF calculator the set of blocks in which the value is
  /// live on entry to the block.   This is used to prune the IDF calculation to
  /// not include blocks where any phi insertion would be dead.
  ///
  /// Note: This set *must* live for the entire lifetime of the IDF calculator.

  void setLiveInBlocks(const SmallPtrSetImpl<BasicBlock *> &Blocks) {
    LiveInBlocks = &Blocks;
    useLiveIn = true;
  }

  /// Reset the live-in block set to be empty, and tell the IDF
  /// calculator to not use liveness anymore.
  void resetLiveInBlocks() {
    LiveInBlocks = nullptr;
    useLiveIn = false;
  }

  /// Calculate iterated dominance frontiers
  ///
  /// This uses the linear-time phi algorithm based on DJ-graphs mentioned in
  /// the file-level comment.  It performs DF->IDF pruning using the live-in
  /// set, to avoid computing the IDF for blocks where an inserted PHI node
  /// would be dead.
  void calculate(SmallVectorImpl<BasicBlock *> &IDFBlocks);

private:
 DominatorTreeBase<BasicBlock, IsPostDom> &DT;
 const GraphDiff<BasicBlock *, IsPostDom> *GD;
 bool useLiveIn;
 const SmallPtrSetImpl<BasicBlock *> *LiveInBlocks;
 const SmallPtrSetImpl<BasicBlock *> *DefBlocks;
};
typedef IDFCalculator<BasicBlock *, false> ForwardIDFCalculator;
typedef IDFCalculator<Inverse<BasicBlock *>, true> ReverseIDFCalculator;
}
#endif
