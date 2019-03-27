//===-- InferFunctionAttrs.h - Infer implicit function attributes ---------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Interfaces for passes which infer implicit function attributes from the
/// name and signature of function declarations.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_INFERFUNCTIONATTRS_H
#define LLVM_TRANSFORMS_IPO_INFERFUNCTIONATTRS_H

#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"

namespace llvm {

/// A pass which infers function attributes from the names and signatures of
/// function declarations in a module.
struct InferFunctionAttrsPass : PassInfoMixin<InferFunctionAttrsPass> {
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

/// Create a legacy pass manager instance of a pass to infer function
/// attributes.
Pass *createInferFunctionAttrsLegacyPass();

}

#endif // LLVM_TRANSFORMS_IPO_INFERFUNCTIONATTRS_H
