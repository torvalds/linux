//===-- RegisterContextPOSIXProcessMonitor_x86.cpp --------------*- C++ -*-===//
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

#include "Plugins/Process/FreeBSD/ProcessFreeBSD.h"
#include "Plugins/Process/FreeBSD/ProcessMonitor.h"
#include "RegisterContextPOSIXProcessMonitor_x86.h"

using namespace lldb_private;
using namespace lldb;

// Support ptrace extensions even when compiled without required kernel support
#ifndef NT_X86_XSTATE
#define NT_X86_XSTATE 0x202
#endif

#define REG_CONTEXT_SIZE (GetGPRSize() + sizeof(FPR))

static uint32_t size_and_rw_bits(size_t size, bool read, bool write) {
  uint32_t rw;

  if (read)
    rw = 0x3; // READ or READ/WRITE
  else if (write)
    rw = 0x1; // WRITE
  else
    assert(0 && "read and write cannot both be false");

  switch (size) {
  case 1:
    return rw;
  case 2:
    return (0x1 << 2) | rw;
  case 4:
    return (0x3 << 2) | rw;
  case 8:
    return (0x2 << 2) | rw;
  default:
    assert(0 && "invalid size, must be one of 1, 2, 4, or 8");
    return 0; // Unreachable. Just to silence compiler.
  }
}

RegisterContextPOSIXProcessMonitor_x86_64::
    RegisterContextPOSIXProcessMonitor_x86_64(
        Thread &thread, uint32_t concrete_frame_idx,
        lldb_private::RegisterInfoInterface *register_info)
    : RegisterContextPOSIX_x86(thread, concrete_frame_idx, register_info) {
  // Store byte offset of fctrl (i.e. first register of FPR) wrt 'UserArea'
  const RegisterInfo *reg_info_fctrl = GetRegisterInfoByName("fctrl");
  m_fctrl_offset_in_userarea = reg_info_fctrl->byte_offset;

  m_iovec.iov_base = &m_fpr.xsave;
  m_iovec.iov_len = sizeof(m_fpr.xsave);
}

ProcessMonitor &RegisterContextPOSIXProcessMonitor_x86_64::GetMonitor() {
  ProcessSP base = CalculateProcess();
  ProcessFreeBSD *process = static_cast<ProcessFreeBSD *>(base.get());
  return process->GetMonitor();
}

bool RegisterContextPOSIXProcessMonitor_x86_64::ReadGPR() {
  ProcessMonitor &monitor = GetMonitor();
  return monitor.ReadGPR(m_thread.GetID(), &m_gpr_x86_64, GetGPRSize());
}

bool RegisterContextPOSIXProcessMonitor_x86_64::ReadFPR() {
  ProcessMonitor &monitor = GetMonitor();
  if (GetFPRType() == eFXSAVE)
    return monitor.ReadFPR(m_thread.GetID(), &m_fpr.fxsave,
                           sizeof(m_fpr.fxsave));

  if (GetFPRType() == eXSAVE)
    return monitor.ReadRegisterSet(m_thread.GetID(), &m_iovec,
                                   sizeof(m_fpr.xsave), NT_X86_XSTATE);
  return false;
}

bool RegisterContextPOSIXProcessMonitor_x86_64::WriteGPR() {
  ProcessMonitor &monitor = GetMonitor();
  return monitor.WriteGPR(m_thread.GetID(), &m_gpr_x86_64, GetGPRSize());
}

bool RegisterContextPOSIXProcessMonitor_x86_64::WriteFPR() {
  ProcessMonitor &monitor = GetMonitor();
  if (GetFPRType() == eFXSAVE)
    return monitor.WriteFPR(m_thread.GetID(), &m_fpr.fxsave,
                            sizeof(m_fpr.fxsave));

  if (GetFPRType() == eXSAVE)
    return monitor.WriteRegisterSet(m_thread.GetID(), &m_iovec,
                                    sizeof(m_fpr.xsave), NT_X86_XSTATE);
  return false;
}

bool RegisterContextPOSIXProcessMonitor_x86_64::ReadRegister(
    const unsigned reg, RegisterValue &value) {
  ProcessMonitor &monitor = GetMonitor();

#if defined(__FreeBSD__)
  if (reg >= m_reg_info.first_dr)
    return monitor.ReadDebugRegisterValue(
        m_thread.GetID(), GetRegisterOffset(reg), GetRegisterName(reg),
        GetRegisterSize(reg), value);
#endif
  return monitor.ReadRegisterValue(m_thread.GetID(), GetRegisterOffset(reg),
                                   GetRegisterName(reg), GetRegisterSize(reg),
                                   value);
}

bool RegisterContextPOSIXProcessMonitor_x86_64::WriteRegister(
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
#if defined(__FreeBSD__)
  if (reg >= m_reg_info.first_dr)
    return monitor.WriteDebugRegisterValue(
        m_thread.GetID(), GetRegisterOffset(reg_to_write),
        GetRegisterName(reg_to_write), value_to_write);
#endif
  return monitor.WriteRegisterValue(
      m_thread.GetID(), GetRegisterOffset(reg_to_write),
      GetRegisterName(reg_to_write), value_to_write);
}

bool RegisterContextPOSIXProcessMonitor_x86_64::ReadRegister(
    const RegisterInfo *reg_info, RegisterValue &value) {
  if (!reg_info)
    return false;

  const uint32_t reg = reg_info->kinds[eRegisterKindLLDB];

  if (IsFPR(reg, GetFPRType())) {
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

  if (reg_info->encoding == eEncodingVector) {
    ByteOrder byte_order = GetByteOrder();

    if (byte_order != ByteOrder::eByteOrderInvalid) {
      if (reg >= m_reg_info.first_st && reg <= m_reg_info.last_st)
        value.SetBytes(m_fpr.fxsave.stmm[reg - m_reg_info.first_st].bytes,
                       reg_info->byte_size, byte_order);
      if (reg >= m_reg_info.first_mm && reg <= m_reg_info.last_mm)
        value.SetBytes(m_fpr.fxsave.stmm[reg - m_reg_info.first_mm].bytes,
                       reg_info->byte_size, byte_order);
      if (reg >= m_reg_info.first_xmm && reg <= m_reg_info.last_xmm)
        value.SetBytes(m_fpr.fxsave.xmm[reg - m_reg_info.first_xmm].bytes,
                       reg_info->byte_size, byte_order);
      if (reg >= m_reg_info.first_ymm && reg <= m_reg_info.last_ymm) {
        // Concatenate ymm using the register halves in xmm.bytes and
        // ymmh.bytes
        if (GetFPRType() == eXSAVE && CopyXSTATEtoYMM(reg, byte_order))
          value.SetBytes(m_ymm_set.ymm[reg - m_reg_info.first_ymm].bytes,
                         reg_info->byte_size, byte_order);
        else
          return false;
      }
      return value.GetType() == RegisterValue::eTypeBytes;
    }
    return false;
  }

  // Get pointer to m_fpr.fxsave variable and set the data from it. Byte
  // offsets of all registers are calculated wrt 'UserArea' structure. However,
  // ReadFPR() reads fpu registers {using ptrace(PT_GETFPREGS,..)} and stores
  // them in 'm_fpr' (of type FPR structure). To extract values of fpu
  // registers, m_fpr should be read at byte offsets calculated wrt to FPR
  // structure.

  // Since, FPR structure is also one of the member of UserArea structure.
  // byte_offset(fpu wrt FPR) = byte_offset(fpu wrt UserArea) -
  // byte_offset(fctrl wrt UserArea)
  assert((reg_info->byte_offset - m_fctrl_offset_in_userarea) < sizeof(m_fpr));
  uint8_t *src =
      (uint8_t *)&m_fpr + reg_info->byte_offset - m_fctrl_offset_in_userarea;
  switch (reg_info->byte_size) {
  case 1:
    value.SetUInt8(*(uint8_t *)src);
    return true;
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

bool RegisterContextPOSIXProcessMonitor_x86_64::WriteRegister(
    const RegisterInfo *reg_info, const RegisterValue &value) {
  const uint32_t reg = reg_info->kinds[eRegisterKindLLDB];

  if (IsGPR(reg))
    return WriteRegister(reg, value);

  if (IsFPR(reg, GetFPRType())) {
    if (reg_info->encoding == eEncodingVector) {
      if (reg >= m_reg_info.first_st && reg <= m_reg_info.last_st)
        ::memcpy(m_fpr.fxsave.stmm[reg - m_reg_info.first_st].bytes,
                 value.GetBytes(), value.GetByteSize());

      if (reg >= m_reg_info.first_mm && reg <= m_reg_info.last_mm)
        ::memcpy(m_fpr.fxsave.stmm[reg - m_reg_info.first_mm].bytes,
                 value.GetBytes(), value.GetByteSize());

      if (reg >= m_reg_info.first_xmm && reg <= m_reg_info.last_xmm)
        ::memcpy(m_fpr.fxsave.xmm[reg - m_reg_info.first_xmm].bytes,
                 value.GetBytes(), value.GetByteSize());

      if (reg >= m_reg_info.first_ymm && reg <= m_reg_info.last_ymm) {
        if (GetFPRType() != eXSAVE)
          return false; // the target processor does not support AVX

        // Store ymm register content, and split into the register halves in
        // xmm.bytes and ymmh.bytes
        ::memcpy(m_ymm_set.ymm[reg - m_reg_info.first_ymm].bytes,
                 value.GetBytes(), value.GetByteSize());
        if (false == CopyYMMtoXSTATE(reg, GetByteOrder()))
          return false;
      }
    } else {
      // Get pointer to m_fpr.fxsave variable and set the data to it. Byte
      // offsets of all registers are calculated wrt 'UserArea' structure.
      // However, WriteFPR() takes m_fpr (of type FPR structure) and writes
      // only fpu registers using ptrace(PT_SETFPREGS,..) API. Hence fpu
      // registers should be written in m_fpr at byte offsets calculated wrt
      // FPR structure.

      // Since, FPR structure is also one of the member of UserArea structure.
      // byte_offset(fpu wrt FPR) = byte_offset(fpu wrt UserArea) -
      // byte_offset(fctrl wrt UserArea)
      assert((reg_info->byte_offset - m_fctrl_offset_in_userarea) <
             sizeof(m_fpr));
      uint8_t *dst = (uint8_t *)&m_fpr + reg_info->byte_offset -
                     m_fctrl_offset_in_userarea;
      switch (reg_info->byte_size) {
      case 1:
        *(uint8_t *)dst = value.GetAsUInt8();
        break;
      case 2:
        *(uint16_t *)dst = value.GetAsUInt16();
        break;
      case 4:
        *(uint32_t *)dst = value.GetAsUInt32();
        break;
      case 8:
        *(uint64_t *)dst = value.GetAsUInt64();
        break;
      default:
        assert(false && "Unhandled data size.");
        return false;
      }
    }

    if (WriteFPR()) {
      if (IsAVX(reg))
        return CopyYMMtoXSTATE(reg, GetByteOrder());
      return true;
    }
  }
  return false;
}

bool RegisterContextPOSIXProcessMonitor_x86_64::ReadAllRegisterValues(
    DataBufferSP &data_sp) {
  bool success = false;
  data_sp.reset(new DataBufferHeap(REG_CONTEXT_SIZE, 0));
  if (data_sp && ReadGPR() && ReadFPR()) {
    uint8_t *dst = data_sp->GetBytes();
    success = dst != 0;

    if (success) {
      ::memcpy(dst, &m_gpr_x86_64, GetGPRSize());
      dst += GetGPRSize();
      if (GetFPRType() == eFXSAVE)
        ::memcpy(dst, &m_fpr.fxsave, sizeof(m_fpr.fxsave));
    }

    if (GetFPRType() == eXSAVE) {
      ByteOrder byte_order = GetByteOrder();

      // Assemble the YMM register content from the register halves.
      for (uint32_t reg = m_reg_info.first_ymm;
           success && reg <= m_reg_info.last_ymm; ++reg)
        success = CopyXSTATEtoYMM(reg, byte_order);

      if (success) {
        // Copy the extended register state including the assembled ymm
        // registers.
        ::memcpy(dst, &m_fpr, sizeof(m_fpr));
      }
    }
  }
  return success;
}

bool RegisterContextPOSIXProcessMonitor_x86_64::WriteAllRegisterValues(
    const DataBufferSP &data_sp) {
  bool success = false;
  if (data_sp && data_sp->GetByteSize() == REG_CONTEXT_SIZE) {
    uint8_t *src = data_sp->GetBytes();
    if (src) {
      ::memcpy(&m_gpr_x86_64, src, GetGPRSize());

      if (WriteGPR()) {
        src += GetGPRSize();
        if (GetFPRType() == eFXSAVE)
          ::memcpy(&m_fpr.fxsave, src, sizeof(m_fpr.fxsave));
        if (GetFPRType() == eXSAVE)
          ::memcpy(&m_fpr.xsave, src, sizeof(m_fpr.xsave));

        success = WriteFPR();
        if (success) {
          if (GetFPRType() == eXSAVE) {
            ByteOrder byte_order = GetByteOrder();

            // Parse the YMM register content from the register halves.
            for (uint32_t reg = m_reg_info.first_ymm;
                 success && reg <= m_reg_info.last_ymm; ++reg)
              success = CopyYMMtoXSTATE(reg, byte_order);
          }
        }
      }
    }
  }
  return success;
}

uint32_t RegisterContextPOSIXProcessMonitor_x86_64::SetHardwareWatchpoint(
    addr_t addr, size_t size, bool read, bool write) {
  const uint32_t num_hw_watchpoints = NumSupportedHardwareWatchpoints();
  uint32_t hw_index;

  for (hw_index = 0; hw_index < num_hw_watchpoints; ++hw_index) {
    if (IsWatchpointVacant(hw_index))
      return SetHardwareWatchpointWithIndex(addr, size, read, write, hw_index);
  }

  return LLDB_INVALID_INDEX32;
}

bool RegisterContextPOSIXProcessMonitor_x86_64::ClearHardwareWatchpoint(
    uint32_t hw_index) {
  if (hw_index < NumSupportedHardwareWatchpoints()) {
    RegisterValue current_dr7_bits;

    if (ReadRegister(m_reg_info.first_dr + 7, current_dr7_bits)) {
      uint64_t new_dr7_bits =
          current_dr7_bits.GetAsUInt64() & ~(3 << (2 * hw_index));

      if (WriteRegister(m_reg_info.first_dr + 7, RegisterValue(new_dr7_bits)))
        return true;
    }
  }

  return false;
}

bool RegisterContextPOSIXProcessMonitor_x86_64::HardwareSingleStep(
    bool enable) {
  enum { TRACE_BIT = 0x100 };
  uint64_t rflags;

  if ((rflags = ReadRegisterAsUnsigned(m_reg_info.gpr_flags, -1UL)) == -1UL)
    return false;

  if (enable) {
    if (rflags & TRACE_BIT)
      return true;

    rflags |= TRACE_BIT;
  } else {
    if (!(rflags & TRACE_BIT))
      return false;

    rflags &= ~TRACE_BIT;
  }

  return WriteRegisterFromUnsigned(m_reg_info.gpr_flags, rflags);
}

bool RegisterContextPOSIXProcessMonitor_x86_64::UpdateAfterBreakpoint() {
  // PC points one byte past the int3 responsible for the breakpoint.
  lldb::addr_t pc;

  if ((pc = GetPC()) == LLDB_INVALID_ADDRESS)
    return false;

  SetPC(pc - 1);
  return true;
}

unsigned RegisterContextPOSIXProcessMonitor_x86_64::GetRegisterIndexFromOffset(
    unsigned offset) {
  unsigned reg;
  for (reg = 0; reg < m_reg_info.num_registers; reg++) {
    if (GetRegisterInfo()[reg].byte_offset == offset)
      break;
  }
  assert(reg < m_reg_info.num_registers && "Invalid register offset.");
  return reg;
}

bool RegisterContextPOSIXProcessMonitor_x86_64::IsWatchpointHit(
    uint32_t hw_index) {
  bool is_hit = false;

  if (m_watchpoints_initialized == false) {
    // Reset the debug status and debug control registers
    RegisterValue zero_bits = RegisterValue(uint64_t(0));
    if (!WriteRegister(m_reg_info.first_dr + 6, zero_bits) ||
        !WriteRegister(m_reg_info.first_dr + 7, zero_bits))
      assert(false && "Could not initialize watchpoint registers");
    m_watchpoints_initialized = true;
  }

  if (hw_index < NumSupportedHardwareWatchpoints()) {
    RegisterValue value;

    if (ReadRegister(m_reg_info.first_dr + 6, value)) {
      uint64_t val = value.GetAsUInt64();
      is_hit = val & (1 << hw_index);
    }
  }

  return is_hit;
}

bool RegisterContextPOSIXProcessMonitor_x86_64::ClearWatchpointHits() {
  return WriteRegister(m_reg_info.first_dr + 6, RegisterValue((uint64_t)0));
}

addr_t RegisterContextPOSIXProcessMonitor_x86_64::GetWatchpointAddress(
    uint32_t hw_index) {
  addr_t wp_monitor_addr = LLDB_INVALID_ADDRESS;

  if (hw_index < NumSupportedHardwareWatchpoints()) {
    if (!IsWatchpointVacant(hw_index)) {
      RegisterValue value;

      if (ReadRegister(m_reg_info.first_dr + hw_index, value))
        wp_monitor_addr = value.GetAsUInt64();
    }
  }

  return wp_monitor_addr;
}

bool RegisterContextPOSIXProcessMonitor_x86_64::IsWatchpointVacant(
    uint32_t hw_index) {
  bool is_vacant = false;
  RegisterValue value;

  assert(hw_index < NumSupportedHardwareWatchpoints());

  if (m_watchpoints_initialized == false) {
    // Reset the debug status and debug control registers
    RegisterValue zero_bits = RegisterValue(uint64_t(0));
    if (!WriteRegister(m_reg_info.first_dr + 6, zero_bits) ||
        !WriteRegister(m_reg_info.first_dr + 7, zero_bits))
      assert(false && "Could not initialize watchpoint registers");
    m_watchpoints_initialized = true;
  }

  if (ReadRegister(m_reg_info.first_dr + 7, value)) {
    uint64_t val = value.GetAsUInt64();
    is_vacant = (val & (3 << 2 * hw_index)) == 0;
  }

  return is_vacant;
}

bool RegisterContextPOSIXProcessMonitor_x86_64::SetHardwareWatchpointWithIndex(
    addr_t addr, size_t size, bool read, bool write, uint32_t hw_index) {
  const uint32_t num_hw_watchpoints = NumSupportedHardwareWatchpoints();

  if (num_hw_watchpoints == 0 || hw_index >= num_hw_watchpoints)
    return false;

  if (!(size == 1 || size == 2 || size == 4 || size == 8))
    return false;

  if (read == false && write == false)
    return false;

  if (!IsWatchpointVacant(hw_index))
    return false;

  // Set both dr7 (debug control register) and dri (debug address register).

  // dr7{7-0} encodes the local/global enable bits:
  //  global enable --. .-- local enable
  //                  | |
  //                  v v
  //      dr0 -> bits{1-0}
  //      dr1 -> bits{3-2}
  //      dr2 -> bits{5-4}
  //      dr3 -> bits{7-6}
  //
  // dr7{31-16} encodes the rw/len bits:
  //  b_x+3, b_x+2, b_x+1, b_x
  //      where bits{x+1, x} => rw
  //            0b00: execute, 0b01: write, 0b11: read-or-write,
  //            0b10: io read-or-write (unused)
  //      and bits{x+3, x+2} => len
  //            0b00: 1-byte, 0b01: 2-byte, 0b11: 4-byte, 0b10: 8-byte
  //
  //      dr0 -> bits{19-16}
  //      dr1 -> bits{23-20}
  //      dr2 -> bits{27-24}
  //      dr3 -> bits{31-28}
  if (hw_index < num_hw_watchpoints) {
    RegisterValue current_dr7_bits;

    if (ReadRegister(m_reg_info.first_dr + 7, current_dr7_bits)) {
      uint64_t new_dr7_bits =
          current_dr7_bits.GetAsUInt64() |
          (1 << (2 * hw_index) |
           size_and_rw_bits(size, read, write) << (16 + 4 * hw_index));

      if (WriteRegister(m_reg_info.first_dr + hw_index, RegisterValue(addr)) &&
          WriteRegister(m_reg_info.first_dr + 7, RegisterValue(new_dr7_bits)))
        return true;
    }
  }

  return false;
}

uint32_t
RegisterContextPOSIXProcessMonitor_x86_64::NumSupportedHardwareWatchpoints() {
  // Available debug address registers: dr0, dr1, dr2, dr3
  return 4;
}
