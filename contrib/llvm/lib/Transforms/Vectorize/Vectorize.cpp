//===-- Vectorize.cpp -----------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements common infrastructure for libLLVMVectorizeOpts.a, which
// implements several vectorization transformations over the LLVM intermediate
// representation, including the C bindings for that library.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Vectorize.h"
#include "llvm-c/Initialization.h"
#include "llvm-c/Transforms/Vectorize.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/InitializePasses.h"

using namespace llvm;

/// initializeVectorizationPasses - Initialize all passes linked into the
/// Vectorization library.
void llvm::initializeVectorization(PassRegistry &Registry) {
  initializeLoopVectorizePass(Registry);
  initializeSLPVectorizerPass(Registry);
  initializeLoadStoreVectorizerLegacyPassPass(Registry);
}

void LLVMInitializeVectorization(LLVMPassRegistryRef R) {
  initializeVectorization(*unwrap(R));
}

void LLVMAddLoopVectorizePass(LLVMPassManagerRef PM) {
  unwrap(PM)->add(createLoopVectorizePass());
}

void LLVMAddSLPVectorizePass(LLVMPassManagerRef PM) {
  unwrap(PM)->add(createSLPVectorizerPass());
}
