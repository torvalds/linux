//===- GlobalSplit.h - global variable splitter -----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass uses inrange annotations on GEP indices to split globals where
// beneficial. Clang currently attaches these annotations to references to
// virtual table globals under the Itanium ABI for the benefit of the
// whole-program virtual call optimization and control flow integrity passes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_GLOBALSPLIT_H
#define LLVM_TRANSFORMS_IPO_GLOBALSPLIT_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Module;

/// Pass to perform split of global variables.
class GlobalSplitPass : public PassInfoMixin<GlobalSplitPass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_IPO_GLOBALSPLIT_H
