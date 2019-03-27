//===- DivRemPairs.h - Hoist/decompose integer division and remainder -----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass hoists and/or decomposes integer division and remainder
// instructions to enable CFG improvements and better codegen.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_DIVREMPAIRS_H
#define LLVM_TRANSFORMS_SCALAR_DIVREMPAIRS_H

#include "llvm/IR/PassManager.h"

namespace llvm {

/// Hoist/decompose integer division and remainder instructions to enable CFG
/// improvements and better codegen.
struct DivRemPairsPass : public PassInfoMixin<DivRemPairsPass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &);
};

}
#endif // LLVM_TRANSFORMS_SCALAR_DIVREMPAIRS_H

