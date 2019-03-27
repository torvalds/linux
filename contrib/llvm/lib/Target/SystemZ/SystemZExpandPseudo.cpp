//==-- SystemZExpandPseudo.cpp - Expand pseudo instructions -------*- C++ -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains a pass that expands pseudo instructions into target
// instructions to allow proper scheduling and other late optimizations.  This
// pass should be run after register allocation but before the post-regalloc
// scheduling pass.
//
//===----------------------------------------------------------------------===//

#include "SystemZ.h"
#include "SystemZInstrInfo.h"
#include "SystemZSubtarget.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
using namespace llvm;

#define SYSTEMZ_EXPAND_PSEUDO_NAME "SystemZ pseudo instruction expansion pass"

namespace llvm {
  void initializeSystemZExpandPseudoPass(PassRegistry&);
}

namespace {
class SystemZExpandPseudo : public MachineFunctionPass {
public:
  static char ID;
  SystemZExpandPseudo() : MachineFunctionPass(ID) {
    initializeSystemZExpandPseudoPass(*PassRegistry::getPassRegistry());
  }

  const SystemZInstrInfo *TII;

  bool runOnMachineFunction(MachineFunction &Fn) override;

  StringRef getPassName() const override { return SYSTEMZ_EXPAND_PSEUDO_NAME; }

private:
  bool expandMBB(MachineBasicBlock &MBB);
  bool expandMI(MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
                MachineBasicBlock::iterator &NextMBBI);
  bool expandLOCRMux(MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
                     MachineBasicBlock::iterator &NextMBBI);
};
char SystemZExpandPseudo::ID = 0;
}

INITIALIZE_PASS(SystemZExpandPseudo, "systemz-expand-pseudo",
                SYSTEMZ_EXPAND_PSEUDO_NAME, false, false)

/// Returns an instance of the pseudo instruction expansion pass.
FunctionPass *llvm::createSystemZExpandPseudoPass(SystemZTargetMachine &TM) {
  return new SystemZExpandPseudo();
}

// MI is a load-register-on-condition pseudo instruction that could not be
// handled as a single hardware instruction.  Replace it by a branch sequence.
bool SystemZExpandPseudo::expandLOCRMux(MachineBasicBlock &MBB,
                                        MachineBasicBlock::iterator MBBI,
                                        MachineBasicBlock::iterator &NextMBBI) {
  MachineFunction &MF = *MBB.getParent();
  const BasicBlock *BB = MBB.getBasicBlock();
  MachineInstr &MI = *MBBI;
  DebugLoc DL = MI.getDebugLoc();
  unsigned DestReg = MI.getOperand(0).getReg();
  unsigned SrcReg = MI.getOperand(2).getReg();
  unsigned CCValid = MI.getOperand(3).getImm();
  unsigned CCMask = MI.getOperand(4).getImm();

  LivePhysRegs LiveRegs(TII->getRegisterInfo());
  LiveRegs.addLiveOuts(MBB);
  for (auto I = std::prev(MBB.end()); I != MBBI; --I)
    LiveRegs.stepBackward(*I);

  // Splice MBB at MI, moving the rest of the block into RestMBB.
  MachineBasicBlock *RestMBB = MF.CreateMachineBasicBlock(BB);
  MF.insert(std::next(MachineFunction::iterator(MBB)), RestMBB);
  RestMBB->splice(RestMBB->begin(), &MBB, MI, MBB.end());
  RestMBB->transferSuccessors(&MBB);
  for (auto I = LiveRegs.begin(); I != LiveRegs.end(); ++I)
    RestMBB->addLiveIn(*I);

  // Create a new block MoveMBB to hold the move instruction.
  MachineBasicBlock *MoveMBB = MF.CreateMachineBasicBlock(BB);
  MF.insert(std::next(MachineFunction::iterator(MBB)), MoveMBB);
  MoveMBB->addLiveIn(SrcReg);
  for (auto I = LiveRegs.begin(); I != LiveRegs.end(); ++I)
    MoveMBB->addLiveIn(*I);

  // At the end of MBB, create a conditional branch to RestMBB if the
  // condition is false, otherwise fall through to MoveMBB.
  BuildMI(&MBB, DL, TII->get(SystemZ::BRC))
    .addImm(CCValid).addImm(CCMask ^ CCValid).addMBB(RestMBB);
  MBB.addSuccessor(RestMBB);
  MBB.addSuccessor(MoveMBB);

  // In MoveMBB, emit an instruction to move SrcReg into DestReg,
  // then fall through to RestMBB.
  TII->copyPhysReg(*MoveMBB, MoveMBB->end(), DL, DestReg, SrcReg,
                   MI.getOperand(2).isKill());
  MoveMBB->addSuccessor(RestMBB);

  NextMBBI = MBB.end();
  MI.eraseFromParent();
  return true;
}

/// If MBBI references a pseudo instruction that should be expanded here,
/// do the expansion and return true.  Otherwise return false.
bool SystemZExpandPseudo::expandMI(MachineBasicBlock &MBB,
                                   MachineBasicBlock::iterator MBBI,
                                   MachineBasicBlock::iterator &NextMBBI) {
  MachineInstr &MI = *MBBI;
  switch (MI.getOpcode()) {
  case SystemZ::LOCRMux:
    return expandLOCRMux(MBB, MBBI, NextMBBI);
  default:
    break;
  }
  return false;
}

/// Iterate over the instructions in basic block MBB and expand any
/// pseudo instructions.  Return true if anything was modified.
bool SystemZExpandPseudo::expandMBB(MachineBasicBlock &MBB) {
  bool Modified = false;

  MachineBasicBlock::iterator MBBI = MBB.begin(), E = MBB.end();
  while (MBBI != E) {
    MachineBasicBlock::iterator NMBBI = std::next(MBBI);
    Modified |= expandMI(MBB, MBBI, NMBBI);
    MBBI = NMBBI;
  }

  return Modified;
}

bool SystemZExpandPseudo::runOnMachineFunction(MachineFunction &MF) {
  TII = static_cast<const SystemZInstrInfo *>(MF.getSubtarget().getInstrInfo());

  bool Modified = false;
  for (auto &MBB : MF)
    Modified |= expandMBB(MBB);
  return Modified;
}

