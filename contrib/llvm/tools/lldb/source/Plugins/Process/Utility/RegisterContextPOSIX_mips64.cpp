//===-- RegisterContextPOSIX_mips64.cpp -------------------------*- C++ -*-===//
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

#include "RegisterContextPOSIX_mips64.h"
#include "RegisterContextFreeBSD_mips64.h"
#include "RegisterContextLinux_mips64.h"
#include "RegisterContextLinux_mips.h"

using namespace lldb_private;
using namespace lldb;

bool RegisterContextPOSIX_mips64::IsGPR(unsigned reg) {
  return reg < m_registers_count[gpr_registers_count]; // GPR's come first.
}

bool RegisterContextPOSIX_mips64::IsFPR(unsigned reg) {
  int set = GetRegisterSetCount();
  if (set > 1)
    return reg < (m_registers_count[fpr_registers_count]
                  + m_registers_count[gpr_registers_count]);
  return false;
}

RegisterContextPOSIX_mips64::RegisterContextPOSIX_mips64(
    Thread &thread, uint32_t concrete_frame_idx,
    RegisterInfoInterface *register_info)
    : RegisterContext(thread, concrete_frame_idx) {
  m_register_info_ap.reset(register_info);
  m_num_registers = GetRegisterCount();
  int set = GetRegisterSetCount();

  const RegisterSet *reg_set_ptr;
  for(int i = 0; i < set; ++i) {
      reg_set_ptr = GetRegisterSet(i);
      m_registers_count[i] = reg_set_ptr->num_registers;
  }

  assert(m_num_registers ==
         static_cast<uint32_t>(m_registers_count[gpr_registers_count] +
                               m_registers_count[fpr_registers_count] +
                               m_registers_count[msa_registers_count]));
}

RegisterContextPOSIX_mips64::~RegisterContextPOSIX_mips64() {}

void RegisterContextPOSIX_mips64::Invalidate() {}

void RegisterContextPOSIX_mips64::InvalidateAllRegisters() {}

unsigned RegisterContextPOSIX_mips64::GetRegisterOffset(unsigned reg) {
  assert(reg < m_num_registers && "Invalid register number.");
  return GetRegisterInfo()[reg].byte_offset;
}

unsigned RegisterContextPOSIX_mips64::GetRegisterSize(unsigned reg) {
  assert(reg < m_num_registers && "Invalid register number.");
  return GetRegisterInfo()[reg].byte_size;
}

size_t RegisterContextPOSIX_mips64::GetRegisterCount() {
  return m_register_info_ap->GetRegisterCount();
}

size_t RegisterContextPOSIX_mips64::GetGPRSize() {
  return m_register_info_ap->GetGPRSize();
}

const RegisterInfo *RegisterContextPOSIX_mips64::GetRegisterInfo() {
  // Commonly, this method is overridden and g_register_infos is copied and
  // specialized. So, use GetRegisterInfo() rather than g_register_infos in
  // this scope.
  return m_register_info_ap->GetRegisterInfo();
}

const RegisterInfo *
RegisterContextPOSIX_mips64::GetRegisterInfoAtIndex(size_t reg) {
  if (reg < m_num_registers)
    return &GetRegisterInfo()[reg];
  else
    return NULL;
}

size_t RegisterContextPOSIX_mips64::GetRegisterSetCount() {
  ArchSpec target_arch = m_register_info_ap->GetTargetArchitecture();
  switch (target_arch.GetTriple().getOS()) {
  case llvm::Triple::Linux: {
    if ((target_arch.GetMachine() == llvm::Triple::mipsel) ||
         (target_arch.GetMachine() == llvm::Triple::mips)) {
      const auto *context = static_cast<const RegisterContextLinux_mips *>
                                        (m_register_info_ap.get());
      return context->GetRegisterSetCount();
    }
    const auto *context = static_cast<const RegisterContextLinux_mips64 *>
                                      (m_register_info_ap.get());
    return context->GetRegisterSetCount();
  }
  default: {
    const auto *context = static_cast<const RegisterContextFreeBSD_mips64 *>
                                      (m_register_info_ap.get());
    return context->GetRegisterSetCount();
  }
                       
  }
}

const RegisterSet *RegisterContextPOSIX_mips64::GetRegisterSet(size_t set) {
  ArchSpec target_arch = m_register_info_ap->GetTargetArchitecture();
  switch (target_arch.GetTriple().getOS()) {
  case llvm::Triple::Linux: {
    if ((target_arch.GetMachine() == llvm::Triple::mipsel) ||
         (target_arch.GetMachine() == llvm::Triple::mips)) {
      const auto *context = static_cast<const RegisterContextLinux_mips *>
                                        (m_register_info_ap.get());
      return context->GetRegisterSet(set);
    }
    const auto *context = static_cast<const RegisterContextLinux_mips64 *>
                                      (m_register_info_ap.get());
    return context->GetRegisterSet(set);
  }
  default: {
    const auto *context = static_cast<const RegisterContextFreeBSD_mips64 *>
                                       (m_register_info_ap.get());
    return context->GetRegisterSet(set);
  }
  }
}

const char *RegisterContextPOSIX_mips64::GetRegisterName(unsigned reg) {
  assert(reg < m_num_registers && "Invalid register offset.");
  return GetRegisterInfo()[reg].name;
}

lldb::ByteOrder RegisterContextPOSIX_mips64::GetByteOrder() {
  // Get the target process whose privileged thread was used for the register
  // read.
  lldb::ByteOrder byte_order = eByteOrderInvalid;
  Process *process = CalculateProcess().get();

  if (process)
    byte_order = process->GetByteOrder();
  return byte_order;
}

bool RegisterContextPOSIX_mips64::IsRegisterSetAvailable(size_t set_index) {
  size_t num_sets = GetRegisterSetCount();

  return (set_index < num_sets);
}

// Used when parsing DWARF and EH frame information and any other object file
// sections that contain register numbers in them.
uint32_t RegisterContextPOSIX_mips64::ConvertRegisterKindToRegisterNumber(
    lldb::RegisterKind kind, uint32_t num) {
  const uint32_t num_regs = m_num_registers;

  assert(kind < kNumRegisterKinds);
  for (uint32_t reg_idx = 0; reg_idx < num_regs; ++reg_idx) {
    const RegisterInfo *reg_info = GetRegisterInfoAtIndex(reg_idx);

    if (reg_info->kinds[kind] == num)
      return reg_idx;
  }

  return LLDB_INVALID_REGNUM;
}
