//===- Scalarizer.h --- Scalarize vector operations -----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This pass converts vector operations into scalar operations, in order
/// to expose optimization opportunities on the individual scalar operations.
/// It is mainly intended for targets that do not have vector units, but it
/// may also be useful for revectorizing code to different vector widths.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_SCALARIZER_H
#define LLVM_TRANSFORMS_SCALAR_SCALARIZER_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class ScalarizerPass : public PassInfoMixin<ScalarizerPass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

/// Create a legacy pass manager instance of the Scalarizer pass
FunctionPass *createScalarizerPass();

}

#endif /* LLVM_TRANSFORMS_SCALAR_SCALARIZER_H */
