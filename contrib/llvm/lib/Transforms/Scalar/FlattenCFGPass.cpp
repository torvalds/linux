//===- FlattenCFGPass.cpp - CFG Flatten Pass ----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements flattening of CFG.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/IR/CFG.h"
#include "llvm/Pass.h"
#include "llvm/Transforms/Scalar.h"
using namespace llvm;

#define DEBUG_TYPE "flattencfg"

namespace {
struct FlattenCFGPass : public FunctionPass {
  static char ID; // Pass identification, replacement for typeid
public:
  FlattenCFGPass() : FunctionPass(ID) {
    initializeFlattenCFGPassPass(*PassRegistry::getPassRegistry());
  }
  bool runOnFunction(Function &F) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<AAResultsWrapperPass>();
  }

private:
  AliasAnalysis *AA;
};
}

char FlattenCFGPass::ID = 0;
INITIALIZE_PASS_BEGIN(FlattenCFGPass, "flattencfg", "Flatten the CFG", false,
                      false)
INITIALIZE_PASS_DEPENDENCY(AAResultsWrapperPass)
INITIALIZE_PASS_END(FlattenCFGPass, "flattencfg", "Flatten the CFG", false,
                    false)

// Public interface to the FlattenCFG pass
FunctionPass *llvm::createFlattenCFGPass() { return new FlattenCFGPass(); }

/// iterativelyFlattenCFG - Call FlattenCFG on all the blocks in the function,
/// iterating until no more changes are made.
static bool iterativelyFlattenCFG(Function &F, AliasAnalysis *AA) {
  bool Changed = false;
  bool LocalChange = true;
  while (LocalChange) {
    LocalChange = false;

    // Loop over all of the basic blocks and remove them if they are unneeded...
    //
    for (Function::iterator BBIt = F.begin(); BBIt != F.end();) {
      if (FlattenCFG(&*BBIt++, AA)) {
        LocalChange = true;
      }
    }
    Changed |= LocalChange;
  }
  return Changed;
}

bool FlattenCFGPass::runOnFunction(Function &F) {
  AA = &getAnalysis<AAResultsWrapperPass>().getAAResults();
  bool EverChanged = false;
  // iterativelyFlattenCFG can make some blocks dead.
  while (iterativelyFlattenCFG(F, AA)) {
    removeUnreachableBlocks(F);
    EverChanged = true;
  }
  return EverChanged;
}
