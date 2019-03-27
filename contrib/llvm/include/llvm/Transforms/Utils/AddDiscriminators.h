//===- AddDiscriminators.h --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass adds DWARF discriminators to the IR. Path discriminators are used
// to decide what CFG path was taken inside sub-graphs whose instructions share
// the same line and column number information.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_ADDDISCRIMINATORS_H
#define LLVM_TRANSFORMS_UTILS_ADDDISCRIMINATORS_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Function;

class AddDiscriminatorsPass : public PassInfoMixin<AddDiscriminatorsPass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_ADDDISCRIMINATORS_H
