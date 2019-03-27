//===-- ARMBaseInfo.h - Top level definitions for ARM ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains small standalone helper functions and enum definitions for
// the ARM target useful for the compiler back-end and the MC libraries.
// As such, it deliberately does not include references to LLVM core
// code gen types, passes, etc..
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_ARM_UTILS_ARMBASEINFO_H
#define LLVM_LIB_TARGET_ARM_UTILS_ARMBASEINFO_H

#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/MC/SubtargetFeature.h"
#include "MCTargetDesc/ARMMCTargetDesc.h"

namespace llvm {

// Enums corresponding to ARM condition codes
namespace ARMCC {
// The CondCodes constants map directly to the 4-bit encoding of the
// condition field for predicated instructions.
enum CondCodes { // Meaning (integer)          Meaning (floating-point)
  EQ,            // Equal                      Equal
  NE,            // Not equal                  Not equal, or unordered
  HS,            // Carry set                  >, ==, or unordered
  LO,            // Carry clear                Less than
  MI,            // Minus, negative            Less than
  PL,            // Plus, positive or zero     >, ==, or unordered
  VS,            // Overflow                   Unordered
  VC,            // No overflow                Not unordered
  HI,            // Unsigned higher            Greater than, or unordered
  LS,            // Unsigned lower or same     Less than or equal
  GE,            // Greater than or equal      Greater than or equal
  LT,            // Less than                  Less than, or unordered
  GT,            // Greater than               Greater than
  LE,            // Less than or equal         <, ==, or unordered
  AL             // Always (unconditional)     Always (unconditional)
};

inline static CondCodes getOppositeCondition(CondCodes CC) {
  switch (CC) {
  default: llvm_unreachable("Unknown condition code");
  case EQ: return NE;
  case NE: return EQ;
  case HS: return LO;
  case LO: return HS;
  case MI: return PL;
  case PL: return MI;
  case VS: return VC;
  case VC: return VS;
  case HI: return LS;
  case LS: return HI;
  case GE: return LT;
  case LT: return GE;
  case GT: return LE;
  case LE: return GT;
  }
}
} // end namespace ARMCC

inline static const char *ARMCondCodeToString(ARMCC::CondCodes CC) {
  switch (CC) {
  case ARMCC::EQ:  return "eq";
  case ARMCC::NE:  return "ne";
  case ARMCC::HS:  return "hs";
  case ARMCC::LO:  return "lo";
  case ARMCC::MI:  return "mi";
  case ARMCC::PL:  return "pl";
  case ARMCC::VS:  return "vs";
  case ARMCC::VC:  return "vc";
  case ARMCC::HI:  return "hi";
  case ARMCC::LS:  return "ls";
  case ARMCC::GE:  return "ge";
  case ARMCC::LT:  return "lt";
  case ARMCC::GT:  return "gt";
  case ARMCC::LE:  return "le";
  case ARMCC::AL:  return "al";
  }
  llvm_unreachable("Unknown condition code");
}

inline static unsigned ARMCondCodeFromString(StringRef CC) {
  return StringSwitch<unsigned>(CC.lower())
    .Case("eq", ARMCC::EQ)
    .Case("ne", ARMCC::NE)
    .Case("hs", ARMCC::HS)
    .Case("cs", ARMCC::HS)
    .Case("lo", ARMCC::LO)
    .Case("cc", ARMCC::LO)
    .Case("mi", ARMCC::MI)
    .Case("pl", ARMCC::PL)
    .Case("vs", ARMCC::VS)
    .Case("vc", ARMCC::VC)
    .Case("hi", ARMCC::HI)
    .Case("ls", ARMCC::LS)
    .Case("ge", ARMCC::GE)
    .Case("lt", ARMCC::LT)
    .Case("gt", ARMCC::GT)
    .Case("le", ARMCC::LE)
    .Case("al", ARMCC::AL)
    .Default(~0U);
}

// System Registers
namespace ARMSysReg {
  struct MClassSysReg {
    const char *Name;
    uint16_t M1Encoding12;
    uint16_t M2M3Encoding8;
    uint16_t Encoding;
    FeatureBitset FeaturesRequired;

    // return true if FeaturesRequired are all present in ActiveFeatures
    bool hasRequiredFeatures(FeatureBitset ActiveFeatures) const {
      return (FeaturesRequired & ActiveFeatures) == FeaturesRequired;
    }

    // returns true if TestFeatures are all present in FeaturesRequired
    bool isInRequiredFeatures(FeatureBitset TestFeatures) const {
      return (FeaturesRequired & TestFeatures) == TestFeatures;
    }
  };

  #define GET_MCLASSSYSREG_DECL
  #include "ARMGenSystemRegister.inc"

  // lookup system register using 12-bit SYSm value.
  // Note: the search is uniqued using M1 mask
  const MClassSysReg *lookupMClassSysRegBy12bitSYSmValue(unsigned SYSm);

  // returns APSR with _<bits> qualifier.
  // Note: ARMv7-M deprecates using MSR APSR without a _<bits> qualifier
  const MClassSysReg *lookupMClassSysRegAPSRNonDeprecated(unsigned SYSm);

  // lookup system registers using 8-bit SYSm value
  const MClassSysReg *lookupMClassSysRegBy8bitSYSmValue(unsigned SYSm);

} // end namespace ARMSysReg

// Banked Registers
namespace ARMBankedReg {
  struct BankedReg {
    const char *Name;
    uint16_t Encoding;
  };
  #define GET_BANKEDREG_DECL
  #include "ARMGenSystemRegister.inc"
} // end namespace ARMBankedReg

} // end namespace llvm

#endif // LLVM_LIB_TARGET_ARM_UTILS_ARMBASEINFO_H
