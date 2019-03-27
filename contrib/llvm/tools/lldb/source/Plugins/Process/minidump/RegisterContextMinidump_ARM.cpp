//===-- RegisterContextMinidump_ARM.cpp -------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "RegisterContextMinidump_ARM.h"

#include "Utility/ARM_DWARF_Registers.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/LLDBAssert.h"
#include "lldb/lldb-enumerations.h"

// C includes
#include <assert.h>

// C++ includes

using namespace lldb;
using namespace lldb_private;
using namespace minidump;

#define INV LLDB_INVALID_REGNUM
#define OFFSET(r) (offsetof(RegisterContextMinidump_ARM::Context, r))

#define DEF_R(i)                                                               \
  {                                                                            \
    "r" #i, nullptr, 4, OFFSET(r) + i * 4, eEncodingUint, eFormatHex,          \
        {INV, dwarf_r##i, INV, INV, reg_r##i}, nullptr, nullptr, nullptr, 0    \
  }

#define DEF_R_ARG(i, n)                                                        \
  {                                                                            \
    "r" #i, "arg" #n, 4, OFFSET(r) + i * 4, eEncodingUint, eFormatHex,         \
        {INV, dwarf_r##i, LLDB_REGNUM_GENERIC_ARG1 + i, INV, reg_r##i},        \
        nullptr, nullptr, nullptr, 0                                           \
  }

#define DEF_D(i)                                                               \
  {                                                                            \
    "d" #i, nullptr, 8, OFFSET(d) + i * 8, eEncodingVector,                    \
        eFormatVectorOfUInt8, {INV, dwarf_d##i, INV, INV, reg_d##i},           \
        nullptr, nullptr, nullptr, 0    \
  }

#define DEF_S(i)                                                               \
  {                                                                            \
    "s" #i, nullptr, 4, OFFSET(s) + i * 4, eEncodingIEEE754, eFormatFloat,     \
        {INV, dwarf_s##i, INV, INV, reg_s##i}, nullptr, nullptr, nullptr, 0    \
  }

#define DEF_Q(i)                                                               \
  {                                                                            \
    "q" #i, nullptr, 16, OFFSET(q) + i * 16, eEncodingVector,                  \
        eFormatVectorOfUInt8, {INV, dwarf_q##i, INV, INV, reg_q##i},           \
        nullptr, nullptr, nullptr, 0    \
  }

// Zero based LLDB register numbers for this register context
enum {
  // General Purpose Registers
  reg_r0,
  reg_r1,
  reg_r2,
  reg_r3,
  reg_r4,
  reg_r5,
  reg_r6,
  reg_r7,
  reg_r8,
  reg_r9,
  reg_r10,
  reg_r11,
  reg_r12,
  reg_sp,
  reg_lr,
  reg_pc,
  reg_cpsr,
  // Floating Point Registers
  reg_fpscr,
  reg_d0,
  reg_d1,
  reg_d2,
  reg_d3,
  reg_d4,
  reg_d5,
  reg_d6,
  reg_d7,
  reg_d8,
  reg_d9,
  reg_d10,
  reg_d11,
  reg_d12,
  reg_d13,
  reg_d14,
  reg_d15,
  reg_d16,
  reg_d17,
  reg_d18,
  reg_d19,
  reg_d20,
  reg_d21,
  reg_d22,
  reg_d23,
  reg_d24,
  reg_d25,
  reg_d26,
  reg_d27,
  reg_d28,
  reg_d29,
  reg_d30,
  reg_d31,
  reg_s0,
  reg_s1,
  reg_s2,
  reg_s3,
  reg_s4,
  reg_s5,
  reg_s6,
  reg_s7,
  reg_s8,
  reg_s9,
  reg_s10,
  reg_s11,
  reg_s12,
  reg_s13,
  reg_s14,
  reg_s15,
  reg_s16,
  reg_s17,
  reg_s18,
  reg_s19,
  reg_s20,
  reg_s21,
  reg_s22,
  reg_s23,
  reg_s24,
  reg_s25,
  reg_s26,
  reg_s27,
  reg_s28,
  reg_s29,
  reg_s30,
  reg_s31,
  reg_q0,
  reg_q1,
  reg_q2,
  reg_q3,
  reg_q4,
  reg_q5,
  reg_q6,
  reg_q7,
  reg_q8,
  reg_q9,
  reg_q10,
  reg_q11,
  reg_q12,
  reg_q13,
  reg_q14,
  reg_q15,
  k_num_regs
};

static RegisterInfo g_reg_info_apple_fp = {
    "fp",
    "r7",
    4,
    OFFSET(r) + 7 * 4,
    eEncodingUint,
    eFormatHex,
    {INV, dwarf_r7, LLDB_REGNUM_GENERIC_FP, INV, reg_r7},
    nullptr,
    nullptr,
    nullptr,
    0};

static RegisterInfo g_reg_info_fp = {
    "fp",
    "r11",
    4,
    OFFSET(r) + 11 * 4,
    eEncodingUint,
    eFormatHex,
    {INV, dwarf_r11, LLDB_REGNUM_GENERIC_FP, INV, reg_r11},
    nullptr,
    nullptr,
    nullptr,
    0};

// Register info definitions for this register context
static RegisterInfo g_reg_infos[] = {
    DEF_R_ARG(0, 1),
    DEF_R_ARG(1, 2),
    DEF_R_ARG(2, 3),
    DEF_R_ARG(3, 4),
    DEF_R(4),
    DEF_R(5),
    DEF_R(6),
    DEF_R(7),
    DEF_R(8),
    DEF_R(9),
    DEF_R(10),
    DEF_R(11),
    DEF_R(12),
    {"sp",
     "r13",
     4,
     OFFSET(r) + 13 * 4,
     eEncodingUint,
     eFormatHex,
     {INV, dwarf_sp, LLDB_REGNUM_GENERIC_SP, INV, reg_sp},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"lr",
     "r14",
     4,
     OFFSET(r) + 14 * 4,
     eEncodingUint,
     eFormatHex,
     {INV, dwarf_lr, LLDB_REGNUM_GENERIC_RA, INV, reg_lr},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"pc",
     "r15",
     4,
     OFFSET(r) + 15 * 4,
     eEncodingUint,
     eFormatHex,
     {INV, dwarf_pc, LLDB_REGNUM_GENERIC_PC, INV, reg_pc},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"cpsr",
     "psr",
     4,
     OFFSET(cpsr),
     eEncodingUint,
     eFormatHex,
     {INV, dwarf_cpsr, LLDB_REGNUM_GENERIC_FLAGS, INV, reg_cpsr},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"fpscr",
     nullptr,
     8,
     OFFSET(fpscr),
     eEncodingUint,
     eFormatHex,
     {INV, INV, INV, INV, reg_fpscr},
     nullptr,
     nullptr,
     nullptr,
     0},
    DEF_D(0),
    DEF_D(1),
    DEF_D(2),
    DEF_D(3),
    DEF_D(4),
    DEF_D(5),
    DEF_D(6),
    DEF_D(7),
    DEF_D(8),
    DEF_D(9),
    DEF_D(10),
    DEF_D(11),
    DEF_D(12),
    DEF_D(13),
    DEF_D(14),
    DEF_D(15),
    DEF_D(16),
    DEF_D(17),
    DEF_D(18),
    DEF_D(19),
    DEF_D(20),
    DEF_D(21),
    DEF_D(22),
    DEF_D(23),
    DEF_D(24),
    DEF_D(25),
    DEF_D(26),
    DEF_D(27),
    DEF_D(28),
    DEF_D(29),
    DEF_D(30),
    DEF_D(31),
    DEF_S(0),
    DEF_S(1),
    DEF_S(2),
    DEF_S(3),
    DEF_S(4),
    DEF_S(5),
    DEF_S(6),
    DEF_S(7),
    DEF_S(8),
    DEF_S(9),
    DEF_S(10),
    DEF_S(11),
    DEF_S(12),
    DEF_S(13),
    DEF_S(14),
    DEF_S(15),
    DEF_S(16),
    DEF_S(17),
    DEF_S(18),
    DEF_S(19),
    DEF_S(20),
    DEF_S(21),
    DEF_S(22),
    DEF_S(23),
    DEF_S(24),
    DEF_S(25),
    DEF_S(26),
    DEF_S(27),
    DEF_S(28),
    DEF_S(29),
    DEF_S(30),
    DEF_S(31),
    DEF_Q(0),
    DEF_Q(1),
    DEF_Q(2),
    DEF_Q(3),
    DEF_Q(4),
    DEF_Q(5),
    DEF_Q(6),
    DEF_Q(7),
    DEF_Q(8),
    DEF_Q(9),
    DEF_Q(10),
    DEF_Q(11),
    DEF_Q(12),
    DEF_Q(13),
    DEF_Q(14),
    DEF_Q(15)};

constexpr size_t k_num_reg_infos = llvm::array_lengthof(g_reg_infos);

// ARM general purpose registers.
const uint32_t g_gpr_regnums[] = {
    reg_r0,
    reg_r1,
    reg_r2,
    reg_r3,
    reg_r4,
    reg_r5,
    reg_r6,
    reg_r7,
    reg_r8,
    reg_r9,
    reg_r10,
    reg_r11,
    reg_r12,
    reg_sp,
    reg_lr,
    reg_pc,
    reg_cpsr,
    LLDB_INVALID_REGNUM // register sets need to end with this flag
};
const uint32_t g_fpu_regnums[] = {
    reg_fpscr,
    reg_d0,
    reg_d1,
    reg_d2,
    reg_d3,
    reg_d4,
    reg_d5,
    reg_d6,
    reg_d7,
    reg_d8,
    reg_d9,
    reg_d10,
    reg_d11,
    reg_d12,
    reg_d13,
    reg_d14,
    reg_d15,
    reg_d16,
    reg_d17,
    reg_d18,
    reg_d19,
    reg_d20,
    reg_d21,
    reg_d22,
    reg_d23,
    reg_d24,
    reg_d25,
    reg_d26,
    reg_d27,
    reg_d28,
    reg_d29,
    reg_d30,
    reg_d31,
    reg_s0,
    reg_s1,
    reg_s2,
    reg_s3,
    reg_s4,
    reg_s5,
    reg_s6,
    reg_s7,
    reg_s8,
    reg_s9,
    reg_s10,
    reg_s11,
    reg_s12,
    reg_s13,
    reg_s14,
    reg_s15,
    reg_s16,
    reg_s17,
    reg_s18,
    reg_s19,
    reg_s20,
    reg_s21,
    reg_s22,
    reg_s23,
    reg_s24,
    reg_s25,
    reg_s26,
    reg_s27,
    reg_s28,
    reg_s29,
    reg_s30,
    reg_s31,
    reg_q0,
    reg_q1,
    reg_q2,
    reg_q3,
    reg_q4,
    reg_q5,
    reg_q6,
    reg_q7,
    reg_q8,
    reg_q9,
    reg_q10,
    reg_q11,
    reg_q12,
    reg_q13,
    reg_q14,
    reg_q15,
    LLDB_INVALID_REGNUM // register sets need to end with this flag
};

// Skip the last LLDB_INVALID_REGNUM in each count below by subtracting 1
constexpr size_t k_num_gpr_regs = llvm::array_lengthof(g_gpr_regnums) - 1;
constexpr size_t k_num_fpu_regs = llvm::array_lengthof(g_fpu_regnums) - 1;

static RegisterSet g_reg_sets[] = {
    {"General Purpose Registers", "gpr", k_num_gpr_regs, g_gpr_regnums},
    {"Floating Point Registers", "fpu", k_num_fpu_regs, g_fpu_regnums},
};

constexpr size_t k_num_reg_sets = llvm::array_lengthof(g_reg_sets);

RegisterContextMinidump_ARM::RegisterContextMinidump_ARM(
    Thread &thread, const DataExtractor &data, bool apple)
    : RegisterContext(thread, 0), m_apple(apple) {
  lldb::offset_t offset = 0;
  m_regs.context_flags = data.GetU32(&offset);
  for (unsigned i = 0; i < llvm::array_lengthof(m_regs.r); ++i)
    m_regs.r[i] = data.GetU32(&offset);
  m_regs.cpsr = data.GetU32(&offset);
  m_regs.fpscr = data.GetU64(&offset);
  for (unsigned i = 0; i < llvm::array_lengthof(m_regs.d); ++i)
    m_regs.d[i] = data.GetU64(&offset);
  lldbassert(k_num_regs == k_num_reg_infos);
}

size_t RegisterContextMinidump_ARM::GetRegisterCount() { return k_num_regs; }

const RegisterInfo *
RegisterContextMinidump_ARM::GetRegisterInfoAtIndex(size_t reg) {
  if (reg < k_num_reg_infos) {
    if (m_apple) {
      if (reg == reg_r7)
        return &g_reg_info_apple_fp;
    } else {
      if (reg == reg_r11)
        return &g_reg_info_fp;
    }
    return &g_reg_infos[reg];
  }
  return nullptr;
}

size_t RegisterContextMinidump_ARM::GetRegisterSetCount() {
  return k_num_reg_sets;
}

const RegisterSet *RegisterContextMinidump_ARM::GetRegisterSet(size_t set) {
  if (set < k_num_reg_sets)
    return &g_reg_sets[set];
  return nullptr;
}

const char *RegisterContextMinidump_ARM::GetRegisterName(unsigned reg) {
  if (reg < k_num_reg_infos)
    return g_reg_infos[reg].name;
  return nullptr;
}

bool RegisterContextMinidump_ARM::ReadRegister(const RegisterInfo *reg_info,
                                               RegisterValue &reg_value) {
  Status error;
  reg_value.SetFromMemoryData(
      reg_info, (const uint8_t *)&m_regs + reg_info->byte_offset,
      reg_info->byte_size, lldb::eByteOrderLittle, error);
  return error.Success();
}

bool RegisterContextMinidump_ARM::WriteRegister(const RegisterInfo *,
                                                const RegisterValue &) {
  return false;
}

uint32_t RegisterContextMinidump_ARM::ConvertRegisterKindToRegisterNumber(
    lldb::RegisterKind kind, uint32_t num) {
  for (size_t i = 0; i < k_num_regs; ++i) {
    if (g_reg_infos[i].kinds[kind] == num)
      return i;
  }
  return LLDB_INVALID_REGNUM;
}
