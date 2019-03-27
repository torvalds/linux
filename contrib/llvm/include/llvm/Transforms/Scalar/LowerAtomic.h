//===- LowerAtomic.cpp - Lower atomic intrinsics ----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
// This pass lowers atomic intrinsics to non-atomic form for use in a known
// non-preemptible environment.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_LOWERATOMIC_H
#define LLVM_TRANSFORMS_SCALAR_LOWERATOMIC_H

#include "llvm/IR/PassManager.h"

namespace llvm {

/// A pass that lowers atomic intrinsic into non-atomic intrinsics.
class LowerAtomicPass : public PassInfoMixin<LowerAtomicPass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &);
};
}

#endif // LLVM_TRANSFORMS_SCALAR_LOWERATOMIC_H
