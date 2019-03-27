//===- Mem2Reg.h - The -mem2reg pass, a wrapper around the Utils lib ------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass is a simple pass wrapper around the PromoteMemToReg function call
// exposed by the Utils library.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_MEM2REG_H
#define LLVM_TRANSFORMS_UTILS_MEM2REG_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Function;

class PromotePass : public PassInfoMixin<PromotePass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_MEM2REG_H
