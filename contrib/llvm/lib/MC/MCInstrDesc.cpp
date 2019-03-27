//===------ llvm/MC/MCInstrDesc.cpp- Instruction Descriptors --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines methods on the MCOperandInfo and MCInstrDesc classes, which
// are used to describe target instructions and their operands.
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"

using namespace llvm;

bool MCInstrDesc::getDeprecatedInfo(MCInst &MI, const MCSubtargetInfo &STI,
                                    std::string &Info) const {
  if (ComplexDeprecationInfo)
    return ComplexDeprecationInfo(MI, STI, Info);
  if (DeprecatedFeature != -1 && STI.getFeatureBits()[DeprecatedFeature]) {
    // FIXME: it would be nice to include the subtarget feature here.
    Info = "deprecated";
    return true;
  }
  return false;
}
bool MCInstrDesc::mayAffectControlFlow(const MCInst &MI,
                                       const MCRegisterInfo &RI) const {
  if (isBranch() || isCall() || isReturn() || isIndirectBranch())
    return true;
  unsigned PC = RI.getProgramCounter();
  if (PC == 0)
    return false;
  if (hasDefOfPhysReg(MI, PC, RI))
    return true;
  return false;
}

bool MCInstrDesc::hasImplicitDefOfPhysReg(unsigned Reg,
                                          const MCRegisterInfo *MRI) const {
  if (const MCPhysReg *ImpDefs = ImplicitDefs)
    for (; *ImpDefs; ++ImpDefs)
      if (*ImpDefs == Reg || (MRI && MRI->isSubRegister(Reg, *ImpDefs)))
        return true;
  return false;
}

bool MCInstrDesc::hasDefOfPhysReg(const MCInst &MI, unsigned Reg,
                                  const MCRegisterInfo &RI) const {
  for (int i = 0, e = NumDefs; i != e; ++i)
    if (MI.getOperand(i).isReg() &&
        RI.isSubRegisterEq(Reg, MI.getOperand(i).getReg()))
      return true;
  if (variadicOpsAreDefs())
    for (int i = NumOperands - 1, e = MI.getNumOperands(); i != e; ++i)
      if (MI.getOperand(i).isReg() &&
          RI.isSubRegisterEq(Reg, MI.getOperand(i).getReg()))
        return true;
  return hasImplicitDefOfPhysReg(Reg, &RI);
}
