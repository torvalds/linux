//===- DeadStoreElimination.h - Fast Dead Store Elimination -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements a trivial dead store elimination that only considers
// basic-block local redundant stores.
//
// FIXME: This should eventually be extended to be a post-dominator tree
// traversal.  Doing so would be pretty trivial.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_DEADSTOREELIMINATION_H
#define LLVM_TRANSFORMS_SCALAR_DEADSTOREELIMINATION_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Function;

/// This class implements a trivial dead store elimination. We consider
/// only the redundant stores that are local to a single Basic Block.
class DSEPass : public PassInfoMixin<DSEPass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_DEADSTOREELIMINATION_H
