//===- LowerInvoke.h - Eliminate Invoke instructions ----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This transformation is designed for use by code generators which do not yet
// support stack unwinding.  This pass converts 'invoke' instructions to 'call'
// instructions, so that any exception-handling 'landingpad' blocks become dead
// code (which can be removed by running the '-simplifycfg' pass afterwards).
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_TRANSFORMS_UTILS_LOWERINVOKE_H
#define LLVM_TRANSFORMS_UTILS_LOWERINVOKE_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class LowerInvokePass : public PassInfoMixin<LowerInvokePass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

}

#endif // LLVM_TRANSFORMS_UTILS_LOWERINVOKE_H
