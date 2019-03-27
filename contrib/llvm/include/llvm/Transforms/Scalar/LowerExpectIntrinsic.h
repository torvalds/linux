//===- LowerExpectIntrinsic.h - LowerExpectIntrinsic pass -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
///
/// The header file for the LowerExpectIntrinsic pass as used by the new pass
/// manager.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_LOWEREXPECTINTRINSIC_H
#define LLVM_TRANSFORMS_SCALAR_LOWEREXPECTINTRINSIC_H

#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"

namespace llvm {

struct LowerExpectIntrinsicPass : PassInfoMixin<LowerExpectIntrinsicPass> {
  /// Run the pass over the function.
  ///
  /// This will lower all of the expect intrinsic calls in this function into
  /// branch weight metadata. That metadata will subsequently feed the analysis
  /// of the probabilities and frequencies of the CFG. After running this pass,
  /// no more expect intrinsics remain, allowing the rest of the optimizer to
  /// ignore them.
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &);
};

}

#endif
