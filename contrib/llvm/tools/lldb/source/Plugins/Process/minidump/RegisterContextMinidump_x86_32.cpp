//===-- RegisterContextMinidump_x86_32.cpp ----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "RegisterContextMinidump_x86_32.h"

#include "lldb/Utility/DataBufferHeap.h"

// C includes
// C++ includes

using namespace lldb_private;
using namespace minidump;

static void writeRegister(const void *reg_src,
                          llvm::MutableArrayRef<uint8_t> reg_dest) {
  memcpy(reg_dest.data(), reg_src, reg_dest.size());
}

lldb::DataBufferSP lldb_private::minidump::ConvertMinidumpContext_x86_32(
    llvm::ArrayRef<uint8_t> source_data,
    RegisterInfoInterface *target_reg_interface) {

  const RegisterInfo *reg_info = target_reg_interface->GetRegisterInfo();

  lldb::DataBufferSP result_context_buf(
      new DataBufferHeap(target_reg_interface->GetGPRSize(), 0));
  uint8_t *result_base = result_context_buf->GetBytes();

  if (source_data.size() < sizeof(MinidumpContext_x86_32))
    return nullptr;

  const MinidumpContext_x86_32 *context;
  consumeObject(source_data, context);

  const MinidumpContext_x86_32_Flags context_flags =
      static_cast<MinidumpContext_x86_32_Flags>(
          static_cast<uint32_t>(context->context_flags));
  auto x86_32_Flag = MinidumpContext_x86_32_Flags::x86_32_Flag;
  auto ControlFlag = MinidumpContext_x86_32_Flags::Control;
  auto IntegerFlag = MinidumpContext_x86_32_Flags::Integer;
  auto SegmentsFlag = MinidumpContext_x86_32_Flags::Segments;

  if ((context_flags & x86_32_Flag) != x86_32_Flag) {
    return nullptr;
  }

  if ((context_flags & ControlFlag) == ControlFlag) {
    writeRegister(&context->ebp,
                  reg_info[lldb_ebp_i386].mutable_data(result_base));
    writeRegister(&context->eip,
                  reg_info[lldb_eip_i386].mutable_data(result_base));
    writeRegister(&context->cs,
                  reg_info[lldb_cs_i386].mutable_data(result_base));
    writeRegister(&context->eflags,
                  reg_info[lldb_eflags_i386].mutable_data(result_base));
    writeRegister(&context->esp,
                  reg_info[lldb_esp_i386].mutable_data(result_base));
    writeRegister(&context->ss,
                  reg_info[lldb_ss_i386].mutable_data(result_base));
  }

  if ((context_flags & SegmentsFlag) == SegmentsFlag) {
    writeRegister(&context->ds,
                  reg_info[lldb_ds_i386].mutable_data(result_base));
    writeRegister(&context->es,
                  reg_info[lldb_es_i386].mutable_data(result_base));
    writeRegister(&context->fs,
                  reg_info[lldb_fs_i386].mutable_data(result_base));
    writeRegister(&context->gs,
                  reg_info[lldb_gs_i386].mutable_data(result_base));
  }

  if ((context_flags & IntegerFlag) == IntegerFlag) {
    writeRegister(&context->eax,
                  reg_info[lldb_eax_i386].mutable_data(result_base));
    writeRegister(&context->ecx,
                  reg_info[lldb_ecx_i386].mutable_data(result_base));
    writeRegister(&context->edx,
                  reg_info[lldb_edx_i386].mutable_data(result_base));
    writeRegister(&context->ebx,
                  reg_info[lldb_ebx_i386].mutable_data(result_base));
    writeRegister(&context->esi,
                  reg_info[lldb_esi_i386].mutable_data(result_base));
    writeRegister(&context->edi,
                  reg_info[lldb_edi_i386].mutable_data(result_base));
  }

  // TODO parse the floating point registers

  return result_context_buf;
}
