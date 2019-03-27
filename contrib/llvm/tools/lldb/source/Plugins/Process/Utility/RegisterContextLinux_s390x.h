//===-- RegisterContextLinux_s390x.h ----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_RegisterContextLinux_s390x_h_
#define liblldb_RegisterContextLinux_s390x_h_

#include "RegisterInfoInterface.h"

class RegisterContextLinux_s390x : public lldb_private::RegisterInfoInterface {
public:
  RegisterContextLinux_s390x(const lldb_private::ArchSpec &target_arch);

  size_t GetGPRSize() const override;

  const lldb_private::RegisterInfo *GetRegisterInfo() const override;

  uint32_t GetRegisterCount() const override;

  uint32_t GetUserRegisterCount() const override;

  const std::vector<lldb_private::RegisterInfo> *
  GetDynamicRegisterInfoP() const override;

private:
  const lldb_private::RegisterInfo *m_register_info_p;
  uint32_t m_register_info_count;
  uint32_t m_user_register_count;
  std::vector<lldb_private::RegisterInfo> d_register_infos;
};

#endif
