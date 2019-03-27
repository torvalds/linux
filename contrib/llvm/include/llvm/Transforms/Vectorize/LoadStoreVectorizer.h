//===- LoadStoreVectorizer.cpp - GPU Load & Store Vectorizer --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_VECTORIZE_LOADSTOREVECTORIZER_H
#define LLVM_TRANSFORMS_VECTORIZE_LOADSTOREVECTORIZER_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class LoadStoreVectorizerPass : public PassInfoMixin<LoadStoreVectorizerPass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

/// Create a legacy pass manager instance of the LoadStoreVectorizer pass
Pass *createLoadStoreVectorizerPass();

}

#endif /* LLVM_TRANSFORMS_VECTORIZE_LOADSTOREVECTORIZER_H */
