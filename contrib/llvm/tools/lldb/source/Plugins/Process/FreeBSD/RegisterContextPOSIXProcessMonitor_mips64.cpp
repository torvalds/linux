//===-- RegisterContextPOSIXProcessMonitor_mips64.cpp -----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/Thread.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/RegisterValue.h"

#include "Plugins/Process/Utility/RegisterContextPOSIX_mips64.h"
#include "ProcessFreeBSD.h"
#include "ProcessMonitor.h"
#include "RegisterContextPOSIXProcessMonitor_mips64.h"

using namespace lldb_private;
using namespace lldb;

#define REG_CONTEXT_SIZE (GetGPRSize())

RegisterContextPOSIXProcessMonitor_mips64::
    RegisterContextPOSIXProcessMonitor_mips64(
        Thread &thread, uint32_t concrete_frame_idx,
        lldb_private::RegisterInfoInterface *register_info)
    : RegisterContextPOSIX_mips64(thread, concrete_frame_idx, register_info) {}

ProcessMonitor &RegisterContextPOSIXProcessMonitor_mips64::GetMonitor() {
  ProcessSP base = CalculateProcess();
  ProcessFreeBSD *process = static_cast<ProcessFreeBSD *>(base.get());
  return process->GetMonitor();
}

bool RegisterContextPOSIXProcessMonitor_mips64::ReadGPR() {
  ProcessMonitor &monitor = GetMonitor();
  return monitor.ReadGPR(m_thread.GetID(), &m_gpr_mips64, GetGPRSize());
}

bool RegisterContextPOSIXProcessMonitor_mips64::ReadFPR() {
  // XXX not yet implemented
  return false;
}

bool RegisterContextPOSIXProcessMonitor_mips64::WriteGPR() {
  ProcessMonitor &monitor = GetMonitor();
  return monitor.WriteGPR(m_thread.GetID(), &m_gpr_mips64, GetGPRSize());
}

bool RegisterContextPOSIXProcessMonitor_mips64::WriteFPR() {
  // XXX not yet implemented
  return false;
}

bool RegisterContextPOSIXProcessMonitor_mips64::ReadRegister(
    const unsigned reg, RegisterValue &value) {
  ProcessMonitor &monitor = GetMonitor();
  return monitor.ReadRegisterValue(m_thread.GetID(), GetRegisterOffset(reg),
                                   GetRegisterName(reg), GetRegisterSize(reg),
                                   value);
}

bool RegisterContextPOSIXProcessMonitor_mips64::WriteRegister(
    const unsigned reg, const RegisterValue &value) {
  unsigned reg_to_write = reg;
  RegisterValue value_to_write = value;

  // Check if this is a subregister of a full register.
  const RegisterInfo *reg_info = GetRegisterInfoAtIndex(reg);
  if (reg_info->invalidate_regs &&
      (reg_info->invalidate_regs[0] != LLDB_INVALID_REGNUM)) {
    RegisterValue full_value;
    uint32_t full_reg = reg_info->invalidate_regs[0];
    const RegisterInfo *full_reg_info = GetRegisterInfoAtIndex(full_reg);

    // Read the full register.
    if (ReadRegister(full_reg_info, full_value)) {
      Status error;
      ByteOrder byte_order = GetByteOrder();
      uint8_t dst[RegisterValue::kMaxRegisterByteSize];

      // Get the bytes for the full register.
      const uint32_t dest_size = full_value.GetAsMemoryData(
          full_reg_info, dst, sizeof(dst), byte_order, error);
      if (error.Success() && dest_size) {
        uint8_t src[RegisterValue::kMaxRegisterByteSize];

        // Get the bytes for the source data.
        const uint32_t src_size = value.GetAsMemoryData(
            reg_info, src, sizeof(src), byte_order, error);
        if (error.Success() && src_size && (src_size < dest_size)) {
          // Copy the src bytes to the destination.
          memcpy(dst + (reg_info->byte_offset & 0x1), src, src_size);
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

bool RegisterContextPOSIXProcessMonitor_mips64::ReadRegister(
    const RegisterInfo *reg_info, RegisterValue &value) {
  if (!reg_info)
    return false;

  const uint32_t reg = reg_info->kinds[eRegisterKindLLDB];

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

    bool success = ReadRegister(full_reg, value);

    if (success) {
      // If our read was not aligned (for ah,bh,ch,dh), shift our returned
      // value one byte to the right.
      if (is_subreg && (reg_info->byte_offset & 0x1))
        value.SetUInt64(value.GetAsUInt64() >> 8);

      // If our return byte size was greater than the return value reg size,
      // then use the type specified by reg_info rather than the uint64_t
      // default
      if (value.GetByteSize() > reg_info->byte_size)
        value.SetType(reg_info);
    }
    return success;
  }

  return false;
}

bool RegisterContextPOSIXProcessMonitor_mips64::WriteRegister(
    const RegisterInfo *reg_info, const RegisterValue &value) {
  const uint32_t reg = reg_info->kinds[eRegisterKindLLDB];

  if (IsGPR(reg))
    return WriteRegister(reg, value);

  return false;
}

bool RegisterContextPOSIXProcessMonitor_mips64::ReadAllRegisterValues(
    DataBufferSP &data_sp) {
  bool success = false;
  data_sp.reset(new DataBufferHeap(REG_CONTEXT_SIZE, 0));
  if (data_sp && ReadGPR() && ReadFPR()) {
    uint8_t *dst = data_sp->GetBytes();
    success = dst != 0;

    if (success) {
      ::memcpy(dst, &m_gpr_mips64, GetGPRSize());
    }
  }
  return success;
}

bool RegisterContextPOSIXProcessMonitor_mips64::WriteAllRegisterValues(
    const DataBufferSP &data_sp) {
  bool success = false;
  if (data_sp && data_sp->GetByteSize() == REG_CONTEXT_SIZE) {
    uint8_t *src = data_sp->GetBytes();
    if (src) {
      ::memcpy(&m_gpr_mips64, src, GetGPRSize());

      if (WriteGPR()) {
        src += GetGPRSize();
      }
    }
  }
  return success;
}

uint32_t RegisterContextPOSIXProcessMonitor_mips64::SetHardwareWatchpoint(
    addr_t addr, size_t size, bool read, bool write) {
  const uint32_t num_hw_watchpoints = NumSupportedHardwareWatchpoints();
  uint32_t hw_index;

  for (hw_index = 0; hw_index < num_hw_watchpoints; ++hw_index) {
    if (IsWatchpointVacant(hw_index))
      return SetHardwareWatchpointWithIndex(addr, size, read, write, hw_index);
  }

  return LLDB_INVALID_INDEX32;
}

bool RegisterContextPOSIXProcessMonitor_mips64::ClearHardwareWatchpoint(
    uint32_t hw_index) {
  return false;
}

bool RegisterContextPOSIXProcessMonitor_mips64::HardwareSingleStep(
    bool enable) {
  return false;
}

bool RegisterContextPOSIXProcessMonitor_mips64::UpdateAfterBreakpoint() {
  // PC points one byte past the int3 responsible for the breakpoint.
  lldb::addr_t pc;

  if ((pc = GetPC()) == LLDB_INVALID_ADDRESS)
    return false;

  SetPC(pc - 1);
  return true;
}

unsigned RegisterContextPOSIXProcessMonitor_mips64::GetRegisterIndexFromOffset(
    unsigned offset) {
  unsigned reg;
  for (reg = 0; reg < k_num_registers_mips64; reg++) {
    if (GetRegisterInfo()[reg].byte_offset == offset)
      break;
  }
  assert(reg < k_num_registers_mips64 && "Invalid register offset.");
  return reg;
}

bool RegisterContextPOSIXProcessMonitor_mips64::IsWatchpointHit(
    uint32_t hw_index) {
  return false;
}

bool RegisterContextPOSIXProcessMonitor_mips64::ClearWatchpointHits() {
  return false;
}

addr_t RegisterContextPOSIXProcessMonitor_mips64::GetWatchpointAddress(
    uint32_t hw_index) {
  return LLDB_INVALID_ADDRESS;
}

bool RegisterContextPOSIXProcessMonitor_mips64::IsWatchpointVacant(
    uint32_t hw_index) {
  return false;
}

bool RegisterContextPOSIXProcessMonitor_mips64::SetHardwareWatchpointWithIndex(
    addr_t addr, size_t size, bool read, bool write, uint32_t hw_index) {
  return false;
}

uint32_t
RegisterContextPOSIXProcessMonitor_mips64::NumSupportedHardwareWatchpoints() {
  return 0;
}
