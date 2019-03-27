//===-- R600RegisterInfo.cpp - R600 Register Information ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// R600 implementation of the TargetRegisterInfo class.
//
//===----------------------------------------------------------------------===//

#include "R600RegisterInfo.h"
#include "AMDGPUTargetMachine.h"
#include "R600Defines.h"
#include "R600InstrInfo.h"
#include "R600MachineFunctionInfo.h"
#include "MCTargetDesc/AMDGPUMCTargetDesc.h"

using namespace llvm;

R600RegisterInfo::R600RegisterInfo() : R600GenRegisterInfo(0) {
  RCW.RegWeight = 0;
  RCW.WeightLimit = 0;
}

#define GET_REGINFO_TARGET_DESC
#include "R600GenRegisterInfo.inc"

BitVector R600RegisterInfo::getReservedRegs(const MachineFunction &MF) const {
  BitVector Reserved(getNumRegs());

  const R600Subtarget &ST = MF.getSubtarget<R600Subtarget>();
  const R600InstrInfo *TII = ST.getInstrInfo();

  reserveRegisterTuples(Reserved, R600::ZERO);
  reserveRegisterTuples(Reserved, R600::HALF);
  reserveRegisterTuples(Reserved, R600::ONE);
  reserveRegisterTuples(Reserved, R600::ONE_INT);
  reserveRegisterTuples(Reserved, R600::NEG_HALF);
  reserveRegisterTuples(Reserved, R600::NEG_ONE);
  reserveRegisterTuples(Reserved, R600::PV_X);
  reserveRegisterTuples(Reserved, R600::ALU_LITERAL_X);
  reserveRegisterTuples(Reserved, R600::ALU_CONST);
  reserveRegisterTuples(Reserved, R600::PREDICATE_BIT);
  reserveRegisterTuples(Reserved, R600::PRED_SEL_OFF);
  reserveRegisterTuples(Reserved, R600::PRED_SEL_ZERO);
  reserveRegisterTuples(Reserved, R600::PRED_SEL_ONE);
  reserveRegisterTuples(Reserved, R600::INDIRECT_BASE_ADDR);

  for (TargetRegisterClass::iterator I = R600::R600_AddrRegClass.begin(),
                        E = R600::R600_AddrRegClass.end(); I != E; ++I) {
    reserveRegisterTuples(Reserved, *I);
  }

  TII->reserveIndirectRegisters(Reserved, MF, *this);

  return Reserved;
}

// Dummy to not crash RegisterClassInfo.
static const MCPhysReg CalleeSavedReg = R600::NoRegister;

const MCPhysReg *R600RegisterInfo::getCalleeSavedRegs(
  const MachineFunction *) const {
  return &CalleeSavedReg;
}

unsigned R600RegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  return R600::NoRegister;
}

unsigned R600RegisterInfo::getHWRegChan(unsigned reg) const {
  return this->getEncodingValue(reg) >> HW_CHAN_SHIFT;
}

unsigned R600RegisterInfo::getHWRegIndex(unsigned Reg) const {
  return GET_REG_INDEX(getEncodingValue(Reg));
}

const TargetRegisterClass * R600RegisterInfo::getCFGStructurizerRegClass(
                                                                   MVT VT) const {
  switch(VT.SimpleTy) {
  default:
  case MVT::i32: return &R600::R600_TReg32RegClass;
  }
}

const RegClassWeight &R600RegisterInfo::getRegClassWeight(
  const TargetRegisterClass *RC) const {
  return RCW;
}

bool R600RegisterInfo::isPhysRegLiveAcrossClauses(unsigned Reg) const {
  assert(!TargetRegisterInfo::isVirtualRegister(Reg));

  switch (Reg) {
  case R600::OQAP:
  case R600::OQBP:
  case R600::AR_X:
    return false;
  default:
    return true;
  }
}

void R600RegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator MI,
                                           int SPAdj,
                                           unsigned FIOperandNum,
                                           RegScavenger *RS) const {
  llvm_unreachable("Subroutines not supported yet");
}

void R600RegisterInfo::reserveRegisterTuples(BitVector &Reserved, unsigned Reg) const {
  MCRegAliasIterator R(Reg, this, true);

  for (; R.isValid(); ++R)
    Reserved.set(*R);
}
