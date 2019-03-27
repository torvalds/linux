//===-- EmulateInstruction.cpp ----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/EmulateInstruction.h"

#include "lldb/Core/Address.h"
#include "lldb/Core/DumpRegisterValue.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/StreamFile.h"
#include "lldb/Symbol/UnwindPlan.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/StreamString.h"
#include "lldb/lldb-forward.h"
#include "lldb/lldb-private-interfaces.h"

#include "llvm/ADT/StringRef.h"

#include <cstring>
#include <memory>

#include <inttypes.h>
#include <stdio.h>

namespace lldb_private {
class Target;
}

using namespace lldb;
using namespace lldb_private;

EmulateInstruction *
EmulateInstruction::FindPlugin(const ArchSpec &arch,
                               InstructionType supported_inst_type,
                               const char *plugin_name) {
  EmulateInstructionCreateInstance create_callback = nullptr;
  if (plugin_name) {
    ConstString const_plugin_name(plugin_name);
    create_callback =
        PluginManager::GetEmulateInstructionCreateCallbackForPluginName(
            const_plugin_name);
    if (create_callback) {
      EmulateInstruction *emulate_insn_ptr =
          create_callback(arch, supported_inst_type);
      if (emulate_insn_ptr)
        return emulate_insn_ptr;
    }
  } else {
    for (uint32_t idx = 0;
         (create_callback =
              PluginManager::GetEmulateInstructionCreateCallbackAtIndex(idx)) !=
         nullptr;
         ++idx) {
      EmulateInstruction *emulate_insn_ptr =
          create_callback(arch, supported_inst_type);
      if (emulate_insn_ptr)
        return emulate_insn_ptr;
    }
  }
  return nullptr;
}

EmulateInstruction::EmulateInstruction(const ArchSpec &arch)
    : m_arch(arch), m_baton(nullptr), m_read_mem_callback(&ReadMemoryDefault),
      m_write_mem_callback(&WriteMemoryDefault),
      m_read_reg_callback(&ReadRegisterDefault),
      m_write_reg_callback(&WriteRegisterDefault),
      m_addr(LLDB_INVALID_ADDRESS) {
  ::memset(&m_opcode, 0, sizeof(m_opcode));
}

bool EmulateInstruction::ReadRegister(const RegisterInfo *reg_info,
                                      RegisterValue &reg_value) {
  if (m_read_reg_callback != nullptr)
    return m_read_reg_callback(this, m_baton, reg_info, reg_value);
  return false;
}

bool EmulateInstruction::ReadRegister(lldb::RegisterKind reg_kind,
                                      uint32_t reg_num,
                                      RegisterValue &reg_value) {
  RegisterInfo reg_info;
  if (GetRegisterInfo(reg_kind, reg_num, reg_info))
    return ReadRegister(&reg_info, reg_value);
  return false;
}

uint64_t EmulateInstruction::ReadRegisterUnsigned(lldb::RegisterKind reg_kind,
                                                  uint32_t reg_num,
                                                  uint64_t fail_value,
                                                  bool *success_ptr) {
  RegisterValue reg_value;
  if (ReadRegister(reg_kind, reg_num, reg_value))
    return reg_value.GetAsUInt64(fail_value, success_ptr);
  if (success_ptr)
    *success_ptr = false;
  return fail_value;
}

uint64_t EmulateInstruction::ReadRegisterUnsigned(const RegisterInfo *reg_info,
                                                  uint64_t fail_value,
                                                  bool *success_ptr) {
  RegisterValue reg_value;
  if (ReadRegister(reg_info, reg_value))
    return reg_value.GetAsUInt64(fail_value, success_ptr);
  if (success_ptr)
    *success_ptr = false;
  return fail_value;
}

bool EmulateInstruction::WriteRegister(const Context &context,
                                       const RegisterInfo *reg_info,
                                       const RegisterValue &reg_value) {
  if (m_write_reg_callback != nullptr)
    return m_write_reg_callback(this, m_baton, context, reg_info, reg_value);
  return false;
}

bool EmulateInstruction::WriteRegister(const Context &context,
                                       lldb::RegisterKind reg_kind,
                                       uint32_t reg_num,
                                       const RegisterValue &reg_value) {
  RegisterInfo reg_info;
  if (GetRegisterInfo(reg_kind, reg_num, reg_info))
    return WriteRegister(context, &reg_info, reg_value);
  return false;
}

bool EmulateInstruction::WriteRegisterUnsigned(const Context &context,
                                               lldb::RegisterKind reg_kind,
                                               uint32_t reg_num,
                                               uint64_t uint_value) {
  RegisterInfo reg_info;
  if (GetRegisterInfo(reg_kind, reg_num, reg_info)) {
    RegisterValue reg_value;
    if (reg_value.SetUInt(uint_value, reg_info.byte_size))
      return WriteRegister(context, &reg_info, reg_value);
  }
  return false;
}

bool EmulateInstruction::WriteRegisterUnsigned(const Context &context,
                                               const RegisterInfo *reg_info,
                                               uint64_t uint_value) {
  if (reg_info != nullptr) {
    RegisterValue reg_value;
    if (reg_value.SetUInt(uint_value, reg_info->byte_size))
      return WriteRegister(context, reg_info, reg_value);
  }
  return false;
}

size_t EmulateInstruction::ReadMemory(const Context &context, lldb::addr_t addr,
                                      void *dst, size_t dst_len) {
  if (m_read_mem_callback != nullptr)
    return m_read_mem_callback(this, m_baton, context, addr, dst, dst_len) ==
           dst_len;
  return false;
}

uint64_t EmulateInstruction::ReadMemoryUnsigned(const Context &context,
                                                lldb::addr_t addr,
                                                size_t byte_size,
                                                uint64_t fail_value,
                                                bool *success_ptr) {
  uint64_t uval64 = 0;
  bool success = false;
  if (byte_size <= 8) {
    uint8_t buf[sizeof(uint64_t)];
    size_t bytes_read =
        m_read_mem_callback(this, m_baton, context, addr, buf, byte_size);
    if (bytes_read == byte_size) {
      lldb::offset_t offset = 0;
      DataExtractor data(buf, byte_size, GetByteOrder(), GetAddressByteSize());
      uval64 = data.GetMaxU64(&offset, byte_size);
      success = true;
    }
  }

  if (success_ptr)
    *success_ptr = success;

  if (!success)
    uval64 = fail_value;
  return uval64;
}

bool EmulateInstruction::WriteMemoryUnsigned(const Context &context,
                                             lldb::addr_t addr, uint64_t uval,
                                             size_t uval_byte_size) {
  StreamString strm(Stream::eBinary, GetAddressByteSize(), GetByteOrder());
  strm.PutMaxHex64(uval, uval_byte_size);

  size_t bytes_written = m_write_mem_callback(
      this, m_baton, context, addr, strm.GetString().data(), uval_byte_size);
  return (bytes_written == uval_byte_size);
}

bool EmulateInstruction::WriteMemory(const Context &context, lldb::addr_t addr,
                                     const void *src, size_t src_len) {
  if (m_write_mem_callback != nullptr)
    return m_write_mem_callback(this, m_baton, context, addr, src, src_len) ==
           src_len;
  return false;
}

void EmulateInstruction::SetBaton(void *baton) { m_baton = baton; }

void EmulateInstruction::SetCallbacks(
    ReadMemoryCallback read_mem_callback,
    WriteMemoryCallback write_mem_callback,
    ReadRegisterCallback read_reg_callback,
    WriteRegisterCallback write_reg_callback) {
  m_read_mem_callback = read_mem_callback;
  m_write_mem_callback = write_mem_callback;
  m_read_reg_callback = read_reg_callback;
  m_write_reg_callback = write_reg_callback;
}

void EmulateInstruction::SetReadMemCallback(
    ReadMemoryCallback read_mem_callback) {
  m_read_mem_callback = read_mem_callback;
}

void EmulateInstruction::SetWriteMemCallback(
    WriteMemoryCallback write_mem_callback) {
  m_write_mem_callback = write_mem_callback;
}

void EmulateInstruction::SetReadRegCallback(
    ReadRegisterCallback read_reg_callback) {
  m_read_reg_callback = read_reg_callback;
}

void EmulateInstruction::SetWriteRegCallback(
    WriteRegisterCallback write_reg_callback) {
  m_write_reg_callback = write_reg_callback;
}

//
//  Read & Write Memory and Registers callback functions.
//

size_t EmulateInstruction::ReadMemoryFrame(EmulateInstruction *instruction,
                                           void *baton, const Context &context,
                                           lldb::addr_t addr, void *dst,
                                           size_t dst_len) {
  if (baton == nullptr || dst == nullptr || dst_len == 0)
    return 0;

  StackFrame *frame = (StackFrame *)baton;

  ProcessSP process_sp(frame->CalculateProcess());
  if (process_sp) {
    Status error;
    return process_sp->ReadMemory(addr, dst, dst_len, error);
  }
  return 0;
}

size_t EmulateInstruction::WriteMemoryFrame(EmulateInstruction *instruction,
                                            void *baton, const Context &context,
                                            lldb::addr_t addr, const void *src,
                                            size_t src_len) {
  if (baton == nullptr || src == nullptr || src_len == 0)
    return 0;

  StackFrame *frame = (StackFrame *)baton;

  ProcessSP process_sp(frame->CalculateProcess());
  if (process_sp) {
    Status error;
    return process_sp->WriteMemory(addr, src, src_len, error);
  }

  return 0;
}

bool EmulateInstruction::ReadRegisterFrame(EmulateInstruction *instruction,
                                           void *baton,
                                           const RegisterInfo *reg_info,
                                           RegisterValue &reg_value) {
  if (baton == nullptr)
    return false;

  StackFrame *frame = (StackFrame *)baton;
  return frame->GetRegisterContext()->ReadRegister(reg_info, reg_value);
}

bool EmulateInstruction::WriteRegisterFrame(EmulateInstruction *instruction,
                                            void *baton, const Context &context,
                                            const RegisterInfo *reg_info,
                                            const RegisterValue &reg_value) {
  if (baton == nullptr)
    return false;

  StackFrame *frame = (StackFrame *)baton;
  return frame->GetRegisterContext()->WriteRegister(reg_info, reg_value);
}

size_t EmulateInstruction::ReadMemoryDefault(EmulateInstruction *instruction,
                                             void *baton,
                                             const Context &context,
                                             lldb::addr_t addr, void *dst,
                                             size_t length) {
  StreamFile strm(stdout, false);
  strm.Printf("    Read from Memory (address = 0x%" PRIx64 ", length = %" PRIu64
              ", context = ",
              addr, (uint64_t)length);
  context.Dump(strm, instruction);
  strm.EOL();
  *((uint64_t *)dst) = 0xdeadbeef;
  return length;
}

size_t EmulateInstruction::WriteMemoryDefault(EmulateInstruction *instruction,
                                              void *baton,
                                              const Context &context,
                                              lldb::addr_t addr,
                                              const void *dst, size_t length) {
  StreamFile strm(stdout, false);
  strm.Printf("    Write to Memory (address = 0x%" PRIx64 ", length = %" PRIu64
              ", context = ",
              addr, (uint64_t)length);
  context.Dump(strm, instruction);
  strm.EOL();
  return length;
}

bool EmulateInstruction::ReadRegisterDefault(EmulateInstruction *instruction,
                                             void *baton,
                                             const RegisterInfo *reg_info,
                                             RegisterValue &reg_value) {
  StreamFile strm(stdout, false);
  strm.Printf("  Read Register (%s)\n", reg_info->name);
  lldb::RegisterKind reg_kind;
  uint32_t reg_num;
  if (GetBestRegisterKindAndNumber(reg_info, reg_kind, reg_num))
    reg_value.SetUInt64((uint64_t)reg_kind << 24 | reg_num);
  else
    reg_value.SetUInt64(0);

  return true;
}

bool EmulateInstruction::WriteRegisterDefault(EmulateInstruction *instruction,
                                              void *baton,
                                              const Context &context,
                                              const RegisterInfo *reg_info,
                                              const RegisterValue &reg_value) {
  StreamFile strm(stdout, false);
  strm.Printf("    Write to Register (name = %s, value = ", reg_info->name);
  DumpRegisterValue(reg_value, &strm, reg_info, false, false, eFormatDefault);
  strm.PutCString(", context = ");
  context.Dump(strm, instruction);
  strm.EOL();
  return true;
}

void EmulateInstruction::Context::Dump(Stream &strm,
                                       EmulateInstruction *instruction) const {
  switch (type) {
  case eContextReadOpcode:
    strm.PutCString("reading opcode");
    break;

  case eContextImmediate:
    strm.PutCString("immediate");
    break;

  case eContextPushRegisterOnStack:
    strm.PutCString("push register");
    break;

  case eContextPopRegisterOffStack:
    strm.PutCString("pop register");
    break;

  case eContextAdjustStackPointer:
    strm.PutCString("adjust sp");
    break;

  case eContextSetFramePointer:
    strm.PutCString("set frame pointer");
    break;

  case eContextAdjustBaseRegister:
    strm.PutCString("adjusting (writing value back to) a base register");
    break;

  case eContextRegisterPlusOffset:
    strm.PutCString("register + offset");
    break;

  case eContextRegisterStore:
    strm.PutCString("store register");
    break;

  case eContextRegisterLoad:
    strm.PutCString("load register");
    break;

  case eContextRelativeBranchImmediate:
    strm.PutCString("relative branch immediate");
    break;

  case eContextAbsoluteBranchRegister:
    strm.PutCString("absolute branch register");
    break;

  case eContextSupervisorCall:
    strm.PutCString("supervisor call");
    break;

  case eContextTableBranchReadMemory:
    strm.PutCString("table branch read memory");
    break;

  case eContextWriteRegisterRandomBits:
    strm.PutCString("write random bits to a register");
    break;

  case eContextWriteMemoryRandomBits:
    strm.PutCString("write random bits to a memory address");
    break;

  case eContextArithmetic:
    strm.PutCString("arithmetic");
    break;

  case eContextReturnFromException:
    strm.PutCString("return from exception");
    break;

  default:
    strm.PutCString("unrecognized context.");
    break;
  }

  switch (info_type) {
  case eInfoTypeRegisterPlusOffset:
    strm.Printf(" (reg_plus_offset = %s%+" PRId64 ")",
                info.RegisterPlusOffset.reg.name,
                info.RegisterPlusOffset.signed_offset);
    break;

  case eInfoTypeRegisterPlusIndirectOffset:
    strm.Printf(" (reg_plus_reg = %s + %s)",
                info.RegisterPlusIndirectOffset.base_reg.name,
                info.RegisterPlusIndirectOffset.offset_reg.name);
    break;

  case eInfoTypeRegisterToRegisterPlusOffset:
    strm.Printf(" (base_and_imm_offset = %s%+" PRId64 ", data_reg = %s)",
                info.RegisterToRegisterPlusOffset.base_reg.name,
                info.RegisterToRegisterPlusOffset.offset,
                info.RegisterToRegisterPlusOffset.data_reg.name);
    break;

  case eInfoTypeRegisterToRegisterPlusIndirectOffset:
    strm.Printf(" (base_and_reg_offset = %s + %s, data_reg = %s)",
                info.RegisterToRegisterPlusIndirectOffset.base_reg.name,
                info.RegisterToRegisterPlusIndirectOffset.offset_reg.name,
                info.RegisterToRegisterPlusIndirectOffset.data_reg.name);
    break;

  case eInfoTypeRegisterRegisterOperands:
    strm.Printf(" (register to register binary op: %s and %s)",
                info.RegisterRegisterOperands.operand1.name,
                info.RegisterRegisterOperands.operand2.name);
    break;

  case eInfoTypeOffset:
    strm.Printf(" (signed_offset = %+" PRId64 ")", info.signed_offset);
    break;

  case eInfoTypeRegister:
    strm.Printf(" (reg = %s)", info.reg.name);
    break;

  case eInfoTypeImmediate:
    strm.Printf(" (unsigned_immediate = %" PRIu64 " (0x%16.16" PRIx64 "))",
                info.unsigned_immediate, info.unsigned_immediate);
    break;

  case eInfoTypeImmediateSigned:
    strm.Printf(" (signed_immediate = %+" PRId64 " (0x%16.16" PRIx64 "))",
                info.signed_immediate, info.signed_immediate);
    break;

  case eInfoTypeAddress:
    strm.Printf(" (address = 0x%" PRIx64 ")", info.address);
    break;

  case eInfoTypeISAAndImmediate:
    strm.Printf(" (isa = %u, unsigned_immediate = %u (0x%8.8x))",
                info.ISAAndImmediate.isa, info.ISAAndImmediate.unsigned_data32,
                info.ISAAndImmediate.unsigned_data32);
    break;

  case eInfoTypeISAAndImmediateSigned:
    strm.Printf(" (isa = %u, signed_immediate = %i (0x%8.8x))",
                info.ISAAndImmediateSigned.isa,
                info.ISAAndImmediateSigned.signed_data32,
                info.ISAAndImmediateSigned.signed_data32);
    break;

  case eInfoTypeISA:
    strm.Printf(" (isa = %u)", info.isa);
    break;

  case eInfoTypeNoArgs:
    break;
  }
}

bool EmulateInstruction::SetInstruction(const Opcode &opcode,
                                        const Address &inst_addr,
                                        Target *target) {
  m_opcode = opcode;
  m_addr = LLDB_INVALID_ADDRESS;
  if (inst_addr.IsValid()) {
    if (target != nullptr)
      m_addr = inst_addr.GetLoadAddress(target);
    if (m_addr == LLDB_INVALID_ADDRESS)
      m_addr = inst_addr.GetFileAddress();
  }
  return true;
}

bool EmulateInstruction::GetBestRegisterKindAndNumber(
    const RegisterInfo *reg_info, lldb::RegisterKind &reg_kind,
    uint32_t &reg_num) {
  // Generic and DWARF should be the two most popular register kinds when
  // emulating instructions since they are the most platform agnostic...
  reg_num = reg_info->kinds[eRegisterKindGeneric];
  if (reg_num != LLDB_INVALID_REGNUM) {
    reg_kind = eRegisterKindGeneric;
    return true;
  }

  reg_num = reg_info->kinds[eRegisterKindDWARF];
  if (reg_num != LLDB_INVALID_REGNUM) {
    reg_kind = eRegisterKindDWARF;
    return true;
  }

  reg_num = reg_info->kinds[eRegisterKindLLDB];
  if (reg_num != LLDB_INVALID_REGNUM) {
    reg_kind = eRegisterKindLLDB;
    return true;
  }

  reg_num = reg_info->kinds[eRegisterKindEHFrame];
  if (reg_num != LLDB_INVALID_REGNUM) {
    reg_kind = eRegisterKindEHFrame;
    return true;
  }

  reg_num = reg_info->kinds[eRegisterKindProcessPlugin];
  if (reg_num != LLDB_INVALID_REGNUM) {
    reg_kind = eRegisterKindProcessPlugin;
    return true;
  }
  return false;
}

uint32_t
EmulateInstruction::GetInternalRegisterNumber(RegisterContext *reg_ctx,
                                              const RegisterInfo &reg_info) {
  lldb::RegisterKind reg_kind;
  uint32_t reg_num;
  if (reg_ctx && GetBestRegisterKindAndNumber(&reg_info, reg_kind, reg_num))
    return reg_ctx->ConvertRegisterKindToRegisterNumber(reg_kind, reg_num);
  return LLDB_INVALID_REGNUM;
}

bool EmulateInstruction::CreateFunctionEntryUnwind(UnwindPlan &unwind_plan) {
  unwind_plan.Clear();
  return false;
}
