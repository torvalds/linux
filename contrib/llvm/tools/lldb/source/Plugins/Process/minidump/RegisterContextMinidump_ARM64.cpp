//===-- RegisterContextMinidump_ARM64.cpp -----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "RegisterContextMinidump_ARM64.h"

#include "Utility/ARM64_DWARF_Registers.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/lldb-enumerations.h"

// C includes
#include <assert.h>

// C++ includes

using namespace lldb;
using namespace lldb_private;
using namespace minidump;

#define INV LLDB_INVALID_REGNUM
#define OFFSET(r) (offsetof(RegisterContextMinidump_ARM64::Context, r))

#define DEF_X(i)                                                               \
  {                                                                            \
    "x" #i, nullptr, 8, OFFSET(x) + i * 8, eEncodingUint, eFormatHex,          \
        {INV, arm64_dwarf::x##i, INV, INV, reg_x##i}, nullptr, nullptr,        \
        nullptr, 0                                                             \
  }

#define DEF_W(i)                                                               \
  {                                                                            \
    "w" #i, nullptr, 4, OFFSET(x) + i * 8, eEncodingUint, eFormatHex,          \
        {INV, INV, INV, INV, reg_w##i}, nullptr, nullptr, nullptr, 0           \
  }

#define DEF_X_ARG(i, n)                                                        \
  {                                                                            \
    "x" #i, "arg" #n, 8, OFFSET(x) + i * 8, eEncodingUint, eFormatHex,         \
        {INV, arm64_dwarf::x##i, LLDB_REGNUM_GENERIC_ARG1 + i, INV, reg_x##i}, \
        nullptr, nullptr, nullptr, 0                                           \
  }

#define DEF_V(i)                                                               \
  {                                                                            \
    "v" #i, nullptr, 16, OFFSET(v) + i * 16, eEncodingVector,                  \
        eFormatVectorOfUInt8, {INV, arm64_dwarf::v##i, INV, INV, reg_v##i},    \
        nullptr, nullptr, nullptr, 0                                           \
  }

#define DEF_D(i)                                                               \
  {                                                                            \
    "d" #i, nullptr, 8, OFFSET(v) + i * 16, eEncodingVector,                   \
        eFormatVectorOfUInt8, {INV, INV, INV, INV, reg_d##i}, nullptr,         \
        nullptr, nullptr, 0                                                    \
  }

#define DEF_S(i)                                                               \
  {                                                                            \
    "s" #i, nullptr, 4, OFFSET(v) + i * 16, eEncodingVector,                   \
        eFormatVectorOfUInt8, {INV, INV, INV, INV, reg_s##i}, nullptr,         \
        nullptr, nullptr, 0                                                    \
  }

#define DEF_H(i)                                                               \
  {                                                                            \
    "h" #i, nullptr, 2, OFFSET(v) + i * 16, eEncodingVector,                   \
        eFormatVectorOfUInt8, {INV, INV, INV, INV, reg_h##i}, nullptr,         \
        nullptr, nullptr, 0                                                    \
  }

// Zero based LLDB register numbers for this register context
enum {
  // General Purpose Registers
  reg_x0 = 0,
  reg_x1,
  reg_x2,
  reg_x3,
  reg_x4,
  reg_x5,
  reg_x6,
  reg_x7,
  reg_x8,
  reg_x9,
  reg_x10,
  reg_x11,
  reg_x12,
  reg_x13,
  reg_x14,
  reg_x15,
  reg_x16,
  reg_x17,
  reg_x18,
  reg_x19,
  reg_x20,
  reg_x21,
  reg_x22,
  reg_x23,
  reg_x24,
  reg_x25,
  reg_x26,
  reg_x27,
  reg_x28,
  reg_fp,
  reg_lr,
  reg_sp,
  reg_pc,
  reg_w0,
  reg_w1,
  reg_w2,
  reg_w3,
  reg_w4,
  reg_w5,
  reg_w6,
  reg_w7,
  reg_w8,
  reg_w9,
  reg_w10,
  reg_w11,
  reg_w12,
  reg_w13,
  reg_w14,
  reg_w15,
  reg_w16,
  reg_w17,
  reg_w18,
  reg_w19,
  reg_w20,
  reg_w21,
  reg_w22,
  reg_w23,
  reg_w24,
  reg_w25,
  reg_w26,
  reg_w27,
  reg_w28,
  reg_w29,
  reg_w30,
  reg_w31,
  reg_cpsr,
  // Floating Point Registers
  reg_fpsr,
  reg_fpcr,
  reg_v0,
  reg_v1,
  reg_v2,
  reg_v3,
  reg_v4,
  reg_v5,
  reg_v6,
  reg_v7,
  reg_v8,
  reg_v9,
  reg_v10,
  reg_v11,
  reg_v12,
  reg_v13,
  reg_v14,
  reg_v15,
  reg_v16,
  reg_v17,
  reg_v18,
  reg_v19,
  reg_v20,
  reg_v21,
  reg_v22,
  reg_v23,
  reg_v24,
  reg_v25,
  reg_v26,
  reg_v27,
  reg_v28,
  reg_v29,
  reg_v30,
  reg_v31,
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
  reg_h0,
  reg_h1,
  reg_h2,
  reg_h3,
  reg_h4,
  reg_h5,
  reg_h6,
  reg_h7,
  reg_h8,
  reg_h9,
  reg_h10,
  reg_h11,
  reg_h12,
  reg_h13,
  reg_h14,
  reg_h15,
  reg_h16,
  reg_h17,
  reg_h18,
  reg_h19,
  reg_h20,
  reg_h21,
  reg_h22,
  reg_h23,
  reg_h24,
  reg_h25,
  reg_h26,
  reg_h27,
  reg_h28,
  reg_h29,
  reg_h30,
  reg_h31,
  k_num_regs
};

// Register info definitions for this register context
static RegisterInfo g_reg_infos[] = {
    DEF_X_ARG(0, 1),
    DEF_X_ARG(1, 2),
    DEF_X_ARG(2, 3),
    DEF_X_ARG(3, 4),
    DEF_X_ARG(4, 5),
    DEF_X_ARG(5, 6),
    DEF_X_ARG(6, 7),
    DEF_X_ARG(7, 8),
    DEF_X(8),
    DEF_X(9),
    DEF_X(10),
    DEF_X(11),
    DEF_X(12),
    DEF_X(13),
    DEF_X(14),
    DEF_X(15),
    DEF_X(16),
    DEF_X(17),
    DEF_X(18),
    DEF_X(19),
    DEF_X(20),
    DEF_X(21),
    DEF_X(22),
    DEF_X(23),
    DEF_X(24),
    DEF_X(25),
    DEF_X(26),
    DEF_X(27),
    DEF_X(28),
    {"fp",
     "x29",
     8,
     OFFSET(x) + 29 * 8,
     eEncodingUint,
     eFormatHex,
     {INV, arm64_dwarf::x29, LLDB_REGNUM_GENERIC_FP, INV, reg_fp},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"lr",
     "x30",
     8,
     OFFSET(x) + 30 * 8,
     eEncodingUint,
     eFormatHex,
     {INV, arm64_dwarf::x30, LLDB_REGNUM_GENERIC_RA, INV, reg_lr},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"sp",
     "x31",
     8,
     OFFSET(x) + 31 * 8,
     eEncodingUint,
     eFormatHex,
     {INV, arm64_dwarf::x31, LLDB_REGNUM_GENERIC_SP, INV, reg_sp},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"pc",
     nullptr,
     8,
     OFFSET(pc),
     eEncodingUint,
     eFormatHex,
     {INV, arm64_dwarf::pc, LLDB_REGNUM_GENERIC_PC, INV, reg_pc},
     nullptr,
     nullptr,
     nullptr,
     0},
    // w0 - w31
    DEF_W(0),
    DEF_W(1),
    DEF_W(2),
    DEF_W(3),
    DEF_W(4),
    DEF_W(5),
    DEF_W(6),
    DEF_W(7),
    DEF_W(8),
    DEF_W(9),
    DEF_W(10),
    DEF_W(11),
    DEF_W(12),
    DEF_W(13),
    DEF_W(14),
    DEF_W(15),
    DEF_W(16),
    DEF_W(17),
    DEF_W(18),
    DEF_W(19),
    DEF_W(20),
    DEF_W(21),
    DEF_W(22),
    DEF_W(23),
    DEF_W(24),
    DEF_W(25),
    DEF_W(26),
    DEF_W(27),
    DEF_W(28),
    DEF_W(29),
    DEF_W(30),
    DEF_W(31),
    {"cpsr",
     "psr",
     4,
     OFFSET(cpsr),
     eEncodingUint,
     eFormatHex,
     {INV, arm64_dwarf::cpsr, LLDB_REGNUM_GENERIC_FLAGS, INV, reg_cpsr},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"fpsr",
     nullptr,
     4,
     OFFSET(fpsr),
     eEncodingUint,
     eFormatHex,
     {INV, INV, INV, INV, reg_fpsr},
     nullptr,
     nullptr,
     nullptr,
     0},
    {"fpcr",
     nullptr,
     4,
     OFFSET(fpcr),
     eEncodingUint,
     eFormatHex,
     {INV, INV, INV, INV, reg_fpcr},
     nullptr,
     nullptr,
     nullptr,
     0},
    // v0 - v31
    DEF_V(0),
    DEF_V(1),
    DEF_V(2),
    DEF_V(3),
    DEF_V(4),
    DEF_V(5),
    DEF_V(6),
    DEF_V(7),
    DEF_V(8),
    DEF_V(9),
    DEF_V(10),
    DEF_V(11),
    DEF_V(12),
    DEF_V(13),
    DEF_V(14),
    DEF_V(15),
    DEF_V(16),
    DEF_V(17),
    DEF_V(18),
    DEF_V(19),
    DEF_V(20),
    DEF_V(21),
    DEF_V(22),
    DEF_V(23),
    DEF_V(24),
    DEF_V(25),
    DEF_V(26),
    DEF_V(27),
    DEF_V(28),
    DEF_V(29),
    DEF_V(30),
    DEF_V(31),
    // d0 - d31
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
    // s0 - s31
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
    // h0 - h31
    DEF_H(0),
    DEF_H(1),
    DEF_H(2),
    DEF_H(3),
    DEF_H(4),
    DEF_H(5),
    DEF_H(6),
    DEF_H(7),
    DEF_H(8),
    DEF_H(9),
    DEF_H(10),
    DEF_H(11),
    DEF_H(12),
    DEF_H(13),
    DEF_H(14),
    DEF_H(15),
    DEF_H(16),
    DEF_H(17),
    DEF_H(18),
    DEF_H(19),
    DEF_H(20),
    DEF_H(21),
    DEF_H(22),
    DEF_H(23),
    DEF_H(24),
    DEF_H(25),
    DEF_H(26),
    DEF_H(27),
    DEF_H(28),
    DEF_H(29),
    DEF_H(30),
    DEF_H(31),
};

constexpr size_t k_num_reg_infos = llvm::array_lengthof(g_reg_infos);

// ARM64 general purpose registers.
const uint32_t g_gpr_regnums[] = {
    reg_x0,
    reg_x1,
    reg_x2,
    reg_x3,
    reg_x4,
    reg_x5,
    reg_x6,
    reg_x7,
    reg_x8,
    reg_x9,
    reg_x10,
    reg_x11,
    reg_x12,
    reg_x13,
    reg_x14,
    reg_x15,
    reg_x16,
    reg_x17,
    reg_x18,
    reg_x19,
    reg_x20,
    reg_x21,
    reg_x22,
    reg_x23,
    reg_x24,
    reg_x25,
    reg_x26,
    reg_x27,
    reg_x28,
    reg_fp,
    reg_lr,
    reg_sp,
    reg_w0,
    reg_w1,
    reg_w2,
    reg_w3,
    reg_w4,
    reg_w5,
    reg_w6,
    reg_w7,
    reg_w8,
    reg_w9,
    reg_w10,
    reg_w11,
    reg_w12,
    reg_w13,
    reg_w14,
    reg_w15,
    reg_w16,
    reg_w17,
    reg_w18,
    reg_w19,
    reg_w20,
    reg_w21,
    reg_w22,
    reg_w23,
    reg_w24,
    reg_w25,
    reg_w26,
    reg_w27,
    reg_w28,
    reg_w29,
    reg_w30,
    reg_w31,
    reg_pc,
    reg_cpsr,
    LLDB_INVALID_REGNUM // register sets need to end with this flag
};
const uint32_t g_fpu_regnums[] = {
    reg_v0,
    reg_v1,
    reg_v2,
    reg_v3,
    reg_v4,
    reg_v5,
    reg_v6,
    reg_v7,
    reg_v8,
    reg_v9,
    reg_v10,
    reg_v11,
    reg_v12,
    reg_v13,
    reg_v14,
    reg_v15,
    reg_v16,
    reg_v17,
    reg_v18,
    reg_v19,
    reg_v20,
    reg_v21,
    reg_v22,
    reg_v23,
    reg_v24,
    reg_v25,
    reg_v26,
    reg_v27,
    reg_v28,
    reg_v29,
    reg_v30,
    reg_v31,
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
    reg_h0,
    reg_h1,
    reg_h2,
    reg_h3,
    reg_h4,
    reg_h5,
    reg_h6,
    reg_h7,
    reg_h8,
    reg_h9,
    reg_h10,
    reg_h11,
    reg_h12,
    reg_h13,
    reg_h14,
    reg_h15,
    reg_h16,
    reg_h17,
    reg_h18,
    reg_h19,
    reg_h20,
    reg_h21,
    reg_h22,
    reg_h23,
    reg_h24,
    reg_h25,
    reg_h26,
    reg_h27,
    reg_h28,
    reg_h29,
    reg_h30,
    reg_h31,
    reg_fpsr,
    reg_fpcr,
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

RegisterContextMinidump_ARM64::RegisterContextMinidump_ARM64(
    Thread &thread, const DataExtractor &data)
    : RegisterContext(thread, 0) {
  lldb::offset_t offset = 0;
  m_regs.context_flags = data.GetU64(&offset);
  for (unsigned i = 0; i < 32; ++i)
    m_regs.x[i] = data.GetU64(&offset);
  m_regs.pc = data.GetU64(&offset);
  m_regs.cpsr = data.GetU32(&offset);
  m_regs.fpsr = data.GetU32(&offset);
  m_regs.fpcr = data.GetU32(&offset);
  auto regs_data = data.GetData(&offset, sizeof(m_regs.v));
  if (regs_data)
    memcpy(m_regs.v, regs_data, sizeof(m_regs.v));
  assert(k_num_regs == k_num_reg_infos);
}
size_t RegisterContextMinidump_ARM64::GetRegisterCount() { return k_num_regs; }

const RegisterInfo *
RegisterContextMinidump_ARM64::GetRegisterInfoAtIndex(size_t reg) {
  if (reg < k_num_reg_infos)
    return &g_reg_infos[reg];
  return nullptr;
}

size_t RegisterContextMinidump_ARM64::GetRegisterSetCount() {
  return k_num_reg_sets;
}

const RegisterSet *RegisterContextMinidump_ARM64::GetRegisterSet(size_t set) {
  if (set < k_num_reg_sets)
    return &g_reg_sets[set];
  return nullptr;
}

const char *RegisterContextMinidump_ARM64::GetRegisterName(unsigned reg) {
  if (reg < k_num_reg_infos)
    return g_reg_infos[reg].name;
  return nullptr;
}

bool RegisterContextMinidump_ARM64::ReadRegister(const RegisterInfo *reg_info,
                                                 RegisterValue &reg_value) {
  Status error;
  reg_value.SetFromMemoryData(
      reg_info, (const uint8_t *)&m_regs + reg_info->byte_offset,
      reg_info->byte_size, lldb::eByteOrderLittle, error);
  return error.Success();
}

bool RegisterContextMinidump_ARM64::WriteRegister(const RegisterInfo *,
                                                  const RegisterValue &) {
  return false;
}

uint32_t RegisterContextMinidump_ARM64::ConvertRegisterKindToRegisterNumber(
    lldb::RegisterKind kind, uint32_t num) {
  for (size_t i = 0; i < k_num_regs; ++i) {
    if (g_reg_infos[i].kinds[kind] == num)
      return i;
  }
  return LLDB_INVALID_REGNUM;
}
