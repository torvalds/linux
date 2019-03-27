//===- LoopRotation.h - Loop Rotation -------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file provides the interface for the Loop Rotation pass.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_LOOPROTATION_H
#define LLVM_TRANSFORMS_SCALAR_LOOPROTATION_H

#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Transforms/Scalar/LoopPassManager.h"

namespace llvm {

/// A simple loop rotation transformation.
class LoopRotatePass : public PassInfoMixin<LoopRotatePass> {
public:
  LoopRotatePass(bool EnableHeaderDuplication = true);
  PreservedAnalyses run(Loop &L, LoopAnalysisManager &AM,
                        LoopStandardAnalysisResults &AR, LPMUpdater &U);

private:
  const bool EnableHeaderDuplication;
};
}

#endif // LLVM_TRANSFORMS_SCALAR_LOOPROTATION_H
