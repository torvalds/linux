//===- InstructionNamer.cpp - Give anonymous instructions names -----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This is a little utility pass that gives instructions names, this is mostly
// useful when diffing the effect of an optimization because deleting an
// unnamed instruction can change all other instruction numbering, making the
// diff very noisy.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/Function.h"
#include "llvm/IR/Type.h"
#include "llvm/Pass.h"
#include "llvm/Transforms/Utils.h"
using namespace llvm;

namespace {
  struct InstNamer : public FunctionPass {
    static char ID; // Pass identification, replacement for typeid
    InstNamer() : FunctionPass(ID) {
      initializeInstNamerPass(*PassRegistry::getPassRegistry());
    }

    void getAnalysisUsage(AnalysisUsage &Info) const override {
      Info.setPreservesAll();
    }

    bool runOnFunction(Function &F) override {
      for (auto &Arg : F.args())
        if (!Arg.hasName())
          Arg.setName("arg");

      for (BasicBlock &BB : F) {
        if (!BB.hasName())
          BB.setName("bb");

        for (Instruction &I : BB)
          if (!I.hasName() && !I.getType()->isVoidTy())
            I.setName("tmp");
      }
      return true;
    }
  };

  char InstNamer::ID = 0;
}

INITIALIZE_PASS(InstNamer, "instnamer",
                "Assign names to anonymous instructions", false, false)
char &llvm::InstructionNamerID = InstNamer::ID;
//===----------------------------------------------------------------------===//
//
// InstructionNamer - Give any unnamed non-void instructions "tmp" names.
//
FunctionPass *llvm::createInstructionNamerPass() {
  return new InstNamer();
}
