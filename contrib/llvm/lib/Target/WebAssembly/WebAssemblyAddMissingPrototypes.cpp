//===-- WebAssemblyAddMissingPrototypes.cpp - Fix prototypeless functions -===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Add prototypes to prototypes-less functions.
///
/// WebAssembly has strict function prototype checking so we need functions
/// declarations to match the call sites.  Clang treats prototype-less functions
/// as varargs (foo(...)) which happens to work on existing platforms but
/// doesn't under WebAssembly.  This pass will find all the call sites of each
/// prototype-less function, ensure they agree, and then set the signature
/// on the function declaration accordingly.
///
//===----------------------------------------------------------------------===//

#include "WebAssembly.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
using namespace llvm;

#define DEBUG_TYPE "wasm-add-missing-prototypes"

namespace {
class WebAssemblyAddMissingPrototypes final : public ModulePass {
  StringRef getPassName() const override {
    return "Add prototypes to prototypes-less functions";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    ModulePass::getAnalysisUsage(AU);
  }

  bool runOnModule(Module &M) override;

public:
  static char ID;
  WebAssemblyAddMissingPrototypes() : ModulePass(ID) {}
};
} // End anonymous namespace

char WebAssemblyAddMissingPrototypes::ID = 0;
INITIALIZE_PASS(WebAssemblyAddMissingPrototypes, DEBUG_TYPE,
                "Add prototypes to prototypes-less functions", false, false)

ModulePass *llvm::createWebAssemblyAddMissingPrototypes() {
  return new WebAssemblyAddMissingPrototypes();
}

bool WebAssemblyAddMissingPrototypes::runOnModule(Module &M) {
  LLVM_DEBUG(dbgs() << "********** Add Missing Prototypes **********\n");

  std::vector<std::pair<Function *, Function *>> Replacements;

  // Find all the prototype-less function declarations
  for (Function &F : M) {
    if (!F.isDeclaration() || !F.hasFnAttribute("no-prototype"))
      continue;

    LLVM_DEBUG(dbgs() << "Found no-prototype function: " << F.getName()
                      << "\n");

    // When clang emits prototype-less C functions it uses (...), i.e. varargs
    // function that take no arguments (have no sentinel).  When we see a
    // no-prototype attribute we expect the function have these properties.
    if (!F.isVarArg())
      report_fatal_error(
          "Functions with 'no-prototype' attribute must take varargs: " +
          F.getName());
    if (F.getFunctionType()->getNumParams() != 0)
      report_fatal_error(
          "Functions with 'no-prototype' attribute should not have params: " +
          F.getName());

    // Create a function prototype based on the first call site (first bitcast)
    // that we find.
    FunctionType *NewType = nullptr;
    Function *NewF = nullptr;
    for (Use &U : F.uses()) {
      LLVM_DEBUG(dbgs() << "prototype-less use: " << F.getName() << "\n");
      if (auto *BC = dyn_cast<BitCastOperator>(U.getUser())) {
        if (auto *DestType = dyn_cast<FunctionType>(
                BC->getDestTy()->getPointerElementType())) {
          if (!NewType) {
            // Create a new function with the correct type
            NewType = DestType;
            NewF = Function::Create(NewType, F.getLinkage(), F.getName());
            NewF->setAttributes(F.getAttributes());
            NewF->removeFnAttr("no-prototype");
          } else {
            if (NewType != DestType) {
              report_fatal_error("Prototypeless function used with "
                                 "conflicting signatures: " +
                                 F.getName());
            }
          }
        }
      }
    }

    if (!NewType) {
      LLVM_DEBUG(
          dbgs() << "could not derive a function prototype from usage: " +
                        F.getName() + "\n");
      continue;
    }

    SmallVector<Instruction *, 4> DeadInsts;

    for (Use &US : F.uses()) {
      User *U = US.getUser();
      if (auto *BC = dyn_cast<BitCastOperator>(U)) {
        if (auto *Inst = dyn_cast<BitCastInst>(U)) {
          // Replace with a new bitcast
          IRBuilder<> Builder(Inst);
          Value *NewCast = Builder.CreatePointerCast(NewF, BC->getDestTy());
          Inst->replaceAllUsesWith(NewCast);
          DeadInsts.push_back(Inst);
        } else if (auto *Const = dyn_cast<ConstantExpr>(U)) {
          Constant *NewConst =
              ConstantExpr::getPointerCast(NewF, BC->getDestTy());
          Const->replaceAllUsesWith(NewConst);
        } else {
          dbgs() << *U->getType() << "\n";
#ifndef NDEBUG
          U->dump();
#endif
          report_fatal_error("unexpected use of prototypeless function: " +
                             F.getName() + "\n");
        }
      }
    }

    for (auto I : DeadInsts)
      I->eraseFromParent();
    Replacements.emplace_back(&F, NewF);
  }


  // Finally replace the old function declarations with the new ones
  for (auto &Pair : Replacements) {
    Function *Old = Pair.first;
    Function *New = Pair.second;
    Old->eraseFromParent();
    M.getFunctionList().push_back(New);
  }

  return !Replacements.empty();
}
