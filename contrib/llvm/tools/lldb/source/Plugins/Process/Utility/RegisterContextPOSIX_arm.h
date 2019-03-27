//===-- RegisterContextPOSIX_arm.h ------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_RegisterContextPOSIX_arm_h_
#define liblldb_RegisterContextPOSIX_arm_h_

#include "RegisterInfoInterface.h"
#include "lldb-arm-register-enums.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Utility/Log.h"

class ProcessMonitor;

class RegisterContextPOSIX_arm : public lldb_private::RegisterContext {
public:
  RegisterContextPOSIX_arm(lldb_private::Thread &thread,
                           uint32_t concrete_frame_idx,
                           lldb_private::RegisterInfoInterface *register_info);

  ~RegisterContextPOSIX_arm() override;

  void Invalidate();

  void InvalidateAllRegisters() override;

  size_t GetRegisterCount() override;

  virtual size_t GetGPRSize();

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

    uint32_t first_fpr_v;
    uint32_t last_fpr_v;

    uint32_t gpr_flags;
  };

  struct QReg {
    uint8_t bytes[16];
  };

  struct FPU {
    union {
      uint32_t s[32];
      uint64_t d[32];
      QReg q[16]; // the 128-bit NEON registers
    } floats;
    uint32_t fpscr;
  };

  uint32_t m_gpr_arm[lldb_private::k_num_gpr_registers_arm]; // 32-bit general
                                                             // purpose
                                                             // registers.
  RegInfo m_reg_info;
  struct RegisterContextPOSIX_arm::FPU
      m_fpr; // floating-point registers including extended register sets.
  std::unique_ptr<lldb_private::RegisterInfoInterface>
      m_register_info_ap; // Register Info Interface (FreeBSD or Linux)

  // Determines if an extended register set is supported on the processor
  // running the inferior process.
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

#endif // liblldb_RegisterContextPOSIX_arm_h_
