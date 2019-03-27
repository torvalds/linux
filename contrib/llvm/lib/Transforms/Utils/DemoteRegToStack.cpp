//===- DemoteRegToStack.cpp - Move a virtual register to the stack --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/DenseMap.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Type.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
using namespace llvm;

/// DemoteRegToStack - This function takes a virtual register computed by an
/// Instruction and replaces it with a slot in the stack frame, allocated via
/// alloca.  This allows the CFG to be changed around without fear of
/// invalidating the SSA information for the value.  It returns the pointer to
/// the alloca inserted to create a stack slot for I.
AllocaInst *llvm::DemoteRegToStack(Instruction &I, bool VolatileLoads,
                                   Instruction *AllocaPoint) {
  if (I.use_empty()) {
    I.eraseFromParent();
    return nullptr;
  }

  Function *F = I.getParent()->getParent();
  const DataLayout &DL = F->getParent()->getDataLayout();

  // Create a stack slot to hold the value.
  AllocaInst *Slot;
  if (AllocaPoint) {
    Slot = new AllocaInst(I.getType(), DL.getAllocaAddrSpace(), nullptr,
                          I.getName()+".reg2mem", AllocaPoint);
  } else {
    Slot = new AllocaInst(I.getType(), DL.getAllocaAddrSpace(), nullptr,
                          I.getName() + ".reg2mem", &F->getEntryBlock().front());
  }

  // We cannot demote invoke instructions to the stack if their normal edge
  // is critical. Therefore, split the critical edge and create a basic block
  // into which the store can be inserted.
  if (InvokeInst *II = dyn_cast<InvokeInst>(&I)) {
    if (!II->getNormalDest()->getSinglePredecessor()) {
      unsigned SuccNum = GetSuccessorNumber(II->getParent(), II->getNormalDest());
      assert(isCriticalEdge(II, SuccNum) && "Expected a critical edge!");
      BasicBlock *BB = SplitCriticalEdge(II, SuccNum);
      assert(BB && "Unable to split critical edge.");
      (void)BB;
    }
  }

  // Change all of the users of the instruction to read from the stack slot.
  while (!I.use_empty()) {
    Instruction *U = cast<Instruction>(I.user_back());
    if (PHINode *PN = dyn_cast<PHINode>(U)) {
      // If this is a PHI node, we can't insert a load of the value before the
      // use.  Instead insert the load in the predecessor block corresponding
      // to the incoming value.
      //
      // Note that if there are multiple edges from a basic block to this PHI
      // node that we cannot have multiple loads. The problem is that the
      // resulting PHI node will have multiple values (from each load) coming in
      // from the same block, which is illegal SSA form. For this reason, we
      // keep track of and reuse loads we insert.
      DenseMap<BasicBlock*, Value*> Loads;
      for (unsigned i = 0, e = PN->getNumIncomingValues(); i != e; ++i)
        if (PN->getIncomingValue(i) == &I) {
          Value *&V = Loads[PN->getIncomingBlock(i)];
          if (!V) {
            // Insert the load into the predecessor block
            V = new LoadInst(Slot, I.getName()+".reload", VolatileLoads,
                             PN->getIncomingBlock(i)->getTerminator());
          }
          PN->setIncomingValue(i, V);
        }

    } else {
      // If this is a normal instruction, just insert a load.
      Value *V = new LoadInst(Slot, I.getName()+".reload", VolatileLoads, U);
      U->replaceUsesOfWith(&I, V);
    }
  }

  // Insert stores of the computed value into the stack slot. We have to be
  // careful if I is an invoke instruction, because we can't insert the store
  // AFTER the terminator instruction.
  BasicBlock::iterator InsertPt;
  if (!I.isTerminator()) {
    InsertPt = ++I.getIterator();
    for (; isa<PHINode>(InsertPt) || InsertPt->isEHPad(); ++InsertPt)
      /* empty */;   // Don't insert before PHI nodes or landingpad instrs.
  } else {
    InvokeInst &II = cast<InvokeInst>(I);
    InsertPt = II.getNormalDest()->getFirstInsertionPt();
  }

  new StoreInst(&I, Slot, &*InsertPt);
  return Slot;
}

/// DemotePHIToStack - This function takes a virtual register computed by a PHI
/// node and replaces it with a slot in the stack frame allocated via alloca.
/// The PHI node is deleted. It returns the pointer to the alloca inserted.
AllocaInst *llvm::DemotePHIToStack(PHINode *P, Instruction *AllocaPoint) {
  if (P->use_empty()) {
    P->eraseFromParent();
    return nullptr;
  }

  const DataLayout &DL = P->getModule()->getDataLayout();

  // Create a stack slot to hold the value.
  AllocaInst *Slot;
  if (AllocaPoint) {
    Slot = new AllocaInst(P->getType(), DL.getAllocaAddrSpace(), nullptr,
                          P->getName()+".reg2mem", AllocaPoint);
  } else {
    Function *F = P->getParent()->getParent();
    Slot = new AllocaInst(P->getType(), DL.getAllocaAddrSpace(), nullptr,
                          P->getName() + ".reg2mem",
                          &F->getEntryBlock().front());
  }

  // Iterate over each operand inserting a store in each predecessor.
  for (unsigned i = 0, e = P->getNumIncomingValues(); i < e; ++i) {
    if (InvokeInst *II = dyn_cast<InvokeInst>(P->getIncomingValue(i))) {
      assert(II->getParent() != P->getIncomingBlock(i) &&
             "Invoke edge not supported yet"); (void)II;
    }
    new StoreInst(P->getIncomingValue(i), Slot,
                  P->getIncomingBlock(i)->getTerminator());
  }

  // Insert a load in place of the PHI and replace all uses.
  BasicBlock::iterator InsertPt = P->getIterator();

  for (; isa<PHINode>(InsertPt) || InsertPt->isEHPad(); ++InsertPt)
    /* empty */;   // Don't insert before PHI nodes or landingpad instrs.

  Value *V = new LoadInst(Slot, P->getName() + ".reload", &*InsertPt);
  P->replaceAllUsesWith(V);

  // Delete PHI.
  P->eraseFromParent();
  return Slot;
}
