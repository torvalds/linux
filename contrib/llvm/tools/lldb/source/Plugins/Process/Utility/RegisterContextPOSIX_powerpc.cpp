//===-- RegisterContextPOSIX_powerpc.cpp -------------------------*- C++
//-*-===//
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

#include "RegisterContextPOSIX_powerpc.h"

using namespace lldb_private;
using namespace lldb;

static const uint32_t g_gpr_regnums[] = {
    gpr_r0_powerpc,  gpr_r1_powerpc,  gpr_r2_powerpc,  gpr_r3_powerpc,
    gpr_r4_powerpc,  gpr_r5_powerpc,  gpr_r6_powerpc,  gpr_r7_powerpc,
    gpr_r8_powerpc,  gpr_r9_powerpc,  gpr_r10_powerpc, gpr_r11_powerpc,
    gpr_r12_powerpc, gpr_r13_powerpc, gpr_r14_powerpc, gpr_r15_powerpc,
    gpr_r16_powerpc, gpr_r17_powerpc, gpr_r18_powerpc, gpr_r19_powerpc,
    gpr_r20_powerpc, gpr_r21_powerpc, gpr_r22_powerpc, gpr_r23_powerpc,
    gpr_r24_powerpc, gpr_r25_powerpc, gpr_r26_powerpc, gpr_r27_powerpc,
    gpr_r28_powerpc, gpr_r29_powerpc, gpr_r30_powerpc, gpr_r31_powerpc,
    gpr_lr_powerpc,  gpr_cr_powerpc,  gpr_xer_powerpc, gpr_ctr_powerpc,
    gpr_pc_powerpc,
};

static const uint32_t g_fpr_regnums[] = {
    fpr_f0_powerpc,    fpr_f1_powerpc,  fpr_f2_powerpc,  fpr_f3_powerpc,
    fpr_f4_powerpc,    fpr_f5_powerpc,  fpr_f6_powerpc,  fpr_f7_powerpc,
    fpr_f8_powerpc,    fpr_f9_powerpc,  fpr_f10_powerpc, fpr_f11_powerpc,
    fpr_f12_powerpc,   fpr_f13_powerpc, fpr_f14_powerpc, fpr_f15_powerpc,
    fpr_f16_powerpc,   fpr_f17_powerpc, fpr_f18_powerpc, fpr_f19_powerpc,
    fpr_f20_powerpc,   fpr_f21_powerpc, fpr_f22_powerpc, fpr_f23_powerpc,
    fpr_f24_powerpc,   fpr_f25_powerpc, fpr_f26_powerpc, fpr_f27_powerpc,
    fpr_f28_powerpc,   fpr_f29_powerpc, fpr_f30_powerpc, fpr_f31_powerpc,
    fpr_fpscr_powerpc,
};

static const uint32_t g_vmx_regnums[] = {
    vmx_v0_powerpc,     vmx_v1_powerpc,   vmx_v2_powerpc,  vmx_v3_powerpc,
    vmx_v4_powerpc,     vmx_v5_powerpc,   vmx_v6_powerpc,  vmx_v7_powerpc,
    vmx_v8_powerpc,     vmx_v9_powerpc,   vmx_v10_powerpc, vmx_v11_powerpc,
    vmx_v12_powerpc,    vmx_v13_powerpc,  vmx_v14_powerpc, vmx_v15_powerpc,
    vmx_v16_powerpc,    vmx_v17_powerpc,  vmx_v18_powerpc, vmx_v19_powerpc,
    vmx_v20_powerpc,    vmx_v21_powerpc,  vmx_v22_powerpc, vmx_v23_powerpc,
    vmx_v24_powerpc,    vmx_v25_powerpc,  vmx_v26_powerpc, vmx_v27_powerpc,
    vmx_v28_powerpc,    vmx_v29_powerpc,  vmx_v30_powerpc, vmx_v31_powerpc,
    vmx_vrsave_powerpc, vmx_vscr_powerpc,
};

// Number of register sets provided by this context.
enum { k_num_register_sets = 3 };

static const RegisterSet g_reg_sets_powerpc[k_num_register_sets] = {
    {"General Purpose Registers", "gpr", k_num_gpr_registers_powerpc,
     g_gpr_regnums},
    {"Floating Point Registers", "fpr", k_num_fpr_registers_powerpc,
     g_fpr_regnums},
    {"Altivec/VMX Registers", "vmx", k_num_vmx_registers_powerpc,
     g_vmx_regnums},
};

static_assert(k_first_gpr_powerpc == 0,
              "GPRs must index starting at 0, or fix IsGPR()");
bool RegisterContextPOSIX_powerpc::IsGPR(unsigned reg) {
  return (reg <= k_last_gpr_powerpc); // GPR's come first.
}

bool RegisterContextPOSIX_powerpc::IsFPR(unsigned reg) {
  return (reg >= k_first_fpr) && (reg <= k_last_fpr);
}

bool RegisterContextPOSIX_powerpc::IsVMX(unsigned reg) {
  return (reg >= k_first_vmx) && (reg <= k_last_vmx);
}

RegisterContextPOSIX_powerpc::RegisterContextPOSIX_powerpc(
    Thread &thread, uint32_t concrete_frame_idx,
    RegisterInfoInterface *register_info)
    : RegisterContext(thread, concrete_frame_idx) {
  m_register_info_ap.reset(register_info);
}

RegisterContextPOSIX_powerpc::~RegisterContextPOSIX_powerpc() {}

void RegisterContextPOSIX_powerpc::Invalidate() {}

void RegisterContextPOSIX_powerpc::InvalidateAllRegisters() {}

unsigned RegisterContextPOSIX_powerpc::GetRegisterOffset(unsigned reg) {
  assert(reg < k_num_registers_powerpc && "Invalid register number.");
  return GetRegisterInfo()[reg].byte_offset;
}

unsigned RegisterContextPOSIX_powerpc::GetRegisterSize(unsigned reg) {
  assert(reg < k_num_registers_powerpc && "Invalid register number.");
  return GetRegisterInfo()[reg].byte_size;
}

size_t RegisterContextPOSIX_powerpc::GetRegisterCount() {
  size_t num_registers = k_num_registers_powerpc;
  return num_registers;
}

size_t RegisterContextPOSIX_powerpc::GetGPRSize() {
  return m_register_info_ap->GetGPRSize();
}

const RegisterInfo *RegisterContextPOSIX_powerpc::GetRegisterInfo() {
  // Commonly, this method is overridden and g_register_infos is copied and
  // specialized. So, use GetRegisterInfo() rather than g_register_infos in
  // this scope.
  return m_register_info_ap->GetRegisterInfo();
}

const RegisterInfo *
RegisterContextPOSIX_powerpc::GetRegisterInfoAtIndex(size_t reg) {
  if (reg < k_num_registers_powerpc)
    return &GetRegisterInfo()[reg];
  else
    return NULL;
}

size_t RegisterContextPOSIX_powerpc::GetRegisterSetCount() {
  size_t sets = 0;
  for (size_t set = 0; set < k_num_register_sets; ++set) {
    if (IsRegisterSetAvailable(set))
      ++sets;
  }

  return sets;
}

const RegisterSet *RegisterContextPOSIX_powerpc::GetRegisterSet(size_t set) {
  if (IsRegisterSetAvailable(set))
    return &g_reg_sets_powerpc[set];
  else
    return NULL;
}

const char *RegisterContextPOSIX_powerpc::GetRegisterName(unsigned reg) {
  assert(reg < k_num_registers_powerpc && "Invalid register offset.");
  return GetRegisterInfo()[reg].name;
}

lldb::ByteOrder RegisterContextPOSIX_powerpc::GetByteOrder() {
  // Get the target process whose privileged thread was used for the register
  // read.
  lldb::ByteOrder byte_order = eByteOrderInvalid;
  Process *process = CalculateProcess().get();

  if (process)
    byte_order = process->GetByteOrder();
  return byte_order;
}

bool RegisterContextPOSIX_powerpc::IsRegisterSetAvailable(size_t set_index) {
  size_t num_sets = k_num_register_sets;

  return (set_index < num_sets);
}

// Used when parsing DWARF and EH frame information and any other object file
// sections that contain register numbers in them.
uint32_t RegisterContextPOSIX_powerpc::ConvertRegisterKindToRegisterNumber(
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
