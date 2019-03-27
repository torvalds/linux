//===---- AlignmentFromAssumptions.h ----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements a ScalarEvolution-based transformation to set
// the alignments of load, stores and memory intrinsics based on the truth
// expressions of assume intrinsics. The primary motivation is to handle
// complex alignment assumptions that apply to vector loads and stores that
// appear after vectorization and unrolling.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_ALIGNMENTFROMASSUMPTIONS_H
#define LLVM_TRANSFORMS_SCALAR_ALIGNMENTFROMASSUMPTIONS_H

#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/PassManager.h"

namespace llvm {

struct AlignmentFromAssumptionsPass
    : public PassInfoMixin<AlignmentFromAssumptionsPass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

  // Glue for old PM.
  bool runImpl(Function &F, AssumptionCache &AC, ScalarEvolution *SE_,
               DominatorTree *DT_);

  ScalarEvolution *SE = nullptr;
  DominatorTree *DT = nullptr;

  bool extractAlignmentInfo(CallInst *I, Value *&AAPtr, const SCEV *&AlignSCEV,
                            const SCEV *&OffSCEV);
  bool processAssumption(CallInst *I);
};
}

#endif // LLVM_TRANSFORMS_SCALAR_ALIGNMENTFROMASSUMPTIONS_H
