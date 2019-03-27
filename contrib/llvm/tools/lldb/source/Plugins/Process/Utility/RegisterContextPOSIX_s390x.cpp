//===-- RegisterContextPOSIX_s390x.cpp --------------------------*- C++ -*-===//
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

#include "RegisterContextPOSIX_s390x.h"
#include "RegisterContext_s390x.h"

using namespace lldb_private;
using namespace lldb;

// s390x 64-bit general purpose registers.
static const uint32_t g_gpr_regnums_s390x[] = {
    lldb_r0_s390x,      lldb_r1_s390x,    lldb_r2_s390x,    lldb_r3_s390x,
    lldb_r4_s390x,      lldb_r5_s390x,    lldb_r6_s390x,    lldb_r7_s390x,
    lldb_r8_s390x,      lldb_r9_s390x,    lldb_r10_s390x,   lldb_r11_s390x,
    lldb_r12_s390x,     lldb_r13_s390x,   lldb_r14_s390x,   lldb_r15_s390x,
    lldb_acr0_s390x,    lldb_acr1_s390x,  lldb_acr2_s390x,  lldb_acr3_s390x,
    lldb_acr4_s390x,    lldb_acr5_s390x,  lldb_acr6_s390x,  lldb_acr7_s390x,
    lldb_acr8_s390x,    lldb_acr9_s390x,  lldb_acr10_s390x, lldb_acr11_s390x,
    lldb_acr12_s390x,   lldb_acr13_s390x, lldb_acr14_s390x, lldb_acr15_s390x,
    lldb_pswm_s390x,    lldb_pswa_s390x,
    LLDB_INVALID_REGNUM // register sets need to end with this flag
};
static_assert((sizeof(g_gpr_regnums_s390x) / sizeof(g_gpr_regnums_s390x[0])) -
                      1 ==
                  k_num_gpr_registers_s390x,
              "g_gpr_regnums_s390x has wrong number of register infos");

// s390x 64-bit floating point registers.
static const uint32_t g_fpu_regnums_s390x[] = {
    lldb_f0_s390x,      lldb_f1_s390x,  lldb_f2_s390x,  lldb_f3_s390x,
    lldb_f4_s390x,      lldb_f5_s390x,  lldb_f6_s390x,  lldb_f7_s390x,
    lldb_f8_s390x,      lldb_f9_s390x,  lldb_f10_s390x, lldb_f11_s390x,
    lldb_f12_s390x,     lldb_f13_s390x, lldb_f14_s390x, lldb_f15_s390x,
    lldb_fpc_s390x,
    LLDB_INVALID_REGNUM // register sets need to end with this flag
};
static_assert((sizeof(g_fpu_regnums_s390x) / sizeof(g_fpu_regnums_s390x[0])) -
                      1 ==
                  k_num_fpr_registers_s390x,
              "g_fpu_regnums_s390x has wrong number of register infos");

// Number of register sets provided by this context.
enum { k_num_register_sets = 2 };

// Register sets for s390x 64-bit.
static const RegisterSet g_reg_sets_s390x[k_num_register_sets] = {
    {"General Purpose Registers", "gpr", k_num_gpr_registers_s390x,
     g_gpr_regnums_s390x},
    {"Floating Point Registers", "fpr", k_num_fpr_registers_s390x,
     g_fpu_regnums_s390x},
};

bool RegisterContextPOSIX_s390x::IsGPR(unsigned reg) {
  return reg <= m_reg_info.last_gpr; // GPRs come first.
}

bool RegisterContextPOSIX_s390x::IsFPR(unsigned reg) {
  return (m_reg_info.first_fpr <= reg && reg <= m_reg_info.last_fpr);
}

RegisterContextPOSIX_s390x::RegisterContextPOSIX_s390x(
    Thread &thread, uint32_t concrete_frame_idx,
    RegisterInfoInterface *register_info)
    : RegisterContext(thread, concrete_frame_idx) {
  m_register_info_ap.reset(register_info);

  switch (register_info->m_target_arch.GetMachine()) {
  case llvm::Triple::systemz:
    m_reg_info.num_registers = k_num_registers_s390x;
    m_reg_info.num_gpr_registers = k_num_gpr_registers_s390x;
    m_reg_info.num_fpr_registers = k_num_fpr_registers_s390x;
    m_reg_info.last_gpr = k_last_gpr_s390x;
    m_reg_info.first_fpr = k_first_fpr_s390x;
    m_reg_info.last_fpr = k_last_fpr_s390x;
    break;
  default:
    assert(false && "Unhandled target architecture.");
    break;
  }
}

RegisterContextPOSIX_s390x::~RegisterContextPOSIX_s390x() {}

void RegisterContextPOSIX_s390x::Invalidate() {}

void RegisterContextPOSIX_s390x::InvalidateAllRegisters() {}

const RegisterInfo *RegisterContextPOSIX_s390x::GetRegisterInfo() {
  return m_register_info_ap->GetRegisterInfo();
}

const RegisterInfo *
RegisterContextPOSIX_s390x::GetRegisterInfoAtIndex(size_t reg) {
  if (reg < m_reg_info.num_registers)
    return &GetRegisterInfo()[reg];
  else
    return NULL;
}

size_t RegisterContextPOSIX_s390x::GetRegisterCount() {
  return m_reg_info.num_registers;
}

unsigned RegisterContextPOSIX_s390x::GetRegisterOffset(unsigned reg) {
  assert(reg < m_reg_info.num_registers && "Invalid register number.");
  return GetRegisterInfo()[reg].byte_offset;
}

unsigned RegisterContextPOSIX_s390x::GetRegisterSize(unsigned reg) {
  assert(reg < m_reg_info.num_registers && "Invalid register number.");
  return GetRegisterInfo()[reg].byte_size;
}

const char *RegisterContextPOSIX_s390x::GetRegisterName(unsigned reg) {
  assert(reg < m_reg_info.num_registers && "Invalid register offset.");
  return GetRegisterInfo()[reg].name;
}

bool RegisterContextPOSIX_s390x::IsRegisterSetAvailable(size_t set_index) {
  return set_index < k_num_register_sets;
}

size_t RegisterContextPOSIX_s390x::GetRegisterSetCount() {
  size_t sets = 0;
  for (size_t set = 0; set < k_num_register_sets; ++set) {
    if (IsRegisterSetAvailable(set))
      ++sets;
  }

  return sets;
}

const RegisterSet *RegisterContextPOSIX_s390x::GetRegisterSet(size_t set) {
  if (IsRegisterSetAvailable(set)) {
    switch (m_register_info_ap->m_target_arch.GetMachine()) {
    case llvm::Triple::systemz:
      return &g_reg_sets_s390x[set];
    default:
      assert(false && "Unhandled target architecture.");
      return NULL;
    }
  }
  return NULL;
}

lldb::ByteOrder RegisterContextPOSIX_s390x::GetByteOrder() {
  // Get the target process whose privileged thread was used for the register
  // read.
  lldb::ByteOrder byte_order = eByteOrderInvalid;
  Process *process = CalculateProcess().get();

  if (process)
    byte_order = process->GetByteOrder();
  return byte_order;
}

// Used when parsing DWARF and EH frame information and any other object file
// sections that contain register numbers in them.
uint32_t RegisterContextPOSIX_s390x::ConvertRegisterKindToRegisterNumber(
    lldb::RegisterKind kind, uint32_t num) {
  const uint32_t num_regs = GetRegisterCount();

  assert(kind < kNumRegisterKinds);
  for (uint32_t reg_idx = 0; reg_idx < num_regs; ++reg_idx) {
    const RegisterInfo *reg_info = GetRegisterInfoAtIndex(reg_idx);

    if (reg_info->kinds[kind] == num)
      return reg_idx;
  }

  return LLDB_INVALID_REGNUM;
}
