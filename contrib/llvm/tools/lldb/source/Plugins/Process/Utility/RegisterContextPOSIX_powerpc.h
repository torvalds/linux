//===-- RegisterContextPOSIX_powerpc.h --------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_RegisterContextPOSIX_powerpc_h_
#define liblldb_RegisterContextPOSIX_powerpc_h_

#include "RegisterContext_powerpc.h"
#include "RegisterInfoInterface.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Utility/Log.h"

class ProcessMonitor;

// ---------------------------------------------------------------------------
// Internal codes for all powerpc registers.
// ---------------------------------------------------------------------------
enum {
  k_first_gpr_powerpc,
  gpr_r0_powerpc = k_first_gpr_powerpc,
  gpr_r1_powerpc,
  gpr_r2_powerpc,
  gpr_r3_powerpc,
  gpr_r4_powerpc,
  gpr_r5_powerpc,
  gpr_r6_powerpc,
  gpr_r7_powerpc,
  gpr_r8_powerpc,
  gpr_r9_powerpc,
  gpr_r10_powerpc,
  gpr_r11_powerpc,
  gpr_r12_powerpc,
  gpr_r13_powerpc,
  gpr_r14_powerpc,
  gpr_r15_powerpc,
  gpr_r16_powerpc,
  gpr_r17_powerpc,
  gpr_r18_powerpc,
  gpr_r19_powerpc,
  gpr_r20_powerpc,
  gpr_r21_powerpc,
  gpr_r22_powerpc,
  gpr_r23_powerpc,
  gpr_r24_powerpc,
  gpr_r25_powerpc,
  gpr_r26_powerpc,
  gpr_r27_powerpc,
  gpr_r28_powerpc,
  gpr_r29_powerpc,
  gpr_r30_powerpc,
  gpr_r31_powerpc,
  gpr_lr_powerpc,
  gpr_cr_powerpc,
  gpr_xer_powerpc,
  gpr_ctr_powerpc,
  gpr_pc_powerpc,
  k_last_gpr_powerpc = gpr_pc_powerpc,

  k_first_fpr,
  fpr_f0_powerpc = k_first_fpr,
  fpr_f1_powerpc,
  fpr_f2_powerpc,
  fpr_f3_powerpc,
  fpr_f4_powerpc,
  fpr_f5_powerpc,
  fpr_f6_powerpc,
  fpr_f7_powerpc,
  fpr_f8_powerpc,
  fpr_f9_powerpc,
  fpr_f10_powerpc,
  fpr_f11_powerpc,
  fpr_f12_powerpc,
  fpr_f13_powerpc,
  fpr_f14_powerpc,
  fpr_f15_powerpc,
  fpr_f16_powerpc,
  fpr_f17_powerpc,
  fpr_f18_powerpc,
  fpr_f19_powerpc,
  fpr_f20_powerpc,
  fpr_f21_powerpc,
  fpr_f22_powerpc,
  fpr_f23_powerpc,
  fpr_f24_powerpc,
  fpr_f25_powerpc,
  fpr_f26_powerpc,
  fpr_f27_powerpc,
  fpr_f28_powerpc,
  fpr_f29_powerpc,
  fpr_f30_powerpc,
  fpr_f31_powerpc,
  fpr_fpscr_powerpc,
  k_last_fpr = fpr_fpscr_powerpc,

  k_first_vmx,
  vmx_v0_powerpc = k_first_vmx,
  vmx_v1_powerpc,
  vmx_v2_powerpc,
  vmx_v3_powerpc,
  vmx_v4_powerpc,
  vmx_v5_powerpc,
  vmx_v6_powerpc,
  vmx_v7_powerpc,
  vmx_v8_powerpc,
  vmx_v9_powerpc,
  vmx_v10_powerpc,
  vmx_v11_powerpc,
  vmx_v12_powerpc,
  vmx_v13_powerpc,
  vmx_v14_powerpc,
  vmx_v15_powerpc,
  vmx_v16_powerpc,
  vmx_v17_powerpc,
  vmx_v18_powerpc,
  vmx_v19_powerpc,
  vmx_v20_powerpc,
  vmx_v21_powerpc,
  vmx_v22_powerpc,
  vmx_v23_powerpc,
  vmx_v24_powerpc,
  vmx_v25_powerpc,
  vmx_v26_powerpc,
  vmx_v27_powerpc,
  vmx_v28_powerpc,
  vmx_v29_powerpc,
  vmx_v30_powerpc,
  vmx_v31_powerpc,
  vmx_vrsave_powerpc,
  vmx_vscr_powerpc,
  k_last_vmx = vmx_vscr_powerpc,

  k_num_registers_powerpc,
  k_num_gpr_registers_powerpc = k_last_gpr_powerpc - k_first_gpr_powerpc + 1,
  k_num_fpr_registers_powerpc = k_last_fpr - k_first_fpr + 1,
  k_num_vmx_registers_powerpc = k_last_vmx - k_first_vmx + 1,
};

class RegisterContextPOSIX_powerpc : public lldb_private::RegisterContext {
public:
  RegisterContextPOSIX_powerpc(
      lldb_private::Thread &thread, uint32_t concrete_frame_idx,
      lldb_private::RegisterInfoInterface *register_info);

  ~RegisterContextPOSIX_powerpc() override;

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
  uint64_t
      m_gpr_powerpc[k_num_gpr_registers_powerpc]; // general purpose registers.
  uint64_t
      m_fpr_powerpc[k_num_fpr_registers_powerpc]; // floating point registers.
  uint32_t m_vmx_powerpc[k_num_vmx_registers_powerpc][4];
  std::unique_ptr<lldb_private::RegisterInfoInterface>
      m_register_info_ap; // Register Info Interface (FreeBSD or Linux)

  // Determines if an extended register set is supported on the processor
  // running the inferior process.
  virtual bool IsRegisterSetAvailable(size_t set_index);

  virtual const lldb_private::RegisterInfo *GetRegisterInfo();

  bool IsGPR(unsigned reg);

  bool IsFPR(unsigned reg);

  bool IsVMX(unsigned reg);

  lldb::ByteOrder GetByteOrder();

  virtual bool ReadGPR() = 0;
  virtual bool ReadFPR() = 0;
  virtual bool ReadVMX() = 0;
  virtual bool WriteGPR() = 0;
  virtual bool WriteFPR() = 0;
  virtual bool WriteVMX() = 0;
};

#endif // liblldb_RegisterContextPOSIX_powerpc_h_
