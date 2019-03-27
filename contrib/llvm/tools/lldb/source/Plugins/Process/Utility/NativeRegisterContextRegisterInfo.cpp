//===-- NativeRegisterContextRegisterInfo.cpp -------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "NativeRegisterContextRegisterInfo.h"
#include "lldb/lldb-private-forward.h"
#include "lldb/lldb-types.h"

using namespace lldb_private;

NativeRegisterContextRegisterInfo::NativeRegisterContextRegisterInfo(
    NativeThreadProtocol &thread,
    RegisterInfoInterface *register_info_interface)
    : NativeRegisterContext(thread),
      m_register_info_interface_up(register_info_interface) {
  assert(register_info_interface && "null register_info_interface");
}

uint32_t NativeRegisterContextRegisterInfo::GetRegisterCount() const {
  return m_register_info_interface_up->GetRegisterCount();
}

uint32_t NativeRegisterContextRegisterInfo::GetUserRegisterCount() const {
  return m_register_info_interface_up->GetUserRegisterCount();
}

const RegisterInfo *NativeRegisterContextRegisterInfo::GetRegisterInfoAtIndex(
    uint32_t reg_index) const {
  if (reg_index <= GetRegisterCount())
    return m_register_info_interface_up->GetRegisterInfo() + reg_index;
  else
    return nullptr;
}

const RegisterInfoInterface &
NativeRegisterContextRegisterInfo::GetRegisterInfoInterface() const {
  return *m_register_info_interface_up;
}
