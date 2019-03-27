//===-- RegisterContextFreeBSD_powerpc.h -------------------------*- C++
//-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_RegisterContextFreeBSD_powerpc_h_
#define liblldb_RegisterContextFreeBSD_powerpc_h_

#include "RegisterInfoInterface.h"

class RegisterContextFreeBSD_powerpc
    : public lldb_private::RegisterInfoInterface {
public:
  RegisterContextFreeBSD_powerpc(const lldb_private::ArchSpec &target_arch);
  ~RegisterContextFreeBSD_powerpc() override;

  size_t GetGPRSize() const override;

  const lldb_private::RegisterInfo *GetRegisterInfo() const override;

  uint32_t GetRegisterCount() const override;
};

class RegisterContextFreeBSD_powerpc32 : public RegisterContextFreeBSD_powerpc {
public:
  RegisterContextFreeBSD_powerpc32(const lldb_private::ArchSpec &target_arch);
  ~RegisterContextFreeBSD_powerpc32() override;

  size_t GetGPRSize() const override;

  const lldb_private::RegisterInfo *GetRegisterInfo() const override;

  uint32_t GetRegisterCount() const override;
};

class RegisterContextFreeBSD_powerpc64 : public RegisterContextFreeBSD_powerpc {
public:
  RegisterContextFreeBSD_powerpc64(const lldb_private::ArchSpec &target_arch);
  ~RegisterContextFreeBSD_powerpc64() override;

  size_t GetGPRSize() const override;

  const lldb_private::RegisterInfo *GetRegisterInfo() const override;

  uint32_t GetRegisterCount() const override;
};

#endif // liblldb_RegisterContextFreeBSD_powerpc_h_
