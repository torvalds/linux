//===- ControlHeightReduction.h - Control Height Reduction ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass merges conditional blocks of code and reduces the number of
// conditional branches in the hot paths based on profiles.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_CONTROLHEIGHTREDUCTION_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_CONTROLHEIGHTREDUCTION_H

#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/PassManager.h"

namespace llvm {

class ControlHeightReductionPass :
      public PassInfoMixin<ControlHeightReductionPass> {
public:
  ControlHeightReductionPass();
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
};
} // end namespace llvm

#endif // LLVM_TRANSFORMS_INSTRUMENTATION_CONTROLHEIGHTREDUCTION_H
