//===- WarnMissedTransforms.h -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Emit warnings if forced code transformations have not been performed.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_WARNMISSEDTRANSFORMS_H
#define LLVM_TRANSFORMS_SCALAR_WARNMISSEDTRANSFORMS_H

#include "llvm/IR/PassManager.h"

namespace llvm {
class Function;
class Loop;
class LPMUpdater;

// New pass manager boilerplate.
class WarnMissedTransformationsPass
    : public PassInfoMixin<WarnMissedTransformationsPass> {
public:
  explicit WarnMissedTransformationsPass() {}

  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

// Legacy pass manager boilerplate.
Pass *createWarnMissedTransformationsPass();
void initializeWarnMissedTransformationsLegacyPass(PassRegistry &);
} // end namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_WARNMISSEDTRANSFORMS_H
