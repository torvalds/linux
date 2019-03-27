//===-- RegisterContextPOSIX_arm.cpp --------------------------*- C++ -*-===//
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

#include "RegisterContextPOSIX_arm.h"

using namespace lldb;
using namespace lldb_private;

// arm general purpose registers.
const uint32_t g_gpr_regnums_arm[] = {
    gpr_r0_arm,         gpr_r1_arm,   gpr_r2_arm,  gpr_r3_arm, gpr_r4_arm,
    gpr_r5_arm,         gpr_r6_arm,   gpr_r7_arm,  gpr_r8_arm, gpr_r9_arm,
    gpr_r10_arm,        gpr_r11_arm,  gpr_r12_arm, gpr_sp_arm, gpr_lr_arm,
    gpr_pc_arm,         gpr_cpsr_arm,
    LLDB_INVALID_REGNUM // register sets need to end with this flag

};
static_assert(((sizeof g_gpr_regnums_arm / sizeof g_gpr_regnums_arm[0]) - 1) ==
                  k_num_gpr_registers_arm,
              "g_gpr_regnums_arm has wrong number of register infos");

// arm floating point registers.
static const uint32_t g_fpu_regnums_arm[] = {
    fpu_s0_arm,         fpu_s1_arm,  fpu_s2_arm,    fpu_s3_arm,  fpu_s4_arm,
    fpu_s5_arm,         fpu_s6_arm,  fpu_s7_arm,    fpu_s8_arm,  fpu_s9_arm,
    fpu_s10_arm,        fpu_s11_arm, fpu_s12_arm,   fpu_s13_arm, fpu_s14_arm,
    fpu_s15_arm,        fpu_s16_arm, fpu_s17_arm,   fpu_s18_arm, fpu_s19_arm,
    fpu_s20_arm,        fpu_s21_arm, fpu_s22_arm,   fpu_s23_arm, fpu_s24_arm,
    fpu_s25_arm,        fpu_s26_arm, fpu_s27_arm,   fpu_s28_arm, fpu_s29_arm,
    fpu_s30_arm,        fpu_s31_arm, fpu_fpscr_arm, fpu_d0_arm,  fpu_d1_arm,
    fpu_d2_arm,         fpu_d3_arm,  fpu_d4_arm,    fpu_d5_arm,  fpu_d6_arm,
    fpu_d7_arm,         fpu_d8_arm,  fpu_d9_arm,    fpu_d10_arm, fpu_d11_arm,
    fpu_d12_arm,        fpu_d13_arm, fpu_d14_arm,   fpu_d15_arm, fpu_d16_arm,
    fpu_d17_arm,        fpu_d18_arm, fpu_d19_arm,   fpu_d20_arm, fpu_d21_arm,
    fpu_d22_arm,        fpu_d23_arm, fpu_d24_arm,   fpu_d25_arm, fpu_d26_arm,
    fpu_d27_arm,        fpu_d28_arm, fpu_d29_arm,   fpu_d30_arm, fpu_d31_arm,
    fpu_q0_arm,         fpu_q1_arm,  fpu_q2_arm,    fpu_q3_arm,  fpu_q4_arm,
    fpu_q5_arm,         fpu_q6_arm,  fpu_q7_arm,    fpu_q8_arm,  fpu_q9_arm,
    fpu_q10_arm,        fpu_q11_arm, fpu_q12_arm,   fpu_q13_arm, fpu_q14_arm,
    fpu_q15_arm,
    LLDB_INVALID_REGNUM // register sets need to end with this flag

};
static_assert(((sizeof g_fpu_regnums_arm / sizeof g_fpu_regnums_arm[0]) - 1) ==
                  k_num_fpr_registers_arm,
              "g_fpu_regnums_arm has wrong number of register infos");

// Number of register sets provided by this context.
enum { k_num_register_sets = 2 };

// Register sets for arm.
static const lldb_private::RegisterSet g_reg_sets_arm[k_num_register_sets] = {
    {"General Purpose Registers", "gpr", k_num_gpr_registers_arm,
     g_gpr_regnums_arm},
    {"Floating Point Registers", "fpu", k_num_fpr_registers_arm,
     g_fpu_regnums_arm}};

bool RegisterContextPOSIX_arm::IsGPR(unsigned reg) {
  return reg <= m_reg_info.last_gpr; // GPR's come first.
}

bool RegisterContextPOSIX_arm::IsFPR(unsigned reg) {
  return (m_reg_info.first_fpr <= reg && reg <= m_reg_info.last_fpr);
}

RegisterContextPOSIX_arm::RegisterContextPOSIX_arm(
    lldb_private::Thread &thread, uint32_t concrete_frame_idx,
    lldb_private::RegisterInfoInterface *register_info)
    : lldb_private::RegisterContext(thread, concrete_frame_idx) {
  m_register_info_ap.reset(register_info);

  switch (register_info->m_target_arch.GetMachine()) {
  case llvm::Triple::arm:
    m_reg_info.num_registers = k_num_registers_arm;
    m_reg_info.num_gpr_registers = k_num_gpr_registers_arm;
    m_reg_info.num_fpr_registers = k_num_fpr_registers_arm;
    m_reg_info.last_gpr = k_last_gpr_arm;
    m_reg_info.first_fpr = k_first_fpr_arm;
    m_reg_info.last_fpr = k_last_fpr_arm;
    m_reg_info.first_fpr_v = fpu_s0_arm;
    m_reg_info.last_fpr_v = fpu_s31_arm;
    m_reg_info.gpr_flags = gpr_cpsr_arm;
    break;
  default:
    assert(false && "Unhandled target architecture.");
    break;
  }

  ::memset(&m_fpr, 0, sizeof m_fpr);
}

RegisterContextPOSIX_arm::~RegisterContextPOSIX_arm() {}

void RegisterContextPOSIX_arm::Invalidate() {}

void RegisterContextPOSIX_arm::InvalidateAllRegisters() {}

unsigned RegisterContextPOSIX_arm::GetRegisterOffset(unsigned reg) {
  assert(reg < m_reg_info.num_registers && "Invalid register number.");
  return GetRegisterInfo()[reg].byte_offset;
}

unsigned RegisterContextPOSIX_arm::GetRegisterSize(unsigned reg) {
  assert(reg < m_reg_info.num_registers && "Invalid register number.");
  return GetRegisterInfo()[reg].byte_size;
}

size_t RegisterContextPOSIX_arm::GetRegisterCount() {
  size_t num_registers =
      m_reg_info.num_gpr_registers + m_reg_info.num_fpr_registers;
  return num_registers;
}

size_t RegisterContextPOSIX_arm::GetGPRSize() {
  return m_register_info_ap->GetGPRSize();
}

const lldb_private::RegisterInfo *RegisterContextPOSIX_arm::GetRegisterInfo() {
  // Commonly, this method is overridden and g_register_infos is copied and
  // specialized. So, use GetRegisterInfo() rather than g_register_infos in
  // this scope.
  return m_register_info_ap->GetRegisterInfo();
}

const lldb_private::RegisterInfo *
RegisterContextPOSIX_arm::GetRegisterInfoAtIndex(size_t reg) {
  if (reg < m_reg_info.num_registers)
    return &GetRegisterInfo()[reg];
  else
    return NULL;
}

size_t RegisterContextPOSIX_arm::GetRegisterSetCount() {
  size_t sets = 0;
  for (size_t set = 0; set < k_num_register_sets; ++set) {
    if (IsRegisterSetAvailable(set))
      ++sets;
  }

  return sets;
}

const lldb_private::RegisterSet *
RegisterContextPOSIX_arm::GetRegisterSet(size_t set) {
  if (IsRegisterSetAvailable(set)) {
    switch (m_register_info_ap->m_target_arch.GetMachine()) {
    case llvm::Triple::arm:
      return &g_reg_sets_arm[set];
    default:
      assert(false && "Unhandled target architecture.");
      return NULL;
    }
  }
  return NULL;
}

const char *RegisterContextPOSIX_arm::GetRegisterName(unsigned reg) {
  assert(reg < m_reg_info.num_registers && "Invalid register offset.");
  return GetRegisterInfo()[reg].name;
}

lldb::ByteOrder RegisterContextPOSIX_arm::GetByteOrder() {
  // Get the target process whose privileged thread was used for the register
  // read.
  lldb::ByteOrder byte_order = lldb::eByteOrderInvalid;
  lldb_private::Process *process = CalculateProcess().get();

  if (process)
    byte_order = process->GetByteOrder();
  return byte_order;
}

bool RegisterContextPOSIX_arm::IsRegisterSetAvailable(size_t set_index) {
  return set_index < k_num_register_sets;
}

// Used when parsing DWARF and EH frame information and any other object file
// sections that contain register numbers in them.
uint32_t RegisterContextPOSIX_arm::ConvertRegisterKindToRegisterNumber(
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
