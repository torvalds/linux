//===-- RegisterContextPOSIX_s390x.h ----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_RegisterContextPOSIX_s390x_h_
#define liblldb_RegisterContextPOSIX_s390x_h_

#include "RegisterContext_s390x.h"
#include "RegisterInfoInterface.h"
#include "lldb-s390x-register-enums.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Utility/Log.h"

class ProcessMonitor;

class RegisterContextPOSIX_s390x : public lldb_private::RegisterContext {
public:
  RegisterContextPOSIX_s390x(
      lldb_private::Thread &thread, uint32_t concrete_frame_idx,
      lldb_private::RegisterInfoInterface *register_info);

  ~RegisterContextPOSIX_s390x() override;

  void Invalidate();

  void InvalidateAllRegisters() override;

  size_t GetRegisterCount() override;

  virtual unsigned GetRegisterSize(unsigned reg);

  virtual unsigned GetRegisterOffset(unsigned reg);

  const lldb_private::RegisterInfo *GetRegisterInfoAtIndex(size_t reg) override;

  size_t GetRegisterSetCount() override;

  const lldb_private::RegisterSet *GetRegisterSet(size_t set) override;

  const char *GetRegisterName(unsigned reg);

  uint32_t ConvertRegisterKindToRegisterNumber(lldb::RegisterKind kind,
                                               uint32_t num) override;

protected:
  struct RegInfo {
    uint32_t num_registers;
    uint32_t num_gpr_registers;
    uint32_t num_fpr_registers;

    uint32_t last_gpr;
    uint32_t first_fpr;
    uint32_t last_fpr;
  };

  RegInfo m_reg_info;
  std::unique_ptr<lldb_private::RegisterInfoInterface> m_register_info_ap;

  virtual bool IsRegisterSetAvailable(size_t set_index);

  virtual const lldb_private::RegisterInfo *GetRegisterInfo();

  bool IsGPR(unsigned reg);

  bool IsFPR(unsigned reg);

  lldb::ByteOrder GetByteOrder();

  virtual bool ReadGPR() = 0;
  virtual bool ReadFPR() = 0;
  virtual bool WriteGPR() = 0;
  virtual bool WriteFPR() = 0;
};

#endif // liblldb_RegisterContextPOSIX_s390x_h_
