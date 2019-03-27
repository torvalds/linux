//===- HotColdSplitting.h ---- Outline Cold Regions -------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//===----------------------------------------------------------------------===//
//
// This pass outlines cold regions to a separate function.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_HOTCOLDSPLITTING_H
#define LLVM_TRANSFORMS_IPO_HOTCOLDSPLITTING_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Module;

/// Pass to outline cold regions.
class HotColdSplittingPass : public PassInfoMixin<HotColdSplittingPass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_IPO_HOTCOLDSPLITTING_H

