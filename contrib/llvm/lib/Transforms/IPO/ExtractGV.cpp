//===-- ExtractGV.cpp - Global Value extraction pass ----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass extracts global values
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/SetVector.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Transforms/IPO.h"
#include <algorithm>
using namespace llvm;

/// Make sure GV is visible from both modules. Delete is true if it is
/// being deleted from this module.
/// This also makes sure GV cannot be dropped so that references from
/// the split module remain valid.
static void makeVisible(GlobalValue &GV, bool Delete) {
  bool Local = GV.hasLocalLinkage();
  if (Local || Delete) {
    GV.setLinkage(GlobalValue::ExternalLinkage);
    if (Local)
      GV.setVisibility(GlobalValue::HiddenVisibility);
    return;
  }

  if (!GV.hasLinkOnceLinkage()) {
    assert(!GV.isDiscardableIfUnused());
    return;
  }

  // Map linkonce* to weak* so that llvm doesn't drop this GV.
  switch(GV.getLinkage()) {
  default:
    llvm_unreachable("Unexpected linkage");
  case GlobalValue::LinkOnceAnyLinkage:
    GV.setLinkage(GlobalValue::WeakAnyLinkage);
    return;
  case GlobalValue::LinkOnceODRLinkage:
    GV.setLinkage(GlobalValue::WeakODRLinkage);
    return;
  }
}

namespace {
  /// A pass to extract specific global values and their dependencies.
  class GVExtractorPass : public ModulePass {
    SetVector<GlobalValue *> Named;
    bool deleteStuff;
  public:
    static char ID; // Pass identification, replacement for typeid

    /// If deleteS is true, this pass deletes the specified global values.
    /// Otherwise, it deletes as much of the module as possible, except for the
    /// global values specified.
    explicit GVExtractorPass(std::vector<GlobalValue*> &GVs,
                             bool deleteS = true)
      : ModulePass(ID), Named(GVs.begin(), GVs.end()), deleteStuff(deleteS) {}

    bool runOnModule(Module &M) override {
      if (skipModule(M))
        return false;

      // Visit the global inline asm.
      if (!deleteStuff)
        M.setModuleInlineAsm("");

      // For simplicity, just give all GlobalValues ExternalLinkage. A trickier
      // implementation could figure out which GlobalValues are actually
      // referenced by the Named set, and which GlobalValues in the rest of
      // the module are referenced by the NamedSet, and get away with leaving
      // more internal and private things internal and private. But for now,
      // be conservative and simple.

      // Visit the GlobalVariables.
      for (Module::global_iterator I = M.global_begin(), E = M.global_end();
           I != E; ++I) {
        bool Delete =
            deleteStuff == (bool)Named.count(&*I) && !I->isDeclaration();
        if (!Delete) {
          if (I->hasAvailableExternallyLinkage())
            continue;
          if (I->getName() == "llvm.global_ctors")
            continue;
        }

        makeVisible(*I, Delete);

        if (Delete) {
          // Make this a declaration and drop it's comdat.
          I->setInitializer(nullptr);
          I->setComdat(nullptr);
        }
      }

      // Visit the Functions.
      for (Function &F : M) {
        bool Delete =
            deleteStuff == (bool)Named.count(&F) && !F.isDeclaration();
        if (!Delete) {
          if (F.hasAvailableExternallyLinkage())
            continue;
        }

        makeVisible(F, Delete);

        if (Delete) {
          // Make this a declaration and drop it's comdat.
          F.deleteBody();
          F.setComdat(nullptr);
        }
      }

      // Visit the Aliases.
      for (Module::alias_iterator I = M.alias_begin(), E = M.alias_end();
           I != E;) {
        Module::alias_iterator CurI = I;
        ++I;

        bool Delete = deleteStuff == (bool)Named.count(&*CurI);
        makeVisible(*CurI, Delete);

        if (Delete) {
          Type *Ty =  CurI->getValueType();

          CurI->removeFromParent();
          llvm::Value *Declaration;
          if (FunctionType *FTy = dyn_cast<FunctionType>(Ty)) {
            Declaration = Function::Create(FTy, GlobalValue::ExternalLinkage,
                                           CurI->getAddressSpace(),
                                           CurI->getName(), &M);

          } else {
            Declaration =
              new GlobalVariable(M, Ty, false, GlobalValue::ExternalLinkage,
                                 nullptr, CurI->getName());

          }
          CurI->replaceAllUsesWith(Declaration);
          delete &*CurI;
        }
      }

      return true;
    }
  };

  char GVExtractorPass::ID = 0;
}

ModulePass *llvm::createGVExtractionPass(std::vector<GlobalValue *> &GVs,
                                         bool deleteFn) {
  return new GVExtractorPass(GVs, deleteFn);
}
