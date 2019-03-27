//===-- RegisterContextMacOSXFrameBackchain.h -------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef lldb_RegisterContextMacOSXFrameBackchain_h_
#define lldb_RegisterContextMacOSXFrameBackchain_h_

#include "lldb/Target/RegisterContext.h"
#include "lldb/lldb-private.h"

#include "UnwindMacOSXFrameBackchain.h"

class RegisterContextMacOSXFrameBackchain
    : public lldb_private::RegisterContext {
public:
  RegisterContextMacOSXFrameBackchain(
      lldb_private::Thread &thread, uint32_t concrete_frame_idx,
      const UnwindMacOSXFrameBackchain::Cursor &cursor);

  ~RegisterContextMacOSXFrameBackchain() override;

  void InvalidateAllRegisters() override;

  size_t GetRegisterCount() override;

  const lldb_private::RegisterInfo *GetRegisterInfoAtIndex(size_t reg) override;

  size_t GetRegisterSetCount() override;

  const lldb_private::RegisterSet *GetRegisterSet(size_t reg_set) override;

  bool ReadRegister(const lldb_private::RegisterInfo *reg_info,
                    lldb_private::RegisterValue &value) override;

  bool WriteRegister(const lldb_private::RegisterInfo *reg_info,
                     const lldb_private::RegisterValue &value) override;

  bool ReadAllRegisterValues(lldb::DataBufferSP &data_sp) override;

  bool WriteAllRegisterValues(const lldb::DataBufferSP &data_sp) override;

  uint32_t ConvertRegisterKindToRegisterNumber(lldb::RegisterKind kind,
                                               uint32_t num) override;

private:
  UnwindMacOSXFrameBackchain::Cursor m_cursor;
  bool m_cursor_is_valid;

  DISALLOW_COPY_AND_ASSIGN(RegisterContextMacOSXFrameBackchain);
};

#endif // lldb_RegisterContextMacOSXFrameBackchain_h_
