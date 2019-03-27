//===-- SIDefines.h - SI Helper Macros ----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
/// \file
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCInstrDesc.h"

#ifndef LLVM_LIB_TARGET_AMDGPU_SIDEFINES_H
#define LLVM_LIB_TARGET_AMDGPU_SIDEFINES_H

namespace llvm {

namespace SIInstrFlags {
// This needs to be kept in sync with the field bits in InstSI.
enum : uint64_t {
  // Low bits - basic encoding information.
  SALU = 1 << 0,
  VALU = 1 << 1,

  // SALU instruction formats.
  SOP1 = 1 << 2,
  SOP2 = 1 << 3,
  SOPC = 1 << 4,
  SOPK = 1 << 5,
  SOPP = 1 << 6,

  // VALU instruction formats.
  VOP1 = 1 << 7,
  VOP2 = 1 << 8,
  VOPC = 1 << 9,

 // TODO: Should this be spilt into VOP3 a and b?
  VOP3 = 1 << 10,
  VOP3P = 1 << 12,

  VINTRP = 1 << 13,
  SDWA = 1 << 14,
  DPP = 1 << 15,

  // Memory instruction formats.
  MUBUF = 1 << 16,
  MTBUF = 1 << 17,
  SMRD = 1 << 18,
  MIMG = 1 << 19,
  EXP = 1 << 20,
  FLAT = 1 << 21,
  DS = 1 << 22,

  // Pseudo instruction formats.
  VGPRSpill = 1 << 23,
  SGPRSpill = 1 << 24,

  // High bits - other information.
  VM_CNT = UINT64_C(1) << 32,
  EXP_CNT = UINT64_C(1) << 33,
  LGKM_CNT = UINT64_C(1) << 34,

  WQM = UINT64_C(1) << 35,
  DisableWQM = UINT64_C(1) << 36,
  Gather4 = UINT64_C(1) << 37,
  SOPK_ZEXT = UINT64_C(1) << 38,
  SCALAR_STORE = UINT64_C(1) << 39,
  FIXED_SIZE = UINT64_C(1) << 40,
  VOPAsmPrefer32Bit = UINT64_C(1) << 41,
  VOP3_OPSEL = UINT64_C(1) << 42,
  maybeAtomic = UINT64_C(1) << 43,
  renamedInGFX9 = UINT64_C(1) << 44,

  // Is a clamp on FP type.
  FPClamp = UINT64_C(1) << 45,

  // Is an integer clamp
  IntClamp = UINT64_C(1) << 46,

  // Clamps lo component of register.
  ClampLo = UINT64_C(1) << 47,

  // Clamps hi component of register.
  // ClampLo and ClampHi set for packed clamp.
  ClampHi = UINT64_C(1) << 48,

  // Is a packed VOP3P instruction.
  IsPacked = UINT64_C(1) << 49,

  // Is a D16 buffer instruction.
  D16Buf = UINT64_C(1) << 50,

  // Uses floating point double precision rounding mode
  FPDPRounding = UINT64_C(1) << 51
};

// v_cmp_class_* etc. use a 10-bit mask for what operation is checked.
// The result is true if any of these tests are true.
enum ClassFlags {
  S_NAN = 1 << 0,        // Signaling NaN
  Q_NAN = 1 << 1,        // Quiet NaN
  N_INFINITY = 1 << 2,   // Negative infinity
  N_NORMAL = 1 << 3,     // Negative normal
  N_SUBNORMAL = 1 << 4,  // Negative subnormal
  N_ZERO = 1 << 5,       // Negative zero
  P_ZERO = 1 << 6,       // Positive zero
  P_SUBNORMAL = 1 << 7,  // Positive subnormal
  P_NORMAL = 1 << 8,     // Positive normal
  P_INFINITY = 1 << 9    // Positive infinity
};
}

namespace AMDGPU {
  enum OperandType {
    /// Operands with register or 32-bit immediate
    OPERAND_REG_IMM_INT32 = MCOI::OPERAND_FIRST_TARGET,
    OPERAND_REG_IMM_INT64,
    OPERAND_REG_IMM_INT16,
    OPERAND_REG_IMM_FP32,
    OPERAND_REG_IMM_FP64,
    OPERAND_REG_IMM_FP16,

    /// Operands with register or inline constant
    OPERAND_REG_INLINE_C_INT16,
    OPERAND_REG_INLINE_C_INT32,
    OPERAND_REG_INLINE_C_INT64,
    OPERAND_REG_INLINE_C_FP16,
    OPERAND_REG_INLINE_C_FP32,
    OPERAND_REG_INLINE_C_FP64,
    OPERAND_REG_INLINE_C_V2FP16,
    OPERAND_REG_INLINE_C_V2INT16,

    OPERAND_REG_IMM_FIRST = OPERAND_REG_IMM_INT32,
    OPERAND_REG_IMM_LAST = OPERAND_REG_IMM_FP16,

    OPERAND_REG_INLINE_C_FIRST = OPERAND_REG_INLINE_C_INT16,
    OPERAND_REG_INLINE_C_LAST = OPERAND_REG_INLINE_C_V2INT16,

    OPERAND_SRC_FIRST = OPERAND_REG_IMM_INT32,
    OPERAND_SRC_LAST = OPERAND_REG_INLINE_C_LAST,

    // Operand for source modifiers for VOP instructions
    OPERAND_INPUT_MODS,

    // Operand for SDWA instructions
    OPERAND_SDWA_VOPC_DST,

    /// Operand with 32-bit immediate that uses the constant bus.
    OPERAND_KIMM32,
    OPERAND_KIMM16
  };
}

namespace SIStackID {
enum StackTypes : uint8_t {
  SCRATCH = 0,
  SGPR_SPILL = 1
};
}

// Input operand modifiers bit-masks
// NEG and SEXT share same bit-mask because they can't be set simultaneously.
namespace SISrcMods {
  enum {
   NEG = 1 << 0,   // Floating-point negate modifier
   ABS = 1 << 1,   // Floating-point absolute modifier
   SEXT = 1 << 0,  // Integer sign-extend modifier
   NEG_HI = ABS,   // Floating-point negate high packed component modifier.
   OP_SEL_0 = 1 << 2,
   OP_SEL_1 = 1 << 3,
   DST_OP_SEL = 1 << 3 // VOP3 dst op_sel (share mask with OP_SEL_1)
  };
}

namespace SIOutMods {
  enum {
    NONE = 0,
    MUL2 = 1,
    MUL4 = 2,
    DIV2 = 3
  };
}

namespace VGPRIndexMode {
  enum {
    SRC0_ENABLE = 1 << 0,
    SRC1_ENABLE = 1 << 1,
    SRC2_ENABLE = 1 << 2,
    DST_ENABLE = 1 << 3
  };
}

namespace AMDGPUAsmVariants {
  enum {
    DEFAULT = 0,
    VOP3 = 1,
    SDWA = 2,
    SDWA9 = 3,
    DPP = 4
  };
}

namespace AMDGPU {
namespace EncValues { // Encoding values of enum9/8/7 operands

enum {
  SGPR_MIN = 0,
  SGPR_MAX = 101,
  TTMP_VI_MIN = 112,
  TTMP_VI_MAX = 123,
  TTMP_GFX9_MIN = 108,
  TTMP_GFX9_MAX = 123,
  INLINE_INTEGER_C_MIN = 128,
  INLINE_INTEGER_C_POSITIVE_MAX = 192, // 64
  INLINE_INTEGER_C_MAX = 208,
  INLINE_FLOATING_C_MIN = 240,
  INLINE_FLOATING_C_MAX = 248,
  LITERAL_CONST = 255,
  VGPR_MIN = 256,
  VGPR_MAX = 511
};

} // namespace EncValues
} // namespace AMDGPU

namespace AMDGPU {
namespace SendMsg { // Encoding of SIMM16 used in s_sendmsg* insns.

enum Id { // Message ID, width(4) [3:0].
  ID_UNKNOWN_ = -1,
  ID_INTERRUPT = 1,
  ID_GS,
  ID_GS_DONE,
  ID_SYSMSG = 15,
  ID_GAPS_LAST_, // Indicate that sequence has gaps.
  ID_GAPS_FIRST_ = ID_INTERRUPT,
  ID_SHIFT_ = 0,
  ID_WIDTH_ = 4,
  ID_MASK_ = (((1 << ID_WIDTH_) - 1) << ID_SHIFT_)
};

enum Op { // Both GS and SYS operation IDs.
  OP_UNKNOWN_ = -1,
  OP_SHIFT_ = 4,
  // width(2) [5:4]
  OP_GS_NOP = 0,
  OP_GS_CUT,
  OP_GS_EMIT,
  OP_GS_EMIT_CUT,
  OP_GS_LAST_,
  OP_GS_FIRST_ = OP_GS_NOP,
  OP_GS_WIDTH_ = 2,
  OP_GS_MASK_ = (((1 << OP_GS_WIDTH_) - 1) << OP_SHIFT_),
  // width(3) [6:4]
  OP_SYS_ECC_ERR_INTERRUPT = 1,
  OP_SYS_REG_RD,
  OP_SYS_HOST_TRAP_ACK,
  OP_SYS_TTRACE_PC,
  OP_SYS_LAST_,
  OP_SYS_FIRST_ = OP_SYS_ECC_ERR_INTERRUPT,
  OP_SYS_WIDTH_ = 3,
  OP_SYS_MASK_ = (((1 << OP_SYS_WIDTH_) - 1) << OP_SHIFT_)
};

enum StreamId { // Stream ID, (2) [9:8].
  STREAM_ID_DEFAULT_ = 0,
  STREAM_ID_LAST_ = 4,
  STREAM_ID_FIRST_ = STREAM_ID_DEFAULT_,
  STREAM_ID_SHIFT_ = 8,
  STREAM_ID_WIDTH_=  2,
  STREAM_ID_MASK_ = (((1 << STREAM_ID_WIDTH_) - 1) << STREAM_ID_SHIFT_)
};

} // namespace SendMsg

namespace Hwreg { // Encoding of SIMM16 used in s_setreg/getreg* insns.

enum Id { // HwRegCode, (6) [5:0]
  ID_UNKNOWN_ = -1,
  ID_SYMBOLIC_FIRST_ = 1, // There are corresponding symbolic names defined.
  ID_MODE = 1,
  ID_STATUS = 2,
  ID_TRAPSTS = 3,
  ID_HW_ID = 4,
  ID_GPR_ALLOC = 5,
  ID_LDS_ALLOC = 6,
  ID_IB_STS = 7,
  ID_MEM_BASES = 15,
  ID_SYMBOLIC_FIRST_GFX9_ = ID_MEM_BASES,
  ID_SYMBOLIC_LAST_ = 16,
  ID_SHIFT_ = 0,
  ID_WIDTH_ = 6,
  ID_MASK_ = (((1 << ID_WIDTH_) - 1) << ID_SHIFT_)
};

enum Offset { // Offset, (5) [10:6]
  OFFSET_DEFAULT_ = 0,
  OFFSET_SHIFT_ = 6,
  OFFSET_WIDTH_ = 5,
  OFFSET_MASK_ = (((1 << OFFSET_WIDTH_) - 1) << OFFSET_SHIFT_),

  OFFSET_SRC_SHARED_BASE = 16,
  OFFSET_SRC_PRIVATE_BASE = 0
};

enum WidthMinusOne { // WidthMinusOne, (5) [15:11]
  WIDTH_M1_DEFAULT_ = 31,
  WIDTH_M1_SHIFT_ = 11,
  WIDTH_M1_WIDTH_ = 5,
  WIDTH_M1_MASK_ = (((1 << WIDTH_M1_WIDTH_) - 1) << WIDTH_M1_SHIFT_),

  WIDTH_M1_SRC_SHARED_BASE = 15,
  WIDTH_M1_SRC_PRIVATE_BASE = 15
};

} // namespace Hwreg

namespace Swizzle { // Encoding of swizzle macro used in ds_swizzle_b32.

enum Id { // id of symbolic names
  ID_QUAD_PERM = 0,
  ID_BITMASK_PERM,
  ID_SWAP,
  ID_REVERSE,
  ID_BROADCAST
};

enum EncBits {

  // swizzle mode encodings

  QUAD_PERM_ENC         = 0x8000,
  QUAD_PERM_ENC_MASK    = 0xFF00,

  BITMASK_PERM_ENC      = 0x0000,
  BITMASK_PERM_ENC_MASK = 0x8000,

  // QUAD_PERM encodings

  LANE_MASK             = 0x3,
  LANE_MAX              = LANE_MASK,
  LANE_SHIFT            = 2,
  LANE_NUM              = 4,

  // BITMASK_PERM encodings

  BITMASK_MASK          = 0x1F,
  BITMASK_MAX           = BITMASK_MASK,
  BITMASK_WIDTH         = 5,

  BITMASK_AND_SHIFT     = 0,
  BITMASK_OR_SHIFT      = 5,
  BITMASK_XOR_SHIFT     = 10
};

} // namespace Swizzle

namespace SDWA {

enum SdwaSel {
  BYTE_0 = 0,
  BYTE_1 = 1,
  BYTE_2 = 2,
  BYTE_3 = 3,
  WORD_0 = 4,
  WORD_1 = 5,
  DWORD = 6,
};

enum DstUnused {
  UNUSED_PAD = 0,
  UNUSED_SEXT = 1,
  UNUSED_PRESERVE = 2,
};

enum SDWA9EncValues{
  SRC_SGPR_MASK = 0x100,
  SRC_VGPR_MASK = 0xFF,
  VOPC_DST_VCC_MASK = 0x80,
  VOPC_DST_SGPR_MASK = 0x7F,

  SRC_VGPR_MIN = 0,
  SRC_VGPR_MAX = 255,
  SRC_SGPR_MIN = 256,
  SRC_SGPR_MAX = 357,
  SRC_TTMP_MIN = 364,
  SRC_TTMP_MAX = 379,
};

} // namespace SDWA

namespace DPP {

enum DppCtrl {
  QUAD_PERM_FIRST   = 0,
  QUAD_PERM_LAST    = 0xFF,
  DPP_UNUSED1       = 0x100,
  ROW_SHL0          = 0x100,
  ROW_SHL_FIRST     = 0x101,
  ROW_SHL_LAST      = 0x10F,
  DPP_UNUSED2       = 0x110,
  ROW_SHR0          = 0x110,
  ROW_SHR_FIRST     = 0x111,
  ROW_SHR_LAST      = 0x11F,
  DPP_UNUSED3       = 0x120,
  ROW_ROR0          = 0x120,
  ROW_ROR_FIRST     = 0x121,
  ROW_ROR_LAST      = 0x12F,
  WAVE_SHL1         = 0x130,
  DPP_UNUSED4_FIRST = 0x131,
  DPP_UNUSED4_LAST  = 0x133,
  WAVE_ROL1         = 0x134,
  DPP_UNUSED5_FIRST = 0x135,
  DPP_UNUSED5_LAST  = 0x137,
  WAVE_SHR1         = 0x138,
  DPP_UNUSED6_FIRST = 0x139,
  DPP_UNUSED6_LAST  = 0x13B,
  WAVE_ROR1         = 0x13C,
  DPP_UNUSED7_FIRST = 0x13D,
  DPP_UNUSED7_LAST  = 0x13F,
  ROW_MIRROR        = 0x140,
  ROW_HALF_MIRROR   = 0x141,
  BCAST15           = 0x142,
  BCAST31           = 0x143,
  DPP_LAST          = BCAST31
};

} // namespace DPP
} // namespace AMDGPU

#define R_00B028_SPI_SHADER_PGM_RSRC1_PS                                0x00B028
#define R_00B02C_SPI_SHADER_PGM_RSRC2_PS                                0x00B02C
#define   S_00B02C_EXTRA_LDS_SIZE(x)                                  (((x) & 0xFF) << 8)
#define R_00B128_SPI_SHADER_PGM_RSRC1_VS                                0x00B128
#define R_00B228_SPI_SHADER_PGM_RSRC1_GS                                0x00B228
#define R_00B328_SPI_SHADER_PGM_RSRC1_ES                                0x00B328
#define R_00B428_SPI_SHADER_PGM_RSRC1_HS                                0x00B428
#define R_00B528_SPI_SHADER_PGM_RSRC1_LS                                0x00B528
#define R_00B848_COMPUTE_PGM_RSRC1                                      0x00B848
#define   S_00B028_VGPRS(x)                                           (((x) & 0x3F) << 0)
#define   S_00B028_SGPRS(x)                                           (((x) & 0x0F) << 6)

#define R_00B84C_COMPUTE_PGM_RSRC2                                      0x00B84C
#define   S_00B84C_SCRATCH_EN(x)                                      (((x) & 0x1) << 0)
#define   G_00B84C_SCRATCH_EN(x)                                      (((x) >> 0) & 0x1)
#define   C_00B84C_SCRATCH_EN                                         0xFFFFFFFE
#define   S_00B84C_USER_SGPR(x)                                       (((x) & 0x1F) << 1)
#define   G_00B84C_USER_SGPR(x)                                       (((x) >> 1) & 0x1F)
#define   C_00B84C_USER_SGPR                                          0xFFFFFFC1
#define   S_00B84C_TRAP_HANDLER(x)                                    (((x) & 0x1) << 6)
#define   G_00B84C_TRAP_HANDLER(x)                                    (((x) >> 6) & 0x1)
#define   C_00B84C_TRAP_HANDLER                                       0xFFFFFFBF
#define   S_00B84C_TGID_X_EN(x)                                       (((x) & 0x1) << 7)
#define   G_00B84C_TGID_X_EN(x)                                       (((x) >> 7) & 0x1)
#define   C_00B84C_TGID_X_EN                                          0xFFFFFF7F
#define   S_00B84C_TGID_Y_EN(x)                                       (((x) & 0x1) << 8)
#define   G_00B84C_TGID_Y_EN(x)                                       (((x) >> 8) & 0x1)
#define   C_00B84C_TGID_Y_EN                                          0xFFFFFEFF
#define   S_00B84C_TGID_Z_EN(x)                                       (((x) & 0x1) << 9)
#define   G_00B84C_TGID_Z_EN(x)                                       (((x) >> 9) & 0x1)
#define   C_00B84C_TGID_Z_EN                                          0xFFFFFDFF
#define   S_00B84C_TG_SIZE_EN(x)                                      (((x) & 0x1) << 10)
#define   G_00B84C_TG_SIZE_EN(x)                                      (((x) >> 10) & 0x1)
#define   C_00B84C_TG_SIZE_EN                                         0xFFFFFBFF
#define   S_00B84C_TIDIG_COMP_CNT(x)                                  (((x) & 0x03) << 11)
#define   G_00B84C_TIDIG_COMP_CNT(x)                                  (((x) >> 11) & 0x03)
#define   C_00B84C_TIDIG_COMP_CNT                                     0xFFFFE7FF
/* CIK */
#define   S_00B84C_EXCP_EN_MSB(x)                                     (((x) & 0x03) << 13)
#define   G_00B84C_EXCP_EN_MSB(x)                                     (((x) >> 13) & 0x03)
#define   C_00B84C_EXCP_EN_MSB                                        0xFFFF9FFF
/*     */
#define   S_00B84C_LDS_SIZE(x)                                        (((x) & 0x1FF) << 15)
#define   G_00B84C_LDS_SIZE(x)                                        (((x) >> 15) & 0x1FF)
#define   C_00B84C_LDS_SIZE                                           0xFF007FFF
#define   S_00B84C_EXCP_EN(x)                                         (((x) & 0x7F) << 24)
#define   G_00B84C_EXCP_EN(x)                                         (((x) >> 24) & 0x7F)
#define   C_00B84C_EXCP_EN

#define R_0286CC_SPI_PS_INPUT_ENA                                       0x0286CC
#define R_0286D0_SPI_PS_INPUT_ADDR                                      0x0286D0

#define R_00B848_COMPUTE_PGM_RSRC1                                      0x00B848
#define   S_00B848_VGPRS(x)                                           (((x) & 0x3F) << 0)
#define   G_00B848_VGPRS(x)                                           (((x) >> 0) & 0x3F)
#define   C_00B848_VGPRS                                              0xFFFFFFC0
#define   S_00B848_SGPRS(x)                                           (((x) & 0x0F) << 6)
#define   G_00B848_SGPRS(x)                                           (((x) >> 6) & 0x0F)
#define   C_00B848_SGPRS                                              0xFFFFFC3F
#define   S_00B848_PRIORITY(x)                                        (((x) & 0x03) << 10)
#define   G_00B848_PRIORITY(x)                                        (((x) >> 10) & 0x03)
#define   C_00B848_PRIORITY                                           0xFFFFF3FF
#define   S_00B848_FLOAT_MODE(x)                                      (((x) & 0xFF) << 12)
#define   G_00B848_FLOAT_MODE(x)                                      (((x) >> 12) & 0xFF)
#define   C_00B848_FLOAT_MODE                                         0xFFF00FFF
#define   S_00B848_PRIV(x)                                            (((x) & 0x1) << 20)
#define   G_00B848_PRIV(x)                                            (((x) >> 20) & 0x1)
#define   C_00B848_PRIV                                               0xFFEFFFFF
#define   S_00B848_DX10_CLAMP(x)                                      (((x) & 0x1) << 21)
#define   G_00B848_DX10_CLAMP(x)                                      (((x) >> 21) & 0x1)
#define   C_00B848_DX10_CLAMP                                         0xFFDFFFFF
#define   S_00B848_DEBUG_MODE(x)                                      (((x) & 0x1) << 22)
#define   G_00B848_DEBUG_MODE(x)                                      (((x) >> 22) & 0x1)
#define   C_00B848_DEBUG_MODE                                         0xFFBFFFFF
#define   S_00B848_IEEE_MODE(x)                                       (((x) & 0x1) << 23)
#define   G_00B848_IEEE_MODE(x)                                       (((x) >> 23) & 0x1)
#define   C_00B848_IEEE_MODE                                          0xFF7FFFFF


// Helpers for setting FLOAT_MODE
#define FP_ROUND_ROUND_TO_NEAREST 0
#define FP_ROUND_ROUND_TO_INF 1
#define FP_ROUND_ROUND_TO_NEGINF 2
#define FP_ROUND_ROUND_TO_ZERO 3

// Bits 3:0 control rounding mode. 1:0 control single precision, 3:2 double
// precision.
#define FP_ROUND_MODE_SP(x) ((x) & 0x3)
#define FP_ROUND_MODE_DP(x) (((x) & 0x3) << 2)

#define FP_DENORM_FLUSH_IN_FLUSH_OUT 0
#define FP_DENORM_FLUSH_OUT 1
#define FP_DENORM_FLUSH_IN 2
#define FP_DENORM_FLUSH_NONE 3


// Bits 7:4 control denormal handling. 5:4 control single precision, 6:7 double
// precision.
#define FP_DENORM_MODE_SP(x) (((x) & 0x3) << 4)
#define FP_DENORM_MODE_DP(x) (((x) & 0x3) << 6)

#define R_00B860_COMPUTE_TMPRING_SIZE                                   0x00B860
#define   S_00B860_WAVESIZE(x)                                        (((x) & 0x1FFF) << 12)

#define R_0286E8_SPI_TMPRING_SIZE                                       0x0286E8
#define   S_0286E8_WAVESIZE(x)                                        (((x) & 0x1FFF) << 12)

#define R_SPILLED_SGPRS         0x4
#define R_SPILLED_VGPRS         0x8
} // End namespace llvm

#endif
