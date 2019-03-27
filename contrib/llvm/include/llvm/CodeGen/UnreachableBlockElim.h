//===-- UnreachableBlockElim.h - Remove unreachable blocks for codegen --===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass is an extremely simple version of the SimplifyCFG pass.  Its sole
// job is to delete LLVM basic blocks that are not reachable from the entry
// node.  To do this, it performs a simple depth first traversal of the CFG,
// then deletes any unvisited nodes.
//
// Note that this pass is really a hack.  In particular, the instruction
// selectors for various targets should just not generate code for unreachable
// blocks.  Until LLVM has a more systematic way of defining instruction
// selectors, however, we cannot really expect them to handle additional
// complexity.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_CODEGEN_UNREACHABLEBLOCKELIM_H
#define LLVM_LIB_CODEGEN_UNREACHABLEBLOCKELIM_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class UnreachableBlockElimPass
    : public PassInfoMixin<UnreachableBlockElimPass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};
} // end namespace llvm

#endif // LLVM_LIB_CODEGEN_UNREACHABLEBLOCKELIM_H
