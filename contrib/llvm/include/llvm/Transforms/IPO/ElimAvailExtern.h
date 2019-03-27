//===- ElimAvailExtern.h - Optimize Global Variables ------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This transform is designed to eliminate available external global
// definitions from the program, turning them into declarations.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_ELIMAVAILEXTERN_H
#define LLVM_TRANSFORMS_IPO_ELIMAVAILEXTERN_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Module;

/// A pass that transforms external global definitions into declarations.
class EliminateAvailableExternallyPass
    : public PassInfoMixin<EliminateAvailableExternallyPass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_IPO_ELIMAVAILEXTERN_H
