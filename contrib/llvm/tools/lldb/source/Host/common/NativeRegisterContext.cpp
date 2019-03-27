//===-- NativeRegisterContext.cpp -------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/common/NativeRegisterContext.h"

#include "lldb/Utility/Log.h"
#include "lldb/Utility/RegisterValue.h"

#include "lldb/Host/PosixApi.h"
#include "lldb/Host/common/NativeProcessProtocol.h"
#include "lldb/Host/common/NativeThreadProtocol.h"

using namespace lldb;
using namespace lldb_private;

NativeRegisterContext::NativeRegisterContext(NativeThreadProtocol &thread)
    : m_thread(thread) {}

//----------------------------------------------------------------------
// Destructor
//----------------------------------------------------------------------
NativeRegisterContext::~NativeRegisterContext() {}

// FIXME revisit invalidation, process stop ids, etc.  Right now we don't
// support caching in NativeRegisterContext.  We can do this later by utilizing
// NativeProcessProtocol::GetStopID () and adding a stop id to
// NativeRegisterContext.

// void
// NativeRegisterContext::InvalidateIfNeeded (bool force) {
//     ProcessSP process_sp (m_thread.GetProcess());
//     bool invalidate = force;
//     uint32_t process_stop_id = UINT32_MAX;

//     if (process_sp)
//         process_stop_id = process_sp->GetStopID();
//     else
//         invalidate = true;

//     if (!invalidate)
//         invalidate = process_stop_id != GetStopID();

//     if (invalidate)
//     {
//         InvalidateAllRegisters ();
//         SetStopID (process_stop_id);
//     }
// }

const RegisterInfo *
NativeRegisterContext::GetRegisterInfoByName(llvm::StringRef reg_name,
                                             uint32_t start_idx) {
  if (reg_name.empty())
    return nullptr;

  const uint32_t num_registers = GetRegisterCount();
  for (uint32_t reg = start_idx; reg < num_registers; ++reg) {
    const RegisterInfo *reg_info = GetRegisterInfoAtIndex(reg);

    if (reg_name.equals_lower(reg_info->name) ||
        reg_name.equals_lower(reg_info->alt_name))
      return reg_info;
  }
  return nullptr;
}

const RegisterInfo *NativeRegisterContext::GetRegisterInfo(uint32_t kind,
                                                           uint32_t num) {
  const uint32_t reg_num = ConvertRegisterKindToRegisterNumber(kind, num);
  if (reg_num == LLDB_INVALID_REGNUM)
    return nullptr;
  return GetRegisterInfoAtIndex(reg_num);
}

const char *NativeRegisterContext::GetRegisterName(uint32_t reg) {
  const RegisterInfo *reg_info = GetRegisterInfoAtIndex(reg);
  if (reg_info)
    return reg_info->name;
  return nullptr;
}

const char *NativeRegisterContext::GetRegisterSetNameForRegisterAtIndex(
    uint32_t reg_index) const {
  const RegisterInfo *const reg_info = GetRegisterInfoAtIndex(reg_index);
  if (!reg_info)
    return nullptr;

  for (uint32_t set_index = 0; set_index < GetRegisterSetCount(); ++set_index) {
    const RegisterSet *const reg_set = GetRegisterSet(set_index);
    if (!reg_set)
      continue;

    for (uint32_t reg_num_index = 0; reg_num_index < reg_set->num_registers;
         ++reg_num_index) {
      const uint32_t reg_num = reg_set->registers[reg_num_index];
      // FIXME double check we're checking the right register kind here.
      if (reg_info->kinds[RegisterKind::eRegisterKindLLDB] == reg_num) {
        // The given register is a member of this register set.  Return the
        // register set name.
        return reg_set->name;
      }
    }
  }

  // Didn't find it.
  return nullptr;
}

lldb::addr_t NativeRegisterContext::GetPC(lldb::addr_t fail_value) {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_THREAD));

  uint32_t reg = ConvertRegisterKindToRegisterNumber(eRegisterKindGeneric,
                                                     LLDB_REGNUM_GENERIC_PC);
  if (log)
    log->Printf("NativeRegisterContext::%s using reg index %" PRIu32
                " (default %" PRIu64 ")",
                __FUNCTION__, reg, fail_value);

  const uint64_t retval = ReadRegisterAsUnsigned(reg, fail_value);

  if (log)
    log->Printf("NativeRegisterContext::%s " PRIu32 " retval %" PRIu64,
                __FUNCTION__, retval);

  return retval;
}

lldb::addr_t
NativeRegisterContext::GetPCfromBreakpointLocation(lldb::addr_t fail_value) {
  return GetPC(fail_value);
}

Status NativeRegisterContext::SetPC(lldb::addr_t pc) {
  uint32_t reg = ConvertRegisterKindToRegisterNumber(eRegisterKindGeneric,
                                                     LLDB_REGNUM_GENERIC_PC);
  return WriteRegisterFromUnsigned(reg, pc);
}

lldb::addr_t NativeRegisterContext::GetSP(lldb::addr_t fail_value) {
  uint32_t reg = ConvertRegisterKindToRegisterNumber(eRegisterKindGeneric,
                                                     LLDB_REGNUM_GENERIC_SP);
  return ReadRegisterAsUnsigned(reg, fail_value);
}

Status NativeRegisterContext::SetSP(lldb::addr_t sp) {
  uint32_t reg = ConvertRegisterKindToRegisterNumber(eRegisterKindGeneric,
                                                     LLDB_REGNUM_GENERIC_SP);
  return WriteRegisterFromUnsigned(reg, sp);
}

lldb::addr_t NativeRegisterContext::GetFP(lldb::addr_t fail_value) {
  uint32_t reg = ConvertRegisterKindToRegisterNumber(eRegisterKindGeneric,
                                                     LLDB_REGNUM_GENERIC_FP);
  return ReadRegisterAsUnsigned(reg, fail_value);
}

Status NativeRegisterContext::SetFP(lldb::addr_t fp) {
  uint32_t reg = ConvertRegisterKindToRegisterNumber(eRegisterKindGeneric,
                                                     LLDB_REGNUM_GENERIC_FP);
  return WriteRegisterFromUnsigned(reg, fp);
}

lldb::addr_t NativeRegisterContext::GetReturnAddress(lldb::addr_t fail_value) {
  uint32_t reg = ConvertRegisterKindToRegisterNumber(eRegisterKindGeneric,
                                                     LLDB_REGNUM_GENERIC_RA);
  return ReadRegisterAsUnsigned(reg, fail_value);
}

lldb::addr_t NativeRegisterContext::GetFlags(lldb::addr_t fail_value) {
  uint32_t reg = ConvertRegisterKindToRegisterNumber(eRegisterKindGeneric,
                                                     LLDB_REGNUM_GENERIC_FLAGS);
  return ReadRegisterAsUnsigned(reg, fail_value);
}

lldb::addr_t
NativeRegisterContext::ReadRegisterAsUnsigned(uint32_t reg,
                                              lldb::addr_t fail_value) {
  if (reg != LLDB_INVALID_REGNUM)
    return ReadRegisterAsUnsigned(GetRegisterInfoAtIndex(reg), fail_value);
  return fail_value;
}

uint64_t
NativeRegisterContext::ReadRegisterAsUnsigned(const RegisterInfo *reg_info,
                                              lldb::addr_t fail_value) {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_THREAD));

  if (reg_info) {
    RegisterValue value;
    Status error = ReadRegister(reg_info, value);
    if (error.Success()) {
      if (log)
        log->Printf("NativeRegisterContext::%s ReadRegister() succeeded, value "
                    "%" PRIu64,
                    __FUNCTION__, value.GetAsUInt64());
      return value.GetAsUInt64();
    } else {
      if (log)
        log->Printf("NativeRegisterContext::%s ReadRegister() failed, error %s",
                    __FUNCTION__, error.AsCString());
    }
  } else {
    if (log)
      log->Printf("NativeRegisterContext::%s ReadRegister() null reg_info",
                  __FUNCTION__);
  }
  return fail_value;
}

Status NativeRegisterContext::WriteRegisterFromUnsigned(uint32_t reg,
                                                        uint64_t uval) {
  if (reg == LLDB_INVALID_REGNUM)
    return Status("NativeRegisterContext::%s (): reg is invalid", __FUNCTION__);
  return WriteRegisterFromUnsigned(GetRegisterInfoAtIndex(reg), uval);
}

Status
NativeRegisterContext::WriteRegisterFromUnsigned(const RegisterInfo *reg_info,
                                                 uint64_t uval) {
  assert(reg_info);
  if (!reg_info)
    return Status("reg_info is nullptr");

  RegisterValue value;
  if (!value.SetUInt(uval, reg_info->byte_size))
    return Status("RegisterValue::SetUInt () failed");

  return WriteRegister(reg_info, value);
}

lldb::tid_t NativeRegisterContext::GetThreadID() const {
  return m_thread.GetID();
}

uint32_t NativeRegisterContext::NumSupportedHardwareBreakpoints() { return 0; }

uint32_t NativeRegisterContext::SetHardwareBreakpoint(lldb::addr_t addr,
                                                      size_t size) {
  return LLDB_INVALID_INDEX32;
}

Status NativeRegisterContext::ClearAllHardwareBreakpoints() {
  return Status("not implemented");
}

bool NativeRegisterContext::ClearHardwareBreakpoint(uint32_t hw_idx) {
  return false;
}

Status NativeRegisterContext::GetHardwareBreakHitIndex(uint32_t &bp_index,
                                                       lldb::addr_t trap_addr) {
  bp_index = LLDB_INVALID_INDEX32;
  return Status("not implemented");
}

uint32_t NativeRegisterContext::NumSupportedHardwareWatchpoints() { return 0; }

uint32_t NativeRegisterContext::SetHardwareWatchpoint(lldb::addr_t addr,
                                                      size_t size,
                                                      uint32_t watch_flags) {
  return LLDB_INVALID_INDEX32;
}

bool NativeRegisterContext::ClearHardwareWatchpoint(uint32_t hw_index) {
  return false;
}

Status NativeRegisterContext::ClearAllHardwareWatchpoints() {
  return Status("not implemented");
}

Status NativeRegisterContext::IsWatchpointHit(uint32_t wp_index, bool &is_hit) {
  is_hit = false;
  return Status("not implemented");
}

Status NativeRegisterContext::GetWatchpointHitIndex(uint32_t &wp_index,
                                                    lldb::addr_t trap_addr) {
  wp_index = LLDB_INVALID_INDEX32;
  return Status("not implemented");
}

Status NativeRegisterContext::IsWatchpointVacant(uint32_t wp_index,
                                                 bool &is_vacant) {
  is_vacant = false;
  return Status("not implemented");
}

lldb::addr_t NativeRegisterContext::GetWatchpointAddress(uint32_t wp_index) {
  return LLDB_INVALID_ADDRESS;
}

lldb::addr_t NativeRegisterContext::GetWatchpointHitAddress(uint32_t wp_index) {
  return LLDB_INVALID_ADDRESS;
}

bool NativeRegisterContext::HardwareSingleStep(bool enable) { return false; }

Status NativeRegisterContext::ReadRegisterValueFromMemory(
    const RegisterInfo *reg_info, lldb::addr_t src_addr, size_t src_len,
    RegisterValue &reg_value) {
  Status error;
  if (reg_info == nullptr) {
    error.SetErrorString("invalid register info argument.");
    return error;
  }

  // Moving from addr into a register
  //
  // Case 1: src_len == dst_len
  //
  //   |AABBCCDD| Address contents
  //   |AABBCCDD| Register contents
  //
  // Case 2: src_len > dst_len
  //
  //   Status!  (The register should always be big enough to hold the data)
  //
  // Case 3: src_len < dst_len
  //
  //   |AABB| Address contents
  //   |AABB0000| Register contents [on little-endian hardware]
  //   |0000AABB| Register contents [on big-endian hardware]
  if (src_len > RegisterValue::kMaxRegisterByteSize) {
    error.SetErrorString("register too small to receive memory data");
    return error;
  }

  const size_t dst_len = reg_info->byte_size;

  if (src_len > dst_len) {
    error.SetErrorStringWithFormat(
        "%" PRIu64 " bytes is too big to store in register %s (%" PRIu64
        " bytes)",
        static_cast<uint64_t>(src_len), reg_info->name,
        static_cast<uint64_t>(dst_len));
    return error;
  }

  NativeProcessProtocol &process = m_thread.GetProcess();
  uint8_t src[RegisterValue::kMaxRegisterByteSize];

  // Read the memory
  size_t bytes_read;
  error = process.ReadMemory(src_addr, src, src_len, bytes_read);
  if (error.Fail())
    return error;

  // Make sure the memory read succeeded...
  if (bytes_read != src_len) {
    // This might happen if we read _some_ bytes but not all
    error.SetErrorStringWithFormat("read %" PRIu64 " of %" PRIu64 " bytes",
                                   static_cast<uint64_t>(bytes_read),
                                   static_cast<uint64_t>(src_len));
    return error;
  }

  // We now have a memory buffer that contains the part or all of the register
  // value. Set the register value using this memory data.
  // TODO: we might need to add a parameter to this function in case the byte
  // order of the memory data doesn't match the process. For now we are
  // assuming they are the same.
  reg_value.SetFromMemoryData(reg_info, src, src_len, process.GetByteOrder(),
                              error);

  return error;
}

Status NativeRegisterContext::WriteRegisterValueToMemory(
    const RegisterInfo *reg_info, lldb::addr_t dst_addr, size_t dst_len,
    const RegisterValue &reg_value) {

  uint8_t dst[RegisterValue::kMaxRegisterByteSize];

  Status error;

  NativeProcessProtocol &process = m_thread.GetProcess();

  // TODO: we might need to add a parameter to this function in case the byte
  // order of the memory data doesn't match the process. For now we are
  // assuming they are the same.
  const size_t bytes_copied = reg_value.GetAsMemoryData(
      reg_info, dst, dst_len, process.GetByteOrder(), error);

  if (error.Success()) {
    if (bytes_copied == 0) {
      error.SetErrorString("byte copy failed.");
    } else {
      size_t bytes_written;
      error = process.WriteMemory(dst_addr, dst, bytes_copied, bytes_written);
      if (error.Fail())
        return error;

      if (bytes_written != bytes_copied) {
        // This might happen if we read _some_ bytes but not all
        error.SetErrorStringWithFormat("only wrote %" PRIu64 " of %" PRIu64
                                       " bytes",
                                       static_cast<uint64_t>(bytes_written),
                                       static_cast<uint64_t>(bytes_copied));
      }
    }
  }

  return error;
}

uint32_t
NativeRegisterContext::ConvertRegisterKindToRegisterNumber(uint32_t kind,
                                                           uint32_t num) const {
  const uint32_t num_regs = GetRegisterCount();

  assert(kind < kNumRegisterKinds);
  for (uint32_t reg_idx = 0; reg_idx < num_regs; ++reg_idx) {
    const RegisterInfo *reg_info = GetRegisterInfoAtIndex(reg_idx);

    if (reg_info->kinds[kind] == num)
      return reg_idx;
  }

  return LLDB_INVALID_REGNUM;
}
