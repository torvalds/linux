//===- EarlyCSE.h - Simple and fast CSE pass --------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file provides the interface for a simple, fast CSE pass.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_EARLYCSE_H
#define LLVM_TRANSFORMS_SCALAR_EARLYCSE_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Function;

/// A simple and fast domtree-based CSE pass.
///
/// This pass does a simple depth-first walk over the dominator tree,
/// eliminating trivially redundant instructions and using instsimplify to
/// canonicalize things as it goes. It is intended to be fast and catch obvious
/// cases so that instcombine and other passes are more effective. It is
/// expected that a later pass of GVN will catch the interesting/hard cases.
struct EarlyCSEPass : PassInfoMixin<EarlyCSEPass> {
  EarlyCSEPass(bool UseMemorySSA = false) : UseMemorySSA(UseMemorySSA) {}

  /// Run the pass over the function.
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

  bool UseMemorySSA;
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_EARLYCSE_H
