//===- HexagonBaseInfo.h - Top level definitions for Hexagon ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains small standalone helper functions and enum definitions for
// the Hexagon target useful for the compiler back-end and the MC libraries.
// As such, it deliberately does not include references to LLVM core
// code gen types, passes, etc..
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HEXAGON_MCTARGETDESC_HEXAGONBASEINFO_H
#define LLVM_LIB_TARGET_HEXAGON_MCTARGETDESC_HEXAGONBASEINFO_H

#include "HexagonDepITypes.h"
#include "MCTargetDesc/HexagonMCTargetDesc.h"

namespace llvm {

/// HexagonII - This namespace holds all of the target specific flags that
/// instruction info tracks.
namespace HexagonII {
  unsigned const TypeCVI_FIRST = TypeCVI_4SLOT_MPY;
  unsigned const TypeCVI_LAST = TypeCVI_ZW;

  enum SubTarget {
    HasV55SubT    = 0x3c,
    HasV60SubT    = 0x38,
  };

  enum AddrMode {
    NoAddrMode     = 0,  // No addressing mode
    Absolute       = 1,  // Absolute addressing mode
    AbsoluteSet    = 2,  // Absolute set addressing mode
    BaseImmOffset  = 3,  // Indirect with offset
    BaseLongOffset = 4,  // Indirect with long offset
    BaseRegOffset  = 5,  // Indirect with register offset
    PostInc        = 6   // Post increment addressing mode
  };

  enum MemAccessSize {
    NoMemAccess = 0,
    ByteAccess,
    HalfWordAccess,
    WordAccess,
    DoubleWordAccess,
    HVXVectorAccess
  };

  // MCInstrDesc TSFlags
  // *** Must match HexagonInstrFormat*.td ***
  enum {
    // This 7-bit field describes the insn type.
    TypePos = 0,
    TypeMask = 0x7f,

    // Solo instructions.
    SoloPos = 7,
    SoloMask = 0x1,
    // Packed only with A or X-type instructions.
    SoloAXPos = 8,
    SoloAXMask = 0x1,
    // Only A-type instruction in first slot or nothing.
    RestrictSlot1AOKPos = 9,
    RestrictSlot1AOKMask = 0x1,

    // Predicated instructions.
    PredicatedPos = 10,
    PredicatedMask = 0x1,
    PredicatedFalsePos = 11,
    PredicatedFalseMask = 0x1,
    PredicatedNewPos = 12,
    PredicatedNewMask = 0x1,
    PredicateLatePos = 13,
    PredicateLateMask = 0x1,

    // New-Value consumer instructions.
    NewValuePos = 14,
    NewValueMask = 0x1,
    // New-Value producer instructions.
    hasNewValuePos = 15,
    hasNewValueMask = 0x1,
    // Which operand consumes or produces a new value.
    NewValueOpPos = 16,
    NewValueOpMask = 0x7,
    // Stores that can become new-value stores.
    mayNVStorePos = 19,
    mayNVStoreMask = 0x1,
    // New-value store instructions.
    NVStorePos = 20,
    NVStoreMask = 0x1,
    // Loads that can become current-value loads.
    mayCVLoadPos = 21,
    mayCVLoadMask = 0x1,
    // Current-value load instructions.
    CVLoadPos = 22,
    CVLoadMask = 0x1,

    // Extendable insns.
    ExtendablePos = 23,
    ExtendableMask = 0x1,
    // Insns must be extended.
    ExtendedPos = 24,
    ExtendedMask = 0x1,
    // Which operand may be extended.
    ExtendableOpPos = 25,
    ExtendableOpMask = 0x7,
    // Signed or unsigned range.
    ExtentSignedPos = 28,
    ExtentSignedMask = 0x1,
    // Number of bits of range before extending operand.
    ExtentBitsPos = 29,
    ExtentBitsMask = 0x1f,
    // Alignment power-of-two before extending operand.
    ExtentAlignPos = 34,
    ExtentAlignMask = 0x3,

    CofMax1Pos = 36,
    CofMax1Mask = 0x1,
    CofRelax1Pos = 37,
    CofRelax1Mask = 0x1,
    CofRelax2Pos = 38,
    CofRelax2Mask = 0x1,

    RestrictNoSlot1StorePos = 39,
    RestrictNoSlot1StoreMask = 0x1,

    // Addressing mode for load/store instructions.
    AddrModePos = 42,
    AddrModeMask = 0x7,
    // Access size for load/store instructions.
    MemAccessSizePos = 45,
    MemAccesSizeMask = 0xf,

    // Branch predicted taken.
    TakenPos = 49,
    TakenMask = 0x1,

    // Floating-point instructions.
    FPPos = 50,
    FPMask = 0x1,

    // New-Value producer-2 instructions.
    hasNewValuePos2 = 52,
    hasNewValueMask2 = 0x1,
    // Which operand consumes or produces a new value.
    NewValueOpPos2 = 53,
    NewValueOpMask2 = 0x7,

    // Accumulator instructions.
    AccumulatorPos = 56,
    AccumulatorMask = 0x1,

    // Complex XU, prevent xu competition by preferring slot3
    PrefersSlot3Pos = 57,
    PrefersSlot3Mask = 0x1,

    // v65
    HasTmpDstPos = 60,
    HasTmpDstMask = 0x1,

    CVINewPos = 62,
    CVINewMask = 0x1,
  };

  // *** The code above must match HexagonInstrFormat*.td *** //

  // Hexagon specific MO operand flag mask.
  enum HexagonMOTargetFlagVal {
    // Hexagon-specific MachineOperand target flags.
    //
    // When changing these, make sure to update
    // getSerializableDirectMachineOperandTargetFlags and
    // getSerializableBitmaskMachineOperandTargetFlags if needed.
    MO_NO_FLAG,

    /// MO_PCREL - On a symbol operand, indicates a PC-relative relocation
    /// Used for computing a global address for PIC compilations
    MO_PCREL,

    /// MO_GOT - Indicates a GOT-relative relocation
    MO_GOT,

    // Low or high part of a symbol.
    MO_LO16,
    MO_HI16,

    // Offset from the base of the SDA.
    MO_GPREL,

    // MO_GDGOT - indicates GOT relative relocation for TLS
    // GeneralDynamic method
    MO_GDGOT,

    // MO_GDPLT - indicates PLT relative relocation for TLS
    // GeneralDynamic method
    MO_GDPLT,

    // MO_IE - indicates non PIC relocation for TLS
    // Initial Executable method
    MO_IE,

    // MO_IEGOT - indicates PIC relocation for TLS
    // Initial Executable method
    MO_IEGOT,

    // MO_TPREL - indicates relocation for TLS
    // local Executable method
    MO_TPREL,

    // HMOTF_ConstExtended
    // Addendum to above, indicates a const extended op
    // Can be used as a mask.
    HMOTF_ConstExtended = 0x80,

    // Union of all bitmasks (currently only HMOTF_ConstExtended).
    MO_Bitmasks = HMOTF_ConstExtended
  };

  // Hexagon Sub-instruction classes.
  enum SubInstructionGroup {
    HSIG_None = 0,
    HSIG_L1,
    HSIG_L2,
    HSIG_S1,
    HSIG_S2,
    HSIG_A,
    HSIG_Compound
  };

  // Hexagon Compound classes.
  enum CompoundGroup {
    HCG_None = 0,
    HCG_A,
    HCG_B,
    HCG_C
  };

  enum InstParseBits {
    INST_PARSE_MASK       = 0x0000c000,
    INST_PARSE_PACKET_END = 0x0000c000,
    INST_PARSE_LOOP_END   = 0x00008000,
    INST_PARSE_NOT_END    = 0x00004000,
    INST_PARSE_DUPLEX     = 0x00000000,
    INST_PARSE_EXTENDER   = 0x00000000
  };

  enum InstIClassBits : unsigned {
    INST_ICLASS_MASK      = 0xf0000000,
    INST_ICLASS_EXTENDER  = 0x00000000,
    INST_ICLASS_J_1       = 0x10000000,
    INST_ICLASS_J_2       = 0x20000000,
    INST_ICLASS_LD_ST_1   = 0x30000000,
    INST_ICLASS_LD_ST_2   = 0x40000000,
    INST_ICLASS_J_3       = 0x50000000,
    INST_ICLASS_CR        = 0x60000000,
    INST_ICLASS_ALU32_1   = 0x70000000,
    INST_ICLASS_XTYPE_1   = 0x80000000,
    INST_ICLASS_LD        = 0x90000000,
    INST_ICLASS_ST        = 0xa0000000,
    INST_ICLASS_ALU32_2   = 0xb0000000,
    INST_ICLASS_XTYPE_2   = 0xc0000000,
    INST_ICLASS_XTYPE_3   = 0xd0000000,
    INST_ICLASS_XTYPE_4   = 0xe0000000,
    INST_ICLASS_ALU32_3   = 0xf0000000
  };

  LLVM_ATTRIBUTE_UNUSED
  static unsigned getMemAccessSizeInBytes(MemAccessSize S) {
    switch (S) {
      case ByteAccess:        return 1;
      case HalfWordAccess:    return 2;
      case WordAccess:        return 4;
      case DoubleWordAccess:  return 8;
      default:                return 0;
    }
  }
} // end namespace HexagonII

} // end namespace llvm

#endif // LLVM_LIB_TARGET_HEXAGON_MCTARGETDESC_HEXAGONBASEINFO_H
