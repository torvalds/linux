//===-- RegisterInfos_mips.h -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===---------------------------------------------------------------------===//

#include <stddef.h>

#include "lldb/Core/dwarf.h"
#include "llvm/Support/Compiler.h"


#ifdef DECLARE_REGISTER_INFOS_MIPS_STRUCT

// Computes the offset of the given GPR in the user data area.
#define GPR_OFFSET(regname)                                                    \
  (LLVM_EXTENSION offsetof(UserArea, gpr) +                                    \
   LLVM_EXTENSION offsetof(GPR_linux_mips, regname))

// Computes the offset of the given FPR in the extended data area.
#define FPR_OFFSET(regname)                                                    \
  (LLVM_EXTENSION offsetof(UserArea, fpr) +                                    \
   LLVM_EXTENSION offsetof(FPR_linux_mips, regname))

// Computes the offset of the given MSA in the extended data area.
#define MSA_OFFSET(regname)                                                    \
  (LLVM_EXTENSION offsetof(UserArea, msa) +                                    \
   LLVM_EXTENSION offsetof(MSA_linux_mips, regname))

// Note that the size and offset will be updated by platform-specific classes.
#define DEFINE_GPR(reg, alt, kind1, kind2, kind3)                              \
  {                                                                            \
    #reg, alt, sizeof(((GPR_linux_mips *) NULL)->reg) / 2,                     \
                      GPR_OFFSET(reg), eEncodingUint, eFormatHex,              \
                                 {kind1, kind2, kind3, ptrace_##reg##_mips,    \
                                  gpr_##reg##_mips },                          \
                                  NULL, NULL, NULL, 0                          \
  }

const uint8_t dwarf_opcode_mips[] = {
    llvm::dwarf::DW_OP_regx,  dwarf_sr_mips,          llvm::dwarf::DW_OP_lit1,
    llvm::dwarf::DW_OP_lit26, llvm::dwarf::DW_OP_shl, llvm::dwarf::DW_OP_and,
    llvm::dwarf::DW_OP_lit26, llvm::dwarf::DW_OP_shr};

#define DEFINE_FPR(reg, alt, kind1, kind2, kind3)                              \
  {                                                                            \
    #reg, alt, sizeof(((FPR_linux_mips *) NULL)->reg),                         \
                      FPR_OFFSET(reg), eEncodingIEEE754, eFormatFloat,         \
                                 {kind1, kind2, kind3, ptrace_##reg##_mips,    \
                                  fpr_##reg##_mips },                          \
                                  NULL, NULL, dwarf_opcode_mips,               \
                                  sizeof(dwarf_opcode_mips)                    \
  }

#define DEFINE_FPR_INFO(reg, alt, kind1, kind2, kind3)                         \
  {                                                                            \
    #reg, alt, sizeof(((FPR_linux_mips *) NULL)->reg),                         \
                      FPR_OFFSET(reg), eEncodingUint, eFormatHex,              \
                                 {kind1, kind2, kind3, ptrace_##reg##_mips,    \
                                  fpr_##reg##_mips },                          \
                                  NULL, NULL, NULL, 0                          \
  }

#define DEFINE_MSA(reg, alt, kind1, kind2, kind3, kind4)                       \
  {                                                                            \
    #reg, alt, sizeof(((MSA_linux_mips *) 0)->reg),                            \
                      MSA_OFFSET(reg), eEncodingVector, eFormatVectorOfUInt8,  \
                                 {kind1, kind2, kind3, kind4,                  \
                                  msa_##reg##_mips },                          \
                                  NULL, NULL, NULL, 0                          \
  }

#define DEFINE_MSA_INFO(reg, alt, kind1, kind2, kind3, kind4)                  \
  {                                                                            \
    #reg, alt, sizeof(((MSA_linux_mips *) 0)->reg),                            \
                      MSA_OFFSET(reg), eEncodingUint, eFormatHex,              \
                                 {kind1, kind2, kind3, kind4,                  \
                                  msa_##reg##_mips },                          \
                                  NULL, NULL, NULL, 0                          \
  }

// RegisterKind: EH_Frame, DWARF, Generic, Procss Plugin, LLDB

static RegisterInfo g_register_infos_mips[] = {
    DEFINE_GPR(zero, "zero", dwarf_zero_mips, dwarf_zero_mips,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(r1, "at", dwarf_r1_mips, dwarf_r1_mips, LLDB_INVALID_REGNUM),
    DEFINE_GPR(r2, nullptr, dwarf_r2_mips, dwarf_r2_mips, LLDB_INVALID_REGNUM),
    DEFINE_GPR(r3, nullptr, dwarf_r3_mips, dwarf_r3_mips, LLDB_INVALID_REGNUM),
    DEFINE_GPR(r4, nullptr, dwarf_r4_mips, dwarf_r4_mips,
               LLDB_REGNUM_GENERIC_ARG1),
    DEFINE_GPR(r5, nullptr, dwarf_r5_mips, dwarf_r5_mips,
               LLDB_REGNUM_GENERIC_ARG2),
    DEFINE_GPR(r6, nullptr, dwarf_r6_mips, dwarf_r6_mips,
               LLDB_REGNUM_GENERIC_ARG3),
    DEFINE_GPR(r7, nullptr, dwarf_r7_mips, dwarf_r7_mips,
               LLDB_REGNUM_GENERIC_ARG4),
    DEFINE_GPR(r8, nullptr, dwarf_r8_mips, dwarf_r8_mips, LLDB_INVALID_REGNUM),
    DEFINE_GPR(r9, nullptr, dwarf_r9_mips, dwarf_r9_mips, LLDB_INVALID_REGNUM),
    DEFINE_GPR(r10, nullptr, dwarf_r10_mips, dwarf_r10_mips,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(r11, nullptr, dwarf_r11_mips, dwarf_r11_mips,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(r12, nullptr, dwarf_r12_mips, dwarf_r12_mips,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(r13, nullptr, dwarf_r13_mips, dwarf_r13_mips,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(r14, nullptr, dwarf_r14_mips, dwarf_r14_mips,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(r15, nullptr, dwarf_r15_mips, dwarf_r15_mips,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(r16, nullptr, dwarf_r16_mips, dwarf_r16_mips,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(r17, nullptr, dwarf_r17_mips, dwarf_r17_mips,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(r18, nullptr, dwarf_r18_mips, dwarf_r18_mips,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(r19, nullptr, dwarf_r19_mips, dwarf_r19_mips,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(r20, nullptr, dwarf_r20_mips, dwarf_r20_mips,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(r21, nullptr, dwarf_r21_mips, dwarf_r21_mips,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(r22, nullptr, dwarf_r22_mips, dwarf_r22_mips,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(r23, nullptr, dwarf_r23_mips, dwarf_r23_mips,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(r24, nullptr, dwarf_r24_mips, dwarf_r24_mips,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(r25, nullptr, dwarf_r25_mips, dwarf_r25_mips,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(r26, nullptr, dwarf_r26_mips, dwarf_r26_mips,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(r27, nullptr, dwarf_r27_mips, dwarf_r27_mips,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(gp, "gp", dwarf_gp_mips, dwarf_gp_mips, LLDB_INVALID_REGNUM),
    DEFINE_GPR(sp, "sp", dwarf_sp_mips, dwarf_sp_mips, LLDB_REGNUM_GENERIC_SP),
    DEFINE_GPR(r30, "fp", dwarf_r30_mips, dwarf_r30_mips,
               LLDB_REGNUM_GENERIC_FP),
    DEFINE_GPR(ra, "ra", dwarf_ra_mips, dwarf_ra_mips, LLDB_REGNUM_GENERIC_RA),
    DEFINE_GPR(sr, "status", dwarf_sr_mips, dwarf_sr_mips,
               LLDB_REGNUM_GENERIC_FLAGS),
    DEFINE_GPR(mullo, nullptr, dwarf_lo_mips, dwarf_lo_mips,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(mulhi, nullptr, dwarf_hi_mips, dwarf_hi_mips,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(badvaddr, nullptr, dwarf_bad_mips, dwarf_bad_mips,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(cause, nullptr, dwarf_cause_mips, dwarf_cause_mips,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(pc, nullptr, dwarf_pc_mips, dwarf_pc_mips,
               LLDB_REGNUM_GENERIC_PC),
    DEFINE_GPR(config5, nullptr, dwarf_config5_mips, dwarf_config5_mips,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(f0, nullptr, dwarf_f0_mips, dwarf_f0_mips, LLDB_INVALID_REGNUM),
    DEFINE_FPR(f1, nullptr, dwarf_f1_mips, dwarf_f1_mips, LLDB_INVALID_REGNUM),
    DEFINE_FPR(f2, nullptr, dwarf_f2_mips, dwarf_f2_mips, LLDB_INVALID_REGNUM),
    DEFINE_FPR(f3, nullptr, dwarf_f3_mips, dwarf_f3_mips, LLDB_INVALID_REGNUM),
    DEFINE_FPR(f4, nullptr, dwarf_f4_mips, dwarf_f4_mips, LLDB_INVALID_REGNUM),
    DEFINE_FPR(f5, nullptr, dwarf_f5_mips, dwarf_f5_mips, LLDB_INVALID_REGNUM),
    DEFINE_FPR(f6, nullptr, dwarf_f6_mips, dwarf_f6_mips, LLDB_INVALID_REGNUM),
    DEFINE_FPR(f7, nullptr, dwarf_f7_mips, dwarf_f7_mips, LLDB_INVALID_REGNUM),
    DEFINE_FPR(f8, nullptr, dwarf_f8_mips, dwarf_f8_mips, LLDB_INVALID_REGNUM),
    DEFINE_FPR(f9, nullptr, dwarf_f9_mips, dwarf_f9_mips, LLDB_INVALID_REGNUM),
    DEFINE_FPR(f10, nullptr, dwarf_f10_mips, dwarf_f10_mips,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(f11, nullptr, dwarf_f11_mips, dwarf_f11_mips,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(f12, nullptr, dwarf_f12_mips, dwarf_f12_mips,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(f13, nullptr, dwarf_f13_mips, dwarf_f13_mips,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(f14, nullptr, dwarf_f14_mips, dwarf_f14_mips,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(f15, nullptr, dwarf_f15_mips, dwarf_f15_mips,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(f16, nullptr, dwarf_f16_mips, dwarf_f16_mips,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(f17, nullptr, dwarf_f17_mips, dwarf_f17_mips,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(f18, nullptr, dwarf_f18_mips, dwarf_f18_mips,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(f19, nullptr, dwarf_f19_mips, dwarf_f19_mips,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(f20, nullptr, dwarf_f20_mips, dwarf_f20_mips,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(f21, nullptr, dwarf_f21_mips, dwarf_f21_mips,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(f22, nullptr, dwarf_f22_mips, dwarf_f22_mips,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(f23, nullptr, dwarf_f23_mips, dwarf_f23_mips,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(f24, nullptr, dwarf_f24_mips, dwarf_f24_mips,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(f25, nullptr, dwarf_f25_mips, dwarf_f25_mips,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(f26, nullptr, dwarf_f26_mips, dwarf_f26_mips,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(f27, nullptr, dwarf_f27_mips, dwarf_f27_mips,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(f28, nullptr, dwarf_f28_mips, dwarf_f28_mips,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(f29, nullptr, dwarf_f29_mips, dwarf_f29_mips,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(f30, nullptr, dwarf_f30_mips, dwarf_f30_mips,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(f31, nullptr, dwarf_f31_mips, dwarf_f31_mips,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR_INFO(fcsr, nullptr, dwarf_fcsr_mips, dwarf_fcsr_mips,
                    LLDB_INVALID_REGNUM),
    DEFINE_FPR_INFO(fir, nullptr, dwarf_fir_mips, dwarf_fir_mips,
                    LLDB_INVALID_REGNUM),
    DEFINE_FPR_INFO(config5, nullptr, dwarf_config5_mips, dwarf_config5_mips,
                    LLDB_INVALID_REGNUM),
    DEFINE_MSA(w0, nullptr, dwarf_w0_mips, dwarf_w0_mips, LLDB_INVALID_REGNUM,
               LLDB_INVALID_REGNUM),
    DEFINE_MSA(w1, nullptr, dwarf_w1_mips, dwarf_w1_mips, LLDB_INVALID_REGNUM,
               LLDB_INVALID_REGNUM),
    DEFINE_MSA(w2, nullptr, dwarf_w2_mips, dwarf_w2_mips, LLDB_INVALID_REGNUM,
               LLDB_INVALID_REGNUM),
    DEFINE_MSA(w3, nullptr, dwarf_w3_mips, dwarf_w3_mips, LLDB_INVALID_REGNUM,
               LLDB_INVALID_REGNUM),
    DEFINE_MSA(w4, nullptr, dwarf_w4_mips, dwarf_w4_mips, LLDB_INVALID_REGNUM,
               LLDB_INVALID_REGNUM),
    DEFINE_MSA(w5, nullptr, dwarf_w5_mips, dwarf_w5_mips, LLDB_INVALID_REGNUM,
               LLDB_INVALID_REGNUM),
    DEFINE_MSA(w6, nullptr, dwarf_w6_mips, dwarf_w6_mips, LLDB_INVALID_REGNUM,
               LLDB_INVALID_REGNUM),
    DEFINE_MSA(w7, nullptr, dwarf_w7_mips, dwarf_w7_mips, LLDB_INVALID_REGNUM,
               LLDB_INVALID_REGNUM),
    DEFINE_MSA(w8, nullptr, dwarf_w8_mips, dwarf_w8_mips, LLDB_INVALID_REGNUM,
               LLDB_INVALID_REGNUM),
    DEFINE_MSA(w9, nullptr, dwarf_w9_mips, dwarf_w9_mips, LLDB_INVALID_REGNUM,
               LLDB_INVALID_REGNUM),
    DEFINE_MSA(w10, nullptr, dwarf_w10_mips, dwarf_w10_mips,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_MSA(w11, nullptr, dwarf_w11_mips, dwarf_w11_mips,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_MSA(w12, nullptr, dwarf_w12_mips, dwarf_w12_mips,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_MSA(w13, nullptr, dwarf_w13_mips, dwarf_w13_mips,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_MSA(w14, nullptr, dwarf_w14_mips, dwarf_w14_mips,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_MSA(w15, nullptr, dwarf_w15_mips, dwarf_w15_mips,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_MSA(w16, nullptr, dwarf_w16_mips, dwarf_w16_mips,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_MSA(w17, nullptr, dwarf_w17_mips, dwarf_w17_mips,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_MSA(w18, nullptr, dwarf_w18_mips, dwarf_w18_mips,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_MSA(w19, nullptr, dwarf_w19_mips, dwarf_w19_mips,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_MSA(w20, nullptr, dwarf_w10_mips, dwarf_w20_mips,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_MSA(w21, nullptr, dwarf_w21_mips, dwarf_w21_mips,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_MSA(w22, nullptr, dwarf_w22_mips, dwarf_w22_mips,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_MSA(w23, nullptr, dwarf_w23_mips, dwarf_w23_mips,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_MSA(w24, nullptr, dwarf_w24_mips, dwarf_w24_mips,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_MSA(w25, nullptr, dwarf_w25_mips, dwarf_w25_mips,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_MSA(w26, nullptr, dwarf_w26_mips, dwarf_w26_mips,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_MSA(w27, nullptr, dwarf_w27_mips, dwarf_w27_mips,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_MSA(w28, nullptr, dwarf_w28_mips, dwarf_w28_mips,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_MSA(w29, nullptr, dwarf_w29_mips, dwarf_w29_mips,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_MSA(w30, nullptr, dwarf_w30_mips, dwarf_w30_mips,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_MSA(w31, nullptr, dwarf_w31_mips, dwarf_w31_mips,
               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_MSA_INFO(mcsr, nullptr, dwarf_mcsr_mips, dwarf_mcsr_mips,
                    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_MSA_INFO(mir, nullptr, dwarf_mir_mips, dwarf_mir_mips,
                    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_MSA_INFO(fcsr, nullptr, dwarf_fcsr_mips, dwarf_fcsr_mips,
                    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_MSA_INFO(fir, nullptr, dwarf_fir_mips, dwarf_fir_mips,
                    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_MSA_INFO(config5, nullptr, dwarf_config5_mips, dwarf_config5_mips,
                    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM)};

static_assert((sizeof(g_register_infos_mips) /
               sizeof(g_register_infos_mips[0])) == k_num_registers_mips,
              "g_register_infos_mips has wrong number of register infos");

#undef GPR_OFFSET
#undef FPR_OFFSET
#undef MSA_OFFSET
#undef DEFINE_GPR
#undef DEFINE_FPR
#undef DEFINE_FPR_INFO
#undef DEFINE_MSA
#undef DEFINE_MSA_INFO

#endif // DECLARE_REGISTER_INFOS_MIPS_STRUCT
