//===-- RegisterContextMemory.cpp -------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "RegisterContextMemory.h"

#include "DynamicRegisterInfo.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/Utility/Status.h"

using namespace lldb;
using namespace lldb_private;

//----------------------------------------------------------------------
// RegisterContextMemory constructor
//----------------------------------------------------------------------
RegisterContextMemory::RegisterContextMemory(Thread &thread,
                                             uint32_t concrete_frame_idx,
                                             DynamicRegisterInfo &reg_infos,
                                             addr_t reg_data_addr)
    : RegisterContext(thread, concrete_frame_idx), m_reg_infos(reg_infos),
      m_reg_valid(), m_reg_data(), m_reg_data_addr(reg_data_addr) {
  // Resize our vector of bools to contain one bool for every register. We will
  // use these boolean values to know when a register value is valid in
  // m_reg_data.
  const size_t num_regs = reg_infos.GetNumRegisters();
  assert(num_regs > 0);
  m_reg_valid.resize(num_regs);

  // Make a heap based buffer that is big enough to store all registers
  DataBufferSP reg_data_sp(
      new DataBufferHeap(reg_infos.GetRegisterDataByteSize(), 0));
  m_reg_data.SetData(reg_data_sp);
}

//----------------------------------------------------------------------
// Destructor
//----------------------------------------------------------------------
RegisterContextMemory::~RegisterContextMemory() {}

void RegisterContextMemory::InvalidateAllRegisters() {
  if (m_reg_data_addr != LLDB_INVALID_ADDRESS)
    SetAllRegisterValid(false);
}

void RegisterContextMemory::SetAllRegisterValid(bool b) {
  std::vector<bool>::iterator pos, end = m_reg_valid.end();
  for (pos = m_reg_valid.begin(); pos != end; ++pos)
    *pos = b;
}

size_t RegisterContextMemory::GetRegisterCount() {
  return m_reg_infos.GetNumRegisters();
}

const RegisterInfo *RegisterContextMemory::GetRegisterInfoAtIndex(size_t reg) {
  return m_reg_infos.GetRegisterInfoAtIndex(reg);
}

size_t RegisterContextMemory::GetRegisterSetCount() {
  return m_reg_infos.GetNumRegisterSets();
}

const RegisterSet *RegisterContextMemory::GetRegisterSet(size_t reg_set) {
  return m_reg_infos.GetRegisterSet(reg_set);
}

uint32_t RegisterContextMemory::ConvertRegisterKindToRegisterNumber(
    lldb::RegisterKind kind, uint32_t num) {
  return m_reg_infos.ConvertRegisterKindToRegisterNumber(kind, num);
}

bool RegisterContextMemory::ReadRegister(const RegisterInfo *reg_info,
                                         RegisterValue &reg_value) {
  const uint32_t reg_num = reg_info->kinds[eRegisterKindLLDB];
  if (!m_reg_valid[reg_num]) {
    if (!ReadAllRegisterValues(m_reg_data.GetSharedDataBuffer()))
      return false;
  }
  const bool partial_data_ok = false;
  return reg_value
      .SetValueFromData(reg_info, m_reg_data, reg_info->byte_offset,
                        partial_data_ok)
      .Success();
}

bool RegisterContextMemory::WriteRegister(const RegisterInfo *reg_info,
                                          const RegisterValue &reg_value) {
  if (m_reg_data_addr != LLDB_INVALID_ADDRESS) {
    const uint32_t reg_num = reg_info->kinds[eRegisterKindLLDB];
    addr_t reg_addr = m_reg_data_addr + reg_info->byte_offset;
    Status error(WriteRegisterValueToMemory(reg_info, reg_addr,
                                            reg_info->byte_size, reg_value));
    m_reg_valid[reg_num] = false;
    return error.Success();
  }
  return false;
}

bool RegisterContextMemory::ReadAllRegisterValues(DataBufferSP &data_sp) {
  if (m_reg_data_addr != LLDB_INVALID_ADDRESS) {
    ProcessSP process_sp(CalculateProcess());
    if (process_sp) {
      Status error;
      if (process_sp->ReadMemory(m_reg_data_addr, data_sp->GetBytes(),
                                 data_sp->GetByteSize(),
                                 error) == data_sp->GetByteSize()) {
        SetAllRegisterValid(true);
        return true;
      }
    }
  }
  return false;
}

bool RegisterContextMemory::WriteAllRegisterValues(
    const DataBufferSP &data_sp) {
  if (m_reg_data_addr != LLDB_INVALID_ADDRESS) {
    ProcessSP process_sp(CalculateProcess());
    if (process_sp) {
      Status error;
      SetAllRegisterValid(false);
      if (process_sp->WriteMemory(m_reg_data_addr, data_sp->GetBytes(),
                                  data_sp->GetByteSize(),
                                  error) == data_sp->GetByteSize())
        return true;
    }
  }
  return false;
}

void RegisterContextMemory::SetAllRegisterData(
    const lldb::DataBufferSP &data_sp) {
  m_reg_data.SetData(data_sp);
  SetAllRegisterValid(true);
}
