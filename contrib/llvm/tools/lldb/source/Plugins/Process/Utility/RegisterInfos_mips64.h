//===-- RegisterInfos_mips64.h ----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <stddef.h>

#include "lldb/Core/dwarf.h"
#include "llvm/Support/Compiler.h"


#ifdef DECLARE_REGISTER_INFOS_MIPS64_STRUCT

// Computes the offset of the given GPR in the user data area.
#ifdef LINUX_MIPS64
#define GPR_OFFSET(regname)                                                    \
  (LLVM_EXTENSION offsetof(UserArea, gpr) +                                    \
   LLVM_EXTENSION offsetof(GPR_linux_mips, regname))
#else
#define GPR_OFFSET(regname) (LLVM_EXTENSION offsetof(GPR_freebsd_mips, regname))
#endif

// Computes the offset of the given FPR in the extended data area.
#define FPR_OFFSET(regname)                                                    \
  (LLVM_EXTENSION offsetof(UserArea, fpr) +                                    \
   LLVM_EXTENSION offsetof(FPR_linux_mips, regname))

// Computes the offset of the given MSA in the extended data area.
#define MSA_OFFSET(regname)                                                    \
  (LLVM_EXTENSION offsetof(UserArea, msa) +                                    \
   LLVM_EXTENSION offsetof(MSA_linux_mips, regname))

// RegisterKind: EHFrame, DWARF, Generic, Process Plugin, LLDB

// Note that the size and offset will be updated by platform-specific classes.
#ifdef LINUX_MIPS64
#define DEFINE_GPR(reg, alt, kind1, kind2, kind3)                              \
  {                                                                            \
    #reg, alt, sizeof(((GPR_linux_mips *) 0)->reg),                            \
                      GPR_OFFSET(reg), eEncodingUint, eFormatHex,              \
                                 {kind1, kind2, kind3, ptrace_##reg##_mips,    \
                                  gpr_##reg##_mips64 },                        \
                                  NULL, NULL, NULL, 0                          \
  }
#else
#define DEFINE_GPR(reg, alt, kind1, kind2, kind3, kind4)                       \
  {                                                                            \
    #reg, alt, sizeof(((GPR_freebsd_mips *) 0)->reg),                          \
                      GPR_OFFSET(reg), eEncodingUint, eFormatHex,              \
                                 {kind1, kind2, kind3, kind4,                  \
                                  gpr_##reg##_mips64 },                        \
                                  NULL, NULL, NULL, 0                          \
  }
#endif

#define DEFINE_GPR_INFO(reg, alt, kind1, kind2, kind3)                         \
  {                                                                            \
    #reg, alt, sizeof(((GPR_linux_mips *) 0)->reg) / 2,                        \
                      GPR_OFFSET(reg), eEncodingUint, eFormatHex,              \
                                 {kind1, kind2, kind3, ptrace_##reg##_mips,    \
                                  gpr_##reg##_mips64 },                        \
                                  NULL, NULL, NULL, 0                          \
  }

const uint8_t dwarf_opcode_mips64[] = {
    llvm::dwarf::DW_OP_regx,  dwarf_sr_mips64,        llvm::dwarf::DW_OP_lit1,
    llvm::dwarf::DW_OP_lit26, llvm::dwarf::DW_OP_shl, llvm::dwarf::DW_OP_and,
    llvm::dwarf::DW_OP_lit26, llvm::dwarf::DW_OP_shr};

#define DEFINE_FPR(reg, alt, kind1, kind2, kind3)                              \
  {                                                                            \
    #reg, alt, sizeof(((FPR_linux_mips *) 0)->reg),                            \
                      FPR_OFFSET(reg), eEncodingIEEE754, eFormatFloat,         \
                                 {kind1, kind2, kind3, ptrace_##reg##_mips,    \
                                  fpr_##reg##_mips64 },                        \
                                  NULL, NULL, dwarf_opcode_mips64,             \
                                  sizeof(dwarf_opcode_mips64)                  \
  }

#define DEFINE_FPR_INFO(reg, alt, kind1, kind2, kind3)                         \
  {                                                                            \
    #reg, alt, sizeof(((FPR_linux_mips *) 0)->reg),                            \
                      FPR_OFFSET(reg), eEncodingUint, eFormatHex,              \
                                 {kind1, kind2, kind3, ptrace_##reg##_mips,    \
                                  fpr_##reg##_mips64 },                        \
                                  NULL, NULL, NULL, 0                          \
  }

#define DEFINE_MSA(reg, alt, kind1, kind2, kind3, kind4)                       \
  {                                                                            \
    #reg, alt, sizeof(((MSA_linux_mips *) 0)->reg),                            \
                      MSA_OFFSET(reg), eEncodingVector, eFormatVectorOfUInt8,  \
                                 {kind1, kind2, kind3, kind4,                  \
                                  msa_##reg##_mips64 },                        \
                                  NULL, NULL, NULL, 0                          \
  }

#define DEFINE_MSA_INFO(reg, alt, kind1, kind2, kind3, kind4)                  \
  {                                                                            \
    #reg, alt, sizeof(((MSA_linux_mips *) 0)->reg),                            \
                      MSA_OFFSET(reg), eEncodingUint, eFormatHex,              \
                                 {kind1, kind2, kind3, kind4,                  \
                                  msa_##reg##_mips64 },                        \
                                  NULL, NULL, NULL, 0                          \
  }

static RegisterInfo g_register_infos_mips64[] = {
// General purpose registers.            EH_Frame,                  DWARF,
// Generic,    Process Plugin
#ifndef LINUX_MIPS64
    DEFINE_GPR(zero, "r0", dwarf_zero_mips64, dwarf_zero_mips64,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_GPR(r1, nullptr, dwarf_r1_mips64, dwarf_r1_mips64,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_GPR(r2, nullptr, dwarf_r2_mips64, dwarf_r2_mips64,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_GPR(r3, nullptr, dwarf_r3_mips64, dwarf_r3_mips64,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_GPR(r4, nullptr, dwarf_r4_mips64, dwarf_r4_mips64,
               LLDB_REGNUM_GENERIC_ARG1, LLDB_INVALID_REGNUM),
    DEFINE_GPR(r5, nullptr, dwarf_r5_mips64, dwarf_r5_mips64,
               LLDB_REGNUM_GENERIC_ARG2, LLDB_INVALID_REGNUM),
    DEFINE_GPR(r6, nullptr, dwarf_r6_mips64, dwarf_r6_mips64,
               LLDB_REGNUM_GENERIC_ARG3, LLDB_INVALID_REGNUM),
    DEFINE_GPR(r7, nullptr, dwarf_r7_mips64, dwarf_r7_mips64,
               LLDB_REGNUM_GENERIC_ARG4, LLDB_INVALID_REGNUM),
    DEFINE_GPR(r8, nullptr, dwarf_r8_mips64, dwarf_r8_mips64,
               LLDB_REGNUM_GENERIC_ARG5, LLDB_INVALID_REGNUM),
    DEFINE_GPR(r9, nullptr, dwarf_r9_mips64, dwarf_r9_mips64,
               LLDB_REGNUM_GENERIC_ARG6, LLDB_INVALID_REGNUM),
    DEFINE_GPR(r10, nullptr, dwarf_r10_mips64, dwarf_r10_mips64,
               LLDB_REGNUM_GENERIC_ARG7, LLDB_INVALID_REGNUM),
    DEFINE_GPR(r11, nullptr, dwarf_r11_mips64, dwarf_r11_mips64,
               LLDB_REGNUM_GENERIC_ARG8, LLDB_INVALID_REGNUM),
    DEFINE_GPR(r12, nullptr, dwarf_r12_mips64, dwarf_r12_mips64,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_GPR(r13, nullptr, dwarf_r13_mips64, dwarf_r13_mips64,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_GPR(r14, nullptr, dwarf_r14_mips64, dwarf_r14_mips64,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_GPR(r15, nullptr, dwarf_r15_mips64, dwarf_r15_mips64,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_GPR(r16, nullptr, dwarf_r16_mips64, dwarf_r16_mips64,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_GPR(r17, nullptr, dwarf_r17_mips64, dwarf_r17_mips64,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_GPR(r18, nullptr, dwarf_r18_mips64, dwarf_r18_mips64,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_GPR(r19, nullptr, dwarf_r19_mips64, dwarf_r19_mips64,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_GPR(r20, nullptr, dwarf_r20_mips64, dwarf_r20_mips64,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_GPR(r21, nullptr, dwarf_r21_mips64, dwarf_r21_mips64,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_GPR(r22, nullptr, dwarf_r22_mips64, dwarf_r22_mips64,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_GPR(r23, nullptr, dwarf_r23_mips64, dwarf_r23_mips64,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_GPR(r24, nullptr, dwarf_r24_mips64, dwarf_r24_mips64,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_GPR(r25, nullptr, dwarf_r25_mips64, dwarf_r25_mips64,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_GPR(r26, nullptr, dwarf_r26_mips64, dwarf_r26_mips64,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_GPR(r27, nullptr, dwarf_r27_mips64, dwarf_r27_mips64,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_GPR(gp, "r28", dwarf_gp_mips64, dwarf_gp_mips64, LLDB_INVALID_REGNUM,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(sp, "r29", dwarf_sp_mips64, dwarf_sp_mips64,
               LLDB_REGNUM_GENERIC_SP, LLDB_INVALID_REGNUM),
    DEFINE_GPR(r30, nullptr, dwarf_r30_mips64, dwarf_r30_mips64,
               LLDB_REGNUM_GENERIC_FP, LLDB_INVALID_REGNUM),
    DEFINE_GPR(ra, "r31", dwarf_ra_mips64, dwarf_ra_mips64,
               LLDB_REGNUM_GENERIC_RA, LLDB_INVALID_REGNUM),
    DEFINE_GPR(sr, nullptr, dwarf_sr_mips64, dwarf_sr_mips64,
               LLDB_REGNUM_GENERIC_FLAGS, LLDB_INVALID_REGNUM),
    DEFINE_GPR(mullo, nullptr, dwarf_lo_mips64, dwarf_lo_mips64,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_GPR(mulhi, nullptr, dwarf_hi_mips64, dwarf_hi_mips64,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_GPR(badvaddr, nullptr, dwarf_bad_mips64, dwarf_bad_mips64,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_GPR(cause, nullptr, dwarf_cause_mips64, dwarf_cause_mips64,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_GPR(pc, "pc", dwarf_pc_mips64, dwarf_pc_mips64,
               LLDB_REGNUM_GENERIC_PC, LLDB_INVALID_REGNUM),
    DEFINE_GPR(ic, nullptr, dwarf_ic_mips64, dwarf_ic_mips64,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_GPR(dummy, nullptr, dwarf_dummy_mips64, dwarf_dummy_mips64,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
#else
    DEFINE_GPR(zero, "r0", dwarf_zero_mips64, dwarf_zero_mips64,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(r1, nullptr, dwarf_r1_mips64, dwarf_r1_mips64,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(r2, nullptr, dwarf_r2_mips64, dwarf_r2_mips64,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(r3, nullptr, dwarf_r3_mips64, dwarf_r3_mips64,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(r4, nullptr, dwarf_r4_mips64, dwarf_r4_mips64,
               LLDB_REGNUM_GENERIC_ARG1),
    DEFINE_GPR(r5, nullptr, dwarf_r5_mips64, dwarf_r5_mips64,
               LLDB_REGNUM_GENERIC_ARG2),
    DEFINE_GPR(r6, nullptr, dwarf_r6_mips64, dwarf_r6_mips64,
               LLDB_REGNUM_GENERIC_ARG3),
    DEFINE_GPR(r7, nullptr, dwarf_r7_mips64, dwarf_r7_mips64,
               LLDB_REGNUM_GENERIC_ARG4),
    DEFINE_GPR(r8, nullptr, dwarf_r8_mips64, dwarf_r8_mips64,
               LLDB_REGNUM_GENERIC_ARG5),
    DEFINE_GPR(r9, nullptr, dwarf_r9_mips64, dwarf_r9_mips64,
               LLDB_REGNUM_GENERIC_ARG6),
    DEFINE_GPR(r10, nullptr, dwarf_r10_mips64, dwarf_r10_mips64,
               LLDB_REGNUM_GENERIC_ARG7),
    DEFINE_GPR(r11, nullptr, dwarf_r11_mips64, dwarf_r11_mips64,
               LLDB_REGNUM_GENERIC_ARG8),
    DEFINE_GPR(r12, nullptr, dwarf_r12_mips64, dwarf_r12_mips64,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(r13, nullptr, dwarf_r13_mips64, dwarf_r13_mips64,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(r14, nullptr, dwarf_r14_mips64, dwarf_r14_mips64,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(r15, nullptr, dwarf_r15_mips64, dwarf_r15_mips64,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(r16, nullptr, dwarf_r16_mips64, dwarf_r16_mips64,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(r17, nullptr, dwarf_r17_mips64, dwarf_r17_mips64,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(r18, nullptr, dwarf_r18_mips64, dwarf_r18_mips64,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(r19, nullptr, dwarf_r19_mips64, dwarf_r19_mips64,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(r20, nullptr, dwarf_r20_mips64, dwarf_r20_mips64,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(r21, nullptr, dwarf_r21_mips64, dwarf_r21_mips64,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(r22, nullptr, dwarf_r22_mips64, dwarf_r22_mips64,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(r23, nullptr, dwarf_r23_mips64, dwarf_r23_mips64,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(r24, nullptr, dwarf_r24_mips64, dwarf_r24_mips64,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(r25, nullptr, dwarf_r25_mips64, dwarf_r25_mips64,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(r26, nullptr, dwarf_r26_mips64, dwarf_r26_mips64,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(r27, nullptr, dwarf_r27_mips64, dwarf_r27_mips64,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(gp, "r28", dwarf_gp_mips64, dwarf_gp_mips64,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(sp, "r29", dwarf_sp_mips64, dwarf_sp_mips64,
               LLDB_REGNUM_GENERIC_SP),
    DEFINE_GPR(r30, nullptr, dwarf_r30_mips64, dwarf_r30_mips64,
               LLDB_REGNUM_GENERIC_FP),
    DEFINE_GPR(ra, "r31", dwarf_ra_mips64, dwarf_ra_mips64,
               LLDB_REGNUM_GENERIC_RA),
    DEFINE_GPR_INFO(sr, nullptr, dwarf_sr_mips64, dwarf_sr_mips64,
                    LLDB_REGNUM_GENERIC_FLAGS),
    DEFINE_GPR(mullo, nullptr, dwarf_lo_mips64, dwarf_lo_mips64,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(mulhi, nullptr, dwarf_hi_mips64, dwarf_hi_mips64,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(badvaddr, nullptr, dwarf_bad_mips64, dwarf_bad_mips64,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR_INFO(cause, nullptr, dwarf_cause_mips64, dwarf_cause_mips64,
                    LLDB_INVALID_REGNUM),
    DEFINE_GPR(pc, "pc", dwarf_pc_mips64, dwarf_pc_mips64,
               LLDB_REGNUM_GENERIC_PC),
    DEFINE_GPR_INFO(config5, nullptr, dwarf_config5_mips64,
                    dwarf_config5_mips64, LLDB_INVALID_REGNUM),
    DEFINE_FPR(f0, nullptr, dwarf_f0_mips64, dwarf_f0_mips64,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(f1, nullptr, dwarf_f1_mips64, dwarf_f1_mips64,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(f2, nullptr, dwarf_f2_mips64, dwarf_f2_mips64,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(f3, nullptr, dwarf_f3_mips64, dwarf_f3_mips64,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(f4, nullptr, dwarf_f4_mips64, dwarf_f4_mips64,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(f5, nullptr, dwarf_f5_mips64, dwarf_f5_mips64,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(f6, nullptr, dwarf_f6_mips64, dwarf_f6_mips64,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(f7, nullptr, dwarf_f7_mips64, dwarf_f7_mips64,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(f8, nullptr, dwarf_f8_mips64, dwarf_f8_mips64,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(f9, nullptr, dwarf_f9_mips64, dwarf_f9_mips64,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(f10, nullptr, dwarf_f10_mips64, dwarf_f10_mips64,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(f11, nullptr, dwarf_f11_mips64, dwarf_f11_mips64,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(f12, nullptr, dwarf_f12_mips64, dwarf_f12_mips64,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(f13, nullptr, dwarf_f13_mips64, dwarf_f13_mips64,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(f14, nullptr, dwarf_f14_mips64, dwarf_f14_mips64,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(f15, nullptr, dwarf_f15_mips64, dwarf_f15_mips64,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(f16, nullptr, dwarf_f16_mips64, dwarf_f16_mips64,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(f17, nullptr, dwarf_f17_mips64, dwarf_f17_mips64,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(f18, nullptr, dwarf_f18_mips64, dwarf_f18_mips64,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(f19, nullptr, dwarf_f19_mips64, dwarf_f19_mips64,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(f20, nullptr, dwarf_f20_mips64, dwarf_f20_mips64,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(f21, nullptr, dwarf_f21_mips64, dwarf_f21_mips64,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(f22, nullptr, dwarf_f22_mips64, dwarf_f22_mips64,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(f23, nullptr, dwarf_f23_mips64, dwarf_f23_mips64,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(f24, nullptr, dwarf_f24_mips64, dwarf_f24_mips64,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(f25, nullptr, dwarf_f25_mips64, dwarf_f25_mips64,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(f26, nullptr, dwarf_f26_mips64, dwarf_f26_mips64,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(f27, nullptr, dwarf_f27_mips64, dwarf_f27_mips64,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(f28, nullptr, dwarf_f28_mips64, dwarf_f28_mips64,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(f29, nullptr, dwarf_f29_mips64, dwarf_f29_mips64,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(f30, nullptr, dwarf_f30_mips64, dwarf_f30_mips64,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(f31, nullptr, dwarf_f31_mips64, dwarf_f31_mips64,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR_INFO(fcsr, nullptr, dwarf_fcsr_mips64, dwarf_fcsr_mips64,
                    LLDB_INVALID_REGNUM),
    DEFINE_FPR_INFO(fir, nullptr, dwarf_fir_mips64, dwarf_fir_mips64,
                    LLDB_INVALID_REGNUM),
    DEFINE_FPR_INFO(config5, nullptr, dwarf_config5_mips64,
                    dwarf_config5_mips64, LLDB_INVALID_REGNUM),
    DEFINE_MSA(w0, nullptr, dwarf_w0_mips64, dwarf_w0_mips64,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_MSA(w1, nullptr, dwarf_w1_mips64, dwarf_w1_mips64,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_MSA(w2, nullptr, dwarf_w2_mips64, dwarf_w2_mips64,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_MSA(w3, nullptr, dwarf_w3_mips64, dwarf_w3_mips64,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_MSA(w4, nullptr, dwarf_w4_mips64, dwarf_w4_mips64,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_MSA(w5, nullptr, dwarf_w5_mips64, dwarf_w5_mips64,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_MSA(w6, nullptr, dwarf_w6_mips64, dwarf_w6_mips64,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_MSA(w7, nullptr, dwarf_w7_mips64, dwarf_w7_mips64,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_MSA(w8, nullptr, dwarf_w8_mips64, dwarf_w8_mips64,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_MSA(w9, nullptr, dwarf_w9_mips64, dwarf_w9_mips64,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_MSA(w10, nullptr, dwarf_w10_mips64, dwarf_w10_mips64,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_MSA(w11, nullptr, dwarf_w11_mips64, dwarf_w11_mips64,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_MSA(w12, nullptr, dwarf_w12_mips64, dwarf_w12_mips64,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_MSA(w13, nullptr, dwarf_w13_mips64, dwarf_w13_mips64,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_MSA(w14, nullptr, dwarf_w14_mips64, dwarf_w14_mips64,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_MSA(w15, nullptr, dwarf_w15_mips64, dwarf_w15_mips64,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_MSA(w16, nullptr, dwarf_w16_mips64, dwarf_w16_mips64,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_MSA(w17, nullptr, dwarf_w17_mips64, dwarf_w17_mips64,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_MSA(w18, nullptr, dwarf_w18_mips64, dwarf_w18_mips64,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_MSA(w19, nullptr, dwarf_w19_mips64, dwarf_w19_mips64,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_MSA(w20, nullptr, dwarf_w10_mips64, dwarf_w20_mips64,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_MSA(w21, nullptr, dwarf_w21_mips64, dwarf_w21_mips64,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_MSA(w22, nullptr, dwarf_w22_mips64, dwarf_w22_mips64,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_MSA(w23, nullptr, dwarf_w23_mips64, dwarf_w23_mips64,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_MSA(w24, nullptr, dwarf_w24_mips64, dwarf_w24_mips64,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_MSA(w25, nullptr, dwarf_w25_mips64, dwarf_w25_mips64,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_MSA(w26, nullptr, dwarf_w26_mips64, dwarf_w26_mips64,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_MSA(w27, nullptr, dwarf_w27_mips64, dwarf_w27_mips64,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_MSA(w28, nullptr, dwarf_w28_mips64, dwarf_w28_mips64,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_MSA(w29, nullptr, dwarf_w29_mips64, dwarf_w29_mips64,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_MSA(w30, nullptr, dwarf_w30_mips64, dwarf_w30_mips64,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_MSA(w31, nullptr, dwarf_w31_mips64, dwarf_w31_mips64,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_MSA_INFO(mcsr, nullptr, dwarf_mcsr_mips64, dwarf_mcsr_mips64,
                    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_MSA_INFO(mir, nullptr, dwarf_mir_mips64, dwarf_mir_mips64,
                    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_MSA_INFO(fcsr, nullptr, dwarf_fcsr_mips64, dwarf_fcsr_mips64,
                    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_MSA_INFO(fir, nullptr, dwarf_fir_mips64, dwarf_fir_mips64,
                    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_MSA_INFO(config5, nullptr, dwarf_config5_mips64,
                    dwarf_config5_mips64, LLDB_INVALID_REGNUM,
                    LLDB_INVALID_REGNUM)
#endif
};

static_assert((sizeof(g_register_infos_mips64) /
               sizeof(g_register_infos_mips64[0])) == k_num_registers_mips64,
              "g_register_infos_mips64 has wrong number of register infos");

#undef DEFINE_GPR
#undef DEFINE_GPR_INFO
#undef DEFINE_FPR
#undef DEFINE_FPR_INFO
#undef DEFINE_MSA
#undef DEFINE_MSA_INFO
#undef GPR_OFFSET
#undef FPR_OFFSET
#undef MSA_OFFSET

#endif // DECLARE_REGISTER_INFOS_MIPS64_STRUCT
