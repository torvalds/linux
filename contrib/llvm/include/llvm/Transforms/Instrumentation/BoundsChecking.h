//===- BoundsChecking.h - Bounds checking instrumentation -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_BOUNDSCHECKING_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_BOUNDSCHECKING_H

#include "llvm/IR/PassManager.h"

namespace llvm {

/// A pass to instrument code and perform run-time bounds checking on loads,
/// stores, and other memory intrinsics.
struct BoundsCheckingPass : PassInfoMixin<BoundsCheckingPass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};


/// Legacy pass creation function for the above pass.
FunctionPass *createBoundsCheckingLegacyPass();

} // end namespace llvm

#endif // LLVM_TRANSFORMS_INSTRUMENTATION_BOUNDSCHECKING_H
