//===- LibCallsShrinkWrap.h - Shrink Wrap Library Calls -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_LIBCALLSSHRINKWRAP_H
#define LLVM_TRANSFORMS_UTILS_LIBCALLSSHRINKWRAP_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class LibCallsShrinkWrapPass : public PassInfoMixin<LibCallsShrinkWrapPass> {
public:
  static StringRef name() { return "LibCallsShrinkWrapPass"; }

  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
};
} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_LIBCALLSSHRINKWRAP_H
