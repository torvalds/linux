//===-- LanaiFrameLowering.cpp - Lanai Frame Information ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the Lanai implementation of TargetFrameLowering class.
//
//===----------------------------------------------------------------------===//

#include "LanaiFrameLowering.h"

#include "LanaiInstrInfo.h"
#include "LanaiMachineFunctionInfo.h"
#include "LanaiSubtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/Function.h"

using namespace llvm;

// Determines the size of the frame and maximum call frame size.
void LanaiFrameLowering::determineFrameLayout(MachineFunction &MF) const {
  MachineFrameInfo &MFI = MF.getFrameInfo();
  const LanaiRegisterInfo *LRI = STI.getRegisterInfo();

  // Get the number of bytes to allocate from the FrameInfo.
  unsigned FrameSize = MFI.getStackSize();

  // Get the alignment.
  unsigned StackAlign = LRI->needsStackRealignment(MF) ? MFI.getMaxAlignment()
                                                       : getStackAlignment();

  // Get the maximum call frame size of all the calls.
  unsigned MaxCallFrameSize = MFI.getMaxCallFrameSize();

  // If we have dynamic alloca then MaxCallFrameSize needs to be aligned so
  // that allocations will be aligned.
  if (MFI.hasVarSizedObjects())
    MaxCallFrameSize = alignTo(MaxCallFrameSize, StackAlign);

  // Update maximum call frame size.
  MFI.setMaxCallFrameSize(MaxCallFrameSize);

  // Include call frame size in total.
  if (!(hasReservedCallFrame(MF) && MFI.adjustsStack()))
    FrameSize += MaxCallFrameSize;

  // Make sure the frame is aligned.
  FrameSize = alignTo(FrameSize, StackAlign);

  // Update frame info.
  MFI.setStackSize(FrameSize);
}

// Iterates through each basic block in a machine function and replaces
// ADJDYNALLOC pseudo instructions with a Lanai:ADDI with the
// maximum call frame size as the immediate.
void LanaiFrameLowering::replaceAdjDynAllocPseudo(MachineFunction &MF) const {
  const LanaiInstrInfo &LII =
      *static_cast<const LanaiInstrInfo *>(STI.getInstrInfo());
  unsigned MaxCallFrameSize = MF.getFrameInfo().getMaxCallFrameSize();

  for (MachineFunction::iterator MBB = MF.begin(), E = MF.end(); MBB != E;
       ++MBB) {
    MachineBasicBlock::iterator MBBI = MBB->begin();
    while (MBBI != MBB->end()) {
      MachineInstr &MI = *MBBI++;
      if (MI.getOpcode() == Lanai::ADJDYNALLOC) {
        DebugLoc DL = MI.getDebugLoc();
        unsigned Dst = MI.getOperand(0).getReg();
        unsigned Src = MI.getOperand(1).getReg();

        BuildMI(*MBB, MI, DL, LII.get(Lanai::ADD_I_LO), Dst)
            .addReg(Src)
            .addImm(MaxCallFrameSize);
        MI.eraseFromParent();
      }
    }
  }
}

// Generates the following sequence for function entry:
//   st %fp,-4[*%sp]        !push old FP
//   add %sp,8,%fp          !generate new FP
//   sub %sp,0x4,%sp        !allocate stack space (as needed)
void LanaiFrameLowering::emitPrologue(MachineFunction &MF,
                                      MachineBasicBlock &MBB) const {
  assert(&MF.front() == &MBB && "Shrink-wrapping not yet supported");

  MachineFrameInfo &MFI = MF.getFrameInfo();
  const LanaiInstrInfo &LII =
      *static_cast<const LanaiInstrInfo *>(STI.getInstrInfo());
  MachineBasicBlock::iterator MBBI = MBB.begin();

  // Debug location must be unknown since the first debug location is used
  // to determine the end of the prologue.
  DebugLoc DL;

  // Determine the correct frame layout
  determineFrameLayout(MF);

  // FIXME: This appears to be overallocating.  Needs investigation.
  // Get the number of bytes to allocate from the FrameInfo.
  unsigned StackSize = MFI.getStackSize();

  // Push old FP
  // st %fp,-4[*%sp]
  BuildMI(MBB, MBBI, DL, LII.get(Lanai::SW_RI))
      .addReg(Lanai::FP)
      .addReg(Lanai::SP)
      .addImm(-4)
      .addImm(LPAC::makePreOp(LPAC::ADD))
      .setMIFlag(MachineInstr::FrameSetup);

  // Generate new FP
  // add %sp,8,%fp
  BuildMI(MBB, MBBI, DL, LII.get(Lanai::ADD_I_LO), Lanai::FP)
      .addReg(Lanai::SP)
      .addImm(8)
      .setMIFlag(MachineInstr::FrameSetup);

  // Allocate space on the stack if needed
  // sub %sp,StackSize,%sp
  if (StackSize != 0) {
    BuildMI(MBB, MBBI, DL, LII.get(Lanai::SUB_I_LO), Lanai::SP)
        .addReg(Lanai::SP)
        .addImm(StackSize)
        .setMIFlag(MachineInstr::FrameSetup);
  }

  // Replace ADJDYNANALLOC
  if (MFI.hasVarSizedObjects())
    replaceAdjDynAllocPseudo(MF);
}

MachineBasicBlock::iterator LanaiFrameLowering::eliminateCallFramePseudoInstr(
    MachineFunction & /*MF*/, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator I) const {
  // Discard ADJCALLSTACKDOWN, ADJCALLSTACKUP instructions.
  return MBB.erase(I);
}

// The function epilogue should not depend on the current stack pointer!
// It should use the frame pointer only.  This is mandatory because
// of alloca; we also take advantage of it to omit stack adjustments
// before returning.
//
// Note that when we go to restore the preserved register values we must
// not try to address their slots by using offsets from the stack pointer.
// That's because the stack pointer may have been moved during the function
// execution due to a call to alloca().  Rather, we must restore all
// preserved registers via offsets from the frame pointer value.
//
// Note also that when the current frame is being "popped" (by adjusting
// the value of the stack pointer) on function exit, we must (for the
// sake of alloca) set the new value of the stack pointer based upon
// the current value of the frame pointer.  We can't just add what we
// believe to be the (static) frame size to the stack pointer because
// if we did that, and alloca() had been called during this function,
// we would end up returning *without* having fully deallocated all of
// the space grabbed by alloca.  If that happened, and a function
// containing one or more alloca() calls was called over and over again,
// then the stack would grow without limit!
//
// RET is lowered to
//      ld -4[%fp],%pc  # modify %pc (two delay slots)
// as the return address is in the stack frame and mov to pc is allowed.
// emitEpilogue emits
//      mov %fp,%sp     # restore the stack pointer
//      ld -8[%fp],%fp  # restore the caller's frame pointer
// before RET and the delay slot filler will move RET such that these
// instructions execute in the delay slots of the load to PC.
void LanaiFrameLowering::emitEpilogue(MachineFunction & /*MF*/,
                                      MachineBasicBlock &MBB) const {
  MachineBasicBlock::iterator MBBI = MBB.getLastNonDebugInstr();
  const LanaiInstrInfo &LII =
      *static_cast<const LanaiInstrInfo *>(STI.getInstrInfo());
  DebugLoc DL = MBBI->getDebugLoc();

  // Restore the stack pointer using the callee's frame pointer value.
  BuildMI(MBB, MBBI, DL, LII.get(Lanai::ADD_I_LO), Lanai::SP)
      .addReg(Lanai::FP)
      .addImm(0);

  // Restore the frame pointer from the stack.
  BuildMI(MBB, MBBI, DL, LII.get(Lanai::LDW_RI), Lanai::FP)
      .addReg(Lanai::FP)
      .addImm(-8)
      .addImm(LPAC::ADD);
}

void LanaiFrameLowering::determineCalleeSaves(MachineFunction &MF,
                                              BitVector &SavedRegs,
                                              RegScavenger *RS) const {
  TargetFrameLowering::determineCalleeSaves(MF, SavedRegs, RS);

  MachineFrameInfo &MFI = MF.getFrameInfo();
  const LanaiRegisterInfo *LRI =
      static_cast<const LanaiRegisterInfo *>(STI.getRegisterInfo());
  int Offset = -4;

  // Reserve 4 bytes for the saved RCA
  MFI.CreateFixedObject(4, Offset, true);
  Offset -= 4;

  // Reserve 4 bytes for the saved FP
  MFI.CreateFixedObject(4, Offset, true);
  Offset -= 4;

  if (LRI->hasBasePointer(MF)) {
    MFI.CreateFixedObject(4, Offset, true);
    SavedRegs.reset(LRI->getBaseRegister());
  }
}
