//===-- StripDeadPrototypes.cpp - Remove unused function declarations ----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass loops over all of the functions in the input module, looking for
// dead declarations and removes them. Dead declarations are declarations of
// functions for which no implementation is available (i.e., declarations for
// unused library functions).
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/IPO/StripDeadPrototypes.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Transforms/IPO.h"

using namespace llvm;

#define DEBUG_TYPE "strip-dead-prototypes"

STATISTIC(NumDeadPrototypes, "Number of dead prototypes removed");

static bool stripDeadPrototypes(Module &M) {
  bool MadeChange = false;

  // Erase dead function prototypes.
  for (Module::iterator I = M.begin(), E = M.end(); I != E; ) {
    Function *F = &*I++;
    // Function must be a prototype and unused.
    if (F->isDeclaration() && F->use_empty()) {
      F->eraseFromParent();
      ++NumDeadPrototypes;
      MadeChange = true;
    }
  }

  // Erase dead global var prototypes.
  for (Module::global_iterator I = M.global_begin(), E = M.global_end();
       I != E; ) {
    GlobalVariable *GV = &*I++;
    // Global must be a prototype and unused.
    if (GV->isDeclaration() && GV->use_empty())
      GV->eraseFromParent();
  }

  // Return an indication of whether we changed anything or not.
  return MadeChange;
}

PreservedAnalyses StripDeadPrototypesPass::run(Module &M,
                                               ModuleAnalysisManager &) {
  if (stripDeadPrototypes(M))
    return PreservedAnalyses::none();
  return PreservedAnalyses::all();
}

namespace {

class StripDeadPrototypesLegacyPass : public ModulePass {
public:
  static char ID; // Pass identification, replacement for typeid
  StripDeadPrototypesLegacyPass() : ModulePass(ID) {
    initializeStripDeadPrototypesLegacyPassPass(
        *PassRegistry::getPassRegistry());
  }
  bool runOnModule(Module &M) override {
    if (skipModule(M))
      return false;

    return stripDeadPrototypes(M);
  }
};

} // end anonymous namespace

char StripDeadPrototypesLegacyPass::ID = 0;
INITIALIZE_PASS(StripDeadPrototypesLegacyPass, "strip-dead-prototypes",
                "Strip Unused Function Prototypes", false, false)

ModulePass *llvm::createStripDeadPrototypesPass() {
  return new StripDeadPrototypesLegacyPass();
}
