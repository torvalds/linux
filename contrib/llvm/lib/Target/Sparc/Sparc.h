//===-- Sparc.h - Top-level interface for Sparc representation --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the entry points for global functions defined in the LLVM
// Sparc back-end.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_SPARC_SPARC_H
#define LLVM_LIB_TARGET_SPARC_SPARC_H

#include "MCTargetDesc/SparcMCTargetDesc.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/TargetMachine.h"

namespace llvm {
  class FunctionPass;
  class SparcTargetMachine;
  class formatted_raw_ostream;
  class AsmPrinter;
  class MCInst;
  class MachineInstr;

  FunctionPass *createSparcISelDag(SparcTargetMachine &TM);
  FunctionPass *createSparcDelaySlotFillerPass();

  void LowerSparcMachineInstrToMCInst(const MachineInstr *MI,
                                      MCInst &OutMI,
                                      AsmPrinter &AP);
} // end namespace llvm;

namespace llvm {
  // Enums corresponding to Sparc condition codes, both icc's and fcc's.  These
  // values must be kept in sync with the ones in the .td file.
  namespace SPCC {
    enum CondCodes {
      ICC_A   =  8   ,  // Always
      ICC_N   =  0   ,  // Never
      ICC_NE  =  9   ,  // Not Equal
      ICC_E   =  1   ,  // Equal
      ICC_G   = 10   ,  // Greater
      ICC_LE  =  2   ,  // Less or Equal
      ICC_GE  = 11   ,  // Greater or Equal
      ICC_L   =  3   ,  // Less
      ICC_GU  = 12   ,  // Greater Unsigned
      ICC_LEU =  4   ,  // Less or Equal Unsigned
      ICC_CC  = 13   ,  // Carry Clear/Great or Equal Unsigned
      ICC_CS  =  5   ,  // Carry Set/Less Unsigned
      ICC_POS = 14   ,  // Positive
      ICC_NEG =  6   ,  // Negative
      ICC_VC  = 15   ,  // Overflow Clear
      ICC_VS  =  7   ,  // Overflow Set

      FCC_A   =  8+16,  // Always
      FCC_N   =  0+16,  // Never
      FCC_U   =  7+16,  // Unordered
      FCC_G   =  6+16,  // Greater
      FCC_UG  =  5+16,  // Unordered or Greater
      FCC_L   =  4+16,  // Less
      FCC_UL  =  3+16,  // Unordered or Less
      FCC_LG  =  2+16,  // Less or Greater
      FCC_NE  =  1+16,  // Not Equal
      FCC_E   =  9+16,  // Equal
      FCC_UE  = 10+16,  // Unordered or Equal
      FCC_GE  = 11+16,  // Greater or Equal
      FCC_UGE = 12+16,  // Unordered or Greater or Equal
      FCC_LE  = 13+16,  // Less or Equal
      FCC_ULE = 14+16,  // Unordered or Less or Equal
      FCC_O   = 15+16,  // Ordered

      CPCC_A   =  8+32,  // Always
      CPCC_N   =  0+32,  // Never
      CPCC_3   =  7+32,
      CPCC_2   =  6+32,
      CPCC_23  =  5+32,
      CPCC_1   =  4+32,
      CPCC_13  =  3+32,
      CPCC_12  =  2+32,
      CPCC_123 =  1+32,
      CPCC_0   =  9+32,
      CPCC_03  = 10+32,
      CPCC_02  = 11+32,
      CPCC_023 = 12+32,
      CPCC_01  = 13+32,
      CPCC_013 = 14+32,
      CPCC_012 = 15+32
    };
  }

  inline static const char *SPARCCondCodeToString(SPCC::CondCodes CC) {
    switch (CC) {
    case SPCC::ICC_A:   return "a";
    case SPCC::ICC_N:   return "n";
    case SPCC::ICC_NE:  return "ne";
    case SPCC::ICC_E:   return "e";
    case SPCC::ICC_G:   return "g";
    case SPCC::ICC_LE:  return "le";
    case SPCC::ICC_GE:  return "ge";
    case SPCC::ICC_L:   return "l";
    case SPCC::ICC_GU:  return "gu";
    case SPCC::ICC_LEU: return "leu";
    case SPCC::ICC_CC:  return "cc";
    case SPCC::ICC_CS:  return "cs";
    case SPCC::ICC_POS: return "pos";
    case SPCC::ICC_NEG: return "neg";
    case SPCC::ICC_VC:  return "vc";
    case SPCC::ICC_VS:  return "vs";
    case SPCC::FCC_A:   return "a";
    case SPCC::FCC_N:   return "n";
    case SPCC::FCC_U:   return "u";
    case SPCC::FCC_G:   return "g";
    case SPCC::FCC_UG:  return "ug";
    case SPCC::FCC_L:   return "l";
    case SPCC::FCC_UL:  return "ul";
    case SPCC::FCC_LG:  return "lg";
    case SPCC::FCC_NE:  return "ne";
    case SPCC::FCC_E:   return "e";
    case SPCC::FCC_UE:  return "ue";
    case SPCC::FCC_GE:  return "ge";
    case SPCC::FCC_UGE: return "uge";
    case SPCC::FCC_LE:  return "le";
    case SPCC::FCC_ULE: return "ule";
    case SPCC::FCC_O:   return "o";
    case SPCC::CPCC_A:   return "a";
    case SPCC::CPCC_N:   return "n";
    case SPCC::CPCC_3:   return "3";
    case SPCC::CPCC_2:   return "2";
    case SPCC::CPCC_23:  return "23";
    case SPCC::CPCC_1:   return "1";
    case SPCC::CPCC_13:  return "13";
    case SPCC::CPCC_12:  return "12";
    case SPCC::CPCC_123: return "123";
    case SPCC::CPCC_0:   return "0";
    case SPCC::CPCC_03:  return "03";
    case SPCC::CPCC_02:  return "02";
    case SPCC::CPCC_023: return "023";
    case SPCC::CPCC_01:  return "01";
    case SPCC::CPCC_013: return "013";
    case SPCC::CPCC_012: return "012";
    }
    llvm_unreachable("Invalid cond code");
  }

  inline static unsigned HI22(int64_t imm) {
    return (unsigned)((imm >> 10) & ((1 << 22)-1));
  }

  inline static unsigned LO10(int64_t imm) {
    return (unsigned)(imm & 0x3FF);
  }

  inline static unsigned HIX22(int64_t imm) {
    return HI22(~imm);
  }

  inline static unsigned LOX10(int64_t imm) {
    return ~LO10(~imm);
  }

}  // end namespace llvm
#endif
