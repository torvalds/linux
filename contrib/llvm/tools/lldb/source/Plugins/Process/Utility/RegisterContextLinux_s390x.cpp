//===-- RegisterContextLinux_s390x.cpp --------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "RegisterContextLinux_s390x.h"
#include "RegisterContextPOSIX_s390x.h"

using namespace lldb_private;
using namespace lldb;

//---------------------------------------------------------------------------
// Include RegisterInfos_s390x to declare our g_register_infos_s390x structure.
//---------------------------------------------------------------------------
#define DECLARE_REGISTER_INFOS_S390X_STRUCT
#include "RegisterInfos_s390x.h"
#undef DECLARE_REGISTER_INFOS_S390X_STRUCT

static const RegisterInfo *GetRegisterInfoPtr(const ArchSpec &target_arch) {
  switch (target_arch.GetMachine()) {
  case llvm::Triple::systemz:
    return g_register_infos_s390x;
  default:
    assert(false && "Unhandled target architecture.");
    return nullptr;
  }
}

static uint32_t GetRegisterInfoCount(const ArchSpec &target_arch) {
  switch (target_arch.GetMachine()) {
  case llvm::Triple::systemz:
    return k_num_registers_s390x;
  default:
    assert(false && "Unhandled target architecture.");
    return 0;
  }
}

static uint32_t GetUserRegisterInfoCount(const ArchSpec &target_arch) {
  switch (target_arch.GetMachine()) {
  case llvm::Triple::systemz:
    return k_num_user_registers_s390x + k_num_linux_registers_s390x;
  default:
    assert(false && "Unhandled target architecture.");
    return 0;
  }
}

RegisterContextLinux_s390x::RegisterContextLinux_s390x(
    const ArchSpec &target_arch)
    : lldb_private::RegisterInfoInterface(target_arch),
      m_register_info_p(GetRegisterInfoPtr(target_arch)),
      m_register_info_count(GetRegisterInfoCount(target_arch)),
      m_user_register_count(GetUserRegisterInfoCount(target_arch)) {}

const std::vector<lldb_private::RegisterInfo> *
RegisterContextLinux_s390x::GetDynamicRegisterInfoP() const {
  return &d_register_infos;
}

const RegisterInfo *RegisterContextLinux_s390x::GetRegisterInfo() const {
  return m_register_info_p;
}

uint32_t RegisterContextLinux_s390x::GetRegisterCount() const {
  return m_register_info_count;
}

uint32_t RegisterContextLinux_s390x::GetUserRegisterCount() const {
  return m_user_register_count;
}

size_t RegisterContextLinux_s390x::GetGPRSize() const { return 0; }
