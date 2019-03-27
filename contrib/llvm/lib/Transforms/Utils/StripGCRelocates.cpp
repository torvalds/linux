//===- StripGCRelocates.cpp - Remove gc.relocates inserted by RewriteStatePoints===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This is a little utility pass that removes the gc.relocates inserted by
// RewriteStatepointsForGC. Note that the generated IR is incorrect,
// but this is useful as a single pass in itself, for analysis of IR, without
// the GC.relocates. The statepoint and gc.result instrinsics would still be
// present.
//===----------------------------------------------------------------------===//

#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Statepoint.h"
#include "llvm/IR/Type.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {
struct StripGCRelocates : public FunctionPass {
  static char ID; // Pass identification, replacement for typeid
  StripGCRelocates() : FunctionPass(ID) {
    initializeStripGCRelocatesPass(*PassRegistry::getPassRegistry());
  }

  void getAnalysisUsage(AnalysisUsage &Info) const override {}

  bool runOnFunction(Function &F) override;

};
char StripGCRelocates::ID = 0;
}

bool StripGCRelocates::runOnFunction(Function &F) {
  // Nothing to do for declarations.
  if (F.isDeclaration())
    return false;
  SmallVector<GCRelocateInst *, 20> GCRelocates;
  // TODO: We currently do not handle gc.relocates that are in landing pads,
  // i.e. not bound to a single statepoint token.
  for (Instruction &I : instructions(F)) {
    if (auto *GCR = dyn_cast<GCRelocateInst>(&I))
      if (isStatepoint(GCR->getOperand(0)))
        GCRelocates.push_back(GCR);
  }
  // All gc.relocates are bound to a single statepoint token. The order of
  // visiting gc.relocates for deletion does not matter.
  for (GCRelocateInst *GCRel : GCRelocates) {
    Value *OrigPtr = GCRel->getDerivedPtr();
    Value *ReplaceGCRel = OrigPtr;

    // All gc_relocates are i8 addrspace(1)* typed, we need a bitcast from i8
    // addrspace(1)* to the type of the OrigPtr, if the are not the same.
    if (GCRel->getType() != OrigPtr->getType())
      ReplaceGCRel = new BitCastInst(OrigPtr, GCRel->getType(), "cast", GCRel);

    // Replace all uses of gc.relocate and delete the gc.relocate
    // There maybe unncessary bitcasts back to the OrigPtr type, an instcombine
    // pass would clear this up.
    GCRel->replaceAllUsesWith(ReplaceGCRel);
    GCRel->eraseFromParent();
  }
  return !GCRelocates.empty();
}

INITIALIZE_PASS(StripGCRelocates, "strip-gc-relocates",
                "Strip gc.relocates inserted through RewriteStatepointsForGC",
                true, false)
