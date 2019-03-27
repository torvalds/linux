//===- InductiveRangeCheckElimination.h - IRCE ------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file provides the interface for the Inductive Range Check Elimination
// loop pass.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_INDUCTIVERANGECHECKELIMINATION_H
#define LLVM_TRANSFORMS_SCALAR_INDUCTIVERANGECHECKELIMINATION_H

#include "llvm/IR/PassManager.h"
#include "llvm/Transforms/Scalar/LoopPassManager.h"

namespace llvm {

class IRCEPass : public PassInfoMixin<IRCEPass> {
public:
  PreservedAnalyses run(Loop &L, LoopAnalysisManager &AM,
                        LoopStandardAnalysisResults &AR, LPMUpdater &U);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_INDUCTIVERANGECHECKELIMINATION_H
