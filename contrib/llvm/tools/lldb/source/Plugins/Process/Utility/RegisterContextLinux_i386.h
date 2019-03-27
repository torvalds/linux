//===-- RegisterContextLinux_i386.h -----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_RegisterContextLinux_i386_H_
#define liblldb_RegisterContextLinux_i386_H_

#include "RegisterInfoInterface.h"

class RegisterContextLinux_i386 : public lldb_private::RegisterInfoInterface {
public:
  RegisterContextLinux_i386(const lldb_private::ArchSpec &target_arch);

  size_t GetGPRSize() const override;

  const lldb_private::RegisterInfo *GetRegisterInfo() const override;

  uint32_t GetRegisterCount() const override;

  uint32_t GetUserRegisterCount() const override;

  const std::vector<lldb_private::RegisterInfo> *
  GetDynamicRegisterInfoP() const override;

private:
  std::vector<lldb_private::RegisterInfo> d_register_infos;
};

#endif
