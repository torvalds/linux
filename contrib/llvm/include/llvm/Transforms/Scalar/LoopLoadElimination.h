//===- LoopLoadElimination.h ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This header defines the LoopLoadEliminationPass object. This pass forwards
/// loaded values around loop backedges to allow their use in subsequent
/// iterations.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_LOOPLOADELIMINATION_H
#define LLVM_TRANSFORMS_SCALAR_LOOPLOADELIMINATION_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Function;

/// Pass to forward loads in a loop around the backedge to subsequent
/// iterations.
struct LoopLoadEliminationPass : public PassInfoMixin<LoopLoadEliminationPass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_LOOPLOADELIMINATION_H
