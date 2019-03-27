//===-- WebAssemblyRegisterInfo.cpp - WebAssembly Register Information ----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the WebAssembly implementation of the
/// TargetRegisterInfo class.
///
//===----------------------------------------------------------------------===//

#include "WebAssemblyRegisterInfo.h"
#include "MCTargetDesc/WebAssemblyMCTargetDesc.h"
#include "WebAssemblyFrameLowering.h"
#include "WebAssemblyInstrInfo.h"
#include "WebAssemblyMachineFunctionInfo.h"
#include "WebAssemblySubtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetOptions.h"
using namespace llvm;

#define DEBUG_TYPE "wasm-reg-info"

#define GET_REGINFO_TARGET_DESC
#include "WebAssemblyGenRegisterInfo.inc"

WebAssemblyRegisterInfo::WebAssemblyRegisterInfo(const Triple &TT)
    : WebAssemblyGenRegisterInfo(0), TT(TT) {}

const MCPhysReg *
WebAssemblyRegisterInfo::getCalleeSavedRegs(const MachineFunction *) const {
  static const MCPhysReg CalleeSavedRegs[] = {0};
  return CalleeSavedRegs;
}

BitVector
WebAssemblyRegisterInfo::getReservedRegs(const MachineFunction & /*MF*/) const {
  BitVector Reserved(getNumRegs());
  for (auto Reg : {WebAssembly::SP32, WebAssembly::SP64, WebAssembly::FP32,
                   WebAssembly::FP64})
    Reserved.set(Reg);
  return Reserved;
}

void WebAssemblyRegisterInfo::eliminateFrameIndex(
    MachineBasicBlock::iterator II, int SPAdj, unsigned FIOperandNum,
    RegScavenger * /*RS*/) const {
  assert(SPAdj == 0);
  MachineInstr &MI = *II;

  MachineBasicBlock &MBB = *MI.getParent();
  MachineFunction &MF = *MBB.getParent();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  int FrameIndex = MI.getOperand(FIOperandNum).getIndex();
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  int64_t FrameOffset = MFI.getStackSize() + MFI.getObjectOffset(FrameIndex);

  assert(MFI.getObjectSize(FrameIndex) != 0 &&
         "We assume that variable-sized objects have already been lowered, "
         "and don't use FrameIndex operands.");
  unsigned FrameRegister = getFrameRegister(MF);

  // If this is the address operand of a load or store, make it relative to SP
  // and fold the frame offset directly in.
  if ((MI.mayLoad() && FIOperandNum == WebAssembly::LoadAddressOperandNo) ||
      (MI.mayStore() && FIOperandNum == WebAssembly::StoreAddressOperandNo)) {
    assert(FrameOffset >= 0 && MI.getOperand(FIOperandNum - 1).getImm() >= 0);
    int64_t Offset = MI.getOperand(FIOperandNum - 1).getImm() + FrameOffset;

    if (static_cast<uint64_t>(Offset) <= std::numeric_limits<uint32_t>::max()) {
      MI.getOperand(FIOperandNum - 1).setImm(Offset);
      MI.getOperand(FIOperandNum)
          .ChangeToRegister(FrameRegister, /*IsDef=*/false);
      return;
    }
  }

  // If this is an address being added to a constant, fold the frame offset
  // into the constant.
  if (MI.getOpcode() == WebAssembly::ADD_I32) {
    MachineOperand &OtherMO = MI.getOperand(3 - FIOperandNum);
    if (OtherMO.isReg()) {
      unsigned OtherMOReg = OtherMO.getReg();
      if (TargetRegisterInfo::isVirtualRegister(OtherMOReg)) {
        MachineInstr *Def = MF.getRegInfo().getUniqueVRegDef(OtherMOReg);
        // TODO: For now we just opportunistically do this in the case where
        // the CONST_I32 happens to have exactly one def and one use. We
        // should generalize this to optimize in more cases.
        if (Def && Def->getOpcode() == WebAssembly::CONST_I32 &&
            MRI.hasOneNonDBGUse(Def->getOperand(0).getReg())) {
          MachineOperand &ImmMO = Def->getOperand(1);
          ImmMO.setImm(ImmMO.getImm() + uint32_t(FrameOffset));
          MI.getOperand(FIOperandNum)
              .ChangeToRegister(FrameRegister, /*IsDef=*/false);
          return;
        }
      }
    }
  }

  // Otherwise create an i32.add SP, offset and make it the operand.
  const auto *TII = MF.getSubtarget<WebAssemblySubtarget>().getInstrInfo();

  unsigned FIRegOperand = FrameRegister;
  if (FrameOffset) {
    // Create i32.add SP, offset and make it the operand.
    const TargetRegisterClass *PtrRC =
        MRI.getTargetRegisterInfo()->getPointerRegClass(MF);
    unsigned OffsetOp = MRI.createVirtualRegister(PtrRC);
    BuildMI(MBB, *II, II->getDebugLoc(), TII->get(WebAssembly::CONST_I32),
            OffsetOp)
        .addImm(FrameOffset);
    FIRegOperand = MRI.createVirtualRegister(PtrRC);
    BuildMI(MBB, *II, II->getDebugLoc(), TII->get(WebAssembly::ADD_I32),
            FIRegOperand)
        .addReg(FrameRegister)
        .addReg(OffsetOp);
  }
  MI.getOperand(FIOperandNum).ChangeToRegister(FIRegOperand, /*IsDef=*/false);
}

unsigned
WebAssemblyRegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  static const unsigned Regs[2][2] = {
      /*            !isArch64Bit       isArch64Bit      */
      /* !hasFP */ {WebAssembly::SP32, WebAssembly::SP64},
      /*  hasFP */ {WebAssembly::FP32, WebAssembly::FP64}};
  const WebAssemblyFrameLowering *TFI = getFrameLowering(MF);
  return Regs[TFI->hasFP(MF)][TT.isArch64Bit()];
}

const TargetRegisterClass *
WebAssemblyRegisterInfo::getPointerRegClass(const MachineFunction &MF,
                                            unsigned Kind) const {
  assert(Kind == 0 && "Only one kind of pointer on WebAssembly");
  if (MF.getSubtarget<WebAssemblySubtarget>().hasAddr64())
    return &WebAssembly::I64RegClass;
  return &WebAssembly::I32RegClass;
}
