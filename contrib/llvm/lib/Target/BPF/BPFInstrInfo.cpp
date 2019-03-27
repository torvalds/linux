//===-- BPFInstrInfo.cpp - BPF Instruction Information ----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the BPF implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#include "BPFInstrInfo.h"
#include "BPF.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <iterator>

#define GET_INSTRINFO_CTOR_DTOR
#include "BPFGenInstrInfo.inc"

using namespace llvm;

BPFInstrInfo::BPFInstrInfo()
    : BPFGenInstrInfo(BPF::ADJCALLSTACKDOWN, BPF::ADJCALLSTACKUP) {}

void BPFInstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                               MachineBasicBlock::iterator I,
                               const DebugLoc &DL, unsigned DestReg,
                               unsigned SrcReg, bool KillSrc) const {
  if (BPF::GPRRegClass.contains(DestReg, SrcReg))
    BuildMI(MBB, I, DL, get(BPF::MOV_rr), DestReg)
        .addReg(SrcReg, getKillRegState(KillSrc));
  else if (BPF::GPR32RegClass.contains(DestReg, SrcReg))
    BuildMI(MBB, I, DL, get(BPF::MOV_rr_32), DestReg)
        .addReg(SrcReg, getKillRegState(KillSrc));
  else
    llvm_unreachable("Impossible reg-to-reg copy");
}

void BPFInstrInfo::expandMEMCPY(MachineBasicBlock::iterator MI) const {
  unsigned DstReg = MI->getOperand(0).getReg();
  unsigned SrcReg = MI->getOperand(1).getReg();
  uint64_t CopyLen = MI->getOperand(2).getImm();
  uint64_t Alignment = MI->getOperand(3).getImm();
  unsigned ScratchReg = MI->getOperand(4).getReg();
  MachineBasicBlock *BB = MI->getParent();
  DebugLoc dl = MI->getDebugLoc();
  unsigned LdOpc, StOpc;

  switch (Alignment) {
  case 1:
    LdOpc = BPF::LDB;
    StOpc = BPF::STB;
    break;
  case 2:
    LdOpc = BPF::LDH;
    StOpc = BPF::STH;
    break;
  case 4:
    LdOpc = BPF::LDW;
    StOpc = BPF::STW;
    break;
  case 8:
    LdOpc = BPF::LDD;
    StOpc = BPF::STD;
    break;
  default:
    llvm_unreachable("unsupported memcpy alignment");
  }

  unsigned IterationNum = CopyLen >> Log2_64(Alignment);
  for(unsigned I = 0; I < IterationNum; ++I) {
    BuildMI(*BB, MI, dl, get(LdOpc))
            .addReg(ScratchReg, RegState::Define).addReg(SrcReg)
            .addImm(I * Alignment);
    BuildMI(*BB, MI, dl, get(StOpc))
            .addReg(ScratchReg, RegState::Kill).addReg(DstReg)
            .addImm(I * Alignment);
  }

  unsigned BytesLeft = CopyLen & (Alignment - 1);
  unsigned Offset = IterationNum * Alignment;
  bool Hanging4Byte = BytesLeft & 0x4;
  bool Hanging2Byte = BytesLeft & 0x2;
  bool Hanging1Byte = BytesLeft & 0x1;
  if (Hanging4Byte) {
    BuildMI(*BB, MI, dl, get(BPF::LDW))
            .addReg(ScratchReg, RegState::Define).addReg(SrcReg).addImm(Offset);
    BuildMI(*BB, MI, dl, get(BPF::STW))
            .addReg(ScratchReg, RegState::Kill).addReg(DstReg).addImm(Offset);
    Offset += 4;
  }
  if (Hanging2Byte) {
    BuildMI(*BB, MI, dl, get(BPF::LDH))
            .addReg(ScratchReg, RegState::Define).addReg(SrcReg).addImm(Offset);
    BuildMI(*BB, MI, dl, get(BPF::STH))
            .addReg(ScratchReg, RegState::Kill).addReg(DstReg).addImm(Offset);
    Offset += 2;
  }
  if (Hanging1Byte) {
    BuildMI(*BB, MI, dl, get(BPF::LDB))
            .addReg(ScratchReg, RegState::Define).addReg(SrcReg).addImm(Offset);
    BuildMI(*BB, MI, dl, get(BPF::STB))
            .addReg(ScratchReg, RegState::Kill).addReg(DstReg).addImm(Offset);
  }

  BB->erase(MI);
}

bool BPFInstrInfo::expandPostRAPseudo(MachineInstr &MI) const {
  if (MI.getOpcode() == BPF::MEMCPY) {
    expandMEMCPY(MI);
    return true;
  }

  return false;
}

void BPFInstrInfo::storeRegToStackSlot(MachineBasicBlock &MBB,
                                       MachineBasicBlock::iterator I,
                                       unsigned SrcReg, bool IsKill, int FI,
                                       const TargetRegisterClass *RC,
                                       const TargetRegisterInfo *TRI) const {
  DebugLoc DL;
  if (I != MBB.end())
    DL = I->getDebugLoc();

  if (RC == &BPF::GPRRegClass)
    BuildMI(MBB, I, DL, get(BPF::STD))
        .addReg(SrcReg, getKillRegState(IsKill))
        .addFrameIndex(FI)
        .addImm(0);
  else if (RC == &BPF::GPR32RegClass)
    BuildMI(MBB, I, DL, get(BPF::STW32))
        .addReg(SrcReg, getKillRegState(IsKill))
        .addFrameIndex(FI)
        .addImm(0);
  else
    llvm_unreachable("Can't store this register to stack slot");
}

void BPFInstrInfo::loadRegFromStackSlot(MachineBasicBlock &MBB,
                                        MachineBasicBlock::iterator I,
                                        unsigned DestReg, int FI,
                                        const TargetRegisterClass *RC,
                                        const TargetRegisterInfo *TRI) const {
  DebugLoc DL;
  if (I != MBB.end())
    DL = I->getDebugLoc();

  if (RC == &BPF::GPRRegClass)
    BuildMI(MBB, I, DL, get(BPF::LDD), DestReg).addFrameIndex(FI).addImm(0);
  else if (RC == &BPF::GPR32RegClass)
    BuildMI(MBB, I, DL, get(BPF::LDW32), DestReg).addFrameIndex(FI).addImm(0);
  else
    llvm_unreachable("Can't load this register from stack slot");
}

bool BPFInstrInfo::analyzeBranch(MachineBasicBlock &MBB,
                                 MachineBasicBlock *&TBB,
                                 MachineBasicBlock *&FBB,
                                 SmallVectorImpl<MachineOperand> &Cond,
                                 bool AllowModify) const {
  // Start from the bottom of the block and work up, examining the
  // terminator instructions.
  MachineBasicBlock::iterator I = MBB.end();
  while (I != MBB.begin()) {
    --I;
    if (I->isDebugInstr())
      continue;

    // Working from the bottom, when we see a non-terminator
    // instruction, we're done.
    if (!isUnpredicatedTerminator(*I))
      break;

    // A terminator that isn't a branch can't easily be handled
    // by this analysis.
    if (!I->isBranch())
      return true;

    // Handle unconditional branches.
    if (I->getOpcode() == BPF::JMP) {
      if (!AllowModify) {
        TBB = I->getOperand(0).getMBB();
        continue;
      }

      // If the block has any instructions after a J, delete them.
      while (std::next(I) != MBB.end())
        std::next(I)->eraseFromParent();
      Cond.clear();
      FBB = nullptr;

      // Delete the J if it's equivalent to a fall-through.
      if (MBB.isLayoutSuccessor(I->getOperand(0).getMBB())) {
        TBB = nullptr;
        I->eraseFromParent();
        I = MBB.end();
        continue;
      }

      // TBB is used to indicate the unconditinal destination.
      TBB = I->getOperand(0).getMBB();
      continue;
    }
    // Cannot handle conditional branches
    return true;
  }

  return false;
}

unsigned BPFInstrInfo::insertBranch(MachineBasicBlock &MBB,
                                    MachineBasicBlock *TBB,
                                    MachineBasicBlock *FBB,
                                    ArrayRef<MachineOperand> Cond,
                                    const DebugLoc &DL,
                                    int *BytesAdded) const {
  assert(!BytesAdded && "code size not handled");

  // Shouldn't be a fall through.
  assert(TBB && "insertBranch must not be told to insert a fallthrough");

  if (Cond.empty()) {
    // Unconditional branch
    assert(!FBB && "Unconditional branch with multiple successors!");
    BuildMI(&MBB, DL, get(BPF::JMP)).addMBB(TBB);
    return 1;
  }

  llvm_unreachable("Unexpected conditional branch");
}

unsigned BPFInstrInfo::removeBranch(MachineBasicBlock &MBB,
                                    int *BytesRemoved) const {
  assert(!BytesRemoved && "code size not handled");

  MachineBasicBlock::iterator I = MBB.end();
  unsigned Count = 0;

  while (I != MBB.begin()) {
    --I;
    if (I->isDebugInstr())
      continue;
    if (I->getOpcode() != BPF::JMP)
      break;
    // Remove the branch.
    I->eraseFromParent();
    I = MBB.end();
    ++Count;
  }

  return Count;
}
