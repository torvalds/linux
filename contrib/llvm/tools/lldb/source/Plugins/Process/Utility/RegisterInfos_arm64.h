//===-- RegisterInfos_arm64.h -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifdef DECLARE_REGISTER_INFOS_ARM64_STRUCT

#include <stddef.h>

#include "lldb/lldb-defines.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-private.h"

#include "Utility/ARM64_DWARF_Registers.h"
#include "Utility/ARM64_ehframe_Registers.h"

#ifndef GPR_OFFSET
#error GPR_OFFSET must be defined before including this header file
#endif

#ifndef GPR_OFFSET_NAME
#error GPR_OFFSET_NAME must be defined before including this header file
#endif

#ifndef FPU_OFFSET
#error FPU_OFFSET must be defined before including this header file
#endif

#ifndef FPU_OFFSET_NAME
#error FPU_OFFSET_NAME must be defined before including this header file
#endif

#ifndef EXC_OFFSET_NAME
#error EXC_OFFSET_NAME must be defined before including this header file
#endif

#ifndef DBG_OFFSET_NAME
#error DBG_OFFSET_NAME must be defined before including this header file
#endif

#ifndef DEFINE_DBG
#error DEFINE_DBG must be defined before including this header file
#endif

// Offsets for a little-endian layout of the register context
#define GPR_W_PSEUDO_REG_ENDIAN_OFFSET 0
#define FPU_S_PSEUDO_REG_ENDIAN_OFFSET 0
#define FPU_D_PSEUDO_REG_ENDIAN_OFFSET 0

enum {
  gpr_x0 = 0,
  gpr_x1,
  gpr_x2,
  gpr_x3,
  gpr_x4,
  gpr_x5,
  gpr_x6,
  gpr_x7,
  gpr_x8,
  gpr_x9,
  gpr_x10,
  gpr_x11,
  gpr_x12,
  gpr_x13,
  gpr_x14,
  gpr_x15,
  gpr_x16,
  gpr_x17,
  gpr_x18,
  gpr_x19,
  gpr_x20,
  gpr_x21,
  gpr_x22,
  gpr_x23,
  gpr_x24,
  gpr_x25,
  gpr_x26,
  gpr_x27,
  gpr_x28,
  gpr_x29 = 29,
  gpr_fp = gpr_x29,
  gpr_x30 = 30,
  gpr_lr = gpr_x30,
  gpr_ra = gpr_x30,
  gpr_x31 = 31,
  gpr_sp = gpr_x31,
  gpr_pc = 32,
  gpr_cpsr,

  gpr_w0,
  gpr_w1,
  gpr_w2,
  gpr_w3,
  gpr_w4,
  gpr_w5,
  gpr_w6,
  gpr_w7,
  gpr_w8,
  gpr_w9,
  gpr_w10,
  gpr_w11,
  gpr_w12,
  gpr_w13,
  gpr_w14,
  gpr_w15,
  gpr_w16,
  gpr_w17,
  gpr_w18,
  gpr_w19,
  gpr_w20,
  gpr_w21,
  gpr_w22,
  gpr_w23,
  gpr_w24,
  gpr_w25,
  gpr_w26,
  gpr_w27,
  gpr_w28,

  fpu_v0,
  fpu_v1,
  fpu_v2,
  fpu_v3,
  fpu_v4,
  fpu_v5,
  fpu_v6,
  fpu_v7,
  fpu_v8,
  fpu_v9,
  fpu_v10,
  fpu_v11,
  fpu_v12,
  fpu_v13,
  fpu_v14,
  fpu_v15,
  fpu_v16,
  fpu_v17,
  fpu_v18,
  fpu_v19,
  fpu_v20,
  fpu_v21,
  fpu_v22,
  fpu_v23,
  fpu_v24,
  fpu_v25,
  fpu_v26,
  fpu_v27,
  fpu_v28,
  fpu_v29,
  fpu_v30,
  fpu_v31,

  fpu_s0,
  fpu_s1,
  fpu_s2,
  fpu_s3,
  fpu_s4,
  fpu_s5,
  fpu_s6,
  fpu_s7,
  fpu_s8,
  fpu_s9,
  fpu_s10,
  fpu_s11,
  fpu_s12,
  fpu_s13,
  fpu_s14,
  fpu_s15,
  fpu_s16,
  fpu_s17,
  fpu_s18,
  fpu_s19,
  fpu_s20,
  fpu_s21,
  fpu_s22,
  fpu_s23,
  fpu_s24,
  fpu_s25,
  fpu_s26,
  fpu_s27,
  fpu_s28,
  fpu_s29,
  fpu_s30,
  fpu_s31,

  fpu_d0,
  fpu_d1,
  fpu_d2,
  fpu_d3,
  fpu_d4,
  fpu_d5,
  fpu_d6,
  fpu_d7,
  fpu_d8,
  fpu_d9,
  fpu_d10,
  fpu_d11,
  fpu_d12,
  fpu_d13,
  fpu_d14,
  fpu_d15,
  fpu_d16,
  fpu_d17,
  fpu_d18,
  fpu_d19,
  fpu_d20,
  fpu_d21,
  fpu_d22,
  fpu_d23,
  fpu_d24,
  fpu_d25,
  fpu_d26,
  fpu_d27,
  fpu_d28,
  fpu_d29,
  fpu_d30,
  fpu_d31,

  fpu_fpsr,
  fpu_fpcr,

  exc_far,
  exc_esr,
  exc_exception,

  dbg_bvr0,
  dbg_bvr1,
  dbg_bvr2,
  dbg_bvr3,
  dbg_bvr4,
  dbg_bvr5,
  dbg_bvr6,
  dbg_bvr7,
  dbg_bvr8,
  dbg_bvr9,
  dbg_bvr10,
  dbg_bvr11,
  dbg_bvr12,
  dbg_bvr13,
  dbg_bvr14,
  dbg_bvr15,

  dbg_bcr0,
  dbg_bcr1,
  dbg_bcr2,
  dbg_bcr3,
  dbg_bcr4,
  dbg_bcr5,
  dbg_bcr6,
  dbg_bcr7,
  dbg_bcr8,
  dbg_bcr9,
  dbg_bcr10,
  dbg_bcr11,
  dbg_bcr12,
  dbg_bcr13,
  dbg_bcr14,
  dbg_bcr15,

  dbg_wvr0,
  dbg_wvr1,
  dbg_wvr2,
  dbg_wvr3,
  dbg_wvr4,
  dbg_wvr5,
  dbg_wvr6,
  dbg_wvr7,
  dbg_wvr8,
  dbg_wvr9,
  dbg_wvr10,
  dbg_wvr11,
  dbg_wvr12,
  dbg_wvr13,
  dbg_wvr14,
  dbg_wvr15,

  dbg_wcr0,
  dbg_wcr1,
  dbg_wcr2,
  dbg_wcr3,
  dbg_wcr4,
  dbg_wcr5,
  dbg_wcr6,
  dbg_wcr7,
  dbg_wcr8,
  dbg_wcr9,
  dbg_wcr10,
  dbg_wcr11,
  dbg_wcr12,
  dbg_wcr13,
  dbg_wcr14,
  dbg_wcr15,

  k_num_registers
};

static uint32_t g_contained_x0[] = {gpr_x0, LLDB_INVALID_REGNUM};
static uint32_t g_contained_x1[] = {gpr_x1, LLDB_INVALID_REGNUM};
static uint32_t g_contained_x2[] = {gpr_x2, LLDB_INVALID_REGNUM};
static uint32_t g_contained_x3[] = {gpr_x3, LLDB_INVALID_REGNUM};
static uint32_t g_contained_x4[] = {gpr_x4, LLDB_INVALID_REGNUM};
static uint32_t g_contained_x5[] = {gpr_x5, LLDB_INVALID_REGNUM};
static uint32_t g_contained_x6[] = {gpr_x6, LLDB_INVALID_REGNUM};
static uint32_t g_contained_x7[] = {gpr_x7, LLDB_INVALID_REGNUM};
static uint32_t g_contained_x8[] = {gpr_x8, LLDB_INVALID_REGNUM};
static uint32_t g_contained_x9[] = {gpr_x9, LLDB_INVALID_REGNUM};
static uint32_t g_contained_x10[] = {gpr_x10, LLDB_INVALID_REGNUM};
static uint32_t g_contained_x11[] = {gpr_x11, LLDB_INVALID_REGNUM};
static uint32_t g_contained_x12[] = {gpr_x12, LLDB_INVALID_REGNUM};
static uint32_t g_contained_x13[] = {gpr_x13, LLDB_INVALID_REGNUM};
static uint32_t g_contained_x14[] = {gpr_x14, LLDB_INVALID_REGNUM};
static uint32_t g_contained_x15[] = {gpr_x15, LLDB_INVALID_REGNUM};
static uint32_t g_contained_x16[] = {gpr_x16, LLDB_INVALID_REGNUM};
static uint32_t g_contained_x17[] = {gpr_x17, LLDB_INVALID_REGNUM};
static uint32_t g_contained_x18[] = {gpr_x18, LLDB_INVALID_REGNUM};
static uint32_t g_contained_x19[] = {gpr_x19, LLDB_INVALID_REGNUM};
static uint32_t g_contained_x20[] = {gpr_x20, LLDB_INVALID_REGNUM};
static uint32_t g_contained_x21[] = {gpr_x21, LLDB_INVALID_REGNUM};
static uint32_t g_contained_x22[] = {gpr_x22, LLDB_INVALID_REGNUM};
static uint32_t g_contained_x23[] = {gpr_x23, LLDB_INVALID_REGNUM};
static uint32_t g_contained_x24[] = {gpr_x24, LLDB_INVALID_REGNUM};
static uint32_t g_contained_x25[] = {gpr_x25, LLDB_INVALID_REGNUM};
static uint32_t g_contained_x26[] = {gpr_x26, LLDB_INVALID_REGNUM};
static uint32_t g_contained_x27[] = {gpr_x27, LLDB_INVALID_REGNUM};
static uint32_t g_contained_x28[] = {gpr_x28, LLDB_INVALID_REGNUM};

static uint32_t g_w0_invalidates[] = {gpr_x0, LLDB_INVALID_REGNUM};
static uint32_t g_w1_invalidates[] = {gpr_x1, LLDB_INVALID_REGNUM};
static uint32_t g_w2_invalidates[] = {gpr_x2, LLDB_INVALID_REGNUM};
static uint32_t g_w3_invalidates[] = {gpr_x3, LLDB_INVALID_REGNUM};
static uint32_t g_w4_invalidates[] = {gpr_x4, LLDB_INVALID_REGNUM};
static uint32_t g_w5_invalidates[] = {gpr_x5, LLDB_INVALID_REGNUM};
static uint32_t g_w6_invalidates[] = {gpr_x6, LLDB_INVALID_REGNUM};
static uint32_t g_w7_invalidates[] = {gpr_x7, LLDB_INVALID_REGNUM};
static uint32_t g_w8_invalidates[] = {gpr_x8, LLDB_INVALID_REGNUM};
static uint32_t g_w9_invalidates[] = {gpr_x9, LLDB_INVALID_REGNUM};
static uint32_t g_w10_invalidates[] = {gpr_x10, LLDB_INVALID_REGNUM};
static uint32_t g_w11_invalidates[] = {gpr_x11, LLDB_INVALID_REGNUM};
static uint32_t g_w12_invalidates[] = {gpr_x12, LLDB_INVALID_REGNUM};
static uint32_t g_w13_invalidates[] = {gpr_x13, LLDB_INVALID_REGNUM};
static uint32_t g_w14_invalidates[] = {gpr_x14, LLDB_INVALID_REGNUM};
static uint32_t g_w15_invalidates[] = {gpr_x15, LLDB_INVALID_REGNUM};
static uint32_t g_w16_invalidates[] = {gpr_x16, LLDB_INVALID_REGNUM};
static uint32_t g_w17_invalidates[] = {gpr_x17, LLDB_INVALID_REGNUM};
static uint32_t g_w18_invalidates[] = {gpr_x18, LLDB_INVALID_REGNUM};
static uint32_t g_w19_invalidates[] = {gpr_x19, LLDB_INVALID_REGNUM};
static uint32_t g_w20_invalidates[] = {gpr_x20, LLDB_INVALID_REGNUM};
static uint32_t g_w21_invalidates[] = {gpr_x21, LLDB_INVALID_REGNUM};
static uint32_t g_w22_invalidates[] = {gpr_x22, LLDB_INVALID_REGNUM};
static uint32_t g_w23_invalidates[] = {gpr_x23, LLDB_INVALID_REGNUM};
static uint32_t g_w24_invalidates[] = {gpr_x24, LLDB_INVALID_REGNUM};
static uint32_t g_w25_invalidates[] = {gpr_x25, LLDB_INVALID_REGNUM};
static uint32_t g_w26_invalidates[] = {gpr_x26, LLDB_INVALID_REGNUM};
static uint32_t g_w27_invalidates[] = {gpr_x27, LLDB_INVALID_REGNUM};
static uint32_t g_w28_invalidates[] = {gpr_x28, LLDB_INVALID_REGNUM};

static uint32_t g_contained_v0[] = {fpu_v0, LLDB_INVALID_REGNUM};
static uint32_t g_contained_v1[] = {fpu_v1, LLDB_INVALID_REGNUM};
static uint32_t g_contained_v2[] = {fpu_v2, LLDB_INVALID_REGNUM};
static uint32_t g_contained_v3[] = {fpu_v3, LLDB_INVALID_REGNUM};
static uint32_t g_contained_v4[] = {fpu_v4, LLDB_INVALID_REGNUM};
static uint32_t g_contained_v5[] = {fpu_v5, LLDB_INVALID_REGNUM};
static uint32_t g_contained_v6[] = {fpu_v6, LLDB_INVALID_REGNUM};
static uint32_t g_contained_v7[] = {fpu_v7, LLDB_INVALID_REGNUM};
static uint32_t g_contained_v8[] = {fpu_v8, LLDB_INVALID_REGNUM};
static uint32_t g_contained_v9[] = {fpu_v9, LLDB_INVALID_REGNUM};
static uint32_t g_contained_v10[] = {fpu_v10, LLDB_INVALID_REGNUM};
static uint32_t g_contained_v11[] = {fpu_v11, LLDB_INVALID_REGNUM};
static uint32_t g_contained_v12[] = {fpu_v12, LLDB_INVALID_REGNUM};
static uint32_t g_contained_v13[] = {fpu_v13, LLDB_INVALID_REGNUM};
static uint32_t g_contained_v14[] = {fpu_v14, LLDB_INVALID_REGNUM};
static uint32_t g_contained_v15[] = {fpu_v15, LLDB_INVALID_REGNUM};
static uint32_t g_contained_v16[] = {fpu_v16, LLDB_INVALID_REGNUM};
static uint32_t g_contained_v17[] = {fpu_v17, LLDB_INVALID_REGNUM};
static uint32_t g_contained_v18[] = {fpu_v18, LLDB_INVALID_REGNUM};
static uint32_t g_contained_v19[] = {fpu_v19, LLDB_INVALID_REGNUM};
static uint32_t g_contained_v20[] = {fpu_v20, LLDB_INVALID_REGNUM};
static uint32_t g_contained_v21[] = {fpu_v21, LLDB_INVALID_REGNUM};
static uint32_t g_contained_v22[] = {fpu_v22, LLDB_INVALID_REGNUM};
static uint32_t g_contained_v23[] = {fpu_v23, LLDB_INVALID_REGNUM};
static uint32_t g_contained_v24[] = {fpu_v24, LLDB_INVALID_REGNUM};
static uint32_t g_contained_v25[] = {fpu_v25, LLDB_INVALID_REGNUM};
static uint32_t g_contained_v26[] = {fpu_v26, LLDB_INVALID_REGNUM};
static uint32_t g_contained_v27[] = {fpu_v27, LLDB_INVALID_REGNUM};
static uint32_t g_contained_v28[] = {fpu_v28, LLDB_INVALID_REGNUM};
static uint32_t g_contained_v29[] = {fpu_v29, LLDB_INVALID_REGNUM};
static uint32_t g_contained_v30[] = {fpu_v30, LLDB_INVALID_REGNUM};
static uint32_t g_contained_v31[] = {fpu_v31, LLDB_INVALID_REGNUM};

static uint32_t g_s0_invalidates[] = {fpu_v0, fpu_d0, LLDB_INVALID_REGNUM};
static uint32_t g_s1_invalidates[] = {fpu_v1, fpu_d1, LLDB_INVALID_REGNUM};
static uint32_t g_s2_invalidates[] = {fpu_v2, fpu_d2, LLDB_INVALID_REGNUM};
static uint32_t g_s3_invalidates[] = {fpu_v3, fpu_d3, LLDB_INVALID_REGNUM};
static uint32_t g_s4_invalidates[] = {fpu_v4, fpu_d4, LLDB_INVALID_REGNUM};
static uint32_t g_s5_invalidates[] = {fpu_v5, fpu_d5, LLDB_INVALID_REGNUM};
static uint32_t g_s6_invalidates[] = {fpu_v6, fpu_d6, LLDB_INVALID_REGNUM};
static uint32_t g_s7_invalidates[] = {fpu_v7, fpu_d7, LLDB_INVALID_REGNUM};
static uint32_t g_s8_invalidates[] = {fpu_v8, fpu_d8, LLDB_INVALID_REGNUM};
static uint32_t g_s9_invalidates[] = {fpu_v9, fpu_d9, LLDB_INVALID_REGNUM};
static uint32_t g_s10_invalidates[] = {fpu_v10, fpu_d10, LLDB_INVALID_REGNUM};
static uint32_t g_s11_invalidates[] = {fpu_v11, fpu_d11, LLDB_INVALID_REGNUM};
static uint32_t g_s12_invalidates[] = {fpu_v12, fpu_d12, LLDB_INVALID_REGNUM};
static uint32_t g_s13_invalidates[] = {fpu_v13, fpu_d13, LLDB_INVALID_REGNUM};
static uint32_t g_s14_invalidates[] = {fpu_v14, fpu_d14, LLDB_INVALID_REGNUM};
static uint32_t g_s15_invalidates[] = {fpu_v15, fpu_d15, LLDB_INVALID_REGNUM};
static uint32_t g_s16_invalidates[] = {fpu_v16, fpu_d16, LLDB_INVALID_REGNUM};
static uint32_t g_s17_invalidates[] = {fpu_v17, fpu_d17, LLDB_INVALID_REGNUM};
static uint32_t g_s18_invalidates[] = {fpu_v18, fpu_d18, LLDB_INVALID_REGNUM};
static uint32_t g_s19_invalidates[] = {fpu_v19, fpu_d19, LLDB_INVALID_REGNUM};
static uint32_t g_s20_invalidates[] = {fpu_v20, fpu_d20, LLDB_INVALID_REGNUM};
static uint32_t g_s21_invalidates[] = {fpu_v21, fpu_d21, LLDB_INVALID_REGNUM};
static uint32_t g_s22_invalidates[] = {fpu_v22, fpu_d22, LLDB_INVALID_REGNUM};
static uint32_t g_s23_invalidates[] = {fpu_v23, fpu_d23, LLDB_INVALID_REGNUM};
static uint32_t g_s24_invalidates[] = {fpu_v24, fpu_d24, LLDB_INVALID_REGNUM};
static uint32_t g_s25_invalidates[] = {fpu_v25, fpu_d25, LLDB_INVALID_REGNUM};
static uint32_t g_s26_invalidates[] = {fpu_v26, fpu_d26, LLDB_INVALID_REGNUM};
static uint32_t g_s27_invalidates[] = {fpu_v27, fpu_d27, LLDB_INVALID_REGNUM};
static uint32_t g_s28_invalidates[] = {fpu_v28, fpu_d28, LLDB_INVALID_REGNUM};
static uint32_t g_s29_invalidates[] = {fpu_v29, fpu_d29, LLDB_INVALID_REGNUM};
static uint32_t g_s30_invalidates[] = {fpu_v30, fpu_d30, LLDB_INVALID_REGNUM};
static uint32_t g_s31_invalidates[] = {fpu_v31, fpu_d31, LLDB_INVALID_REGNUM};

static uint32_t g_d0_invalidates[] = {fpu_v0, fpu_s0, LLDB_INVALID_REGNUM};
static uint32_t g_d1_invalidates[] = {fpu_v1, fpu_s1, LLDB_INVALID_REGNUM};
static uint32_t g_d2_invalidates[] = {fpu_v2, fpu_s2, LLDB_INVALID_REGNUM};
static uint32_t g_d3_invalidates[] = {fpu_v3, fpu_s3, LLDB_INVALID_REGNUM};
static uint32_t g_d4_invalidates[] = {fpu_v4, fpu_s4, LLDB_INVALID_REGNUM};
static uint32_t g_d5_invalidates[] = {fpu_v5, fpu_s5, LLDB_INVALID_REGNUM};
static uint32_t g_d6_invalidates[] = {fpu_v6, fpu_s6, LLDB_INVALID_REGNUM};
static uint32_t g_d7_invalidates[] = {fpu_v7, fpu_s7, LLDB_INVALID_REGNUM};
static uint32_t g_d8_invalidates[] = {fpu_v8, fpu_s8, LLDB_INVALID_REGNUM};
static uint32_t g_d9_invalidates[] = {fpu_v9, fpu_s9, LLDB_INVALID_REGNUM};
static uint32_t g_d10_invalidates[] = {fpu_v10, fpu_s10, LLDB_INVALID_REGNUM};
static uint32_t g_d11_invalidates[] = {fpu_v11, fpu_s11, LLDB_INVALID_REGNUM};
static uint32_t g_d12_invalidates[] = {fpu_v12, fpu_s12, LLDB_INVALID_REGNUM};
static uint32_t g_d13_invalidates[] = {fpu_v13, fpu_s13, LLDB_INVALID_REGNUM};
static uint32_t g_d14_invalidates[] = {fpu_v14, fpu_s14, LLDB_INVALID_REGNUM};
static uint32_t g_d15_invalidates[] = {fpu_v15, fpu_s15, LLDB_INVALID_REGNUM};
static uint32_t g_d16_invalidates[] = {fpu_v16, fpu_s16, LLDB_INVALID_REGNUM};
static uint32_t g_d17_invalidates[] = {fpu_v17, fpu_s17, LLDB_INVALID_REGNUM};
static uint32_t g_d18_invalidates[] = {fpu_v18, fpu_s18, LLDB_INVALID_REGNUM};
static uint32_t g_d19_invalidates[] = {fpu_v19, fpu_s19, LLDB_INVALID_REGNUM};
static uint32_t g_d20_invalidates[] = {fpu_v20, fpu_s20, LLDB_INVALID_REGNUM};
static uint32_t g_d21_invalidates[] = {fpu_v21, fpu_s21, LLDB_INVALID_REGNUM};
static uint32_t g_d22_invalidates[] = {fpu_v22, fpu_s22, LLDB_INVALID_REGNUM};
static uint32_t g_d23_invalidates[] = {fpu_v23, fpu_s23, LLDB_INVALID_REGNUM};
static uint32_t g_d24_invalidates[] = {fpu_v24, fpu_s24, LLDB_INVALID_REGNUM};
static uint32_t g_d25_invalidates[] = {fpu_v25, fpu_s25, LLDB_INVALID_REGNUM};
static uint32_t g_d26_invalidates[] = {fpu_v26, fpu_s26, LLDB_INVALID_REGNUM};
static uint32_t g_d27_invalidates[] = {fpu_v27, fpu_s27, LLDB_INVALID_REGNUM};
static uint32_t g_d28_invalidates[] = {fpu_v28, fpu_s28, LLDB_INVALID_REGNUM};
static uint32_t g_d29_invalidates[] = {fpu_v29, fpu_s29, LLDB_INVALID_REGNUM};
static uint32_t g_d30_invalidates[] = {fpu_v30, fpu_s30, LLDB_INVALID_REGNUM};
static uint32_t g_d31_invalidates[] = {fpu_v31, fpu_s31, LLDB_INVALID_REGNUM};

static lldb_private::RegisterInfo g_register_infos_arm64_le[] = {
    // clang-format off
  // General purpose registers
  // NAME   ALT     SZ  OFFSET          ENCODING             FORMAT             EH_FRAME            DWARF             GENERIC                   PROCESS PLUGIN       LLDB      VALUE REGS      INVAL    DYNEXPR  SZ
  // =====  ======= ==  =============   ===================  ================   =================   ===============   ========================  ===================  ======    ==============  =======  =======  ==
    {"x0",  nullptr, 8, GPR_OFFSET(0),  lldb::eEncodingUint, lldb::eFormatHex, {arm64_ehframe::x0,  arm64_dwarf::x0,  LLDB_REGNUM_GENERIC_ARG1, LLDB_INVALID_REGNUM, gpr_x0},  nullptr,        nullptr, nullptr, 0},
    {"x1",  nullptr, 8, GPR_OFFSET(1),  lldb::eEncodingUint, lldb::eFormatHex, {arm64_ehframe::x1,  arm64_dwarf::x1,  LLDB_REGNUM_GENERIC_ARG2, LLDB_INVALID_REGNUM, gpr_x1},  nullptr,        nullptr, nullptr, 0},
    {"x2",  nullptr, 8, GPR_OFFSET(2),  lldb::eEncodingUint, lldb::eFormatHex, {arm64_ehframe::x2,  arm64_dwarf::x2,  LLDB_REGNUM_GENERIC_ARG3, LLDB_INVALID_REGNUM, gpr_x2},  nullptr,        nullptr, nullptr, 0},
    {"x3",  nullptr, 8, GPR_OFFSET(3),  lldb::eEncodingUint, lldb::eFormatHex, {arm64_ehframe::x3,  arm64_dwarf::x3,  LLDB_REGNUM_GENERIC_ARG4, LLDB_INVALID_REGNUM, gpr_x3},  nullptr,        nullptr, nullptr, 0},
    {"x4",  nullptr, 8, GPR_OFFSET(4),  lldb::eEncodingUint, lldb::eFormatHex, {arm64_ehframe::x4,  arm64_dwarf::x4,  LLDB_REGNUM_GENERIC_ARG5, LLDB_INVALID_REGNUM, gpr_x4},  nullptr,        nullptr, nullptr, 0},
    {"x5",  nullptr, 8, GPR_OFFSET(5),  lldb::eEncodingUint, lldb::eFormatHex, {arm64_ehframe::x5,  arm64_dwarf::x5,  LLDB_REGNUM_GENERIC_ARG6, LLDB_INVALID_REGNUM, gpr_x5},  nullptr,        nullptr, nullptr, 0},
    {"x6",  nullptr, 8, GPR_OFFSET(6),  lldb::eEncodingUint, lldb::eFormatHex, {arm64_ehframe::x6,  arm64_dwarf::x6,  LLDB_REGNUM_GENERIC_ARG7, LLDB_INVALID_REGNUM, gpr_x6},  nullptr,        nullptr, nullptr, 0},
    {"x7",  nullptr, 8, GPR_OFFSET(7),  lldb::eEncodingUint, lldb::eFormatHex, {arm64_ehframe::x7,  arm64_dwarf::x7,  LLDB_REGNUM_GENERIC_ARG8, LLDB_INVALID_REGNUM, gpr_x7},  nullptr,        nullptr, nullptr, 0},
    {"x8",  nullptr, 8, GPR_OFFSET(8),  lldb::eEncodingUint, lldb::eFormatHex, {arm64_ehframe::x8,  arm64_dwarf::x8,  LLDB_INVALID_REGNUM,      LLDB_INVALID_REGNUM, gpr_x8},  nullptr,        nullptr, nullptr, 0},
    {"x9",  nullptr, 8, GPR_OFFSET(9),  lldb::eEncodingUint, lldb::eFormatHex, {arm64_ehframe::x9,  arm64_dwarf::x9,  LLDB_INVALID_REGNUM,      LLDB_INVALID_REGNUM, gpr_x9},  nullptr,        nullptr, nullptr, 0},
    {"x10", nullptr, 8, GPR_OFFSET(10), lldb::eEncodingUint, lldb::eFormatHex, {arm64_ehframe::x10, arm64_dwarf::x10, LLDB_INVALID_REGNUM,      LLDB_INVALID_REGNUM, gpr_x10}, nullptr,        nullptr, nullptr, 0},
    {"x11", nullptr, 8, GPR_OFFSET(11), lldb::eEncodingUint, lldb::eFormatHex, {arm64_ehframe::x11, arm64_dwarf::x11, LLDB_INVALID_REGNUM,      LLDB_INVALID_REGNUM, gpr_x11}, nullptr,        nullptr, nullptr, 0},
    {"x12", nullptr, 8, GPR_OFFSET(12), lldb::eEncodingUint, lldb::eFormatHex, {arm64_ehframe::x12, arm64_dwarf::x12, LLDB_INVALID_REGNUM,      LLDB_INVALID_REGNUM, gpr_x12}, nullptr,        nullptr, nullptr, 0},
    {"x13", nullptr, 8, GPR_OFFSET(13), lldb::eEncodingUint, lldb::eFormatHex, {arm64_ehframe::x13, arm64_dwarf::x13, LLDB_INVALID_REGNUM,      LLDB_INVALID_REGNUM, gpr_x13}, nullptr,        nullptr, nullptr, 0},
    {"x14", nullptr, 8, GPR_OFFSET(14), lldb::eEncodingUint, lldb::eFormatHex, {arm64_ehframe::x14, arm64_dwarf::x14, LLDB_INVALID_REGNUM,      LLDB_INVALID_REGNUM, gpr_x14}, nullptr,        nullptr, nullptr, 0},
    {"x15", nullptr, 8, GPR_OFFSET(15), lldb::eEncodingUint, lldb::eFormatHex, {arm64_ehframe::x15, arm64_dwarf::x15, LLDB_INVALID_REGNUM,      LLDB_INVALID_REGNUM, gpr_x15}, nullptr,        nullptr, nullptr, 0},
    {"x16", nullptr, 8, GPR_OFFSET(16), lldb::eEncodingUint, lldb::eFormatHex, {arm64_ehframe::x16, arm64_dwarf::x16, LLDB_INVALID_REGNUM,      LLDB_INVALID_REGNUM, gpr_x16}, nullptr,        nullptr, nullptr, 0},
    {"x17", nullptr, 8, GPR_OFFSET(17), lldb::eEncodingUint, lldb::eFormatHex, {arm64_ehframe::x17, arm64_dwarf::x17, LLDB_INVALID_REGNUM,      LLDB_INVALID_REGNUM, gpr_x17}, nullptr,        nullptr, nullptr, 0},
    {"x18", nullptr, 8, GPR_OFFSET(18), lldb::eEncodingUint, lldb::eFormatHex, {arm64_ehframe::x18, arm64_dwarf::x18, LLDB_INVALID_REGNUM,      LLDB_INVALID_REGNUM, gpr_x18}, nullptr,        nullptr, nullptr, 0},
    {"x19", nullptr, 8, GPR_OFFSET(19), lldb::eEncodingUint, lldb::eFormatHex, {arm64_ehframe::x19, arm64_dwarf::x19, LLDB_INVALID_REGNUM,      LLDB_INVALID_REGNUM, gpr_x19}, nullptr,        nullptr, nullptr, 0},
    {"x20", nullptr, 8, GPR_OFFSET(20), lldb::eEncodingUint, lldb::eFormatHex, {arm64_ehframe::x20, arm64_dwarf::x20, LLDB_INVALID_REGNUM,      LLDB_INVALID_REGNUM, gpr_x20}, nullptr,        nullptr, nullptr, 0},
    {"x21", nullptr, 8, GPR_OFFSET(21), lldb::eEncodingUint, lldb::eFormatHex, {arm64_ehframe::x21, arm64_dwarf::x21, LLDB_INVALID_REGNUM,      LLDB_INVALID_REGNUM, gpr_x21}, nullptr,        nullptr, nullptr, 0},
    {"x22", nullptr, 8, GPR_OFFSET(22), lldb::eEncodingUint, lldb::eFormatHex, {arm64_ehframe::x22, arm64_dwarf::x22, LLDB_INVALID_REGNUM,      LLDB_INVALID_REGNUM, gpr_x22}, nullptr,        nullptr, nullptr, 0},
    {"x23", nullptr, 8, GPR_OFFSET(23), lldb::eEncodingUint, lldb::eFormatHex, {arm64_ehframe::x23, arm64_dwarf::x23, LLDB_INVALID_REGNUM,      LLDB_INVALID_REGNUM, gpr_x23}, nullptr,        nullptr, nullptr, 0},
    {"x24", nullptr, 8, GPR_OFFSET(24), lldb::eEncodingUint, lldb::eFormatHex, {arm64_ehframe::x24, arm64_dwarf::x24, LLDB_INVALID_REGNUM,      LLDB_INVALID_REGNUM, gpr_x24}, nullptr,        nullptr, nullptr, 0},
    {"x25", nullptr, 8, GPR_OFFSET(25), lldb::eEncodingUint, lldb::eFormatHex, {arm64_ehframe::x25, arm64_dwarf::x25, LLDB_INVALID_REGNUM,      LLDB_INVALID_REGNUM, gpr_x25}, nullptr,        nullptr, nullptr, 0},
    {"x26", nullptr, 8, GPR_OFFSET(26), lldb::eEncodingUint, lldb::eFormatHex, {arm64_ehframe::x26, arm64_dwarf::x26, LLDB_INVALID_REGNUM,      LLDB_INVALID_REGNUM, gpr_x26}, nullptr,        nullptr, nullptr, 0},
    {"x27", nullptr, 8, GPR_OFFSET(27), lldb::eEncodingUint, lldb::eFormatHex, {arm64_ehframe::x27, arm64_dwarf::x27, LLDB_INVALID_REGNUM,      LLDB_INVALID_REGNUM, gpr_x27}, nullptr,        nullptr, nullptr, 0},
    {"x28", nullptr, 8, GPR_OFFSET(28), lldb::eEncodingUint, lldb::eFormatHex, {arm64_ehframe::x28, arm64_dwarf::x28, LLDB_INVALID_REGNUM,      LLDB_INVALID_REGNUM, gpr_x28}, nullptr,        nullptr, nullptr, 0},
    {"fp",  "x29",   8, GPR_OFFSET(29), lldb::eEncodingUint, lldb::eFormatHex, {arm64_ehframe::fp, arm64_dwarf::fp,   LLDB_REGNUM_GENERIC_FP,   LLDB_INVALID_REGNUM, gpr_fp},  nullptr,        nullptr, nullptr, 0},
    {"lr",  "x30",   8, GPR_OFFSET(30), lldb::eEncodingUint, lldb::eFormatHex, {arm64_ehframe::lr, arm64_dwarf::lr,   LLDB_REGNUM_GENERIC_RA,   LLDB_INVALID_REGNUM, gpr_lr},  nullptr,        nullptr, nullptr, 0},
    {"sp",  "x31",   8, GPR_OFFSET(31), lldb::eEncodingUint, lldb::eFormatHex, {arm64_ehframe::sp, arm64_dwarf::sp,   LLDB_REGNUM_GENERIC_SP,   LLDB_INVALID_REGNUM, gpr_sp},  nullptr,        nullptr, nullptr, 0},
    {"pc",  nullptr, 8, GPR_OFFSET(32), lldb::eEncodingUint, lldb::eFormatHex, {arm64_ehframe::pc, arm64_dwarf::pc,   LLDB_REGNUM_GENERIC_PC,   LLDB_INVALID_REGNUM, gpr_pc},  nullptr,        nullptr, nullptr, 0},

    {"cpsr",nullptr, 4, GPR_OFFSET_NAME(cpsr), lldb::eEncodingUint, lldb::eFormatHex, {arm64_ehframe::cpsr, arm64_dwarf::cpsr, LLDB_REGNUM_GENERIC_FLAGS, LLDB_INVALID_REGNUM, gpr_cpsr}, nullptr, nullptr, nullptr, 0},

  // NAME   ALT     SZ  OFFSET                                           ENCODING             FORMAT             EH_FRAME             DWARF                GENERIC              PROCESS PLUGIN       LLDB      VALUE            INVALIDATES        DYNEXPR  SZ
  // =====  ======= ==  ==============================================   ===================  ================   =================    ===============      ===================  ===================  ======    ===============  =================  =======  ==
    {"w0",  nullptr, 4, GPR_OFFSET(0) + GPR_W_PSEUDO_REG_ENDIAN_OFFSET,  lldb::eEncodingUint, lldb::eFormatHex, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, gpr_w0},  g_contained_x0,  g_w0_invalidates,  nullptr, 0},
    {"w1",  nullptr, 4, GPR_OFFSET(1) + GPR_W_PSEUDO_REG_ENDIAN_OFFSET,  lldb::eEncodingUint, lldb::eFormatHex, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, gpr_w1},  g_contained_x1,  g_w1_invalidates,  nullptr, 0},
    {"w2",  nullptr, 4, GPR_OFFSET(2) + GPR_W_PSEUDO_REG_ENDIAN_OFFSET,  lldb::eEncodingUint, lldb::eFormatHex, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, gpr_w2},  g_contained_x2,  g_w2_invalidates,  nullptr, 0},
    {"w3",  nullptr, 4, GPR_OFFSET(3) + GPR_W_PSEUDO_REG_ENDIAN_OFFSET,  lldb::eEncodingUint, lldb::eFormatHex, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, gpr_w3},  g_contained_x3,  g_w3_invalidates,  nullptr, 0},
    {"w4",  nullptr, 4, GPR_OFFSET(4) + GPR_W_PSEUDO_REG_ENDIAN_OFFSET,  lldb::eEncodingUint, lldb::eFormatHex, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, gpr_w4},  g_contained_x4,  g_w4_invalidates,  nullptr, 0},
    {"w5",  nullptr, 4, GPR_OFFSET(5) + GPR_W_PSEUDO_REG_ENDIAN_OFFSET,  lldb::eEncodingUint, lldb::eFormatHex, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, gpr_w5},  g_contained_x5,  g_w5_invalidates,  nullptr, 0},
    {"w6",  nullptr, 4, GPR_OFFSET(6) + GPR_W_PSEUDO_REG_ENDIAN_OFFSET,  lldb::eEncodingUint, lldb::eFormatHex, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, gpr_w6},  g_contained_x6,  g_w6_invalidates,  nullptr, 0},
    {"w7",  nullptr, 4, GPR_OFFSET(7) + GPR_W_PSEUDO_REG_ENDIAN_OFFSET,  lldb::eEncodingUint, lldb::eFormatHex, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, gpr_w7},  g_contained_x7,  g_w7_invalidates,  nullptr, 0},
    {"w8",  nullptr, 4, GPR_OFFSET(8) + GPR_W_PSEUDO_REG_ENDIAN_OFFSET,  lldb::eEncodingUint, lldb::eFormatHex, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, gpr_w8},  g_contained_x8,  g_w8_invalidates,  nullptr, 0},
    {"w9",  nullptr, 4, GPR_OFFSET(9) + GPR_W_PSEUDO_REG_ENDIAN_OFFSET,  lldb::eEncodingUint, lldb::eFormatHex, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, gpr_w9},  g_contained_x9,  g_w9_invalidates,  nullptr, 0},
    {"w10", nullptr, 4, GPR_OFFSET(10) + GPR_W_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingUint, lldb::eFormatHex, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, gpr_w10}, g_contained_x10, g_w10_invalidates, nullptr, 0},
    {"w11", nullptr, 4, GPR_OFFSET(11) + GPR_W_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingUint, lldb::eFormatHex, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, gpr_w11}, g_contained_x11, g_w11_invalidates, nullptr, 0},
    {"w12", nullptr, 4, GPR_OFFSET(12) + GPR_W_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingUint, lldb::eFormatHex, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, gpr_w12}, g_contained_x12, g_w12_invalidates, nullptr, 0},
    {"w13", nullptr, 4, GPR_OFFSET(13) + GPR_W_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingUint, lldb::eFormatHex, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, gpr_w13}, g_contained_x13, g_w13_invalidates, nullptr, 0},
    {"w14", nullptr, 4, GPR_OFFSET(14) + GPR_W_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingUint, lldb::eFormatHex, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, gpr_w14}, g_contained_x14, g_w14_invalidates, nullptr, 0},
    {"w15", nullptr, 4, GPR_OFFSET(15) + GPR_W_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingUint, lldb::eFormatHex, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, gpr_w15}, g_contained_x15, g_w15_invalidates, nullptr, 0},
    {"w16", nullptr, 4, GPR_OFFSET(16) + GPR_W_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingUint, lldb::eFormatHex, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, gpr_w16}, g_contained_x16, g_w16_invalidates, nullptr, 0},
    {"w17", nullptr, 4, GPR_OFFSET(17) + GPR_W_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingUint, lldb::eFormatHex, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, gpr_w17}, g_contained_x17, g_w17_invalidates, nullptr, 0},
    {"w18", nullptr, 4, GPR_OFFSET(18) + GPR_W_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingUint, lldb::eFormatHex, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, gpr_w18}, g_contained_x18, g_w18_invalidates, nullptr, 0},
    {"w19", nullptr, 4, GPR_OFFSET(19) + GPR_W_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingUint, lldb::eFormatHex, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, gpr_w19}, g_contained_x19, g_w19_invalidates, nullptr, 0},
    {"w20", nullptr, 4, GPR_OFFSET(20) + GPR_W_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingUint, lldb::eFormatHex, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, gpr_w20}, g_contained_x20, g_w20_invalidates, nullptr, 0},
    {"w21", nullptr, 4, GPR_OFFSET(21) + GPR_W_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingUint, lldb::eFormatHex, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, gpr_w21}, g_contained_x21, g_w21_invalidates, nullptr, 0},
    {"w22", nullptr, 4, GPR_OFFSET(22) + GPR_W_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingUint, lldb::eFormatHex, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, gpr_w22}, g_contained_x22, g_w22_invalidates, nullptr, 0},
    {"w23", nullptr, 4, GPR_OFFSET(23) + GPR_W_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingUint, lldb::eFormatHex, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, gpr_w23}, g_contained_x23, g_w23_invalidates, nullptr, 0},
    {"w24", nullptr, 4, GPR_OFFSET(24) + GPR_W_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingUint, lldb::eFormatHex, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, gpr_w24}, g_contained_x24, g_w24_invalidates, nullptr, 0},
    {"w25", nullptr, 4, GPR_OFFSET(25) + GPR_W_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingUint, lldb::eFormatHex, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, gpr_w25}, g_contained_x25, g_w25_invalidates, nullptr, 0},
    {"w26", nullptr, 4, GPR_OFFSET(26) + GPR_W_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingUint, lldb::eFormatHex, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, gpr_w26}, g_contained_x26, g_w26_invalidates, nullptr, 0},
    {"w27", nullptr, 4, GPR_OFFSET(27) + GPR_W_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingUint, lldb::eFormatHex, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, gpr_w27}, g_contained_x27, g_w27_invalidates, nullptr, 0},
    {"w28", nullptr, 4, GPR_OFFSET(28) + GPR_W_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingUint, lldb::eFormatHex, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, gpr_w28}, g_contained_x28, g_w28_invalidates, nullptr, 0},

  // NAME   ALT      SZ  OFFSET         ENCODING                FORMAT                       EH_FRAME             DWARF             GENERIC              PROCESS PLUGIN       LLDB      VALUE REGS      INVAL    DYNEXPR  SZ
  // =====  =======  ==  =============  ===================     ================             =================    ===============   ===================  ===================  ======    ==============  =======  =======  ==
    {"v0",  nullptr, 16, FPU_OFFSET(0), lldb::eEncodingVector,  lldb::eFormatVectorOfUInt8, {LLDB_INVALID_REGNUM, arm64_dwarf::v0,  LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_v0},  nullptr,        nullptr, nullptr, 0},
    {"v1",  nullptr, 16, FPU_OFFSET(1), lldb::eEncodingVector,  lldb::eFormatVectorOfUInt8, {LLDB_INVALID_REGNUM, arm64_dwarf::v1,  LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_v1},  nullptr,        nullptr, nullptr, 0},
    {"v2",  nullptr, 16, FPU_OFFSET(2), lldb::eEncodingVector,  lldb::eFormatVectorOfUInt8, {LLDB_INVALID_REGNUM, arm64_dwarf::v2,  LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_v2},  nullptr,        nullptr, nullptr, 0},
    {"v3",  nullptr, 16, FPU_OFFSET(3), lldb::eEncodingVector,  lldb::eFormatVectorOfUInt8, {LLDB_INVALID_REGNUM, arm64_dwarf::v3,  LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_v3},  nullptr,        nullptr, nullptr, 0},
    {"v4",  nullptr, 16, FPU_OFFSET(4), lldb::eEncodingVector,  lldb::eFormatVectorOfUInt8, {LLDB_INVALID_REGNUM, arm64_dwarf::v4,  LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_v4},  nullptr,        nullptr, nullptr, 0},
    {"v5",  nullptr, 16, FPU_OFFSET(5), lldb::eEncodingVector,  lldb::eFormatVectorOfUInt8, {LLDB_INVALID_REGNUM, arm64_dwarf::v5,  LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_v5},  nullptr,        nullptr, nullptr, 0},
    {"v6",  nullptr, 16, FPU_OFFSET(6), lldb::eEncodingVector,  lldb::eFormatVectorOfUInt8, {LLDB_INVALID_REGNUM, arm64_dwarf::v6,  LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_v6},  nullptr,        nullptr, nullptr, 0},
    {"v7",  nullptr, 16, FPU_OFFSET(7), lldb::eEncodingVector,  lldb::eFormatVectorOfUInt8, {LLDB_INVALID_REGNUM, arm64_dwarf::v7,  LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_v7},  nullptr,        nullptr, nullptr, 0},
    {"v8",  nullptr, 16, FPU_OFFSET(8), lldb::eEncodingVector,  lldb::eFormatVectorOfUInt8, {LLDB_INVALID_REGNUM, arm64_dwarf::v8,  LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_v8},  nullptr,        nullptr, nullptr, 0},
    {"v9",  nullptr, 16, FPU_OFFSET(9), lldb::eEncodingVector,  lldb::eFormatVectorOfUInt8, {LLDB_INVALID_REGNUM, arm64_dwarf::v9,  LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_v9},  nullptr,        nullptr, nullptr, 0},
    {"v10", nullptr, 16, FPU_OFFSET(10), lldb::eEncodingVector, lldb::eFormatVectorOfUInt8, {LLDB_INVALID_REGNUM, arm64_dwarf::v10, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_v10}, nullptr,        nullptr, nullptr, 0},
    {"v11", nullptr, 16, FPU_OFFSET(11), lldb::eEncodingVector, lldb::eFormatVectorOfUInt8, {LLDB_INVALID_REGNUM, arm64_dwarf::v11, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_v11}, nullptr,        nullptr, nullptr, 0},
    {"v12", nullptr, 16, FPU_OFFSET(12), lldb::eEncodingVector, lldb::eFormatVectorOfUInt8, {LLDB_INVALID_REGNUM, arm64_dwarf::v12, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_v12}, nullptr,        nullptr, nullptr, 0},
    {"v13", nullptr, 16, FPU_OFFSET(13), lldb::eEncodingVector, lldb::eFormatVectorOfUInt8, {LLDB_INVALID_REGNUM, arm64_dwarf::v13, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_v13}, nullptr,        nullptr, nullptr, 0},
    {"v14", nullptr, 16, FPU_OFFSET(14), lldb::eEncodingVector, lldb::eFormatVectorOfUInt8, {LLDB_INVALID_REGNUM, arm64_dwarf::v14, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_v14}, nullptr,        nullptr, nullptr, 0},
    {"v15", nullptr, 16, FPU_OFFSET(15), lldb::eEncodingVector, lldb::eFormatVectorOfUInt8, {LLDB_INVALID_REGNUM, arm64_dwarf::v15, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_v15}, nullptr,        nullptr, nullptr, 0},
    {"v16", nullptr, 16, FPU_OFFSET(16), lldb::eEncodingVector, lldb::eFormatVectorOfUInt8, {LLDB_INVALID_REGNUM, arm64_dwarf::v16, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_v16}, nullptr,        nullptr, nullptr, 0},
    {"v17", nullptr, 16, FPU_OFFSET(17), lldb::eEncodingVector, lldb::eFormatVectorOfUInt8, {LLDB_INVALID_REGNUM, arm64_dwarf::v17, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_v17}, nullptr,        nullptr, nullptr, 0},
    {"v18", nullptr, 16, FPU_OFFSET(18), lldb::eEncodingVector, lldb::eFormatVectorOfUInt8, {LLDB_INVALID_REGNUM, arm64_dwarf::v18, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_v18}, nullptr,        nullptr, nullptr, 0},
    {"v19", nullptr, 16, FPU_OFFSET(19), lldb::eEncodingVector, lldb::eFormatVectorOfUInt8, {LLDB_INVALID_REGNUM, arm64_dwarf::v19, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_v19}, nullptr,        nullptr, nullptr, 0},
    {"v20", nullptr, 16, FPU_OFFSET(20), lldb::eEncodingVector, lldb::eFormatVectorOfUInt8, {LLDB_INVALID_REGNUM, arm64_dwarf::v20, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_v20}, nullptr,        nullptr, nullptr, 0},
    {"v21", nullptr, 16, FPU_OFFSET(21), lldb::eEncodingVector, lldb::eFormatVectorOfUInt8, {LLDB_INVALID_REGNUM, arm64_dwarf::v21, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_v21}, nullptr,        nullptr, nullptr, 0},
    {"v22", nullptr, 16, FPU_OFFSET(22), lldb::eEncodingVector, lldb::eFormatVectorOfUInt8, {LLDB_INVALID_REGNUM, arm64_dwarf::v22, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_v22}, nullptr,        nullptr, nullptr, 0},
    {"v23", nullptr, 16, FPU_OFFSET(23), lldb::eEncodingVector, lldb::eFormatVectorOfUInt8, {LLDB_INVALID_REGNUM, arm64_dwarf::v23, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_v23}, nullptr,        nullptr, nullptr, 0},
    {"v24", nullptr, 16, FPU_OFFSET(24), lldb::eEncodingVector, lldb::eFormatVectorOfUInt8, {LLDB_INVALID_REGNUM, arm64_dwarf::v24, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_v24}, nullptr,        nullptr, nullptr, 0},
    {"v25", nullptr, 16, FPU_OFFSET(25), lldb::eEncodingVector, lldb::eFormatVectorOfUInt8, {LLDB_INVALID_REGNUM, arm64_dwarf::v25, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_v25}, nullptr,        nullptr, nullptr, 0},
    {"v26", nullptr, 16, FPU_OFFSET(26), lldb::eEncodingVector, lldb::eFormatVectorOfUInt8, {LLDB_INVALID_REGNUM, arm64_dwarf::v26, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_v26}, nullptr,        nullptr, nullptr, 0},
    {"v27", nullptr, 16, FPU_OFFSET(27), lldb::eEncodingVector, lldb::eFormatVectorOfUInt8, {LLDB_INVALID_REGNUM, arm64_dwarf::v27, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_v27}, nullptr,        nullptr, nullptr, 0},
    {"v28", nullptr, 16, FPU_OFFSET(28), lldb::eEncodingVector, lldb::eFormatVectorOfUInt8, {LLDB_INVALID_REGNUM, arm64_dwarf::v28, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_v28}, nullptr,        nullptr, nullptr, 0},
    {"v29", nullptr, 16, FPU_OFFSET(29), lldb::eEncodingVector, lldb::eFormatVectorOfUInt8, {LLDB_INVALID_REGNUM, arm64_dwarf::v29, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_v29}, nullptr,        nullptr, nullptr, 0},
    {"v30", nullptr, 16, FPU_OFFSET(30), lldb::eEncodingVector, lldb::eFormatVectorOfUInt8, {LLDB_INVALID_REGNUM, arm64_dwarf::v30, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_v30}, nullptr,        nullptr, nullptr, 0},
    {"v31", nullptr, 16, FPU_OFFSET(31), lldb::eEncodingVector, lldb::eFormatVectorOfUInt8, {LLDB_INVALID_REGNUM, arm64_dwarf::v31, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_v31}, nullptr,        nullptr, nullptr, 0},

  // NAME   ALT     SZ  OFFSET                                           ENCODING                FORMAT               EH_FRAME             DWARF                GENERIC              PROCESS PLUGIN       LLDB      VALUE REGS       INVALIDATES        DYNEXPR  SZ
  // =====  ======= ==  ==============================================   ===================     ================     =================    ===============      ===================  ===================  ======    ===============  =================  =======  ==
    {"s0",  nullptr, 4, FPU_OFFSET(0)  + FPU_S_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_s0},  g_contained_v0,  g_s0_invalidates,  nullptr, 0},
    {"s1",  nullptr, 4, FPU_OFFSET(1)  + FPU_S_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_s1},  g_contained_v1,  g_s1_invalidates,  nullptr, 0},
    {"s2",  nullptr, 4, FPU_OFFSET(2)  + FPU_S_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_s2},  g_contained_v2,  g_s2_invalidates,  nullptr, 0},
    {"s3",  nullptr, 4, FPU_OFFSET(3)  + FPU_S_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_s3},  g_contained_v3,  g_s3_invalidates,  nullptr, 0},
    {"s4",  nullptr, 4, FPU_OFFSET(4)  + FPU_S_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_s4},  g_contained_v4,  g_s4_invalidates,  nullptr, 0},
    {"s5",  nullptr, 4, FPU_OFFSET(5)  + FPU_S_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_s5},  g_contained_v5,  g_s5_invalidates,  nullptr, 0},
    {"s6",  nullptr, 4, FPU_OFFSET(6)  + FPU_S_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_s6},  g_contained_v6,  g_s6_invalidates,  nullptr, 0},
    {"s7",  nullptr, 4, FPU_OFFSET(7)  + FPU_S_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_s7},  g_contained_v7,  g_s7_invalidates,  nullptr, 0},
    {"s8",  nullptr, 4, FPU_OFFSET(8)  + FPU_S_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_s8},  g_contained_v8,  g_s8_invalidates,  nullptr, 0},
    {"s9",  nullptr, 4, FPU_OFFSET(9)  + FPU_S_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_s9},  g_contained_v9,  g_s9_invalidates,  nullptr, 0},
    {"s10", nullptr, 4, FPU_OFFSET(10) + FPU_S_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_s10}, g_contained_v10, g_s10_invalidates, nullptr, 0},
    {"s11", nullptr, 4, FPU_OFFSET(11) + FPU_S_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_s11}, g_contained_v11, g_s11_invalidates, nullptr, 0},
    {"s12", nullptr, 4, FPU_OFFSET(12) + FPU_S_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_s12}, g_contained_v12, g_s12_invalidates, nullptr, 0},
    {"s13", nullptr, 4, FPU_OFFSET(13) + FPU_S_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_s13}, g_contained_v13, g_s13_invalidates, nullptr, 0},
    {"s14", nullptr, 4, FPU_OFFSET(14) + FPU_S_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_s14}, g_contained_v14, g_s14_invalidates, nullptr, 0},
    {"s15", nullptr, 4, FPU_OFFSET(15) + FPU_S_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_s15}, g_contained_v15, g_s15_invalidates, nullptr, 0},
    {"s16", nullptr, 4, FPU_OFFSET(16) + FPU_S_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_s16}, g_contained_v16, g_s16_invalidates, nullptr, 0},
    {"s17", nullptr, 4, FPU_OFFSET(17) + FPU_S_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_s17}, g_contained_v17, g_s17_invalidates, nullptr, 0},
    {"s18", nullptr, 4, FPU_OFFSET(18) + FPU_S_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_s18}, g_contained_v18, g_s18_invalidates, nullptr, 0},
    {"s19", nullptr, 4, FPU_OFFSET(19) + FPU_S_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_s19}, g_contained_v19, g_s19_invalidates, nullptr, 0},
    {"s20", nullptr, 4, FPU_OFFSET(20) + FPU_S_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_s20}, g_contained_v20, g_s20_invalidates, nullptr, 0},
    {"s21", nullptr, 4, FPU_OFFSET(21) + FPU_S_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_s21}, g_contained_v21, g_s21_invalidates, nullptr, 0},
    {"s22", nullptr, 4, FPU_OFFSET(22) + FPU_S_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_s22}, g_contained_v22, g_s22_invalidates, nullptr, 0},
    {"s23", nullptr, 4, FPU_OFFSET(23) + FPU_S_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_s23}, g_contained_v23, g_s23_invalidates, nullptr, 0},
    {"s24", nullptr, 4, FPU_OFFSET(24) + FPU_S_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_s24}, g_contained_v24, g_s24_invalidates, nullptr, 0},
    {"s25", nullptr, 4, FPU_OFFSET(25) + FPU_S_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_s25}, g_contained_v25, g_s25_invalidates, nullptr, 0},
    {"s26", nullptr, 4, FPU_OFFSET(26) + FPU_S_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_s26}, g_contained_v26, g_s26_invalidates, nullptr, 0},
    {"s27", nullptr, 4, FPU_OFFSET(27) + FPU_S_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_s27}, g_contained_v27, g_s27_invalidates, nullptr, 0},
    {"s28", nullptr, 4, FPU_OFFSET(28) + FPU_S_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_s28}, g_contained_v28, g_s28_invalidates, nullptr, 0},
    {"s29", nullptr, 4, FPU_OFFSET(29) + FPU_S_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_s29}, g_contained_v29, g_s29_invalidates, nullptr, 0},
    {"s30", nullptr, 4, FPU_OFFSET(30) + FPU_S_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_s30}, g_contained_v30, g_s30_invalidates, nullptr, 0},
    {"s31", nullptr, 4, FPU_OFFSET(31) + FPU_S_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_s31}, g_contained_v31, g_s31_invalidates, nullptr, 0},

    {"d0",  nullptr, 8, FPU_OFFSET(0)  + FPU_D_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_d0},  g_contained_v0,  g_d0_invalidates,  nullptr, 0},
    {"d1",  nullptr, 8, FPU_OFFSET(1)  + FPU_D_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_d1},  g_contained_v1,  g_d1_invalidates,  nullptr, 0},
    {"d2",  nullptr, 8, FPU_OFFSET(2)  + FPU_D_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_d2},  g_contained_v2,  g_d2_invalidates,  nullptr, 0},
    {"d3",  nullptr, 8, FPU_OFFSET(3)  + FPU_D_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_d3},  g_contained_v3,  g_d3_invalidates,  nullptr, 0},
    {"d4",  nullptr, 8, FPU_OFFSET(4)  + FPU_D_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_d4},  g_contained_v4,  g_d4_invalidates,  nullptr, 0},
    {"d5",  nullptr, 8, FPU_OFFSET(5)  + FPU_D_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_d5},  g_contained_v5,  g_d5_invalidates,  nullptr, 0},
    {"d6",  nullptr, 8, FPU_OFFSET(6)  + FPU_D_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_d6},  g_contained_v6,  g_d6_invalidates,  nullptr, 0},
    {"d7",  nullptr, 8, FPU_OFFSET(7)  + FPU_D_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_d7},  g_contained_v7,  g_d7_invalidates,  nullptr, 0},
    {"d8",  nullptr, 8, FPU_OFFSET(8)  + FPU_D_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_d8},  g_contained_v8,  g_d8_invalidates,  nullptr, 0},
    {"d9",  nullptr, 8, FPU_OFFSET(9)  + FPU_D_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_d9},  g_contained_v9,  g_d9_invalidates,  nullptr, 0},
    {"d10", nullptr, 8, FPU_OFFSET(10) + FPU_D_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_d10}, g_contained_v10, g_d10_invalidates, nullptr, 0},
    {"d11", nullptr, 8, FPU_OFFSET(11) + FPU_D_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_d11}, g_contained_v11, g_d11_invalidates, nullptr, 0},
    {"d12", nullptr, 8, FPU_OFFSET(12) + FPU_D_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_d12}, g_contained_v12, g_d12_invalidates, nullptr, 0},
    {"d13", nullptr, 8, FPU_OFFSET(13) + FPU_D_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_d13}, g_contained_v13, g_d13_invalidates, nullptr, 0},
    {"d14", nullptr, 8, FPU_OFFSET(14) + FPU_D_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_d14}, g_contained_v14, g_d14_invalidates, nullptr, 0},
    {"d15", nullptr, 8, FPU_OFFSET(15) + FPU_D_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_d15}, g_contained_v15, g_d15_invalidates, nullptr, 0},
    {"d16", nullptr, 8, FPU_OFFSET(16) + FPU_D_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_d16}, g_contained_v16, g_d16_invalidates, nullptr, 0},
    {"d17", nullptr, 8, FPU_OFFSET(17) + FPU_D_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_d17}, g_contained_v17, g_d17_invalidates, nullptr, 0},
    {"d18", nullptr, 8, FPU_OFFSET(18) + FPU_D_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_d18}, g_contained_v18, g_d18_invalidates, nullptr, 0},
    {"d19", nullptr, 8, FPU_OFFSET(19) + FPU_D_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_d19}, g_contained_v19, g_d19_invalidates, nullptr, 0},
    {"d20", nullptr, 8, FPU_OFFSET(20) + FPU_D_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_d20}, g_contained_v20, g_d20_invalidates, nullptr, 0},
    {"d21", nullptr, 8, FPU_OFFSET(21) + FPU_D_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_d21}, g_contained_v21, g_d21_invalidates, nullptr, 0},
    {"d22", nullptr, 8, FPU_OFFSET(22) + FPU_D_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_d22}, g_contained_v22, g_d22_invalidates, nullptr, 0},
    {"d23", nullptr, 8, FPU_OFFSET(23) + FPU_D_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_d23}, g_contained_v23, g_d23_invalidates, nullptr, 0},
    {"d24", nullptr, 8, FPU_OFFSET(24) + FPU_D_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_d24}, g_contained_v24, g_d24_invalidates, nullptr, 0},
    {"d25", nullptr, 8, FPU_OFFSET(25) + FPU_D_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_d25}, g_contained_v25, g_d25_invalidates, nullptr, 0},
    {"d26", nullptr, 8, FPU_OFFSET(26) + FPU_D_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_d26}, g_contained_v26, g_d26_invalidates, nullptr, 0},
    {"d27", nullptr, 8, FPU_OFFSET(27) + FPU_D_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_d27}, g_contained_v27, g_d27_invalidates, nullptr, 0},
    {"d28", nullptr, 8, FPU_OFFSET(28) + FPU_D_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_d28}, g_contained_v28, g_d28_invalidates, nullptr, 0},
    {"d29", nullptr, 8, FPU_OFFSET(29) + FPU_D_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_d29}, g_contained_v29, g_d29_invalidates, nullptr, 0},
    {"d30", nullptr, 8, FPU_OFFSET(30) + FPU_D_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_d30}, g_contained_v30, g_d30_invalidates, nullptr, 0},
    {"d31", nullptr, 8, FPU_OFFSET(31) + FPU_D_PSEUDO_REG_ENDIAN_OFFSET, lldb::eEncodingIEEE754, lldb::eFormatFloat, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_d31}, g_contained_v31, g_d31_invalidates, nullptr, 0},

    {"fpsr", nullptr, 4, FPU_OFFSET_NAME(fpsr), lldb::eEncodingUint, lldb::eFormatHex, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_fpsr}, nullptr, nullptr, nullptr, 0},
    {"fpcr", nullptr, 4, FPU_OFFSET_NAME(fpcr), lldb::eEncodingUint, lldb::eFormatHex, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, fpu_fpcr}, nullptr, nullptr, nullptr, 0},

    {"far", nullptr, 8, EXC_OFFSET_NAME(far), lldb::eEncodingUint, lldb::eFormatHex, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, exc_far}, nullptr, nullptr, nullptr, 0},
    {"esr", nullptr, 4, EXC_OFFSET_NAME(esr), lldb::eEncodingUint, lldb::eFormatHex, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, exc_esr}, nullptr, nullptr, nullptr, 0},
    {"exception", nullptr, 4, EXC_OFFSET_NAME(exception), lldb::eEncodingUint, lldb::eFormatHex, {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, exc_exception}, nullptr, nullptr, nullptr, 0},

    {DEFINE_DBG(bvr, 0)},
    {DEFINE_DBG(bvr, 1)},
    {DEFINE_DBG(bvr, 2)},
    {DEFINE_DBG(bvr, 3)},
    {DEFINE_DBG(bvr, 4)},
    {DEFINE_DBG(bvr, 5)},
    {DEFINE_DBG(bvr, 6)},
    {DEFINE_DBG(bvr, 7)},
    {DEFINE_DBG(bvr, 8)},
    {DEFINE_DBG(bvr, 9)},
    {DEFINE_DBG(bvr, 10)},
    {DEFINE_DBG(bvr, 11)},
    {DEFINE_DBG(bvr, 12)},
    {DEFINE_DBG(bvr, 13)},
    {DEFINE_DBG(bvr, 14)},
    {DEFINE_DBG(bvr, 15)},

    {DEFINE_DBG(bcr, 0)},
    {DEFINE_DBG(bcr, 1)},
    {DEFINE_DBG(bcr, 2)},
    {DEFINE_DBG(bcr, 3)},
    {DEFINE_DBG(bcr, 4)},
    {DEFINE_DBG(bcr, 5)},
    {DEFINE_DBG(bcr, 6)},
    {DEFINE_DBG(bcr, 7)},
    {DEFINE_DBG(bcr, 8)},
    {DEFINE_DBG(bcr, 9)},
    {DEFINE_DBG(bcr, 10)},
    {DEFINE_DBG(bcr, 11)},
    {DEFINE_DBG(bcr, 12)},
    {DEFINE_DBG(bcr, 13)},
    {DEFINE_DBG(bcr, 14)},
    {DEFINE_DBG(bcr, 15)},

    {DEFINE_DBG(wvr, 0)},
    {DEFINE_DBG(wvr, 1)},
    {DEFINE_DBG(wvr, 2)},
    {DEFINE_DBG(wvr, 3)},
    {DEFINE_DBG(wvr, 4)},
    {DEFINE_DBG(wvr, 5)},
    {DEFINE_DBG(wvr, 6)},
    {DEFINE_DBG(wvr, 7)},
    {DEFINE_DBG(wvr, 8)},
    {DEFINE_DBG(wvr, 9)},
    {DEFINE_DBG(wvr, 10)},
    {DEFINE_DBG(wvr, 11)},
    {DEFINE_DBG(wvr, 12)},
    {DEFINE_DBG(wvr, 13)},
    {DEFINE_DBG(wvr, 14)},
    {DEFINE_DBG(wvr, 15)},

    {DEFINE_DBG(wcr, 0)},
    {DEFINE_DBG(wcr, 1)},
    {DEFINE_DBG(wcr, 2)},
    {DEFINE_DBG(wcr, 3)},
    {DEFINE_DBG(wcr, 4)},
    {DEFINE_DBG(wcr, 5)},
    {DEFINE_DBG(wcr, 6)},
    {DEFINE_DBG(wcr, 7)},
    {DEFINE_DBG(wcr, 8)},
    {DEFINE_DBG(wcr, 9)},
    {DEFINE_DBG(wcr, 10)},
    {DEFINE_DBG(wcr, 11)},
    {DEFINE_DBG(wcr, 12)},
    {DEFINE_DBG(wcr, 13)},
    {DEFINE_DBG(wcr, 14)},
    {DEFINE_DBG(wcr, 15)}
    // clang-format on
};

#endif // DECLARE_REGISTER_INFOS_ARM64_STRUCT
