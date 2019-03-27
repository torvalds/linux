//===-- lldb-s390x-register-enums.h -----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef lldb_s390x_register_enums_h
#define lldb_s390x_register_enums_h

namespace lldb_private {
// LLDB register codes (e.g. RegisterKind == eRegisterKindLLDB)

//---------------------------------------------------------------------------
// Internal codes for all s390x registers.
//---------------------------------------------------------------------------
enum {
  k_first_gpr_s390x,
  lldb_r0_s390x = k_first_gpr_s390x,
  lldb_r1_s390x,
  lldb_r2_s390x,
  lldb_r3_s390x,
  lldb_r4_s390x,
  lldb_r5_s390x,
  lldb_r6_s390x,
  lldb_r7_s390x,
  lldb_r8_s390x,
  lldb_r9_s390x,
  lldb_r10_s390x,
  lldb_r11_s390x,
  lldb_r12_s390x,
  lldb_r13_s390x,
  lldb_r14_s390x,
  lldb_r15_s390x,
  lldb_acr0_s390x,
  lldb_acr1_s390x,
  lldb_acr2_s390x,
  lldb_acr3_s390x,
  lldb_acr4_s390x,
  lldb_acr5_s390x,
  lldb_acr6_s390x,
  lldb_acr7_s390x,
  lldb_acr8_s390x,
  lldb_acr9_s390x,
  lldb_acr10_s390x,
  lldb_acr11_s390x,
  lldb_acr12_s390x,
  lldb_acr13_s390x,
  lldb_acr14_s390x,
  lldb_acr15_s390x,
  lldb_pswm_s390x,
  lldb_pswa_s390x,
  k_last_gpr_s390x = lldb_pswa_s390x,

  k_first_fpr_s390x,
  lldb_f0_s390x = k_first_fpr_s390x,
  lldb_f1_s390x,
  lldb_f2_s390x,
  lldb_f3_s390x,
  lldb_f4_s390x,
  lldb_f5_s390x,
  lldb_f6_s390x,
  lldb_f7_s390x,
  lldb_f8_s390x,
  lldb_f9_s390x,
  lldb_f10_s390x,
  lldb_f11_s390x,
  lldb_f12_s390x,
  lldb_f13_s390x,
  lldb_f14_s390x,
  lldb_f15_s390x,
  lldb_fpc_s390x,
  k_last_fpr_s390x = lldb_fpc_s390x,

  // These are only available on Linux.
  k_first_linux_s390x,
  lldb_orig_r2_s390x = k_first_linux_s390x,
  lldb_last_break_s390x,
  lldb_system_call_s390x,
  k_last_linux_s390x = lldb_system_call_s390x,

  k_num_registers_s390x,
  k_num_gpr_registers_s390x = k_last_gpr_s390x - k_first_gpr_s390x + 1,
  k_num_fpr_registers_s390x = k_last_fpr_s390x - k_first_fpr_s390x + 1,
  k_num_linux_registers_s390x = k_last_linux_s390x - k_first_linux_s390x + 1,
  k_num_user_registers_s390x =
      k_num_gpr_registers_s390x + k_num_fpr_registers_s390x,
};
}

#endif // #ifndef lldb_s390x_register_enums_h
