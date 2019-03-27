//===- LoopTraversal.cpp - Optimal basic block traversal order --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/LoopTraversal.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/CodeGen/MachineFunction.h"

using namespace llvm;

bool LoopTraversal::isBlockDone(MachineBasicBlock *MBB) {
  unsigned MBBNumber = MBB->getNumber();
  assert(MBBNumber < MBBInfos.size() && "Unexpected basic block number.");
  return MBBInfos[MBBNumber].PrimaryCompleted &&
         MBBInfos[MBBNumber].IncomingCompleted ==
             MBBInfos[MBBNumber].PrimaryIncoming &&
         MBBInfos[MBBNumber].IncomingProcessed == MBB->pred_size();
}

LoopTraversal::TraversalOrder LoopTraversal::traverse(MachineFunction &MF) {
  // Initialize the MMBInfos
  MBBInfos.assign(MF.getNumBlockIDs(), MBBInfo());

  MachineBasicBlock *Entry = &*MF.begin();
  ReversePostOrderTraversal<MachineBasicBlock *> RPOT(Entry);
  SmallVector<MachineBasicBlock *, 4> Workqueue;
  SmallVector<TraversedMBBInfo, 4> MBBTraversalOrder;
  for (MachineBasicBlock *MBB : RPOT) {
    // N.B: IncomingProcessed and IncomingCompleted were already updated while
    // processing this block's predecessors.
    unsigned MBBNumber = MBB->getNumber();
    assert(MBBNumber < MBBInfos.size() && "Unexpected basic block number.");
    MBBInfos[MBBNumber].PrimaryCompleted = true;
    MBBInfos[MBBNumber].PrimaryIncoming = MBBInfos[MBBNumber].IncomingProcessed;
    bool Primary = true;
    Workqueue.push_back(MBB);
    while (!Workqueue.empty()) {
      MachineBasicBlock *ActiveMBB = &*Workqueue.back();
      Workqueue.pop_back();
      bool Done = isBlockDone(ActiveMBB);
      MBBTraversalOrder.push_back(TraversedMBBInfo(ActiveMBB, Primary, Done));
      for (MachineBasicBlock *Succ : ActiveMBB->successors()) {
        unsigned SuccNumber = Succ->getNumber();
        assert(SuccNumber < MBBInfos.size() &&
               "Unexpected basic block number.");
        if (!isBlockDone(Succ)) {
          if (Primary)
            MBBInfos[SuccNumber].IncomingProcessed++;
          if (Done)
            MBBInfos[SuccNumber].IncomingCompleted++;
          if (isBlockDone(Succ))
            Workqueue.push_back(Succ);
        }
      }
      Primary = false;
    }
  }

  // We need to go through again and finalize any blocks that are not done yet.
  // This is possible if blocks have dead predecessors, so we didn't visit them
  // above.
  for (MachineBasicBlock *MBB : RPOT) {
    if (!isBlockDone(MBB))
      MBBTraversalOrder.push_back(TraversedMBBInfo(MBB, false, true));
    // Don't update successors here. We'll get to them anyway through this
    // loop.
  }

  MBBInfos.clear();

  return MBBTraversalOrder;
}
