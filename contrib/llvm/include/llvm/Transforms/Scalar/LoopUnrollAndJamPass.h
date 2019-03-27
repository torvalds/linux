//===- LoopUnrollAndJamPass.h -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_LOOPUNROLLANDJAMPASS_H
#define LLVM_TRANSFORMS_SCALAR_LOOPUNROLLANDJAMPASS_H

#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/PassManager.h"

namespace llvm {

class Loop;
struct LoopStandardAnalysisResults;
class LPMUpdater;

/// A simple loop rotation transformation.
class LoopUnrollAndJamPass : public PassInfoMixin<LoopUnrollAndJamPass> {
  const int OptLevel;

public:
  explicit LoopUnrollAndJamPass(int OptLevel = 2) : OptLevel(OptLevel) {}
  PreservedAnalyses run(Loop &L, LoopAnalysisManager &AM,
                        LoopStandardAnalysisResults &AR, LPMUpdater &U);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_LOOPUNROLLANDJAMPASS_H
