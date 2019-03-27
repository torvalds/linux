//===-- RegisterContextPOSIX_ppc64le.h --------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_RegisterContextPOSIX_ppc64le_h_
#define liblldb_RegisterContextPOSIX_ppc64le_h_

#include "Plugins/Process/Utility/lldb-ppc64le-register-enums.h"
#include "RegisterInfoInterface.h"
#include "Utility/PPC64LE_DWARF_Registers.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Utility/Log.h"

class RegisterContextPOSIX_ppc64le : public lldb_private::RegisterContext {
public:
  RegisterContextPOSIX_ppc64le(
      lldb_private::Thread &thread, uint32_t concrete_frame_idx,
      lldb_private::RegisterInfoInterface *register_info);

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
  // 64-bit general purpose registers.
  uint64_t m_gpr_ppc64le[k_num_gpr_registers_ppc64le];

  // floating-point registers including extended register.
  uint64_t m_fpr_ppc64le[k_num_fpr_registers_ppc64le];

  // VMX registers.
  uint64_t m_vmx_ppc64le[k_num_vmx_registers_ppc64le * 2];

  // VSX registers.
  uint64_t m_vsx_ppc64le[k_num_vsx_registers_ppc64le * 2];

  std::unique_ptr<lldb_private::RegisterInfoInterface> m_register_info_ap;

  // Determines if an extended register set is supported on the processor
  // running the inferior process.
  virtual bool IsRegisterSetAvailable(size_t set_index);

  virtual const lldb_private::RegisterInfo *GetRegisterInfo();

  bool IsGPR(unsigned reg);

  bool IsFPR(unsigned reg);

  bool IsVMX(unsigned reg);

  bool IsVSX(unsigned reg);

  lldb::ByteOrder GetByteOrder();
};

#endif // liblldb_RegisterContextPOSIX_ppc64le_h_
