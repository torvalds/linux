//===- LoopSink.h - Loop Sink Pass ------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file provides the interface for the Loop Sink pass.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_LOOPSINK_H
#define LLVM_TRANSFORMS_SCALAR_LOOPSINK_H

#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Transforms/Scalar/LoopPassManager.h"

namespace llvm {

/// A pass that does profile-guided sinking of instructions into loops.
///
/// This is a function pass as it shouldn't be composed into any kind of
/// unified loop pass pipeline. The goal of it is to sink code into loops that
/// is loop invariant but only required within the loop body when doing so
/// reduces the global expected dynamic frequency with which it executes.
/// A classic example is an extremely cold branch within a loop body.
///
/// We do this as a separate pass so that during normal optimization all
/// invariant operations can be held outside the loop body to simplify
/// fundamental analyses and transforms of the loop.
class LoopSinkPass : public PassInfoMixin<LoopSinkPass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
};
}

#endif // LLVM_TRANSFORMS_SCALAR_LOOPSINK_H
