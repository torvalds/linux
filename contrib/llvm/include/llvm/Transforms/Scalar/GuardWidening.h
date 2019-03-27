//===- GuardWidening.h - ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Guard widening is an optimization over the @llvm.experimental.guard intrinsic
// that (optimistically) combines multiple guards into one to have fewer checks
// at runtime.
//
//===----------------------------------------------------------------------===//


#ifndef LLVM_TRANSFORMS_SCALAR_GUARD_WIDENING_H
#define LLVM_TRANSFORMS_SCALAR_GUARD_WIDENING_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Function;

struct GuardWideningPass : public PassInfoMixin<GuardWideningPass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};
}


#endif  // LLVM_TRANSFORMS_SCALAR_GUARD_WIDENING_H
