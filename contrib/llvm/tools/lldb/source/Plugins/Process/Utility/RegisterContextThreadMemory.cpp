//===-- RegisterContextThreadMemory.cpp -------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/OperatingSystem.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/Status.h"
#include "lldb/lldb-private.h"

#include "RegisterContextThreadMemory.h"

using namespace lldb;
using namespace lldb_private;

RegisterContextThreadMemory::RegisterContextThreadMemory(
    Thread &thread, lldb::addr_t register_data_addr)
    : RegisterContext(thread, 0), m_thread_wp(thread.shared_from_this()),
      m_reg_ctx_sp(), m_register_data_addr(register_data_addr), m_stop_id(0) {}

RegisterContextThreadMemory::~RegisterContextThreadMemory() {}

void RegisterContextThreadMemory::UpdateRegisterContext() {
  ThreadSP thread_sp(m_thread_wp.lock());
  if (thread_sp) {
    ProcessSP process_sp(thread_sp->GetProcess());

    if (process_sp) {
      const uint32_t stop_id = process_sp->GetModID().GetStopID();
      if (m_stop_id != stop_id) {
        m_stop_id = stop_id;
        m_reg_ctx_sp.reset();
      }
      if (!m_reg_ctx_sp) {
        ThreadSP backing_thread_sp(thread_sp->GetBackingThread());
        if (backing_thread_sp) {
          m_reg_ctx_sp = backing_thread_sp->GetRegisterContext();
        } else {
          OperatingSystem *os = process_sp->GetOperatingSystem();
          if (os->IsOperatingSystemPluginThread(thread_sp))
            m_reg_ctx_sp = os->CreateRegisterContextForThread(
                thread_sp.get(), m_register_data_addr);
        }
      }
    } else {
      m_reg_ctx_sp.reset();
    }
  } else {
    m_reg_ctx_sp.reset();
  }
}

//------------------------------------------------------------------
// Subclasses must override these functions
//------------------------------------------------------------------
void RegisterContextThreadMemory::InvalidateAllRegisters() {
  UpdateRegisterContext();
  if (m_reg_ctx_sp)
    m_reg_ctx_sp->InvalidateAllRegisters();
}

size_t RegisterContextThreadMemory::GetRegisterCount() {
  UpdateRegisterContext();
  if (m_reg_ctx_sp)
    return m_reg_ctx_sp->GetRegisterCount();
  return 0;
}

const RegisterInfo *
RegisterContextThreadMemory::GetRegisterInfoAtIndex(size_t reg) {
  UpdateRegisterContext();
  if (m_reg_ctx_sp)
    return m_reg_ctx_sp->GetRegisterInfoAtIndex(reg);
  return NULL;
}

size_t RegisterContextThreadMemory::GetRegisterSetCount() {
  UpdateRegisterContext();
  if (m_reg_ctx_sp)
    return m_reg_ctx_sp->GetRegisterSetCount();
  return 0;
}

const RegisterSet *RegisterContextThreadMemory::GetRegisterSet(size_t reg_set) {
  UpdateRegisterContext();
  if (m_reg_ctx_sp)
    return m_reg_ctx_sp->GetRegisterSet(reg_set);
  return NULL;
}

bool RegisterContextThreadMemory::ReadRegister(const RegisterInfo *reg_info,
                                               RegisterValue &reg_value) {
  UpdateRegisterContext();
  if (m_reg_ctx_sp)
    return m_reg_ctx_sp->ReadRegister(reg_info, reg_value);
  return false;
}

bool RegisterContextThreadMemory::WriteRegister(
    const RegisterInfo *reg_info, const RegisterValue &reg_value) {
  UpdateRegisterContext();
  if (m_reg_ctx_sp)
    return m_reg_ctx_sp->WriteRegister(reg_info, reg_value);
  return false;
}

bool RegisterContextThreadMemory::ReadAllRegisterValues(
    lldb::DataBufferSP &data_sp) {
  UpdateRegisterContext();
  if (m_reg_ctx_sp)
    return m_reg_ctx_sp->ReadAllRegisterValues(data_sp);
  return false;
}

bool RegisterContextThreadMemory::WriteAllRegisterValues(
    const lldb::DataBufferSP &data_sp) {
  UpdateRegisterContext();
  if (m_reg_ctx_sp)
    return m_reg_ctx_sp->WriteAllRegisterValues(data_sp);
  return false;
}

bool RegisterContextThreadMemory::CopyFromRegisterContext(
    lldb::RegisterContextSP reg_ctx_sp) {
  UpdateRegisterContext();
  if (m_reg_ctx_sp)
    return m_reg_ctx_sp->CopyFromRegisterContext(reg_ctx_sp);
  return false;
}

uint32_t RegisterContextThreadMemory::ConvertRegisterKindToRegisterNumber(
    lldb::RegisterKind kind, uint32_t num) {
  UpdateRegisterContext();
  if (m_reg_ctx_sp)
    return m_reg_ctx_sp->ConvertRegisterKindToRegisterNumber(kind, num);
  return false;
}

uint32_t RegisterContextThreadMemory::NumSupportedHardwareBreakpoints() {
  UpdateRegisterContext();
  if (m_reg_ctx_sp)
    return m_reg_ctx_sp->NumSupportedHardwareBreakpoints();
  return false;
}

uint32_t RegisterContextThreadMemory::SetHardwareBreakpoint(lldb::addr_t addr,
                                                            size_t size) {
  UpdateRegisterContext();
  if (m_reg_ctx_sp)
    return m_reg_ctx_sp->SetHardwareBreakpoint(addr, size);
  return 0;
}

bool RegisterContextThreadMemory::ClearHardwareBreakpoint(uint32_t hw_idx) {
  UpdateRegisterContext();
  if (m_reg_ctx_sp)
    return m_reg_ctx_sp->ClearHardwareBreakpoint(hw_idx);
  return false;
}

uint32_t RegisterContextThreadMemory::NumSupportedHardwareWatchpoints() {
  UpdateRegisterContext();
  if (m_reg_ctx_sp)
    return m_reg_ctx_sp->NumSupportedHardwareWatchpoints();
  return 0;
}

uint32_t RegisterContextThreadMemory::SetHardwareWatchpoint(lldb::addr_t addr,
                                                            size_t size,
                                                            bool read,
                                                            bool write) {
  UpdateRegisterContext();
  if (m_reg_ctx_sp)
    return m_reg_ctx_sp->SetHardwareWatchpoint(addr, size, read, write);
  return 0;
}

bool RegisterContextThreadMemory::ClearHardwareWatchpoint(uint32_t hw_index) {
  UpdateRegisterContext();
  if (m_reg_ctx_sp)
    return m_reg_ctx_sp->ClearHardwareWatchpoint(hw_index);
  return false;
}

bool RegisterContextThreadMemory::HardwareSingleStep(bool enable) {
  UpdateRegisterContext();
  if (m_reg_ctx_sp)
    return m_reg_ctx_sp->HardwareSingleStep(enable);
  return false;
}

Status RegisterContextThreadMemory::ReadRegisterValueFromMemory(
    const lldb_private::RegisterInfo *reg_info, lldb::addr_t src_addr,
    uint32_t src_len, RegisterValue &reg_value) {
  UpdateRegisterContext();
  if (m_reg_ctx_sp)
    return m_reg_ctx_sp->ReadRegisterValueFromMemory(reg_info, src_addr,
                                                     src_len, reg_value);
  Status error;
  error.SetErrorString("invalid register context");
  return error;
}

Status RegisterContextThreadMemory::WriteRegisterValueToMemory(
    const lldb_private::RegisterInfo *reg_info, lldb::addr_t dst_addr,
    uint32_t dst_len, const RegisterValue &reg_value) {
  UpdateRegisterContext();
  if (m_reg_ctx_sp)
    return m_reg_ctx_sp->WriteRegisterValueToMemory(reg_info, dst_addr, dst_len,
                                                    reg_value);
  Status error;
  error.SetErrorString("invalid register context");
  return error;
}
