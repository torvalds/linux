//===--- ARMEHABI.h - ARM Exception Handling ABI ----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the constants for the ARM unwind opcodes and exception
// handling table entry kinds.
//
// The enumerations and constants in this file reflect the ARM EHABI
// Specification as published by ARM.
//
// Exception Handling ABI for the ARM Architecture r2.09 - November 30, 2012
//
// http://infocenter.arm.com/help/topic/com.arm.doc.ihi0038a/IHI0038A_ehabi.pdf
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_ARMEHABI_H
#define LLVM_SUPPORT_ARMEHABI_H

namespace llvm {
namespace ARM {
namespace EHABI {
  /// ARM exception handling table entry kinds
  enum EHTEntryKind {
    EHT_GENERIC = 0x00,
    EHT_COMPACT = 0x80
  };

  enum {
    /// Special entry for the function never unwind
    EXIDX_CANTUNWIND = 0x1
  };

  /// ARM-defined frame unwinding opcodes
  enum UnwindOpcodes {
    // Format: 00xxxxxx
    // Purpose: vsp = vsp + ((x << 2) + 4)
    UNWIND_OPCODE_INC_VSP = 0x00,

    // Format: 01xxxxxx
    // Purpose: vsp = vsp - ((x << 2) + 4)
    UNWIND_OPCODE_DEC_VSP = 0x40,

    // Format: 10000000 00000000
    // Purpose: refuse to unwind
    UNWIND_OPCODE_REFUSE = 0x8000,

    // Format: 1000xxxx xxxxxxxx
    // Purpose: pop r[15:12], r[11:4]
    // Constraint: x != 0
    UNWIND_OPCODE_POP_REG_MASK_R4 = 0x8000,

    // Format: 1001xxxx
    // Purpose: vsp = r[x]
    // Constraint: x != 13 && x != 15
    UNWIND_OPCODE_SET_VSP = 0x90,

    // Format: 10100xxx
    // Purpose: pop r[(4+x):4]
    UNWIND_OPCODE_POP_REG_RANGE_R4 = 0xa0,

    // Format: 10101xxx
    // Purpose: pop r14, r[(4+x):4]
    UNWIND_OPCODE_POP_REG_RANGE_R4_R14 = 0xa8,

    // Format: 10110000
    // Purpose: finish
    UNWIND_OPCODE_FINISH = 0xb0,

    // Format: 10110001 0000xxxx
    // Purpose: pop r[3:0]
    // Constraint: x != 0
    UNWIND_OPCODE_POP_REG_MASK = 0xb100,

    // Format: 10110010 x(uleb128)
    // Purpose: vsp = vsp + ((x << 2) + 0x204)
    UNWIND_OPCODE_INC_VSP_ULEB128 = 0xb2,

    // Format: 10110011 xxxxyyyy
    // Purpose: pop d[(x+y):x]
    UNWIND_OPCODE_POP_VFP_REG_RANGE_FSTMFDX = 0xb300,

    // Format: 10111xxx
    // Purpose: pop d[(8+x):8]
    UNWIND_OPCODE_POP_VFP_REG_RANGE_FSTMFDX_D8 = 0xb8,

    // Format: 11000xxx
    // Purpose: pop wR[(10+x):10]
    UNWIND_OPCODE_POP_WIRELESS_MMX_REG_RANGE_WR10 = 0xc0,

    // Format: 11000110 xxxxyyyy
    // Purpose: pop wR[(x+y):x]
    UNWIND_OPCODE_POP_WIRELESS_MMX_REG_RANGE = 0xc600,

    // Format: 11000111 0000xxxx
    // Purpose: pop wCGR[3:0]
    // Constraint: x != 0
    UNWIND_OPCODE_POP_WIRELESS_MMX_REG_MASK = 0xc700,

    // Format: 11001000 xxxxyyyy
    // Purpose: pop d[(16+x+y):(16+x)]
    UNWIND_OPCODE_POP_VFP_REG_RANGE_FSTMFDD_D16 = 0xc800,

    // Format: 11001001 xxxxyyyy
    // Purpose: pop d[(x+y):x]
    UNWIND_OPCODE_POP_VFP_REG_RANGE_FSTMFDD = 0xc900,

    // Format: 11010xxx
    // Purpose: pop d[(8+x):8]
    UNWIND_OPCODE_POP_VFP_REG_RANGE_FSTMFDD_D8 = 0xd0
  };

  /// ARM-defined Personality Routine Index
  enum PersonalityRoutineIndex {
    // To make the exception handling table become more compact, ARM defined
    // several personality routines in EHABI.  There are 3 different
    // personality routines in ARM EHABI currently.  It is possible to have 16
    // pre-defined personality routines at most.
    AEABI_UNWIND_CPP_PR0 = 0,
    AEABI_UNWIND_CPP_PR1 = 1,
    AEABI_UNWIND_CPP_PR2 = 2,

    NUM_PERSONALITY_INDEX
  };
}
}
}

#endif
