//===-- XCoreRegisterInfo.cpp - XCore Register Information ----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the XCore implementation of the MRegisterInfo class.
//
//===----------------------------------------------------------------------===//

#include "XCoreRegisterInfo.h"
#include "XCore.h"
#include "XCoreInstrInfo.h"
#include "XCoreMachineFunctionInfo.h"
#include "XCoreSubtarget.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"

using namespace llvm;

#define DEBUG_TYPE "xcore-reg-info"

#define GET_REGINFO_TARGET_DESC
#include "XCoreGenRegisterInfo.inc"

XCoreRegisterInfo::XCoreRegisterInfo()
  : XCoreGenRegisterInfo(XCore::LR) {
}

// helper functions
static inline bool isImmUs(unsigned val) {
  return val <= 11;
}

static inline bool isImmU6(unsigned val) {
  return val < (1 << 6);
}

static inline bool isImmU16(unsigned val) {
  return val < (1 << 16);
}


static void InsertFPImmInst(MachineBasicBlock::iterator II,
                            const XCoreInstrInfo &TII,
                            unsigned Reg, unsigned FrameReg, int Offset ) {
  MachineInstr &MI = *II;
  MachineBasicBlock &MBB = *MI.getParent();
  DebugLoc dl = MI.getDebugLoc();

  switch (MI.getOpcode()) {
  case XCore::LDWFI:
    BuildMI(MBB, II, dl, TII.get(XCore::LDW_2rus), Reg)
          .addReg(FrameReg)
          .addImm(Offset)
          .addMemOperand(*MI.memoperands_begin());
    break;
  case XCore::STWFI:
    BuildMI(MBB, II, dl, TII.get(XCore::STW_2rus))
          .addReg(Reg, getKillRegState(MI.getOperand(0).isKill()))
          .addReg(FrameReg)
          .addImm(Offset)
          .addMemOperand(*MI.memoperands_begin());
    break;
  case XCore::LDAWFI:
    BuildMI(MBB, II, dl, TII.get(XCore::LDAWF_l2rus), Reg)
          .addReg(FrameReg)
          .addImm(Offset);
    break;
  default:
    llvm_unreachable("Unexpected Opcode");
  }
}

static void InsertFPConstInst(MachineBasicBlock::iterator II,
                              const XCoreInstrInfo &TII,
                              unsigned Reg, unsigned FrameReg,
                              int Offset, RegScavenger *RS ) {
  assert(RS && "requiresRegisterScavenging failed");
  MachineInstr &MI = *II;
  MachineBasicBlock &MBB = *MI.getParent();
  DebugLoc dl = MI.getDebugLoc();
  unsigned ScratchOffset = RS->scavengeRegister(&XCore::GRRegsRegClass, II, 0);
  RS->setRegUsed(ScratchOffset);
  TII.loadImmediate(MBB, II, ScratchOffset, Offset);

  switch (MI.getOpcode()) {
  case XCore::LDWFI:
    BuildMI(MBB, II, dl, TII.get(XCore::LDW_3r), Reg)
          .addReg(FrameReg)
          .addReg(ScratchOffset, RegState::Kill)
          .addMemOperand(*MI.memoperands_begin());
    break;
  case XCore::STWFI:
    BuildMI(MBB, II, dl, TII.get(XCore::STW_l3r))
          .addReg(Reg, getKillRegState(MI.getOperand(0).isKill()))
          .addReg(FrameReg)
          .addReg(ScratchOffset, RegState::Kill)
          .addMemOperand(*MI.memoperands_begin());
    break;
  case XCore::LDAWFI:
    BuildMI(MBB, II, dl, TII.get(XCore::LDAWF_l3r), Reg)
          .addReg(FrameReg)
          .addReg(ScratchOffset, RegState::Kill);
    break;
  default:
    llvm_unreachable("Unexpected Opcode");
  }
}

static void InsertSPImmInst(MachineBasicBlock::iterator II,
                            const XCoreInstrInfo &TII,
                            unsigned Reg, int Offset) {
  MachineInstr &MI = *II;
  MachineBasicBlock &MBB = *MI.getParent();
  DebugLoc dl = MI.getDebugLoc();
  bool isU6 = isImmU6(Offset);

  switch (MI.getOpcode()) {
  int NewOpcode;
  case XCore::LDWFI:
    NewOpcode = (isU6) ? XCore::LDWSP_ru6 : XCore::LDWSP_lru6;
    BuildMI(MBB, II, dl, TII.get(NewOpcode), Reg)
          .addImm(Offset)
          .addMemOperand(*MI.memoperands_begin());
    break;
  case XCore::STWFI:
    NewOpcode = (isU6) ? XCore::STWSP_ru6 : XCore::STWSP_lru6;
    BuildMI(MBB, II, dl, TII.get(NewOpcode))
          .addReg(Reg, getKillRegState(MI.getOperand(0).isKill()))
          .addImm(Offset)
          .addMemOperand(*MI.memoperands_begin());
    break;
  case XCore::LDAWFI:
    NewOpcode = (isU6) ? XCore::LDAWSP_ru6 : XCore::LDAWSP_lru6;
    BuildMI(MBB, II, dl, TII.get(NewOpcode), Reg)
          .addImm(Offset);
    break;
  default:
    llvm_unreachable("Unexpected Opcode");
  }
}

static void InsertSPConstInst(MachineBasicBlock::iterator II,
                                const XCoreInstrInfo &TII,
                                unsigned Reg, int Offset, RegScavenger *RS ) {
  assert(RS && "requiresRegisterScavenging failed");
  MachineInstr &MI = *II;
  MachineBasicBlock &MBB = *MI.getParent();
  DebugLoc dl = MI.getDebugLoc();
  unsigned OpCode = MI.getOpcode();

  unsigned ScratchBase;
  if (OpCode==XCore::STWFI) {
    ScratchBase = RS->scavengeRegister(&XCore::GRRegsRegClass, II, 0);
    RS->setRegUsed(ScratchBase);
  } else
    ScratchBase = Reg;
  BuildMI(MBB, II, dl, TII.get(XCore::LDAWSP_ru6), ScratchBase).addImm(0);
  unsigned ScratchOffset = RS->scavengeRegister(&XCore::GRRegsRegClass, II, 0);
  RS->setRegUsed(ScratchOffset);
  TII.loadImmediate(MBB, II, ScratchOffset, Offset);

  switch (OpCode) {
  case XCore::LDWFI:
    BuildMI(MBB, II, dl, TII.get(XCore::LDW_3r), Reg)
          .addReg(ScratchBase, RegState::Kill)
          .addReg(ScratchOffset, RegState::Kill)
          .addMemOperand(*MI.memoperands_begin());
    break;
  case XCore::STWFI:
    BuildMI(MBB, II, dl, TII.get(XCore::STW_l3r))
          .addReg(Reg, getKillRegState(MI.getOperand(0).isKill()))
          .addReg(ScratchBase, RegState::Kill)
          .addReg(ScratchOffset, RegState::Kill)
          .addMemOperand(*MI.memoperands_begin());
    break;
  case XCore::LDAWFI:
    BuildMI(MBB, II, dl, TII.get(XCore::LDAWF_l3r), Reg)
          .addReg(ScratchBase, RegState::Kill)
          .addReg(ScratchOffset, RegState::Kill);
    break;
  default:
    llvm_unreachable("Unexpected Opcode");
  }
}

bool XCoreRegisterInfo::needsFrameMoves(const MachineFunction &MF) {
  return MF.getMMI().hasDebugInfo() || MF.getFunction().needsUnwindTableEntry();
}

const MCPhysReg *
XCoreRegisterInfo::getCalleeSavedRegs(const MachineFunction *MF) const {
  // The callee saved registers LR & FP are explicitly handled during
  // emitPrologue & emitEpilogue and related functions.
  static const MCPhysReg CalleeSavedRegs[] = {
    XCore::R4, XCore::R5, XCore::R6, XCore::R7,
    XCore::R8, XCore::R9, XCore::R10,
    0
  };
  static const MCPhysReg CalleeSavedRegsFP[] = {
    XCore::R4, XCore::R5, XCore::R6, XCore::R7,
    XCore::R8, XCore::R9,
    0
  };
  const XCoreFrameLowering *TFI = getFrameLowering(*MF);
  if (TFI->hasFP(*MF))
    return CalleeSavedRegsFP;
  return CalleeSavedRegs;
}

BitVector XCoreRegisterInfo::getReservedRegs(const MachineFunction &MF) const {
  BitVector Reserved(getNumRegs());
  const XCoreFrameLowering *TFI = getFrameLowering(MF);

  Reserved.set(XCore::CP);
  Reserved.set(XCore::DP);
  Reserved.set(XCore::SP);
  Reserved.set(XCore::LR);
  if (TFI->hasFP(MF)) {
    Reserved.set(XCore::R10);
  }
  return Reserved;
}

bool
XCoreRegisterInfo::requiresRegisterScavenging(const MachineFunction &MF) const {
  return true;
}

bool
XCoreRegisterInfo::trackLivenessAfterRegAlloc(const MachineFunction &MF) const {
  return true;
}

bool
XCoreRegisterInfo::useFPForScavengingIndex(const MachineFunction &MF) const {
  return false;
}

void
XCoreRegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator II,
                                       int SPAdj, unsigned FIOperandNum,
                                       RegScavenger *RS) const {
  assert(SPAdj == 0 && "Unexpected");
  MachineInstr &MI = *II;
  MachineOperand &FrameOp = MI.getOperand(FIOperandNum);
  int FrameIndex = FrameOp.getIndex();

  MachineFunction &MF = *MI.getParent()->getParent();
  const XCoreInstrInfo &TII =
      *static_cast<const XCoreInstrInfo *>(MF.getSubtarget().getInstrInfo());

  const XCoreFrameLowering *TFI = getFrameLowering(MF);
  int Offset = MF.getFrameInfo().getObjectOffset(FrameIndex);
  int StackSize = MF.getFrameInfo().getStackSize();

  #ifndef NDEBUG
  LLVM_DEBUG(errs() << "\nFunction         : " << MF.getName() << "\n");
  LLVM_DEBUG(errs() << "<--------->\n");
  LLVM_DEBUG(MI.print(errs()));
  LLVM_DEBUG(errs() << "FrameIndex         : " << FrameIndex << "\n");
  LLVM_DEBUG(errs() << "FrameOffset        : " << Offset << "\n");
  LLVM_DEBUG(errs() << "StackSize          : " << StackSize << "\n");
#endif

  Offset += StackSize;

  unsigned FrameReg = getFrameRegister(MF);

  // Special handling of DBG_VALUE instructions.
  if (MI.isDebugValue()) {
    MI.getOperand(FIOperandNum).ChangeToRegister(FrameReg, false /*isDef*/);
    MI.getOperand(FIOperandNum + 1).ChangeToImmediate(Offset);
    return;
  }

  // fold constant into offset.
  Offset += MI.getOperand(FIOperandNum + 1).getImm();
  MI.getOperand(FIOperandNum + 1).ChangeToImmediate(0);

  assert(Offset%4 == 0 && "Misaligned stack offset");
  LLVM_DEBUG(errs() << "Offset             : " << Offset << "\n"
                    << "<--------->\n");
  Offset/=4;

  unsigned Reg = MI.getOperand(0).getReg();
  assert(XCore::GRRegsRegClass.contains(Reg) && "Unexpected register operand");

  if (TFI->hasFP(MF)) {
    if (isImmUs(Offset))
      InsertFPImmInst(II, TII, Reg, FrameReg, Offset);
    else
      InsertFPConstInst(II, TII, Reg, FrameReg, Offset, RS);
  } else {
    if (isImmU16(Offset))
      InsertSPImmInst(II, TII, Reg, Offset);
    else
      InsertSPConstInst(II, TII, Reg, Offset, RS);
  }
  // Erase old instruction.
  MachineBasicBlock &MBB = *MI.getParent();
  MBB.erase(II);
}


unsigned XCoreRegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  const XCoreFrameLowering *TFI = getFrameLowering(MF);

  return TFI->hasFP(MF) ? XCore::R10 : XCore::SP;
}
