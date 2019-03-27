//===-- RegisterContextMinidump_x86_64.cpp ----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "RegisterContextMinidump_x86_64.h"

#include "lldb/Utility/DataBufferHeap.h"

// C includes
// C++ includes

using namespace lldb_private;
using namespace minidump;

static llvm::MutableArrayRef<uint8_t> getDestRegister(uint8_t *context,
                                                      const RegisterInfo &reg) {
  auto bytes = reg.mutable_data(context);

  switch (reg.kinds[lldb::eRegisterKindLLDB]) {
  case lldb_cs_x86_64:
  case lldb_ds_x86_64:
  case lldb_es_x86_64:
  case lldb_fs_x86_64:
  case lldb_gs_x86_64:
  case lldb_ss_x86_64:
    return bytes.take_front(2);
    break;
  case lldb_rflags_x86_64:
    return bytes.take_front(4);
    break;
  default:
    return bytes.take_front(8);
    break;
  }
}

static void writeRegister(const void *reg_src, uint8_t *context,
                          const RegisterInfo &reg) {
  llvm::MutableArrayRef<uint8_t> reg_dest = getDestRegister(context, reg);
  memcpy(reg_dest.data(), reg_src, reg_dest.size());
}

lldb::DataBufferSP lldb_private::minidump::ConvertMinidumpContext_x86_64(
    llvm::ArrayRef<uint8_t> source_data,
    RegisterInfoInterface *target_reg_interface) {

  const RegisterInfo *reg_info = target_reg_interface->GetRegisterInfo();

  lldb::DataBufferSP result_context_buf(
      new DataBufferHeap(target_reg_interface->GetGPRSize(), 0));
  uint8_t *result_base = result_context_buf->GetBytes();

  if (source_data.size() < sizeof(MinidumpContext_x86_64))
    return nullptr;

  const MinidumpContext_x86_64 *context;
  consumeObject(source_data, context);

  const MinidumpContext_x86_64_Flags context_flags =
      static_cast<MinidumpContext_x86_64_Flags>(
          static_cast<uint32_t>(context->context_flags));
  auto x86_64_Flag = MinidumpContext_x86_64_Flags::x86_64_Flag;
  auto ControlFlag = MinidumpContext_x86_64_Flags::Control;
  auto IntegerFlag = MinidumpContext_x86_64_Flags::Integer;
  auto SegmentsFlag = MinidumpContext_x86_64_Flags::Segments;

  if ((context_flags & x86_64_Flag) != x86_64_Flag)
    return nullptr;

  if ((context_flags & ControlFlag) == ControlFlag) {
    writeRegister(&context->cs, result_base, reg_info[lldb_cs_x86_64]);
    writeRegister(&context->ss, result_base, reg_info[lldb_ss_x86_64]);
    writeRegister(&context->eflags, result_base, reg_info[lldb_rflags_x86_64]);
    writeRegister(&context->rsp, result_base, reg_info[lldb_rsp_x86_64]);
    writeRegister(&context->rip, result_base, reg_info[lldb_rip_x86_64]);
  }

  if ((context_flags & SegmentsFlag) == SegmentsFlag) {
    writeRegister(&context->ds, result_base, reg_info[lldb_ds_x86_64]);
    writeRegister(&context->es, result_base, reg_info[lldb_es_x86_64]);
    writeRegister(&context->fs, result_base, reg_info[lldb_fs_x86_64]);
    writeRegister(&context->gs, result_base, reg_info[lldb_gs_x86_64]);
  }

  if ((context_flags & IntegerFlag) == IntegerFlag) {
    writeRegister(&context->rax, result_base, reg_info[lldb_rax_x86_64]);
    writeRegister(&context->rcx, result_base, reg_info[lldb_rcx_x86_64]);
    writeRegister(&context->rdx, result_base, reg_info[lldb_rdx_x86_64]);
    writeRegister(&context->rbx, result_base, reg_info[lldb_rbx_x86_64]);
    writeRegister(&context->rbp, result_base, reg_info[lldb_rbp_x86_64]);
    writeRegister(&context->rsi, result_base, reg_info[lldb_rsi_x86_64]);
    writeRegister(&context->rdi, result_base, reg_info[lldb_rdi_x86_64]);
    writeRegister(&context->r8, result_base, reg_info[lldb_r8_x86_64]);
    writeRegister(&context->r9, result_base, reg_info[lldb_r9_x86_64]);
    writeRegister(&context->r10, result_base, reg_info[lldb_r10_x86_64]);
    writeRegister(&context->r11, result_base, reg_info[lldb_r11_x86_64]);
    writeRegister(&context->r12, result_base, reg_info[lldb_r12_x86_64]);
    writeRegister(&context->r13, result_base, reg_info[lldb_r13_x86_64]);
    writeRegister(&context->r14, result_base, reg_info[lldb_r14_x86_64]);
    writeRegister(&context->r15, result_base, reg_info[lldb_r15_x86_64]);
  }

  // TODO parse the floating point registers

  return result_context_buf;
}
