//===-- RegisterContextPOSIX_arm64.cpp --------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <cstring>
#include <errno.h>
#include <stdint.h>

#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Endian.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/Utility/Scalar.h"
#include "llvm/Support/Compiler.h"

#include "RegisterContextPOSIX_arm64.h"

using namespace lldb;
using namespace lldb_private;

// ARM64 general purpose registers.
const uint32_t g_gpr_regnums_arm64[] = {
    gpr_x0_arm64,       gpr_x1_arm64,   gpr_x2_arm64,  gpr_x3_arm64,
    gpr_x4_arm64,       gpr_x5_arm64,   gpr_x6_arm64,  gpr_x7_arm64,
    gpr_x8_arm64,       gpr_x9_arm64,   gpr_x10_arm64, gpr_x11_arm64,
    gpr_x12_arm64,      gpr_x13_arm64,  gpr_x14_arm64, gpr_x15_arm64,
    gpr_x16_arm64,      gpr_x17_arm64,  gpr_x18_arm64, gpr_x19_arm64,
    gpr_x20_arm64,      gpr_x21_arm64,  gpr_x22_arm64, gpr_x23_arm64,
    gpr_x24_arm64,      gpr_x25_arm64,  gpr_x26_arm64, gpr_x27_arm64,
    gpr_x28_arm64,      gpr_fp_arm64,   gpr_lr_arm64,  gpr_sp_arm64,
    gpr_pc_arm64,       gpr_cpsr_arm64, gpr_w0_arm64,  gpr_w1_arm64,
    gpr_w2_arm64,       gpr_w3_arm64,   gpr_w4_arm64,  gpr_w5_arm64,
    gpr_w6_arm64,       gpr_w7_arm64,   gpr_w8_arm64,  gpr_w9_arm64,
    gpr_w10_arm64,      gpr_w11_arm64,  gpr_w12_arm64, gpr_w13_arm64,
    gpr_w14_arm64,      gpr_w15_arm64,  gpr_w16_arm64, gpr_w17_arm64,
    gpr_w18_arm64,      gpr_w19_arm64,  gpr_w20_arm64, gpr_w21_arm64,
    gpr_w22_arm64,      gpr_w23_arm64,  gpr_w24_arm64, gpr_w25_arm64,
    gpr_w26_arm64,      gpr_w27_arm64,  gpr_w28_arm64,
    LLDB_INVALID_REGNUM // register sets need to end with this flag
};
static_assert(((sizeof g_gpr_regnums_arm64 / sizeof g_gpr_regnums_arm64[0]) -
               1) == k_num_gpr_registers_arm64,
              "g_gpr_regnums_arm64 has wrong number of register infos");

// ARM64 floating point registers.
static const uint32_t g_fpu_regnums_arm64[] = {
    fpu_v0_arm64,       fpu_v1_arm64,   fpu_v2_arm64,  fpu_v3_arm64,
    fpu_v4_arm64,       fpu_v5_arm64,   fpu_v6_arm64,  fpu_v7_arm64,
    fpu_v8_arm64,       fpu_v9_arm64,   fpu_v10_arm64, fpu_v11_arm64,
    fpu_v12_arm64,      fpu_v13_arm64,  fpu_v14_arm64, fpu_v15_arm64,
    fpu_v16_arm64,      fpu_v17_arm64,  fpu_v18_arm64, fpu_v19_arm64,
    fpu_v20_arm64,      fpu_v21_arm64,  fpu_v22_arm64, fpu_v23_arm64,
    fpu_v24_arm64,      fpu_v25_arm64,  fpu_v26_arm64, fpu_v27_arm64,
    fpu_v28_arm64,      fpu_v29_arm64,  fpu_v30_arm64, fpu_v31_arm64,
    fpu_s0_arm64,       fpu_s1_arm64,   fpu_s2_arm64,  fpu_s3_arm64,
    fpu_s4_arm64,       fpu_s5_arm64,   fpu_s6_arm64,  fpu_s7_arm64,
    fpu_s8_arm64,       fpu_s9_arm64,   fpu_s10_arm64, fpu_s11_arm64,
    fpu_s12_arm64,      fpu_s13_arm64,  fpu_s14_arm64, fpu_s15_arm64,
    fpu_s16_arm64,      fpu_s17_arm64,  fpu_s18_arm64, fpu_s19_arm64,
    fpu_s20_arm64,      fpu_s21_arm64,  fpu_s22_arm64, fpu_s23_arm64,
    fpu_s24_arm64,      fpu_s25_arm64,  fpu_s26_arm64, fpu_s27_arm64,
    fpu_s28_arm64,      fpu_s29_arm64,  fpu_s30_arm64, fpu_s31_arm64,

    fpu_d0_arm64,       fpu_d1_arm64,   fpu_d2_arm64,  fpu_d3_arm64,
    fpu_d4_arm64,       fpu_d5_arm64,   fpu_d6_arm64,  fpu_d7_arm64,
    fpu_d8_arm64,       fpu_d9_arm64,   fpu_d10_arm64, fpu_d11_arm64,
    fpu_d12_arm64,      fpu_d13_arm64,  fpu_d14_arm64, fpu_d15_arm64,
    fpu_d16_arm64,      fpu_d17_arm64,  fpu_d18_arm64, fpu_d19_arm64,
    fpu_d20_arm64,      fpu_d21_arm64,  fpu_d22_arm64, fpu_d23_arm64,
    fpu_d24_arm64,      fpu_d25_arm64,  fpu_d26_arm64, fpu_d27_arm64,
    fpu_d28_arm64,      fpu_d29_arm64,  fpu_d30_arm64, fpu_d31_arm64,
    fpu_fpsr_arm64,     fpu_fpcr_arm64,
    LLDB_INVALID_REGNUM // register sets need to end with this flag
};
static_assert(((sizeof g_fpu_regnums_arm64 / sizeof g_fpu_regnums_arm64[0]) -
               1) == k_num_fpr_registers_arm64,
              "g_fpu_regnums_arm64 has wrong number of register infos");

// Number of register sets provided by this context.
enum { k_num_register_sets = 2 };

// Register sets for ARM64.
static const lldb_private::RegisterSet g_reg_sets_arm64[k_num_register_sets] = {
    {"General Purpose Registers", "gpr", k_num_gpr_registers_arm64,
     g_gpr_regnums_arm64},
    {"Floating Point Registers", "fpu", k_num_fpr_registers_arm64,
     g_fpu_regnums_arm64}};

bool RegisterContextPOSIX_arm64::IsGPR(unsigned reg) {
  return reg <= m_reg_info.last_gpr; // GPR's come first.
}

bool RegisterContextPOSIX_arm64::IsFPR(unsigned reg) {
  return (m_reg_info.first_fpr <= reg && reg <= m_reg_info.last_fpr);
}

RegisterContextPOSIX_arm64::RegisterContextPOSIX_arm64(
    lldb_private::Thread &thread, uint32_t concrete_frame_idx,
    lldb_private::RegisterInfoInterface *register_info)
    : lldb_private::RegisterContext(thread, concrete_frame_idx) {
  m_register_info_ap.reset(register_info);

  switch (register_info->m_target_arch.GetMachine()) {
  case llvm::Triple::aarch64:
    m_reg_info.num_registers = k_num_registers_arm64;
    m_reg_info.num_gpr_registers = k_num_gpr_registers_arm64;
    m_reg_info.num_fpr_registers = k_num_fpr_registers_arm64;
    m_reg_info.last_gpr = k_last_gpr_arm64;
    m_reg_info.first_fpr = k_first_fpr_arm64;
    m_reg_info.last_fpr = k_last_fpr_arm64;
    m_reg_info.first_fpr_v = fpu_v0_arm64;
    m_reg_info.last_fpr_v = fpu_v31_arm64;
    m_reg_info.gpr_flags = gpr_cpsr_arm64;
    break;
  default:
    assert(false && "Unhandled target architecture.");
    break;
  }

  ::memset(&m_fpr, 0, sizeof m_fpr);
}

RegisterContextPOSIX_arm64::~RegisterContextPOSIX_arm64() {}

void RegisterContextPOSIX_arm64::Invalidate() {}

void RegisterContextPOSIX_arm64::InvalidateAllRegisters() {}

unsigned RegisterContextPOSIX_arm64::GetRegisterOffset(unsigned reg) {
  assert(reg < m_reg_info.num_registers && "Invalid register number.");
  return GetRegisterInfo()[reg].byte_offset;
}

unsigned RegisterContextPOSIX_arm64::GetRegisterSize(unsigned reg) {
  assert(reg < m_reg_info.num_registers && "Invalid register number.");
  return GetRegisterInfo()[reg].byte_size;
}

size_t RegisterContextPOSIX_arm64::GetRegisterCount() {
  size_t num_registers =
      m_reg_info.num_gpr_registers + m_reg_info.num_fpr_registers;
  return num_registers;
}

size_t RegisterContextPOSIX_arm64::GetGPRSize() {
  return m_register_info_ap->GetGPRSize();
}

const lldb_private::RegisterInfo *
RegisterContextPOSIX_arm64::GetRegisterInfo() {
  // Commonly, this method is overridden and g_register_infos is copied and
  // specialized. So, use GetRegisterInfo() rather than g_register_infos in
  // this scope.
  return m_register_info_ap->GetRegisterInfo();
}

const lldb_private::RegisterInfo *
RegisterContextPOSIX_arm64::GetRegisterInfoAtIndex(size_t reg) {
  if (reg < m_reg_info.num_registers)
    return &GetRegisterInfo()[reg];
  else
    return NULL;
}

size_t RegisterContextPOSIX_arm64::GetRegisterSetCount() {
  size_t sets = 0;
  for (size_t set = 0; set < k_num_register_sets; ++set) {
    if (IsRegisterSetAvailable(set))
      ++sets;
  }

  return sets;
}

const lldb_private::RegisterSet *
RegisterContextPOSIX_arm64::GetRegisterSet(size_t set) {
  if (IsRegisterSetAvailable(set)) {
    switch (m_register_info_ap->m_target_arch.GetMachine()) {
    case llvm::Triple::aarch64:
      return &g_reg_sets_arm64[set];
    default:
      assert(false && "Unhandled target architecture.");
      return NULL;
    }
  }
  return NULL;
}

const char *RegisterContextPOSIX_arm64::GetRegisterName(unsigned reg) {
  assert(reg < m_reg_info.num_registers && "Invalid register offset.");
  return GetRegisterInfo()[reg].name;
}

lldb::ByteOrder RegisterContextPOSIX_arm64::GetByteOrder() {
  // Get the target process whose privileged thread was used for the register
  // read.
  lldb::ByteOrder byte_order = lldb::eByteOrderInvalid;
  lldb_private::Process *process = CalculateProcess().get();

  if (process)
    byte_order = process->GetByteOrder();
  return byte_order;
}

bool RegisterContextPOSIX_arm64::IsRegisterSetAvailable(size_t set_index) {
  return set_index < k_num_register_sets;
}

// Used when parsing DWARF and EH frame information and any other object file
// sections that contain register numbers in them.
uint32_t RegisterContextPOSIX_arm64::ConvertRegisterKindToRegisterNumber(
    lldb::RegisterKind kind, uint32_t num) {
  const uint32_t num_regs = GetRegisterCount();

  assert(kind < lldb::kNumRegisterKinds);
  for (uint32_t reg_idx = 0; reg_idx < num_regs; ++reg_idx) {
    const lldb_private::RegisterInfo *reg_info =
        GetRegisterInfoAtIndex(reg_idx);

    if (reg_info->kinds[kind] == num)
      return reg_idx;
  }

  return LLDB_INVALID_REGNUM;
}
