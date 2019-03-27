//===-- RegisterContextLinux_mips.h ---------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_RegisterContextLinux_mips_H_
#define liblldb_RegisterContextLinux_mips_H_

#include "RegisterInfoInterface.h"
#include "lldb/lldb-private.h"

class RegisterContextLinux_mips : public lldb_private::RegisterInfoInterface {
public:
  RegisterContextLinux_mips(const lldb_private::ArchSpec &target_arch,
                            bool msa_present = true);

  size_t GetGPRSize() const override;

  const lldb_private::RegisterInfo *GetRegisterInfo() const override;

  const lldb_private::RegisterSet *GetRegisterSet(size_t set) const;

  size_t GetRegisterSetCount() const;

  uint32_t GetRegisterCount() const override;

  uint32_t GetUserRegisterCount() const override;

private:
  uint32_t m_user_register_count;
};

#endif
