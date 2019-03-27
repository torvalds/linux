//===- GlobalOpt.h - Optimize Global Variables ------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass transforms simple global variables that never have their address
// taken.  If obviously true, it marks read/write globals as constant, deletes
// variables only stored to, etc.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_GLOBALOPT_H
#define LLVM_TRANSFORMS_IPO_GLOBALOPT_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Module;

/// Optimize globals that never have their address taken.
class GlobalOptPass : public PassInfoMixin<GlobalOptPass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_IPO_GLOBALOPT_H
