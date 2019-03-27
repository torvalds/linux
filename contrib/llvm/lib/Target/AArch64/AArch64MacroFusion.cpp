//===- AArch64MacroFusion.cpp - AArch64 Macro Fusion ----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file This file contains the AArch64 implementation of the DAG scheduling
///  mutation to pair instructions back to back.
//
//===----------------------------------------------------------------------===//

#include "AArch64Subtarget.h"
#include "llvm/CodeGen/MacroFusion.h"
#include "llvm/CodeGen/TargetInstrInfo.h"

using namespace llvm;

namespace {

/// CMN, CMP, TST followed by Bcc
static bool isArithmeticBccPair(const MachineInstr *FirstMI,
                                const MachineInstr &SecondMI) {
  if (SecondMI.getOpcode() != AArch64::Bcc)
    return false;

  // Assume the 1st instr to be a wildcard if it is unspecified.
  if (FirstMI == nullptr)
    return true;

  switch (FirstMI->getOpcode()) {
  case AArch64::ADDSWri:
  case AArch64::ADDSWrr:
  case AArch64::ADDSXri:
  case AArch64::ADDSXrr:
  case AArch64::ANDSWri:
  case AArch64::ANDSWrr:
  case AArch64::ANDSXri:
  case AArch64::ANDSXrr:
  case AArch64::SUBSWri:
  case AArch64::SUBSWrr:
  case AArch64::SUBSXri:
  case AArch64::SUBSXrr:
  case AArch64::BICSWrr:
  case AArch64::BICSXrr:
    return true;
  case AArch64::ADDSWrs:
  case AArch64::ADDSXrs:
  case AArch64::ANDSWrs:
  case AArch64::ANDSXrs:
  case AArch64::SUBSWrs:
  case AArch64::SUBSXrs:
  case AArch64::BICSWrs:
  case AArch64::BICSXrs:
    // Shift value can be 0 making these behave like the "rr" variant...
    return !AArch64InstrInfo::hasShiftedReg(*FirstMI);
  }

  return false;
}

/// ALU operations followed by CBZ/CBNZ.
static bool isArithmeticCbzPair(const MachineInstr *FirstMI,
                                const MachineInstr &SecondMI) {
  if (SecondMI.getOpcode() != AArch64::CBZW &&
      SecondMI.getOpcode() != AArch64::CBZX &&
      SecondMI.getOpcode() != AArch64::CBNZW &&
      SecondMI.getOpcode() != AArch64::CBNZX)
    return false;

  // Assume the 1st instr to be a wildcard if it is unspecified.
  if (FirstMI == nullptr)
    return true;

  switch (FirstMI->getOpcode()) {
  case AArch64::ADDWri:
  case AArch64::ADDWrr:
  case AArch64::ADDXri:
  case AArch64::ADDXrr:
  case AArch64::ANDWri:
  case AArch64::ANDWrr:
  case AArch64::ANDXri:
  case AArch64::ANDXrr:
  case AArch64::EORWri:
  case AArch64::EORWrr:
  case AArch64::EORXri:
  case AArch64::EORXrr:
  case AArch64::ORRWri:
  case AArch64::ORRWrr:
  case AArch64::ORRXri:
  case AArch64::ORRXrr:
  case AArch64::SUBWri:
  case AArch64::SUBWrr:
  case AArch64::SUBXri:
  case AArch64::SUBXrr:
    return true;
  case AArch64::ADDWrs:
  case AArch64::ADDXrs:
  case AArch64::ANDWrs:
  case AArch64::ANDXrs:
  case AArch64::SUBWrs:
  case AArch64::SUBXrs:
  case AArch64::BICWrs:
  case AArch64::BICXrs:
    // Shift value can be 0 making these behave like the "rr" variant...
    return !AArch64InstrInfo::hasShiftedReg(*FirstMI);
  }

  return false;
}

/// AES crypto encoding or decoding.
static bool isAESPair(const MachineInstr *FirstMI,
                      const MachineInstr &SecondMI) {
  // Assume the 1st instr to be a wildcard if it is unspecified.
  switch (SecondMI.getOpcode()) {
  // AES encode.
  case AArch64::AESMCrr:
  case AArch64::AESMCrrTied:
    return FirstMI == nullptr || FirstMI->getOpcode() == AArch64::AESErr;
  // AES decode.
  case AArch64::AESIMCrr:
  case AArch64::AESIMCrrTied:
    return FirstMI == nullptr || FirstMI->getOpcode() == AArch64::AESDrr;
  }

  return false;
}

/// AESE/AESD/PMULL + EOR.
static bool isCryptoEORPair(const MachineInstr *FirstMI,
                            const MachineInstr &SecondMI) {
  if (SecondMI.getOpcode() != AArch64::EORv16i8)
    return false;

  // Assume the 1st instr to be a wildcard if it is unspecified.
  if (FirstMI == nullptr)
    return true;

  switch (FirstMI->getOpcode()) {
  case AArch64::AESErr:
  case AArch64::AESDrr:
  case AArch64::PMULLv16i8:
  case AArch64::PMULLv8i8:
  case AArch64::PMULLv1i64:
  case AArch64::PMULLv2i64:
    return true;
  }

  return false;
}

/// Literal generation.
static bool isLiteralsPair(const MachineInstr *FirstMI,
                           const MachineInstr &SecondMI) {
  // Assume the 1st instr to be a wildcard if it is unspecified.

  // PC relative address.
  if ((FirstMI == nullptr || FirstMI->getOpcode() == AArch64::ADRP) &&
      SecondMI.getOpcode() == AArch64::ADDXri)
    return true;

  // 32 bit immediate.
  if ((FirstMI == nullptr || FirstMI->getOpcode() == AArch64::MOVZWi) &&
      (SecondMI.getOpcode() == AArch64::MOVKWi &&
       SecondMI.getOperand(3).getImm() == 16))
    return true;

  // Lower half of 64 bit immediate.
  if((FirstMI == nullptr || FirstMI->getOpcode() == AArch64::MOVZXi) &&
     (SecondMI.getOpcode() == AArch64::MOVKXi &&
      SecondMI.getOperand(3).getImm() == 16))
    return true;

  // Upper half of 64 bit immediate.
  if ((FirstMI == nullptr ||
       (FirstMI->getOpcode() == AArch64::MOVKXi &&
        FirstMI->getOperand(3).getImm() == 32)) &&
      (SecondMI.getOpcode() == AArch64::MOVKXi &&
       SecondMI.getOperand(3).getImm() == 48))
    return true;

  return false;
}

/// Fuse address generation and loads or stores.
static bool isAddressLdStPair(const MachineInstr *FirstMI,
                              const MachineInstr &SecondMI) {
  switch (SecondMI.getOpcode()) {
  case AArch64::STRBBui:
  case AArch64::STRBui:
  case AArch64::STRDui:
  case AArch64::STRHHui:
  case AArch64::STRHui:
  case AArch64::STRQui:
  case AArch64::STRSui:
  case AArch64::STRWui:
  case AArch64::STRXui:
  case AArch64::LDRBBui:
  case AArch64::LDRBui:
  case AArch64::LDRDui:
  case AArch64::LDRHHui:
  case AArch64::LDRHui:
  case AArch64::LDRQui:
  case AArch64::LDRSui:
  case AArch64::LDRWui:
  case AArch64::LDRXui:
  case AArch64::LDRSBWui:
  case AArch64::LDRSBXui:
  case AArch64::LDRSHWui:
  case AArch64::LDRSHXui:
  case AArch64::LDRSWui:
    // Assume the 1st instr to be a wildcard if it is unspecified.
    if (FirstMI == nullptr)
      return true;

   switch (FirstMI->getOpcode()) {
    case AArch64::ADR:
      return SecondMI.getOperand(2).getImm() == 0;
    case AArch64::ADRP:
      return true;
    }
  }

  return false;
}

/// Compare and conditional select.
static bool isCCSelectPair(const MachineInstr *FirstMI,
                           const MachineInstr &SecondMI) {
  // 32 bits
  if (SecondMI.getOpcode() == AArch64::CSELWr) {
    // Assume the 1st instr to be a wildcard if it is unspecified.
    if (FirstMI == nullptr)
      return true;

    if (FirstMI->definesRegister(AArch64::WZR))
      switch (FirstMI->getOpcode()) {
      case AArch64::SUBSWrs:
        return !AArch64InstrInfo::hasShiftedReg(*FirstMI);
      case AArch64::SUBSWrx:
        return !AArch64InstrInfo::hasExtendedReg(*FirstMI);
      case AArch64::SUBSWrr:
      case AArch64::SUBSWri:
        return true;
      }
  }

  // 64 bits
  if (SecondMI.getOpcode() == AArch64::CSELXr) {
    // Assume the 1st instr to be a wildcard if it is unspecified.
    if (FirstMI == nullptr)
      return true;

    if (FirstMI->definesRegister(AArch64::XZR))
      switch (FirstMI->getOpcode()) {
      case AArch64::SUBSXrs:
        return !AArch64InstrInfo::hasShiftedReg(*FirstMI);
      case AArch64::SUBSXrx:
      case AArch64::SUBSXrx64:
        return !AArch64InstrInfo::hasExtendedReg(*FirstMI);
      case AArch64::SUBSXrr:
      case AArch64::SUBSXri:
        return true;
      }
  }

  return false;
}

// Arithmetic and logic.
static bool isArithmeticLogicPair(const MachineInstr *FirstMI,
                                  const MachineInstr &SecondMI) {
  if (AArch64InstrInfo::hasShiftedReg(SecondMI))
    return false;

  switch (SecondMI.getOpcode()) {
  // Arithmetic
  case AArch64::ADDWrr:
  case AArch64::ADDXrr:
  case AArch64::SUBWrr:
  case AArch64::SUBXrr:
  case AArch64::ADDWrs:
  case AArch64::ADDXrs:
  case AArch64::SUBWrs:
  case AArch64::SUBXrs:
  // Logic
  case AArch64::ANDWrr:
  case AArch64::ANDXrr:
  case AArch64::BICWrr:
  case AArch64::BICXrr:
  case AArch64::EONWrr:
  case AArch64::EONXrr:
  case AArch64::EORWrr:
  case AArch64::EORXrr:
  case AArch64::ORNWrr:
  case AArch64::ORNXrr:
  case AArch64::ORRWrr:
  case AArch64::ORRXrr:
  case AArch64::ANDWrs:
  case AArch64::ANDXrs:
  case AArch64::BICWrs:
  case AArch64::BICXrs:
  case AArch64::EONWrs:
  case AArch64::EONXrs:
  case AArch64::EORWrs:
  case AArch64::EORXrs:
  case AArch64::ORNWrs:
  case AArch64::ORNXrs:
  case AArch64::ORRWrs:
  case AArch64::ORRXrs:
    // Assume the 1st instr to be a wildcard if it is unspecified.
    if (FirstMI == nullptr)
      return true;

    // Arithmetic
    switch (FirstMI->getOpcode()) {
    case AArch64::ADDWrr:
    case AArch64::ADDXrr:
    case AArch64::ADDSWrr:
    case AArch64::ADDSXrr:
    case AArch64::SUBWrr:
    case AArch64::SUBXrr:
    case AArch64::SUBSWrr:
    case AArch64::SUBSXrr:
      return true;
    case AArch64::ADDWrs:
    case AArch64::ADDXrs:
    case AArch64::ADDSWrs:
    case AArch64::ADDSXrs:
    case AArch64::SUBWrs:
    case AArch64::SUBXrs:
    case AArch64::SUBSWrs:
    case AArch64::SUBSXrs:
      return !AArch64InstrInfo::hasShiftedReg(*FirstMI);
    }
    break;

  // Arithmetic, setting flags.
  case AArch64::ADDSWrr:
  case AArch64::ADDSXrr:
  case AArch64::SUBSWrr:
  case AArch64::SUBSXrr:
  case AArch64::ADDSWrs:
  case AArch64::ADDSXrs:
  case AArch64::SUBSWrs:
  case AArch64::SUBSXrs:
    // Assume the 1st instr to be a wildcard if it is unspecified.
    if (FirstMI == nullptr)
      return true;

    // Arithmetic, not setting flags.
    switch (FirstMI->getOpcode()) {
    case AArch64::ADDWrr:
    case AArch64::ADDXrr:
    case AArch64::SUBWrr:
    case AArch64::SUBXrr:
      return true;
    case AArch64::ADDWrs:
    case AArch64::ADDXrs:
    case AArch64::SUBWrs:
    case AArch64::SUBXrs:
      return !AArch64InstrInfo::hasShiftedReg(*FirstMI);
    }
    break;
  }

  return false;
}

/// \brief Check if the instr pair, FirstMI and SecondMI, should be fused
/// together. Given SecondMI, when FirstMI is unspecified, then check if
/// SecondMI may be part of a fused pair at all.
static bool shouldScheduleAdjacent(const TargetInstrInfo &TII,
                                   const TargetSubtargetInfo &TSI,
                                   const MachineInstr *FirstMI,
                                   const MachineInstr &SecondMI) {
  const AArch64Subtarget &ST = static_cast<const AArch64Subtarget&>(TSI);

  // All checking functions assume that the 1st instr is a wildcard if it is
  // unspecified.
  if (ST.hasArithmeticBccFusion() && isArithmeticBccPair(FirstMI, SecondMI))
    return true;
  if (ST.hasArithmeticCbzFusion() && isArithmeticCbzPair(FirstMI, SecondMI))
    return true;
  if (ST.hasFuseAES() && isAESPair(FirstMI, SecondMI))
    return true;
  if (ST.hasFuseCryptoEOR() && isCryptoEORPair(FirstMI, SecondMI))
    return true;
  if (ST.hasFuseLiterals() && isLiteralsPair(FirstMI, SecondMI))
    return true;
  if (ST.hasFuseAddress() && isAddressLdStPair(FirstMI, SecondMI))
    return true;
  if (ST.hasFuseCCSelect() && isCCSelectPair(FirstMI, SecondMI))
    return true;
  if (ST.hasFuseArithmeticLogic() && isArithmeticLogicPair(FirstMI, SecondMI))
    return true;

  return false;
}

} // end namespace


namespace llvm {

std::unique_ptr<ScheduleDAGMutation> createAArch64MacroFusionDAGMutation () {
  return createMacroFusionDAGMutation(shouldScheduleAdjacent);
}

} // end namespace llvm
