//===----- ExpandReductions.h - Expand experimental reduction intrinsics --===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_EXPANDREDUCTIONS_H
#define LLVM_CODEGEN_EXPANDREDUCTIONS_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class ExpandReductionsPass
    : public PassInfoMixin<ExpandReductionsPass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};
} // end namespace llvm

#endif // LLVM_CODEGEN_EXPANDREDUCTIONS_H
