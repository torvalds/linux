//===--- LowerGuardIntrinsic.h - Lower the guard intrinsic ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass lowers the llvm.experimental.guard intrinsic to a conditional call
// to @llvm.experimental.deoptimize.  Once this happens, the guard can no longer
// be widened.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_TRANSFORMS_SCALAR_LOWERGUARDINTRINSIC_H
#define LLVM_TRANSFORMS_SCALAR_LOWERGUARDINTRINSIC_H

#include "llvm/IR/PassManager.h"

namespace llvm {

struct LowerGuardIntrinsicPass : PassInfoMixin<LowerGuardIntrinsicPass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

}

#endif //LLVM_TRANSFORMS_SCALAR_LOWERGUARDINTRINSIC_H
