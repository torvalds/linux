//===--- PartiallyInlineLibCalls.h - Partially inline libcalls --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass tries to partially inline the fast path of well-known library
// functions, such as using square-root instructions for cases where sqrt()
// does not need to set errno.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_PARTIALLYINLINELIBCALLS_H
#define LLVM_TRANSFORMS_SCALAR_PARTIALLYINLINELIBCALLS_H

#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"

namespace llvm {
class PartiallyInlineLibCallsPass
    : public PassInfoMixin<PartiallyInlineLibCallsPass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};
}

#endif // LLVM_TRANSFORMS_SCALAR_PARTIALLYINLINELIBCALLS_H
