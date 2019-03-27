//===-- RegisterContextPOSIXProcessMonitor_arm64.cpp -----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===---------------------------------------------------------------------===//

#include "lldb/Target/Thread.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/RegisterValue.h"

#include "Plugins/Process/Utility/RegisterContextPOSIX_arm64.h"
#include "ProcessFreeBSD.h"
#include "ProcessMonitor.h"
#include "RegisterContextPOSIXProcessMonitor_arm64.h"

#define REG_CONTEXT_SIZE (GetGPRSize())

using namespace lldb;
using namespace lldb_private;

RegisterContextPOSIXProcessMonitor_arm64::
    RegisterContextPOSIXProcessMonitor_arm64(
        lldb_private::Thread &thread, uint32_t concrete_frame_idx,
        lldb_private::RegisterInfoInterface *register_info)
    : RegisterContextPOSIX_arm64(thread, concrete_frame_idx, register_info) {}

ProcessMonitor &RegisterContextPOSIXProcessMonitor_arm64::GetMonitor() {
  lldb::ProcessSP base = CalculateProcess();
  ProcessFreeBSD *process = static_cast<ProcessFreeBSD *>(base.get());
  return process->GetMonitor();
}

bool RegisterContextPOSIXProcessMonitor_arm64::ReadGPR() {
  ProcessMonitor &monitor = GetMonitor();
  return monitor.ReadGPR(m_thread.GetID(), &m_gpr_arm64, GetGPRSize());
}

bool RegisterContextPOSIXProcessMonitor_arm64::ReadFPR() {
  ProcessMonitor &monitor = GetMonitor();
  return monitor.ReadFPR(m_thread.GetID(), &m_fpr, sizeof m_fpr);
}

bool RegisterContextPOSIXProcessMonitor_arm64::WriteGPR() {
  ProcessMonitor &monitor = GetMonitor();
  return monitor.WriteGPR(m_thread.GetID(), &m_gpr_arm64, GetGPRSize());
}

bool RegisterContextPOSIXProcessMonitor_arm64::WriteFPR() {
  ProcessMonitor &monitor = GetMonitor();
  return monitor.WriteFPR(m_thread.GetID(), &m_fpr, sizeof m_fpr);
}

bool RegisterContextPOSIXProcessMonitor_arm64::ReadRegister(
    const unsigned reg, lldb_private::RegisterValue &value) {
  ProcessMonitor &monitor = GetMonitor();
  return monitor.ReadRegisterValue(m_thread.GetID(), GetRegisterOffset(reg),
                                   GetRegisterName(reg), GetRegisterSize(reg),
                                   value);
}

bool RegisterContextPOSIXProcessMonitor_arm64::WriteRegister(
    const unsigned reg, const lldb_private::RegisterValue &value) {
  unsigned reg_to_write = reg;
  lldb_private::RegisterValue value_to_write = value;

  // Check if this is a subregister of a full register.
  const lldb_private::RegisterInfo *reg_info = GetRegisterInfoAtIndex(reg);
  if (reg_info->invalidate_regs &&
      (reg_info->invalidate_regs[0] != LLDB_INVALID_REGNUM)) {
    lldb_private::RegisterValue full_value;
    uint32_t full_reg = reg_info->invalidate_regs[0];
    const lldb_private::RegisterInfo *full_reg_info =
        GetRegisterInfoAtIndex(full_reg);

    // Read the full register.
    if (ReadRegister(full_reg_info, full_value)) {
      lldb_private::Status error;
      lldb::ByteOrder byte_order = GetByteOrder();
      uint8_t dst[lldb_private::RegisterValue::kMaxRegisterByteSize];

      // Get the bytes for the full register.
      const uint32_t dest_size = full_value.GetAsMemoryData(
          full_reg_info, dst, sizeof(dst), byte_order, error);
      if (error.Success() && dest_size) {
        uint8_t src[lldb_private::RegisterValue::kMaxRegisterByteSize];

        // Get the bytes for the source data.
        const uint32_t src_size = value.GetAsMemoryData(
            reg_info, src, sizeof(src), byte_order, error);
        if (error.Success() && src_size && (src_size < dest_size)) {
          // Copy the src bytes to the destination.
          ::memcpy(dst + (reg_info->byte_offset & 0x1), src, src_size);
          // Set this full register as the value to write.
          value_to_write.SetBytes(dst, full_value.GetByteSize(), byte_order);
          value_to_write.SetType(full_reg_info);
          reg_to_write = full_reg;
        }
      }
    }
  }

  ProcessMonitor &monitor = GetMonitor();
  return monitor.WriteRegisterValue(
      m_thread.GetID(), GetRegisterOffset(reg_to_write),
      GetRegisterName(reg_to_write), value_to_write);
}

bool RegisterContextPOSIXProcessMonitor_arm64::ReadRegister(
    const lldb_private::RegisterInfo *reg_info,
    lldb_private::RegisterValue &value) {
  if (!reg_info)
    return false;

  const uint32_t reg = reg_info->kinds[lldb::eRegisterKindLLDB];

  if (IsFPR(reg)) {
    if (!ReadFPR())
      return false;
  } else {
    uint32_t full_reg = reg;
    bool is_subreg = reg_info->invalidate_regs &&
                     (reg_info->invalidate_regs[0] != LLDB_INVALID_REGNUM);

    if (is_subreg) {
      // Read the full aligned 64-bit register.
      full_reg = reg_info->invalidate_regs[0];
    }
    return ReadRegister(full_reg, value);
  }

  // Get pointer to m_fpr variable and set the data from it.
  assert(reg_info->byte_offset < sizeof m_fpr);
  uint8_t *src = (uint8_t *)&m_fpr + reg_info->byte_offset;
  switch (reg_info->byte_size) {
  case 2:
    value.SetUInt16(*(uint16_t *)src);
    return true;
  case 4:
    value.SetUInt32(*(uint32_t *)src);
    return true;
  case 8:
    value.SetUInt64(*(uint64_t *)src);
    return true;
  default:
    assert(false && "Unhandled data size.");
    return false;
  }
}

bool RegisterContextPOSIXProcessMonitor_arm64::WriteRegister(
    const lldb_private::RegisterInfo *reg_info,
    const lldb_private::RegisterValue &value) {
  const uint32_t reg = reg_info->kinds[lldb::eRegisterKindLLDB];

  if (IsGPR(reg))
    return WriteRegister(reg, value);

  return false;
}

bool RegisterContextPOSIXProcessMonitor_arm64::ReadAllRegisterValues(
    lldb::DataBufferSP &data_sp) {
  bool success = false;
  data_sp.reset(new lldb_private::DataBufferHeap(REG_CONTEXT_SIZE, 0));
  if (data_sp && ReadGPR() && ReadFPR()) {
    uint8_t *dst = data_sp->GetBytes();
    success = dst != 0;

    if (success) {
      ::memcpy(dst, &m_gpr_arm64, GetGPRSize());
      dst += GetGPRSize();
      ::memcpy(dst, &m_fpr, sizeof m_fpr);
    }
  }
  return success;
}

bool RegisterContextPOSIXProcessMonitor_arm64::WriteAllRegisterValues(
    const lldb::DataBufferSP &data_sp) {
  bool success = false;
  if (data_sp && data_sp->GetByteSize() == REG_CONTEXT_SIZE) {
    uint8_t *src = data_sp->GetBytes();
    if (src) {
      ::memcpy(&m_gpr_arm64, src, GetGPRSize());
      if (WriteGPR()) {
        src += GetGPRSize();
        ::memcpy(&m_fpr, src, sizeof m_fpr);
        success = WriteFPR();
      }
    }
  }
  return success;
}

uint32_t RegisterContextPOSIXProcessMonitor_arm64::SetHardwareWatchpoint(
    lldb::addr_t addr, size_t size, bool read, bool write) {
  const uint32_t num_hw_watchpoints = NumSupportedHardwareWatchpoints();
  uint32_t hw_index;

  for (hw_index = 0; hw_index < num_hw_watchpoints; ++hw_index) {
    if (IsWatchpointVacant(hw_index))
      return SetHardwareWatchpointWithIndex(addr, size, read, write, hw_index);
  }

  return LLDB_INVALID_INDEX32;
}

bool RegisterContextPOSIXProcessMonitor_arm64::ClearHardwareWatchpoint(
    uint32_t hw_index) {
  return false;
}

bool RegisterContextPOSIXProcessMonitor_arm64::HardwareSingleStep(bool enable) {
  return false;
}

bool RegisterContextPOSIXProcessMonitor_arm64::UpdateAfterBreakpoint() {
  if (GetPC() == LLDB_INVALID_ADDRESS)
    return false;

  return true;
}

unsigned RegisterContextPOSIXProcessMonitor_arm64::GetRegisterIndexFromOffset(
    unsigned offset) {
  unsigned reg;
  for (reg = 0; reg < k_num_registers_arm64; reg++) {
    if (GetRegisterInfo()[reg].byte_offset == offset)
      break;
  }
  assert(reg < k_num_registers_arm64 && "Invalid register offset.");
  return reg;
}

bool RegisterContextPOSIXProcessMonitor_arm64::IsWatchpointHit(
    uint32_t hw_index) {
  return false;
}

bool RegisterContextPOSIXProcessMonitor_arm64::ClearWatchpointHits() {
  return false;
}

lldb::addr_t RegisterContextPOSIXProcessMonitor_arm64::GetWatchpointAddress(
    uint32_t hw_index) {
  return LLDB_INVALID_ADDRESS;
}

bool RegisterContextPOSIXProcessMonitor_arm64::IsWatchpointVacant(
    uint32_t hw_index) {
  return false;
}

bool RegisterContextPOSIXProcessMonitor_arm64::SetHardwareWatchpointWithIndex(
    lldb::addr_t addr, size_t size, bool read, bool write, uint32_t hw_index) {
  return false;
}

uint32_t
RegisterContextPOSIXProcessMonitor_arm64::NumSupportedHardwareWatchpoints() {
  return 0;
}
