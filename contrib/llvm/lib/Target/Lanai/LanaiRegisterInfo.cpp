//===-- LanaiRegisterInfo.cpp - Lanai Register Information ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the Lanai implementation of the TargetRegisterInfo class.
//
//===----------------------------------------------------------------------===//

#include "LanaiRegisterInfo.h"
#include "Lanai.h"
#include "LanaiSubtarget.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/ErrorHandling.h"

#define GET_REGINFO_TARGET_DESC
#include "LanaiGenRegisterInfo.inc"

using namespace llvm;

LanaiRegisterInfo::LanaiRegisterInfo() : LanaiGenRegisterInfo(Lanai::RCA) {}

const uint16_t *
LanaiRegisterInfo::getCalleeSavedRegs(const MachineFunction * /*MF*/) const {
  return CSR_SaveList;
}

BitVector LanaiRegisterInfo::getReservedRegs(const MachineFunction &MF) const {
  BitVector Reserved(getNumRegs());

  Reserved.set(Lanai::R0);
  Reserved.set(Lanai::R1);
  Reserved.set(Lanai::PC);
  Reserved.set(Lanai::R2);
  Reserved.set(Lanai::SP);
  Reserved.set(Lanai::R4);
  Reserved.set(Lanai::FP);
  Reserved.set(Lanai::R5);
  Reserved.set(Lanai::RR1);
  Reserved.set(Lanai::R10);
  Reserved.set(Lanai::RR2);
  Reserved.set(Lanai::R11);
  Reserved.set(Lanai::RCA);
  Reserved.set(Lanai::R15);
  if (hasBasePointer(MF))
    Reserved.set(getBaseRegister());
  return Reserved;
}

bool LanaiRegisterInfo::requiresRegisterScavenging(
    const MachineFunction & /*MF*/) const {
  return true;
}

bool LanaiRegisterInfo::trackLivenessAfterRegAlloc(
    const MachineFunction & /*MF*/) const {
  return true;
}

static bool isALUArithLoOpcode(unsigned Opcode) {
  switch (Opcode) {
  case Lanai::ADD_I_LO:
  case Lanai::SUB_I_LO:
  case Lanai::ADD_F_I_LO:
  case Lanai::SUB_F_I_LO:
  case Lanai::ADDC_I_LO:
  case Lanai::SUBB_I_LO:
  case Lanai::ADDC_F_I_LO:
  case Lanai::SUBB_F_I_LO:
    return true;
  default:
    return false;
  }
}

static unsigned getOppositeALULoOpcode(unsigned Opcode) {
  switch (Opcode) {
  case Lanai::ADD_I_LO:
    return Lanai::SUB_I_LO;
  case Lanai::SUB_I_LO:
    return Lanai::ADD_I_LO;
  case Lanai::ADD_F_I_LO:
    return Lanai::SUB_F_I_LO;
  case Lanai::SUB_F_I_LO:
    return Lanai::ADD_F_I_LO;
  case Lanai::ADDC_I_LO:
    return Lanai::SUBB_I_LO;
  case Lanai::SUBB_I_LO:
    return Lanai::ADDC_I_LO;
  case Lanai::ADDC_F_I_LO:
    return Lanai::SUBB_F_I_LO;
  case Lanai::SUBB_F_I_LO:
    return Lanai::ADDC_F_I_LO;
  default:
    llvm_unreachable("Invalid ALU lo opcode");
  }
}

static unsigned getRRMOpcodeVariant(unsigned Opcode) {
  switch (Opcode) {
  case Lanai::LDBs_RI:
    return Lanai::LDBs_RR;
  case Lanai::LDBz_RI:
    return Lanai::LDBz_RR;
  case Lanai::LDHs_RI:
    return Lanai::LDHs_RR;
  case Lanai::LDHz_RI:
    return Lanai::LDHz_RR;
  case Lanai::LDW_RI:
    return Lanai::LDW_RR;
  case Lanai::STB_RI:
    return Lanai::STB_RR;
  case Lanai::STH_RI:
    return Lanai::STH_RR;
  case Lanai::SW_RI:
    return Lanai::SW_RR;
  default:
    llvm_unreachable("Opcode has no RRM variant");
  }
}

void LanaiRegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator II,
                                            int SPAdj, unsigned FIOperandNum,
                                            RegScavenger *RS) const {
  assert(SPAdj == 0 && "Unexpected");

  MachineInstr &MI = *II;
  MachineFunction &MF = *MI.getParent()->getParent();
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
  const TargetFrameLowering *TFI = MF.getSubtarget().getFrameLowering();
  bool HasFP = TFI->hasFP(MF);
  DebugLoc DL = MI.getDebugLoc();

  int FrameIndex = MI.getOperand(FIOperandNum).getIndex();

  int Offset = MF.getFrameInfo().getObjectOffset(FrameIndex) +
               MI.getOperand(FIOperandNum + 1).getImm();

  // Addressable stack objects are addressed using neg. offsets from fp
  // or pos. offsets from sp/basepointer
  if (!HasFP || (needsStackRealignment(MF) && FrameIndex >= 0))
    Offset += MF.getFrameInfo().getStackSize();

  unsigned FrameReg = getFrameRegister(MF);
  if (FrameIndex >= 0) {
    if (hasBasePointer(MF))
      FrameReg = getBaseRegister();
    else if (needsStackRealignment(MF))
      FrameReg = Lanai::SP;
  }

  // Replace frame index with a frame pointer reference.
  // If the offset is small enough to fit in the immediate field, directly
  // encode it.
  // Otherwise scavenge a register and encode it into a MOVHI, OR_I_LO sequence.
  if ((isSPLSOpcode(MI.getOpcode()) && !isInt<10>(Offset)) ||
      !isInt<16>(Offset)) {
    assert(RS && "Register scavenging must be on");
    unsigned Reg = RS->FindUnusedReg(&Lanai::GPRRegClass);
    if (!Reg)
      Reg = RS->scavengeRegister(&Lanai::GPRRegClass, II, SPAdj);
    assert(Reg && "Register scavenger failed");

    bool HasNegOffset = false;
    // ALU ops have unsigned immediate values. If the Offset is negative, we
    // negate it here and reverse the opcode later.
    if (Offset < 0) {
      HasNegOffset = true;
      Offset = -Offset;
    }

    if (!isInt<16>(Offset)) {
      // Reg = hi(offset) | lo(offset)
      BuildMI(*MI.getParent(), II, DL, TII->get(Lanai::MOVHI), Reg)
          .addImm(static_cast<uint32_t>(Offset) >> 16);
      BuildMI(*MI.getParent(), II, DL, TII->get(Lanai::OR_I_LO), Reg)
          .addReg(Reg)
          .addImm(Offset & 0xffffU);
    } else {
      // Reg = mov(offset)
      BuildMI(*MI.getParent(), II, DL, TII->get(Lanai::ADD_I_LO), Reg)
          .addImm(0)
          .addImm(Offset);
    }
    // Reg = FrameReg OP Reg
    if (MI.getOpcode() == Lanai::ADD_I_LO) {
      BuildMI(*MI.getParent(), II, DL,
              HasNegOffset ? TII->get(Lanai::SUB_R) : TII->get(Lanai::ADD_R),
              MI.getOperand(0).getReg())
          .addReg(FrameReg)
          .addReg(Reg)
          .addImm(LPCC::ICC_T);
      MI.eraseFromParent();
      return;
    }
    if (isSPLSOpcode(MI.getOpcode()) || isRMOpcode(MI.getOpcode())) {
      MI.setDesc(TII->get(getRRMOpcodeVariant(MI.getOpcode())));
      if (HasNegOffset) {
        // Change the ALU op (operand 3) from LPAC::ADD (the default) to
        // LPAC::SUB with the already negated offset.
        assert((MI.getOperand(3).getImm() == LPAC::ADD) &&
               "Unexpected ALU op in RRM instruction");
        MI.getOperand(3).setImm(LPAC::SUB);
      }
    } else
      llvm_unreachable("Unexpected opcode in frame index operation");

    MI.getOperand(FIOperandNum).ChangeToRegister(FrameReg, /*isDef=*/false);
    MI.getOperand(FIOperandNum + 1)
        .ChangeToRegister(Reg, /*isDef=*/false, /*isImp=*/false,
                          /*isKill=*/true);
    return;
  }

  // ALU arithmetic ops take unsigned immediates. If the offset is negative,
  // we replace the instruction with one that inverts the opcode and negates
  // the immediate.
  if ((Offset < 0) && isALUArithLoOpcode(MI.getOpcode())) {
    unsigned NewOpcode = getOppositeALULoOpcode(MI.getOpcode());
    // We know this is an ALU op, so we know the operands are as follows:
    // 0: destination register
    // 1: source register (frame register)
    // 2: immediate
    BuildMI(*MI.getParent(), II, DL, TII->get(NewOpcode),
            MI.getOperand(0).getReg())
        .addReg(FrameReg)
        .addImm(-Offset);
    MI.eraseFromParent();
  } else {
    MI.getOperand(FIOperandNum).ChangeToRegister(FrameReg, /*isDef=*/false);
    MI.getOperand(FIOperandNum + 1).ChangeToImmediate(Offset);
  }
}

bool LanaiRegisterInfo::hasBasePointer(const MachineFunction &MF) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  // When we need stack realignment and there are dynamic allocas, we can't
  // reference off of the stack pointer, so we reserve a base pointer.
  if (needsStackRealignment(MF) && MFI.hasVarSizedObjects())
    return true;

  return false;
}

unsigned LanaiRegisterInfo::getRARegister() const { return Lanai::RCA; }

unsigned
LanaiRegisterInfo::getFrameRegister(const MachineFunction & /*MF*/) const {
  return Lanai::FP;
}

unsigned LanaiRegisterInfo::getBaseRegister() const { return Lanai::R14; }

const uint32_t *
LanaiRegisterInfo::getCallPreservedMask(const MachineFunction & /*MF*/,
                                        CallingConv::ID /*CC*/) const {
  return CSR_RegMask;
}
