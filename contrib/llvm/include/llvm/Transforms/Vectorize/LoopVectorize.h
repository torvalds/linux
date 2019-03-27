//===- LoopVectorize.h ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This is the LLVM loop vectorizer. This pass modifies 'vectorizable' loops
// and generates target-independent LLVM-IR.
// The vectorizer uses the TargetTransformInfo analysis to estimate the costs
// of instructions in order to estimate the profitability of vectorization.
//
// The loop vectorizer combines consecutive loop iterations into a single
// 'wide' iteration. After this transformation the index is incremented
// by the SIMD vector width, and not by one.
//
// This pass has three parts:
// 1. The main loop pass that drives the different parts.
// 2. LoopVectorizationLegality - A unit that checks for the legality
//    of the vectorization.
// 3. InnerLoopVectorizer - A unit that performs the actual
//    widening of instructions.
// 4. LoopVectorizationCostModel - A unit that checks for the profitability
//    of vectorization. It decides on the optimal vector width, which
//    can be one, if vectorization is not profitable.
//
// There is a development effort going on to migrate loop vectorizer to the
// VPlan infrastructure and to introduce outer loop vectorization support (see
// docs/Proposal/VectorizationPlan.rst and
// http://lists.llvm.org/pipermail/llvm-dev/2017-December/119523.html). For this
// purpose, we temporarily introduced the VPlan-native vectorization path: an
// alternative vectorization path that is natively implemented on top of the
// VPlan infrastructure. See EnableVPlanNativePath for enabling.
//
//===----------------------------------------------------------------------===//
//
// The reduction-variable vectorization is based on the paper:
//  D. Nuzman and R. Henderson. Multi-platform Auto-vectorization.
//
// Variable uniformity checks are inspired by:
//  Karrenberg, R. and Hack, S. Whole Function Vectorization.
//
// The interleaved access vectorization is based on the paper:
//  Dorit Nuzman, Ira Rosen and Ayal Zaks.  Auto-Vectorization of Interleaved
//  Data for SIMD
//
// Other ideas/concepts are from:
//  A. Zaks and D. Nuzman. Autovectorization in GCC-two years later.
//
//  S. Maleki, Y. Gao, M. Garzaran, T. Wong and D. Padua.  An Evaluation of
//  Vectorizing Compilers.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_VECTORIZE_LOOPVECTORIZE_H
#define LLVM_TRANSFORMS_VECTORIZE_LOOPVECTORIZE_H

#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/IR/PassManager.h"
#include <functional>

namespace llvm {

class AssumptionCache;
class BlockFrequencyInfo;
class DemandedBits;
class DominatorTree;
class Function;
class Loop;
class LoopAccessInfo;
class LoopInfo;
class OptimizationRemarkEmitter;
class ScalarEvolution;
class TargetLibraryInfo;
class TargetTransformInfo;

/// The LoopVectorize Pass.
struct LoopVectorizePass : public PassInfoMixin<LoopVectorizePass> {
  /// If false, consider all loops for interleaving.
  /// If true, only loops that explicitly request interleaving are considered.
  bool InterleaveOnlyWhenForced = false;

  /// If false, consider all loops for vectorization.
  /// If true, only loops that explicitly request vectorization are considered.
  bool VectorizeOnlyWhenForced = false;

  ScalarEvolution *SE;
  LoopInfo *LI;
  TargetTransformInfo *TTI;
  DominatorTree *DT;
  BlockFrequencyInfo *BFI;
  TargetLibraryInfo *TLI;
  DemandedBits *DB;
  AliasAnalysis *AA;
  AssumptionCache *AC;
  std::function<const LoopAccessInfo &(Loop &)> *GetLAA;
  OptimizationRemarkEmitter *ORE;

  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

  // Shim for old PM.
  bool runImpl(Function &F, ScalarEvolution &SE_, LoopInfo &LI_,
               TargetTransformInfo &TTI_, DominatorTree &DT_,
               BlockFrequencyInfo &BFI_, TargetLibraryInfo *TLI_,
               DemandedBits &DB_, AliasAnalysis &AA_, AssumptionCache &AC_,
               std::function<const LoopAccessInfo &(Loop &)> &GetLAA_,
               OptimizationRemarkEmitter &ORE);

  bool processLoop(Loop *L);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_VECTORIZE_LOOPVECTORIZE_H
