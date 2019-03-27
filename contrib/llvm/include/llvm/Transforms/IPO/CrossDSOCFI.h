//===-- CrossDSOCFI.cpp - Externalize this module's CFI checks --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass exports all llvm.bitset's found in the module in the form of a
// __cfi_check function, which can be used to verify cross-DSO call targets.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_CROSSDSOCFI_H
#define LLVM_TRANSFORMS_IPO_CROSSDSOCFI_H

#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"

namespace llvm {
class CrossDSOCFIPass : public PassInfoMixin<CrossDSOCFIPass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};
}
#endif // LLVM_TRANSFORMS_IPO_CROSSDSOCFI_H

