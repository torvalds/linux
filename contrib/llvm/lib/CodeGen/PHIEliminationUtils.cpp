//===-- PHIEliminationUtils.cpp - Helper functions for PHI elimination ----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "PHIEliminationUtils.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
using namespace llvm;

// findCopyInsertPoint - Find a safe place in MBB to insert a copy from SrcReg
// when following the CFG edge to SuccMBB. This needs to be after any def of
// SrcReg, but before any subsequent point where control flow might jump out of
// the basic block.
MachineBasicBlock::iterator
llvm::findPHICopyInsertPoint(MachineBasicBlock* MBB, MachineBasicBlock* SuccMBB,
                             unsigned SrcReg) {
  // Handle the trivial case trivially.
  if (MBB->empty())
    return MBB->begin();

  // Usually, we just want to insert the copy before the first terminator
  // instruction. However, for the edge going to a landing pad, we must insert
  // the copy before the call/invoke instruction.
  if (!SuccMBB->isEHPad())
    return MBB->getFirstTerminator();

  // Discover any defs/uses in this basic block.
  SmallPtrSet<MachineInstr*, 8> DefUsesInMBB;
  MachineRegisterInfo& MRI = MBB->getParent()->getRegInfo();
  for (MachineInstr &RI : MRI.reg_instructions(SrcReg)) {
    if (RI.getParent() == MBB)
      DefUsesInMBB.insert(&RI);
  }

  MachineBasicBlock::iterator InsertPoint;
  if (DefUsesInMBB.empty()) {
    // No defs.  Insert the copy at the start of the basic block.
    InsertPoint = MBB->begin();
  } else if (DefUsesInMBB.size() == 1) {
    // Insert the copy immediately after the def/use.
    InsertPoint = *DefUsesInMBB.begin();
    ++InsertPoint;
  } else {
    // Insert the copy immediately after the last def/use.
    InsertPoint = MBB->end();
    while (!DefUsesInMBB.count(&*--InsertPoint)) {}
    ++InsertPoint;
  }

  // Make sure the copy goes after any phi nodes but before
  // any debug nodes.
  return MBB->SkipPHIsAndLabels(InsertPoint);
}
