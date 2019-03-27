//===-- lldb-mips-freebsd-register-enums.h ----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef lldb_mips_freebsd_register_enums_h
#define lldb_mips_freebsd_register_enums_h

namespace lldb_private {
// LLDB register codes (e.g. RegisterKind == eRegisterKindLLDB)

//---------------------------------------------------------------------------
// Internal codes for all mips registers.
//---------------------------------------------------------------------------
enum {
  k_first_gpr_mips64,
  gpr_zero_mips64 = k_first_gpr_mips64,
  gpr_r1_mips64,
  gpr_r2_mips64,
  gpr_r3_mips64,
  gpr_r4_mips64,
  gpr_r5_mips64,
  gpr_r6_mips64,
  gpr_r7_mips64,
  gpr_r8_mips64,
  gpr_r9_mips64,
  gpr_r10_mips64,
  gpr_r11_mips64,
  gpr_r12_mips64,
  gpr_r13_mips64,
  gpr_r14_mips64,
  gpr_r15_mips64,
  gpr_r16_mips64,
  gpr_r17_mips64,
  gpr_r18_mips64,
  gpr_r19_mips64,
  gpr_r20_mips64,
  gpr_r21_mips64,
  gpr_r22_mips64,
  gpr_r23_mips64,
  gpr_r24_mips64,
  gpr_r25_mips64,
  gpr_r26_mips64,
  gpr_r27_mips64,
  gpr_gp_mips64,
  gpr_sp_mips64,
  gpr_r30_mips64,
  gpr_ra_mips64,
  gpr_sr_mips64,
  gpr_mullo_mips64,
  gpr_mulhi_mips64,
  gpr_badvaddr_mips64,
  gpr_cause_mips64,
  gpr_pc_mips64,
  gpr_ic_mips64,
  gpr_dummy_mips64,
  k_last_gpr_mips64 = gpr_dummy_mips64,

  k_num_registers_mips64,

  k_num_gpr_registers_mips64 = k_last_gpr_mips64 - k_first_gpr_mips64 + 1
};
}
#endif // #ifndef lldb_mips_freebsd_register_enums_h
