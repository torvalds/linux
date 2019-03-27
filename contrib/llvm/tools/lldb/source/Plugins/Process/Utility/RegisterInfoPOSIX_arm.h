//===-- RegisterInfoPOSIX_arm.h ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_RegisterInfoPOSIX_arm_h_
#define liblldb_RegisterInfoPOSIX_arm_h_

#include "RegisterInfoInterface.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/lldb-private.h"

class RegisterInfoPOSIX_arm : public lldb_private::RegisterInfoInterface {
public:
  struct GPR {
    uint32_t r[16]; // R0-R15
    uint32_t cpsr;  // CPSR
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
  struct EXC {
    uint32_t exception;
    uint32_t fsr; /* Fault status */
    uint32_t far; /* Virtual Fault Address */
  };

  struct DBG {
    uint32_t bvr[16];
    uint32_t bcr[16];
    uint32_t wvr[16];
    uint32_t wcr[16];
  };

  RegisterInfoPOSIX_arm(const lldb_private::ArchSpec &target_arch);

  size_t GetGPRSize() const override;

  const lldb_private::RegisterInfo *GetRegisterInfo() const override;

  uint32_t GetRegisterCount() const override;

private:
  const lldb_private::RegisterInfo *m_register_info_p;
  uint32_t m_register_info_count;
};

#endif // liblldb_RegisterInfoPOSIX_arm_h_
