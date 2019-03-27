//===-- ForceFunctionAttrs.h - Force function attrs for debugging ---------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
/// Super simple passes to force specific function attrs from the commandline
/// into the IR for debugging purposes.
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_FORCEFUNCTIONATTRS_H
#define LLVM_TRANSFORMS_IPO_FORCEFUNCTIONATTRS_H

#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"

namespace llvm {

/// Pass which forces specific function attributes into the IR, primarily as
/// a debugging tool.
struct ForceFunctionAttrsPass : PassInfoMixin<ForceFunctionAttrsPass> {
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &);
};

/// Create a legacy pass manager instance of a pass to force function attrs.
Pass *createForceFunctionAttrsLegacyPass();

}

#endif // LLVM_TRANSFORMS_IPO_FORCEFUNCTIONATTRS_H
