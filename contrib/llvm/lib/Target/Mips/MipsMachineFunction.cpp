//===-- MipsMachineFunctionInfo.cpp - Private data used for Mips ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "MipsMachineFunction.h"
#include "MCTargetDesc/MipsABIInfo.h"
#include "MipsSubtarget.h"
#include "MipsTargetMachine.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/PseudoSourceValue.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

static cl::opt<bool>
FixGlobalBaseReg("mips-fix-global-base-reg", cl::Hidden, cl::init(true),
                 cl::desc("Always use $gp as the global base register."));

MipsFunctionInfo::~MipsFunctionInfo() = default;

bool MipsFunctionInfo::globalBaseRegSet() const {
  return GlobalBaseReg;
}

static const TargetRegisterClass &getGlobalBaseRegClass(MachineFunction &MF) {
  auto &STI = static_cast<const MipsSubtarget &>(MF.getSubtarget());
  auto &TM = static_cast<const MipsTargetMachine &>(MF.getTarget());

  if (STI.inMips16Mode())
    return Mips::CPU16RegsRegClass;

  if (STI.inMicroMipsMode())
    return Mips::GPRMM16RegClass;

  if (TM.getABI().IsN64())
    return Mips::GPR64RegClass;

  return Mips::GPR32RegClass;
}

unsigned MipsFunctionInfo::getGlobalBaseReg() {
  if (!GlobalBaseReg)
    GlobalBaseReg =
        MF.getRegInfo().createVirtualRegister(&getGlobalBaseRegClass(MF));
  return GlobalBaseReg;
}

void MipsFunctionInfo::createEhDataRegsFI() {
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  for (int I = 0; I < 4; ++I) {
    const TargetRegisterClass &RC =
        static_cast<const MipsTargetMachine &>(MF.getTarget()).getABI().IsN64()
            ? Mips::GPR64RegClass
            : Mips::GPR32RegClass;

    EhDataRegFI[I] = MF.getFrameInfo().CreateStackObject(TRI.getSpillSize(RC),
        TRI.getSpillAlignment(RC), false);
  }
}

void MipsFunctionInfo::createISRRegFI() {
  // ISRs require spill slots for Status & ErrorPC Coprocessor 0 registers.
  // The current implementation only supports Mips32r2+ not Mips64rX. Status
  // is always 32 bits, ErrorPC is 32 or 64 bits dependent on architecture,
  // however Mips32r2+ is the supported architecture.
  const TargetRegisterClass &RC = Mips::GPR32RegClass;
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (int I = 0; I < 2; ++I)
    ISRDataRegFI[I] = MF.getFrameInfo().CreateStackObject(
        TRI.getSpillSize(RC), TRI.getSpillAlignment(RC), false);
}

bool MipsFunctionInfo::isEhDataRegFI(int FI) const {
  return CallsEhReturn && (FI == EhDataRegFI[0] || FI == EhDataRegFI[1]
                        || FI == EhDataRegFI[2] || FI == EhDataRegFI[3]);
}

bool MipsFunctionInfo::isISRRegFI(int FI) const {
  return IsISR && (FI == ISRDataRegFI[0] || FI == ISRDataRegFI[1]);
}
MachinePointerInfo MipsFunctionInfo::callPtrInfo(const char *ES) {
  return MachinePointerInfo(MF.getPSVManager().getExternalSymbolCallEntry(ES));
}

MachinePointerInfo MipsFunctionInfo::callPtrInfo(const GlobalValue *GV) {
  return MachinePointerInfo(MF.getPSVManager().getGlobalValueCallEntry(GV));
}

int MipsFunctionInfo::getMoveF64ViaSpillFI(const TargetRegisterClass *RC) {
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  if (MoveF64ViaSpillFI == -1) {
    MoveF64ViaSpillFI = MF.getFrameInfo().CreateStackObject(
        TRI.getSpillSize(*RC), TRI.getSpillAlignment(*RC), false);
  }
  return MoveF64ViaSpillFI;
}

void MipsFunctionInfo::anchor() {}
