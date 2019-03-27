//===-- NameAnonGlobals.h - Anonymous Global Naming Pass --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements naming anonymous globals to make sure they can be
// referred to by ThinLTO.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_NAMEANONGLOBALSS_H
#define LLVM_TRANSFORMS_UTILS_NAMEANONGLOBALSS_H

#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"

namespace llvm {

/// Simple pass that provides a name to every anonymous globals.
class NameAnonGlobalPass : public PassInfoMixin<NameAnonGlobalPass> {
public:
  NameAnonGlobalPass() = default;

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_NAMEANONGLOBALS_H
