//===- ElimAvailExtern.cpp - DCE unreachable internal functions -----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This transform is designed to eliminate available external global
// definitions from the program, turning them into declarations.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/IPO/ElimAvailExtern.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/Utils/GlobalStatus.h"

using namespace llvm;

#define DEBUG_TYPE "elim-avail-extern"

STATISTIC(NumFunctions, "Number of functions removed");
STATISTIC(NumVariables, "Number of global variables removed");

static bool eliminateAvailableExternally(Module &M) {
  bool Changed = false;

  // Drop initializers of available externally global variables.
  for (GlobalVariable &GV : M.globals()) {
    if (!GV.hasAvailableExternallyLinkage())
      continue;
    if (GV.hasInitializer()) {
      Constant *Init = GV.getInitializer();
      GV.setInitializer(nullptr);
      if (isSafeToDestroyConstant(Init))
        Init->destroyConstant();
    }
    GV.removeDeadConstantUsers();
    GV.setLinkage(GlobalValue::ExternalLinkage);
    NumVariables++;
    Changed = true;
  }

  // Drop the bodies of available externally functions.
  for (Function &F : M) {
    if (!F.hasAvailableExternallyLinkage())
      continue;
    if (!F.isDeclaration())
      // This will set the linkage to external
      F.deleteBody();
    F.removeDeadConstantUsers();
    NumFunctions++;
    Changed = true;
  }

  return Changed;
}

PreservedAnalyses
EliminateAvailableExternallyPass::run(Module &M, ModuleAnalysisManager &) {
  if (!eliminateAvailableExternally(M))
    return PreservedAnalyses::all();
  return PreservedAnalyses::none();
}

namespace {

struct EliminateAvailableExternallyLegacyPass : public ModulePass {
  static char ID; // Pass identification, replacement for typeid

  EliminateAvailableExternallyLegacyPass() : ModulePass(ID) {
    initializeEliminateAvailableExternallyLegacyPassPass(
        *PassRegistry::getPassRegistry());
  }

  // run - Do the EliminateAvailableExternally pass on the specified module,
  // optionally updating the specified callgraph to reflect the changes.
  bool runOnModule(Module &M) override {
    if (skipModule(M))
      return false;
    return eliminateAvailableExternally(M);
  }
};

} // end anonymous namespace

char EliminateAvailableExternallyLegacyPass::ID = 0;

INITIALIZE_PASS(EliminateAvailableExternallyLegacyPass, "elim-avail-extern",
                "Eliminate Available Externally Globals", false, false)

ModulePass *llvm::createEliminateAvailableExternallyPass() {
  return new EliminateAvailableExternallyLegacyPass();
}
