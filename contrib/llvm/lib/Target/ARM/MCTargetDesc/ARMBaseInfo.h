//===-- ARMBaseInfo.h - Top level definitions for ARM -------- --*- C++ -*-===//
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

#ifndef LLVM_LIB_TARGET_ARM_MCTARGETDESC_ARMBASEINFO_H
#define LLVM_LIB_TARGET_ARM_MCTARGETDESC_ARMBASEINFO_H

#include "ARMMCTargetDesc.h"
#include "llvm/Support/ErrorHandling.h"
#include "Utils/ARMBaseInfo.h"

namespace llvm {

namespace ARM_PROC {
  enum IMod {
    IE = 2,
    ID = 3
  };

  enum IFlags {
    F = 1,
    I = 2,
    A = 4
  };

  inline static const char *IFlagsToString(unsigned val) {
    switch (val) {
    default: llvm_unreachable("Unknown iflags operand");
    case F: return "f";
    case I: return "i";
    case A: return "a";
    }
  }

  inline static const char *IModToString(unsigned val) {
    switch (val) {
    default: llvm_unreachable("Unknown imod operand");
    case IE: return "ie";
    case ID: return "id";
    }
  }
}

namespace ARM_MB {
  // The Memory Barrier Option constants map directly to the 4-bit encoding of
  // the option field for memory barrier operations.
  enum MemBOpt {
    RESERVED_0 = 0,
    OSHLD = 1,
    OSHST = 2,
    OSH   = 3,
    RESERVED_4 = 4,
    NSHLD = 5,
    NSHST = 6,
    NSH   = 7,
    RESERVED_8 = 8,
    ISHLD = 9,
    ISHST = 10,
    ISH   = 11,
    RESERVED_12 = 12,
    LD = 13,
    ST    = 14,
    SY    = 15
  };

  inline static const char *MemBOptToString(unsigned val, bool HasV8) {
    switch (val) {
    default: llvm_unreachable("Unknown memory operation");
    case SY:    return "sy";
    case ST:    return "st";
    case LD: return HasV8 ? "ld" : "#0xd";
    case RESERVED_12: return "#0xc";
    case ISH:   return "ish";
    case ISHST: return "ishst";
    case ISHLD: return HasV8 ?  "ishld" : "#0x9";
    case RESERVED_8: return "#0x8";
    case NSH:   return "nsh";
    case NSHST: return "nshst";
    case NSHLD: return HasV8 ? "nshld" : "#0x5";
    case RESERVED_4: return "#0x4";
    case OSH:   return "osh";
    case OSHST: return "oshst";
    case OSHLD: return HasV8 ? "oshld" : "#0x1";
    case RESERVED_0: return "#0x0";
    }
  }
} // namespace ARM_MB

namespace ARM_TSB {
  enum TraceSyncBOpt {
    CSYNC = 0
  };

  inline static const char *TraceSyncBOptToString(unsigned val) {
    switch (val) {
    default:
      llvm_unreachable("Unknown trace synchronization barrier operation");
      case CSYNC: return "csync";
    }
  }
} // namespace ARM_TSB

namespace ARM_ISB {
  enum InstSyncBOpt {
    RESERVED_0 = 0,
    RESERVED_1 = 1,
    RESERVED_2 = 2,
    RESERVED_3 = 3,
    RESERVED_4 = 4,
    RESERVED_5 = 5,
    RESERVED_6 = 6,
    RESERVED_7 = 7,
    RESERVED_8 = 8,
    RESERVED_9 = 9,
    RESERVED_10 = 10,
    RESERVED_11 = 11,
    RESERVED_12 = 12,
    RESERVED_13 = 13,
    RESERVED_14 = 14,
    SY = 15
  };

  inline static const char *InstSyncBOptToString(unsigned val) {
    switch (val) {
    default:
      llvm_unreachable("Unknown memory operation");
      case RESERVED_0:  return "#0x0";
      case RESERVED_1:  return "#0x1";
      case RESERVED_2:  return "#0x2";
      case RESERVED_3:  return "#0x3";
      case RESERVED_4:  return "#0x4";
      case RESERVED_5:  return "#0x5";
      case RESERVED_6:  return "#0x6";
      case RESERVED_7:  return "#0x7";
      case RESERVED_8:  return "#0x8";
      case RESERVED_9:  return "#0x9";
      case RESERVED_10: return "#0xa";
      case RESERVED_11: return "#0xb";
      case RESERVED_12: return "#0xc";
      case RESERVED_13: return "#0xd";
      case RESERVED_14: return "#0xe";
      case SY:          return "sy";
    }
  }
} // namespace ARM_ISB

/// isARMLowRegister - Returns true if the register is a low register (r0-r7).
///
static inline bool isARMLowRegister(unsigned Reg) {
  using namespace ARM;
  switch (Reg) {
  case R0:  case R1:  case R2:  case R3:
  case R4:  case R5:  case R6:  case R7:
    return true;
  default:
    return false;
  }
}

/// ARMII - This namespace holds all of the target specific flags that
/// instruction info tracks.
///
namespace ARMII {

  /// ARM Index Modes
  enum IndexMode {
    IndexModeNone  = 0,
    IndexModePre   = 1,
    IndexModePost  = 2,
    IndexModeUpd   = 3
  };

  /// ARM Addressing Modes
  enum AddrMode {
    AddrModeNone    = 0,
    AddrMode1       = 1,
    AddrMode2       = 2,
    AddrMode3       = 3,
    AddrMode4       = 4,
    AddrMode5       = 5,
    AddrMode6       = 6,
    AddrModeT1_1    = 7,
    AddrModeT1_2    = 8,
    AddrModeT1_4    = 9,
    AddrModeT1_s    = 10, // i8 * 4 for pc and sp relative data
    AddrModeT2_i12  = 11,
    AddrModeT2_i8   = 12,
    AddrModeT2_so   = 13,
    AddrModeT2_pc   = 14, // +/- i12 for pc relative data
    AddrModeT2_i8s4 = 15, // i8 * 4
    AddrMode_i12    = 16,
    AddrMode5FP16   = 17,  // i8 * 2
    AddrModeT2_ldrex = 18, // i8 * 4, with unscaled offset in MCInst
  };

  inline static const char *AddrModeToString(AddrMode addrmode) {
    switch (addrmode) {
    case AddrModeNone:    return "AddrModeNone";
    case AddrMode1:       return "AddrMode1";
    case AddrMode2:       return "AddrMode2";
    case AddrMode3:       return "AddrMode3";
    case AddrMode4:       return "AddrMode4";
    case AddrMode5:       return "AddrMode5";
    case AddrMode5FP16:   return "AddrMode5FP16";
    case AddrMode6:       return "AddrMode6";
    case AddrModeT1_1:    return "AddrModeT1_1";
    case AddrModeT1_2:    return "AddrModeT1_2";
    case AddrModeT1_4:    return "AddrModeT1_4";
    case AddrModeT1_s:    return "AddrModeT1_s";
    case AddrModeT2_i12:  return "AddrModeT2_i12";
    case AddrModeT2_i8:   return "AddrModeT2_i8";
    case AddrModeT2_so:   return "AddrModeT2_so";
    case AddrModeT2_pc:   return "AddrModeT2_pc";
    case AddrModeT2_i8s4: return "AddrModeT2_i8s4";
    case AddrMode_i12:    return "AddrMode_i12";
    case AddrModeT2_ldrex:return "AddrModeT2_ldrex";
    }
  }

  /// Target Operand Flag enum.
  enum TOF {
    //===------------------------------------------------------------------===//
    // ARM Specific MachineOperand flags.

    MO_NO_FLAG = 0,

    /// MO_LO16 - On a symbol operand, this represents a relocation containing
    /// lower 16 bit of the address. Used only via movw instruction.
    MO_LO16 = 0x1,

    /// MO_HI16 - On a symbol operand, this represents a relocation containing
    /// higher 16 bit of the address. Used only via movt instruction.
    MO_HI16 = 0x2,

    /// MO_OPTION_MASK - Most flags are mutually exclusive; this mask selects
    /// just that part of the flag set.
    MO_OPTION_MASK = 0x3,

    /// MO_COFFSTUB - On a symbol operand "FOO", this indicates that the
    /// reference is actually to the ".refptrp.FOO" symbol.  This is used for
    /// stub symbols on windows.
    MO_COFFSTUB = 0x4,

    /// MO_GOT - On a symbol operand, this represents a GOT relative relocation.
    MO_GOT = 0x8,

    /// MO_SBREL - On a symbol operand, this represents a static base relative
    /// relocation. Used in movw and movt instructions.
    MO_SBREL = 0x10,

    /// MO_DLLIMPORT - On a symbol operand, this represents that the reference
    /// to the symbol is for an import stub.  This is used for DLL import
    /// storage class indication on Windows.
    MO_DLLIMPORT = 0x20,

    /// MO_SECREL - On a symbol operand this indicates that the immediate is
    /// the offset from beginning of section.
    ///
    /// This is the TLS offset for the COFF/Windows TLS mechanism.
    MO_SECREL = 0x40,

    /// MO_NONLAZY - This is an independent flag, on a symbol operand "FOO" it
    /// represents a symbol which, if indirect, will get special Darwin mangling
    /// as a non-lazy-ptr indirect symbol (i.e. "L_FOO$non_lazy_ptr"). Can be
    /// combined with MO_LO16, MO_HI16 or MO_NO_FLAG (in a constant-pool, for
    /// example).
    MO_NONLAZY = 0x80,

    // It's undefined behaviour if an enum overflows the range between its
    // smallest and largest values, but since these are |ed together, it can
    // happen. Put a sentinel in (values of this enum are stored as "unsigned
    // char").
    MO_UNUSED_MAXIMUM = 0xff
  };

  enum {
    //===------------------------------------------------------------------===//
    // Instruction Flags.

    //===------------------------------------------------------------------===//
    // This four-bit field describes the addressing mode used.
    AddrModeMask  = 0x1f, // The AddrMode enums are declared in ARMBaseInfo.h

    // IndexMode - Unindex, pre-indexed, or post-indexed are valid for load
    // and store ops only.  Generic "updating" flag is used for ld/st multiple.
    // The index mode enums are declared in ARMBaseInfo.h
    IndexModeShift = 5,
    IndexModeMask  = 3 << IndexModeShift,

    //===------------------------------------------------------------------===//
    // Instruction encoding formats.
    //
    FormShift     = 7,
    FormMask      = 0x3f << FormShift,

    // Pseudo instructions
    Pseudo        = 0  << FormShift,

    // Multiply instructions
    MulFrm        = 1  << FormShift,

    // Branch instructions
    BrFrm         = 2  << FormShift,
    BrMiscFrm     = 3  << FormShift,

    // Data Processing instructions
    DPFrm         = 4  << FormShift,
    DPSoRegFrm    = 5  << FormShift,

    // Load and Store
    LdFrm         = 6  << FormShift,
    StFrm         = 7  << FormShift,
    LdMiscFrm     = 8  << FormShift,
    StMiscFrm     = 9  << FormShift,
    LdStMulFrm    = 10 << FormShift,

    LdStExFrm     = 11 << FormShift,

    // Miscellaneous arithmetic instructions
    ArithMiscFrm  = 12 << FormShift,
    SatFrm        = 13 << FormShift,

    // Extend instructions
    ExtFrm        = 14 << FormShift,

    // VFP formats
    VFPUnaryFrm   = 15 << FormShift,
    VFPBinaryFrm  = 16 << FormShift,
    VFPConv1Frm   = 17 << FormShift,
    VFPConv2Frm   = 18 << FormShift,
    VFPConv3Frm   = 19 << FormShift,
    VFPConv4Frm   = 20 << FormShift,
    VFPConv5Frm   = 21 << FormShift,
    VFPLdStFrm    = 22 << FormShift,
    VFPLdStMulFrm = 23 << FormShift,
    VFPMiscFrm    = 24 << FormShift,

    // Thumb format
    ThumbFrm      = 25 << FormShift,

    // Miscelleaneous format
    MiscFrm       = 26 << FormShift,

    // NEON formats
    NGetLnFrm     = 27 << FormShift,
    NSetLnFrm     = 28 << FormShift,
    NDupFrm       = 29 << FormShift,
    NLdStFrm      = 30 << FormShift,
    N1RegModImmFrm= 31 << FormShift,
    N2RegFrm      = 32 << FormShift,
    NVCVTFrm      = 33 << FormShift,
    NVDupLnFrm    = 34 << FormShift,
    N2RegVShLFrm  = 35 << FormShift,
    N2RegVShRFrm  = 36 << FormShift,
    N3RegFrm      = 37 << FormShift,
    N3RegVShFrm   = 38 << FormShift,
    NVExtFrm      = 39 << FormShift,
    NVMulSLFrm    = 40 << FormShift,
    NVTBLFrm      = 41 << FormShift,
    N3RegCplxFrm  = 43 << FormShift,

    //===------------------------------------------------------------------===//
    // Misc flags.

    // UnaryDP - Indicates this is a unary data processing instruction, i.e.
    // it doesn't have a Rn operand.
    UnaryDP       = 1 << 13,

    // Xform16Bit - Indicates this Thumb2 instruction may be transformed into
    // a 16-bit Thumb instruction if certain conditions are met.
    Xform16Bit    = 1 << 14,

    // ThumbArithFlagSetting - The instruction is a 16-bit flag setting Thumb
    // instruction. Used by the parser to determine whether to require the 'S'
    // suffix on the mnemonic (when not in an IT block) or preclude it (when
    // in an IT block).
    ThumbArithFlagSetting = 1 << 18,

    //===------------------------------------------------------------------===//
    // Code domain.
    DomainShift   = 15,
    DomainMask    = 7 << DomainShift,
    DomainGeneral = 0 << DomainShift,
    DomainVFP     = 1 << DomainShift,
    DomainNEON    = 2 << DomainShift,
    DomainNEONA8  = 4 << DomainShift,

    //===------------------------------------------------------------------===//
    // Field shifts - such shifts are used to set field while generating
    // machine instructions.
    //
    // FIXME: This list will need adjusting/fixing as the MC code emitter
    // takes shape and the ARMCodeEmitter.cpp bits go away.
    ShiftTypeShift = 4,

    M_BitShift     = 5,
    ShiftImmShift  = 5,
    ShiftShift     = 7,
    N_BitShift     = 7,
    ImmHiShift     = 8,
    SoRotImmShift  = 8,
    RegRsShift     = 8,
    ExtRotImmShift = 10,
    RegRdLoShift   = 12,
    RegRdShift     = 12,
    RegRdHiShift   = 16,
    RegRnShift     = 16,
    S_BitShift     = 20,
    W_BitShift     = 21,
    AM3_I_BitShift = 22,
    D_BitShift     = 22,
    U_BitShift     = 23,
    P_BitShift     = 24,
    I_BitShift     = 25,
    CondShift      = 28
  };

} // end namespace ARMII

} // end namespace llvm;

#endif
