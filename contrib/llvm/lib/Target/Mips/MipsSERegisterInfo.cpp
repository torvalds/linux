//===-- MipsSERegisterInfo.cpp - MIPS32/64 Register Information -== -------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the MIPS32/64 implementation of the TargetRegisterInfo
// class.
//
//===----------------------------------------------------------------------===//

#include "MipsSERegisterInfo.h"
#include "Mips.h"
#include "MipsMachineFunction.h"
#include "MipsSEInstrInfo.h"
#include "MipsSubtarget.h"
#include "MipsTargetMachine.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"

using namespace llvm;

#define DEBUG_TYPE "mips-reg-info"

MipsSERegisterInfo::MipsSERegisterInfo() : MipsRegisterInfo() {}

bool MipsSERegisterInfo::
requiresRegisterScavenging(const MachineFunction &MF) const {
  return true;
}

bool MipsSERegisterInfo::
requiresFrameIndexScavenging(const MachineFunction &MF) const {
  return true;
}

const TargetRegisterClass *
MipsSERegisterInfo::intRegClass(unsigned Size) const {
  if (Size == 4)
    return &Mips::GPR32RegClass;

  assert(Size == 8);
  return &Mips::GPR64RegClass;
}

/// Get the size of the offset supported by the given load/store/inline asm.
/// The result includes the effects of any scale factors applied to the
/// instruction immediate.
static inline unsigned getLoadStoreOffsetSizeInBits(const unsigned Opcode,
                                                    MachineOperand MO) {
  switch (Opcode) {
  case Mips::LD_B:
  case Mips::ST_B:
    return 10;
  case Mips::LD_H:
  case Mips::ST_H:
    return 10 + 1 /* scale factor */;
  case Mips::LD_W:
  case Mips::ST_W:
    return 10 + 2 /* scale factor */;
  case Mips::LD_D:
  case Mips::ST_D:
    return 10 + 3 /* scale factor */;
  case Mips::LL:
  case Mips::LL64:
  case Mips::LLD:
  case Mips::LLE:
  case Mips::SC:
  case Mips::SC64:
  case Mips::SCD:
  case Mips::SCE:
    return 16;
  case Mips::LLE_MM:
  case Mips::LL_MM:
  case Mips::SCE_MM:
  case Mips::SC_MM:
    return 12;
  case Mips::LL64_R6:
  case Mips::LL_R6:
  case Mips::LLD_R6:
  case Mips::SC64_R6:
  case Mips::SCD_R6:
  case Mips::SC_R6:
  case Mips::LL_MMR6:
  case Mips::SC_MMR6:
    return 9;
  case Mips::INLINEASM: {
    unsigned ConstraintID = InlineAsm::getMemoryConstraintID(MO.getImm());
    switch (ConstraintID) {
    case InlineAsm::Constraint_ZC: {
      const MipsSubtarget &Subtarget = MO.getParent()
                                           ->getParent()
                                           ->getParent()
                                           ->getSubtarget<MipsSubtarget>();
      if (Subtarget.inMicroMipsMode())
        return 12;

      if (Subtarget.hasMips32r6())
        return 9;

      return 16;
    }
    default:
      return 16;
    }
  }
  default:
    return 16;
  }
}

/// Get the scale factor applied to the immediate in the given load/store.
static inline unsigned getLoadStoreOffsetAlign(const unsigned Opcode) {
  switch (Opcode) {
  case Mips::LD_H:
  case Mips::ST_H:
    return 2;
  case Mips::LD_W:
  case Mips::ST_W:
    return 4;
  case Mips::LD_D:
  case Mips::ST_D:
    return 8;
  default:
    return 1;
  }
}

void MipsSERegisterInfo::eliminateFI(MachineBasicBlock::iterator II,
                                     unsigned OpNo, int FrameIndex,
                                     uint64_t StackSize,
                                     int64_t SPOffset) const {
  MachineInstr &MI = *II;
  MachineFunction &MF = *MI.getParent()->getParent();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  MipsFunctionInfo *MipsFI = MF.getInfo<MipsFunctionInfo>();

  MipsABIInfo ABI =
      static_cast<const MipsTargetMachine &>(MF.getTarget()).getABI();
  const MipsRegisterInfo *RegInfo =
    static_cast<const MipsRegisterInfo *>(MF.getSubtarget().getRegisterInfo());

  const std::vector<CalleeSavedInfo> &CSI = MFI.getCalleeSavedInfo();
  int MinCSFI = 0;
  int MaxCSFI = -1;

  if (CSI.size()) {
    MinCSFI = CSI[0].getFrameIdx();
    MaxCSFI = CSI[CSI.size() - 1].getFrameIdx();
  }

  bool EhDataRegFI = MipsFI->isEhDataRegFI(FrameIndex);
  bool IsISRRegFI = MipsFI->isISRRegFI(FrameIndex);
  // The following stack frame objects are always referenced relative to $sp:
  //  1. Outgoing arguments.
  //  2. Pointer to dynamically allocated stack space.
  //  3. Locations for callee-saved registers.
  //  4. Locations for eh data registers.
  //  5. Locations for ISR saved Coprocessor 0 registers 12 & 14.
  // Everything else is referenced relative to whatever register
  // getFrameRegister() returns.
  unsigned FrameReg;

  if ((FrameIndex >= MinCSFI && FrameIndex <= MaxCSFI) || EhDataRegFI ||
      IsISRRegFI)
    FrameReg = ABI.GetStackPtr();
  else if (RegInfo->needsStackRealignment(MF)) {
    if (MFI.hasVarSizedObjects() && !MFI.isFixedObjectIndex(FrameIndex))
      FrameReg = ABI.GetBasePtr();
    else if (MFI.isFixedObjectIndex(FrameIndex))
      FrameReg = getFrameRegister(MF);
    else
      FrameReg = ABI.GetStackPtr();
  } else
    FrameReg = getFrameRegister(MF);

  // Calculate final offset.
  // - There is no need to change the offset if the frame object is one of the
  //   following: an outgoing argument, pointer to a dynamically allocated
  //   stack space or a $gp restore location,
  // - If the frame object is any of the following, its offset must be adjusted
  //   by adding the size of the stack:
  //   incoming argument, callee-saved register location or local variable.
  bool IsKill = false;
  int64_t Offset;

  Offset = SPOffset + (int64_t)StackSize;
  Offset += MI.getOperand(OpNo + 1).getImm();

  LLVM_DEBUG(errs() << "Offset     : " << Offset << "\n"
                    << "<--------->\n");

  if (!MI.isDebugValue()) {
    // Make sure Offset fits within the field available.
    // For MSA instructions, this is a 10-bit signed immediate (scaled by
    // element size), otherwise it is a 16-bit signed immediate.
    unsigned OffsetBitSize =
        getLoadStoreOffsetSizeInBits(MI.getOpcode(), MI.getOperand(OpNo - 1));
    unsigned OffsetAlign = getLoadStoreOffsetAlign(MI.getOpcode());

    if (OffsetBitSize < 16 && isInt<16>(Offset) &&
        (!isIntN(OffsetBitSize, Offset) ||
         OffsetToAlignment(Offset, OffsetAlign) != 0)) {
      // If we have an offset that needs to fit into a signed n-bit immediate
      // (where n < 16) and doesn't, but does fit into 16-bits then use an ADDiu
      MachineBasicBlock &MBB = *MI.getParent();
      DebugLoc DL = II->getDebugLoc();
      const TargetRegisterClass *PtrRC =
          ABI.ArePtrs64bit() ? &Mips::GPR64RegClass : &Mips::GPR32RegClass;
      MachineRegisterInfo &RegInfo = MBB.getParent()->getRegInfo();
      unsigned Reg = RegInfo.createVirtualRegister(PtrRC);
      const MipsSEInstrInfo &TII =
          *static_cast<const MipsSEInstrInfo *>(
              MBB.getParent()->getSubtarget().getInstrInfo());
      BuildMI(MBB, II, DL, TII.get(ABI.GetPtrAddiuOp()), Reg)
          .addReg(FrameReg)
          .addImm(Offset);

      FrameReg = Reg;
      Offset = 0;
      IsKill = true;
    } else if (!isInt<16>(Offset)) {
      // Otherwise split the offset into 16-bit pieces and add it in multiple
      // instructions.
      MachineBasicBlock &MBB = *MI.getParent();
      DebugLoc DL = II->getDebugLoc();
      unsigned NewImm = 0;
      const MipsSEInstrInfo &TII =
          *static_cast<const MipsSEInstrInfo *>(
              MBB.getParent()->getSubtarget().getInstrInfo());
      unsigned Reg = TII.loadImmediate(Offset, MBB, II, DL,
                                       OffsetBitSize == 16 ? &NewImm : nullptr);
      BuildMI(MBB, II, DL, TII.get(ABI.GetPtrAdduOp()), Reg).addReg(FrameReg)
        .addReg(Reg, RegState::Kill);

      FrameReg = Reg;
      Offset = SignExtend64<16>(NewImm);
      IsKill = true;
    }
  }

  MI.getOperand(OpNo).ChangeToRegister(FrameReg, false, false, IsKill);
  MI.getOperand(OpNo + 1).ChangeToImmediate(Offset);
}
