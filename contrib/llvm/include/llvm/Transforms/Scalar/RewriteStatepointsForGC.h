//===- RewriteStatepointsForGC.h - ------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file provides interface to "Rewrite Statepoints for GC" pass.
//
// This passe rewrites call/invoke instructions so as to make potential
// relocations performed by the garbage collector explicit in the IR.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_REWRITE_STATEPOINTS_FOR_GC_H
#define LLVM_TRANSFORMS_SCALAR_REWRITE_STATEPOINTS_FOR_GC_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class DominatorTree;
class Function;
class Module;
class TargetTransformInfo;
class TargetLibraryInfo;

struct RewriteStatepointsForGC : public PassInfoMixin<RewriteStatepointsForGC> {
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);

  bool runOnFunction(Function &F, DominatorTree &, TargetTransformInfo &,
                     const TargetLibraryInfo &);
};

} // namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_REWRITE_STATEPOINTS_FOR_GC_H
