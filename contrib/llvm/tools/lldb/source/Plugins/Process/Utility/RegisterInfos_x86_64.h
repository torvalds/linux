//===-- RegisterInfos_x86_64.h ----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// This file is meant to be textually included. Do not #include modular
// headers here.

// Computes the offset of the given GPR in the user data area.
#define GPR_OFFSET(regname) (LLVM_EXTENSION offsetof(GPR, regname))

// Computes the offset of the given FPR in the extended data area.
#define FPR_OFFSET(regname)                                                    \
  (LLVM_EXTENSION offsetof(UserArea, fpr) +                                    \
   LLVM_EXTENSION offsetof(FPR, fxsave) +                                      \
   LLVM_EXTENSION offsetof(FXSAVE, regname))

// Computes the offset of the YMM register assembled from register halves.
// Based on DNBArchImplX86_64.cpp from debugserver
#define YMM_OFFSET(reg_index)                                                  \
  (LLVM_EXTENSION offsetof(UserArea, fpr) +                                    \
   LLVM_EXTENSION offsetof(FPR, xsave) +                                       \
   LLVM_EXTENSION offsetof(XSAVE, ymmh[0]) + (32 * reg_index))

#define BNDR_OFFSET(reg_index)                                                 \
  (LLVM_EXTENSION offsetof(UserArea, fpr) +                                    \
   LLVM_EXTENSION offsetof(FPR, xsave) +                                       \
   LLVM_EXTENSION offsetof(XSAVE, mpxr[reg_index]))

#define BNDC_OFFSET(reg_index)                                                 \
  (LLVM_EXTENSION offsetof(UserArea, fpr) +                                    \
   LLVM_EXTENSION offsetof(FPR, xsave) +                                       \
   LLVM_EXTENSION offsetof(XSAVE, mpxc[reg_index]))

#ifdef DECLARE_REGISTER_INFOS_X86_64_STRUCT

// Number of bytes needed to represent a FPR.
#define FPR_SIZE(reg) sizeof(((FXSAVE *)nullptr)->reg)

// Number of bytes needed to represent the i'th FP register.
#define FP_SIZE sizeof(((MMSReg *)nullptr)->bytes)

// Number of bytes needed to represent an XMM register.
#define XMM_SIZE sizeof(XMMReg)

// Number of bytes needed to represent a YMM register.
#define YMM_SIZE sizeof(YMMReg)

// Number of bytes needed to represent MPX registers.
#define BNDR_SIZE sizeof(MPXReg)
#define BNDC_SIZE sizeof(MPXCsr)

#define DR_SIZE sizeof(((DBG *)nullptr)->dr[0])

// RegisterKind: EHFrame, DWARF, Generic, Process Plugin, LLDB

// Note that the size and offset will be updated by platform-specific classes.
#define DEFINE_GPR(reg, alt, kind1, kind2, kind3, kind4)                       \
  {                                                                            \
    #reg, alt, sizeof(((GPR *)nullptr)->reg),                                  \
                      GPR_OFFSET(reg), eEncodingUint, eFormatHex,              \
                                 {kind1, kind2, kind3, kind4,                  \
                                  lldb_##reg##_x86_64 },                       \
                                  nullptr, nullptr, nullptr, 0                 \
  }

#define DEFINE_FPR(name, reg, kind1, kind2, kind3, kind4)                      \
  {                                                                            \
    #name, nullptr, FPR_SIZE(reg), FPR_OFFSET(reg), eEncodingUint, eFormatHex, \
                                           {kind1, kind2, kind3, kind4,        \
                                            lldb_##name##_x86_64 },            \
                                            nullptr, nullptr, nullptr, 0       \
  }

#define DEFINE_FP_ST(reg, i)                                                   \
  {                                                                            \
    #reg #i, nullptr, FP_SIZE,                                                 \
        LLVM_EXTENSION FPR_OFFSET(                                             \
            stmm[i]), eEncodingVector, eFormatVectorOfUInt8,                   \
            {dwarf_st##i##_x86_64, dwarf_st##i##_x86_64, LLDB_INVALID_REGNUM,  \
             LLDB_INVALID_REGNUM, lldb_st##i##_x86_64 },                       \
             nullptr, nullptr, nullptr, 0                                      \
  }

#define DEFINE_FP_MM(reg, i)                                                   \
  {                                                                            \
    #reg #i, nullptr, sizeof(uint64_t),                                        \
                          LLVM_EXTENSION FPR_OFFSET(                           \
                              stmm[i]), eEncodingUint, eFormatHex,             \
                              {dwarf_mm##i##_x86_64, dwarf_mm##i##_x86_64,     \
                               LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,       \
                               lldb_mm##i##_x86_64 },                          \
                               nullptr, nullptr, nullptr, 0                    \
  }

#define DEFINE_XMM(reg, i)                                                     \
  {                                                                            \
    #reg #i, nullptr, XMM_SIZE,                                                \
        LLVM_EXTENSION FPR_OFFSET(                                             \
            reg[i]), eEncodingVector, eFormatVectorOfUInt8,                    \
            {dwarf_##reg##i##_x86_64, dwarf_##reg##i##_x86_64,                 \
             LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,                         \
             lldb_##reg##i##_x86_64 },                                         \
             nullptr, nullptr, nullptr, 0                                      \
  }

#define DEFINE_YMM(reg, i)                                                     \
  {                                                                            \
    #reg #i, nullptr, YMM_SIZE,                                                \
        LLVM_EXTENSION YMM_OFFSET(i), eEncodingVector, eFormatVectorOfUInt8,   \
                                  {dwarf_##reg##i##h_x86_64,                   \
                                   dwarf_##reg##i##h_x86_64,                   \
                                   LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,   \
                                   lldb_##reg##i##_x86_64 },                   \
                                   nullptr, nullptr, nullptr, 0                \
  }

#define DEFINE_BNDR(reg, i)                                                    \
  {                                                                            \
    #reg #i, nullptr, BNDR_SIZE,                                               \
        LLVM_EXTENSION BNDR_OFFSET(i), eEncodingVector, eFormatVectorOfUInt64, \
                                   {dwarf_##reg##i##_x86_64,                   \
                                    dwarf_##reg##i##_x86_64,                   \
                                    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,  \
                                    lldb_##reg##i##_x86_64 },                  \
                                    nullptr, nullptr, nullptr, 0               \
  }

#define DEFINE_BNDC(name, i)                                                   \
  {                                                                            \
    #name, nullptr, BNDC_SIZE,                                                 \
        LLVM_EXTENSION BNDC_OFFSET(i), eEncodingVector, eFormatVectorOfUInt8,  \
        {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,        \
         LLDB_INVALID_REGNUM, lldb_##name##_x86_64 },                          \
         nullptr, nullptr, nullptr, 0                                          \
  }

#define DEFINE_DR(reg, i)                                                      \
  {                                                                            \
    #reg #i, nullptr, DR_SIZE,                                                 \
        DR_OFFSET(i), eEncodingUint, eFormatHex,                               \
                  {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,                   \
                   LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,                   \
                   lldb_##reg##i##_x86_64 },                                   \
                   nullptr, nullptr, nullptr, 0                                \
  }

#define DEFINE_GPR_PSEUDO_32(reg32, reg64)                                     \
  {                                                                            \
    #reg32, nullptr, 4,                                                        \
        GPR_OFFSET(reg64), eEncodingUint, eFormatHex,                          \
                   {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,                  \
                    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,                  \
                    lldb_##reg32##_x86_64 },                                   \
                    RegisterContextPOSIX_x86::g_contained_##reg64,             \
                    RegisterContextPOSIX_x86::g_invalidate_##reg64, nullptr, 0 \
  }

#define DEFINE_GPR_PSEUDO_16(reg16, reg64)                                     \
  {                                                                            \
    #reg16, nullptr, 2,                                                        \
        GPR_OFFSET(reg64), eEncodingUint, eFormatHex,                          \
                   {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,                  \
                    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,                  \
                    lldb_##reg16##_x86_64 },                                   \
                    RegisterContextPOSIX_x86::g_contained_##reg64,             \
                    RegisterContextPOSIX_x86::g_invalidate_##reg64, nullptr, 0 \
  }

#define DEFINE_GPR_PSEUDO_8H(reg8, reg64)                                      \
  {                                                                            \
    #reg8, nullptr, 1,                                                         \
        GPR_OFFSET(reg64) + 1, eEncodingUint, eFormatHex,                      \
                   {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,                  \
                    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,                  \
                    lldb_##reg8##_x86_64 },                                    \
                    RegisterContextPOSIX_x86::g_contained_##reg64,             \
                    RegisterContextPOSIX_x86::g_invalidate_##reg64, nullptr, 0 \
  }

#define DEFINE_GPR_PSEUDO_8L(reg8, reg64)                                      \
  {                                                                            \
    #reg8, nullptr, 1,                                                         \
        GPR_OFFSET(reg64), eEncodingUint, eFormatHex,                          \
                   {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,                  \
                    LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,                  \
                    lldb_##reg8##_x86_64 },                                    \
                    RegisterContextPOSIX_x86::g_contained_##reg64,             \
                    RegisterContextPOSIX_x86::g_invalidate_##reg64, nullptr, 0 \
  }

// clang-format off
static RegisterInfo g_register_infos_x86_64[] = {
// General purpose registers     EH_Frame              DWARF                 Generic                     Process Plugin
//  ===========================  ==================    ================      =========================   ====================
    DEFINE_GPR(rax,    nullptr,  dwarf_rax_x86_64,     dwarf_rax_x86_64,     LLDB_INVALID_REGNUM,        LLDB_INVALID_REGNUM),
    DEFINE_GPR(rbx,    nullptr,  dwarf_rbx_x86_64,     dwarf_rbx_x86_64,     LLDB_INVALID_REGNUM,        LLDB_INVALID_REGNUM),
    DEFINE_GPR(rcx,    "arg4",   dwarf_rcx_x86_64,     dwarf_rcx_x86_64,     LLDB_REGNUM_GENERIC_ARG4,   LLDB_INVALID_REGNUM),
    DEFINE_GPR(rdx,    "arg3",   dwarf_rdx_x86_64,     dwarf_rdx_x86_64,     LLDB_REGNUM_GENERIC_ARG3,   LLDB_INVALID_REGNUM),
    DEFINE_GPR(rdi,    "arg1",   dwarf_rdi_x86_64,     dwarf_rdi_x86_64,     LLDB_REGNUM_GENERIC_ARG1,   LLDB_INVALID_REGNUM),
    DEFINE_GPR(rsi,    "arg2",   dwarf_rsi_x86_64,     dwarf_rsi_x86_64,     LLDB_REGNUM_GENERIC_ARG2,   LLDB_INVALID_REGNUM),
    DEFINE_GPR(rbp,    "fp",     dwarf_rbp_x86_64,     dwarf_rbp_x86_64,     LLDB_REGNUM_GENERIC_FP,     LLDB_INVALID_REGNUM),
    DEFINE_GPR(rsp,    "sp",     dwarf_rsp_x86_64,     dwarf_rsp_x86_64,     LLDB_REGNUM_GENERIC_SP,     LLDB_INVALID_REGNUM),
    DEFINE_GPR(r8,     "arg5",   dwarf_r8_x86_64,      dwarf_r8_x86_64,      LLDB_REGNUM_GENERIC_ARG5,   LLDB_INVALID_REGNUM),
    DEFINE_GPR(r9,     "arg6",   dwarf_r9_x86_64,      dwarf_r9_x86_64,      LLDB_REGNUM_GENERIC_ARG6,   LLDB_INVALID_REGNUM),
    DEFINE_GPR(r10,    nullptr,  dwarf_r10_x86_64,     dwarf_r10_x86_64,     LLDB_INVALID_REGNUM,        LLDB_INVALID_REGNUM),
    DEFINE_GPR(r11,    nullptr,  dwarf_r11_x86_64,     dwarf_r11_x86_64,     LLDB_INVALID_REGNUM,        LLDB_INVALID_REGNUM),
    DEFINE_GPR(r12,    nullptr,  dwarf_r12_x86_64,     dwarf_r12_x86_64,     LLDB_INVALID_REGNUM,        LLDB_INVALID_REGNUM),
    DEFINE_GPR(r13,    nullptr,  dwarf_r13_x86_64,     dwarf_r13_x86_64,     LLDB_INVALID_REGNUM,        LLDB_INVALID_REGNUM),
    DEFINE_GPR(r14,    nullptr,  dwarf_r14_x86_64,     dwarf_r14_x86_64,     LLDB_INVALID_REGNUM,        LLDB_INVALID_REGNUM),
    DEFINE_GPR(r15,    nullptr,  dwarf_r15_x86_64,     dwarf_r15_x86_64,     LLDB_INVALID_REGNUM,        LLDB_INVALID_REGNUM),
    DEFINE_GPR(rip,    "pc",     dwarf_rip_x86_64,     dwarf_rip_x86_64,     LLDB_REGNUM_GENERIC_PC,     LLDB_INVALID_REGNUM),
    DEFINE_GPR(rflags, "flags",  dwarf_rflags_x86_64,  dwarf_rflags_x86_64,  LLDB_REGNUM_GENERIC_FLAGS,  LLDB_INVALID_REGNUM),
    DEFINE_GPR(cs,     nullptr,  dwarf_cs_x86_64,      dwarf_cs_x86_64,      LLDB_INVALID_REGNUM,        LLDB_INVALID_REGNUM),
    DEFINE_GPR(fs,     nullptr,  dwarf_fs_x86_64,      dwarf_fs_x86_64,      LLDB_INVALID_REGNUM,        LLDB_INVALID_REGNUM),
    DEFINE_GPR(gs,     nullptr,  dwarf_gs_x86_64,      dwarf_gs_x86_64,      LLDB_INVALID_REGNUM,        LLDB_INVALID_REGNUM),
    DEFINE_GPR(ss,     nullptr,  dwarf_ss_x86_64,      dwarf_ss_x86_64,      LLDB_INVALID_REGNUM,        LLDB_INVALID_REGNUM),
    DEFINE_GPR(ds,     nullptr,  dwarf_ds_x86_64,      dwarf_ds_x86_64,      LLDB_INVALID_REGNUM,        LLDB_INVALID_REGNUM),
    DEFINE_GPR(es,     nullptr,  dwarf_es_x86_64,      dwarf_es_x86_64,      LLDB_INVALID_REGNUM,        LLDB_INVALID_REGNUM),

    DEFINE_GPR_PSEUDO_32(eax, rax), DEFINE_GPR_PSEUDO_32(ebx, rbx),
    DEFINE_GPR_PSEUDO_32(ecx, rcx), DEFINE_GPR_PSEUDO_32(edx, rdx),
    DEFINE_GPR_PSEUDO_32(edi, rdi), DEFINE_GPR_PSEUDO_32(esi, rsi),
    DEFINE_GPR_PSEUDO_32(ebp, rbp), DEFINE_GPR_PSEUDO_32(esp, rsp),
    DEFINE_GPR_PSEUDO_32(r8d, r8), DEFINE_GPR_PSEUDO_32(r9d, r9),
    DEFINE_GPR_PSEUDO_32(r10d, r10), DEFINE_GPR_PSEUDO_32(r11d, r11),
    DEFINE_GPR_PSEUDO_32(r12d, r12), DEFINE_GPR_PSEUDO_32(r13d, r13),
    DEFINE_GPR_PSEUDO_32(r14d, r14), DEFINE_GPR_PSEUDO_32(r15d, r15),
    DEFINE_GPR_PSEUDO_16(ax, rax), DEFINE_GPR_PSEUDO_16(bx, rbx),
    DEFINE_GPR_PSEUDO_16(cx, rcx), DEFINE_GPR_PSEUDO_16(dx, rdx),
    DEFINE_GPR_PSEUDO_16(di, rdi), DEFINE_GPR_PSEUDO_16(si, rsi),
    DEFINE_GPR_PSEUDO_16(bp, rbp), DEFINE_GPR_PSEUDO_16(sp, rsp),
    DEFINE_GPR_PSEUDO_16(r8w, r8), DEFINE_GPR_PSEUDO_16(r9w, r9),
    DEFINE_GPR_PSEUDO_16(r10w, r10), DEFINE_GPR_PSEUDO_16(r11w, r11),
    DEFINE_GPR_PSEUDO_16(r12w, r12), DEFINE_GPR_PSEUDO_16(r13w, r13),
    DEFINE_GPR_PSEUDO_16(r14w, r14), DEFINE_GPR_PSEUDO_16(r15w, r15),
    DEFINE_GPR_PSEUDO_8H(ah, rax), DEFINE_GPR_PSEUDO_8H(bh, rbx),
    DEFINE_GPR_PSEUDO_8H(ch, rcx), DEFINE_GPR_PSEUDO_8H(dh, rdx),
    DEFINE_GPR_PSEUDO_8L(al, rax), DEFINE_GPR_PSEUDO_8L(bl, rbx),
    DEFINE_GPR_PSEUDO_8L(cl, rcx), DEFINE_GPR_PSEUDO_8L(dl, rdx),
    DEFINE_GPR_PSEUDO_8L(dil, rdi), DEFINE_GPR_PSEUDO_8L(sil, rsi),
    DEFINE_GPR_PSEUDO_8L(bpl, rbp), DEFINE_GPR_PSEUDO_8L(spl, rsp),
    DEFINE_GPR_PSEUDO_8L(r8l, r8), DEFINE_GPR_PSEUDO_8L(r9l, r9),
    DEFINE_GPR_PSEUDO_8L(r10l, r10), DEFINE_GPR_PSEUDO_8L(r11l, r11),
    DEFINE_GPR_PSEUDO_8L(r12l, r12), DEFINE_GPR_PSEUDO_8L(r13l, r13),
    DEFINE_GPR_PSEUDO_8L(r14l, r14), DEFINE_GPR_PSEUDO_8L(r15l, r15),

//  i387 Floating point registers.         EH_frame             DWARF                Generic              Process Plugin
//  ====================================== ===============      ==================   ===================  ====================
    DEFINE_FPR(fctrl,     fctrl,           dwarf_fctrl_x86_64,  dwarf_fctrl_x86_64,  LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_FPR(fstat,     fstat,           dwarf_fstat_x86_64,  dwarf_fstat_x86_64,  LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_FPR(ftag,      ftag,            LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_FPR(fop,       fop,             LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_FPR(fiseg,     ptr.i386_.fiseg, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_FPR(fioff,     ptr.i386_.fioff, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_FPR(foseg,     ptr.i386_.foseg, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_FPR(fooff,     ptr.i386_.fooff, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_FPR(mxcsr,     mxcsr,           dwarf_mxcsr_x86_64,  dwarf_mxcsr_x86_64,  LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),
    DEFINE_FPR(mxcsrmask, mxcsrmask,       LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM),

    // FP registers.
    DEFINE_FP_ST(st, 0), DEFINE_FP_ST(st, 1), DEFINE_FP_ST(st, 2),
    DEFINE_FP_ST(st, 3), DEFINE_FP_ST(st, 4), DEFINE_FP_ST(st, 5),
    DEFINE_FP_ST(st, 6), DEFINE_FP_ST(st, 7), DEFINE_FP_MM(mm, 0),
    DEFINE_FP_MM(mm, 1), DEFINE_FP_MM(mm, 2), DEFINE_FP_MM(mm, 3),
    DEFINE_FP_MM(mm, 4), DEFINE_FP_MM(mm, 5), DEFINE_FP_MM(mm, 6),
    DEFINE_FP_MM(mm, 7),

    // XMM registers
    DEFINE_XMM(xmm, 0), DEFINE_XMM(xmm, 1), DEFINE_XMM(xmm, 2),
    DEFINE_XMM(xmm, 3), DEFINE_XMM(xmm, 4), DEFINE_XMM(xmm, 5),
    DEFINE_XMM(xmm, 6), DEFINE_XMM(xmm, 7), DEFINE_XMM(xmm, 8),
    DEFINE_XMM(xmm, 9), DEFINE_XMM(xmm, 10), DEFINE_XMM(xmm, 11),
    DEFINE_XMM(xmm, 12), DEFINE_XMM(xmm, 13), DEFINE_XMM(xmm, 14),
    DEFINE_XMM(xmm, 15),

    // Copy of YMM registers assembled from xmm and ymmh
    DEFINE_YMM(ymm, 0), DEFINE_YMM(ymm, 1), DEFINE_YMM(ymm, 2),
    DEFINE_YMM(ymm, 3), DEFINE_YMM(ymm, 4), DEFINE_YMM(ymm, 5),
    DEFINE_YMM(ymm, 6), DEFINE_YMM(ymm, 7), DEFINE_YMM(ymm, 8),
    DEFINE_YMM(ymm, 9), DEFINE_YMM(ymm, 10), DEFINE_YMM(ymm, 11),
    DEFINE_YMM(ymm, 12), DEFINE_YMM(ymm, 13), DEFINE_YMM(ymm, 14),
    DEFINE_YMM(ymm, 15),

    // MPX registers
    DEFINE_BNDR(bnd, 0),
    DEFINE_BNDR(bnd, 1),
    DEFINE_BNDR(bnd, 2),
    DEFINE_BNDR(bnd, 3),

    DEFINE_BNDC(bndcfgu, 0),
    DEFINE_BNDC(bndstatus, 1),

    // Debug registers for lldb internal use
    DEFINE_DR(dr, 0), DEFINE_DR(dr, 1), DEFINE_DR(dr, 2), DEFINE_DR(dr, 3),
    DEFINE_DR(dr, 4), DEFINE_DR(dr, 5), DEFINE_DR(dr, 6), DEFINE_DR(dr, 7)};

// clang-format on

static_assert((sizeof(g_register_infos_x86_64) /
               sizeof(g_register_infos_x86_64[0])) == k_num_registers_x86_64,
              "g_register_infos_x86_64 has wrong number of register infos");

#undef FPR_SIZE
#undef FP_SIZE
#undef XMM_SIZE
#undef YMM_SIZE
#undef DEFINE_GPR
#undef DEFINE_FPR
#undef DEFINE_FP
#undef DEFINE_XMM
#undef DEFINE_YMM
#undef DEFINE_BNDR
#undef DEFINE_BNDC
#undef DEFINE_DR
#undef DEFINE_GPR_PSEUDO_32
#undef DEFINE_GPR_PSEUDO_16
#undef DEFINE_GPR_PSEUDO_8H
#undef DEFINE_GPR_PSEUDO_8L

#endif // DECLARE_REGISTER_INFOS_X86_64_STRUCT

#ifdef UPDATE_REGISTER_INFOS_I386_STRUCT_WITH_X86_64_OFFSETS

#define UPDATE_GPR_INFO(reg, reg64)                                            \
  do {                                                                         \
    g_register_infos[lldb_##reg##_i386].byte_offset = GPR_OFFSET(reg64);       \
  } while (false);

#define UPDATE_GPR_INFO_8H(reg, reg64)                                         \
  do {                                                                         \
    g_register_infos[lldb_##reg##_i386].byte_offset = GPR_OFFSET(reg64) + 1;   \
  } while (false);

#define UPDATE_FPR_INFO(reg, reg64)                                            \
  do {                                                                         \
    g_register_infos[lldb_##reg##_i386].byte_offset = FPR_OFFSET(reg64);       \
  } while (false);

#define UPDATE_FP_INFO(reg, i)                                                 \
  do {                                                                         \
    g_register_infos[lldb_##reg##i##_i386].byte_offset = FPR_OFFSET(stmm[i]);  \
  } while (false);

#define UPDATE_XMM_INFO(reg, i)                                                \
  do {                                                                         \
    g_register_infos[lldb_##reg##i##_i386].byte_offset = FPR_OFFSET(reg[i]);   \
  } while (false);

#define UPDATE_YMM_INFO(reg, i)                                                \
  do {                                                                         \
    g_register_infos[lldb_##reg##i##_i386].byte_offset = YMM_OFFSET(i);        \
  } while (false);

#define UPDATE_DR_INFO(reg_index)                                              \
  do {                                                                         \
    g_register_infos[lldb_dr##reg_index##_i386].byte_offset =                  \
        DR_OFFSET(reg_index);                                                  \
  } while (false);

// Update the register offsets
UPDATE_GPR_INFO(eax, rax);
UPDATE_GPR_INFO(ebx, rbx);
UPDATE_GPR_INFO(ecx, rcx);
UPDATE_GPR_INFO(edx, rdx);
UPDATE_GPR_INFO(edi, rdi);
UPDATE_GPR_INFO(esi, rsi);
UPDATE_GPR_INFO(ebp, rbp);
UPDATE_GPR_INFO(esp, rsp);
UPDATE_GPR_INFO(eip, rip);
UPDATE_GPR_INFO(eflags, rflags);
UPDATE_GPR_INFO(cs, cs);
UPDATE_GPR_INFO(fs, fs);
UPDATE_GPR_INFO(gs, gs);
UPDATE_GPR_INFO(ss, ss);
UPDATE_GPR_INFO(ds, ds);
UPDATE_GPR_INFO(es, es);

UPDATE_GPR_INFO(ax, rax);
UPDATE_GPR_INFO(bx, rbx);
UPDATE_GPR_INFO(cx, rcx);
UPDATE_GPR_INFO(dx, rdx);
UPDATE_GPR_INFO(di, rdi);
UPDATE_GPR_INFO(si, rsi);
UPDATE_GPR_INFO(bp, rbp);
UPDATE_GPR_INFO(sp, rsp);
UPDATE_GPR_INFO_8H(ah, rax);
UPDATE_GPR_INFO_8H(bh, rbx);
UPDATE_GPR_INFO_8H(ch, rcx);
UPDATE_GPR_INFO_8H(dh, rdx);
UPDATE_GPR_INFO(al, rax);
UPDATE_GPR_INFO(bl, rbx);
UPDATE_GPR_INFO(cl, rcx);
UPDATE_GPR_INFO(dl, rdx);

UPDATE_FPR_INFO(fctrl, fctrl);
UPDATE_FPR_INFO(fstat, fstat);
UPDATE_FPR_INFO(ftag, ftag);
UPDATE_FPR_INFO(fop, fop);
UPDATE_FPR_INFO(fiseg, ptr.i386_.fiseg);
UPDATE_FPR_INFO(fioff, ptr.i386_.fioff);
UPDATE_FPR_INFO(fooff, ptr.i386_.fooff);
UPDATE_FPR_INFO(foseg, ptr.i386_.foseg);
UPDATE_FPR_INFO(mxcsr, mxcsr);
UPDATE_FPR_INFO(mxcsrmask, mxcsrmask);

UPDATE_FP_INFO(st, 0);
UPDATE_FP_INFO(st, 1);
UPDATE_FP_INFO(st, 2);
UPDATE_FP_INFO(st, 3);
UPDATE_FP_INFO(st, 4);
UPDATE_FP_INFO(st, 5);
UPDATE_FP_INFO(st, 6);
UPDATE_FP_INFO(st, 7);
UPDATE_FP_INFO(mm, 0);
UPDATE_FP_INFO(mm, 1);
UPDATE_FP_INFO(mm, 2);
UPDATE_FP_INFO(mm, 3);
UPDATE_FP_INFO(mm, 4);
UPDATE_FP_INFO(mm, 5);
UPDATE_FP_INFO(mm, 6);
UPDATE_FP_INFO(mm, 7);

UPDATE_XMM_INFO(xmm, 0);
UPDATE_XMM_INFO(xmm, 1);
UPDATE_XMM_INFO(xmm, 2);
UPDATE_XMM_INFO(xmm, 3);
UPDATE_XMM_INFO(xmm, 4);
UPDATE_XMM_INFO(xmm, 5);
UPDATE_XMM_INFO(xmm, 6);
UPDATE_XMM_INFO(xmm, 7);

UPDATE_YMM_INFO(ymm, 0);
UPDATE_YMM_INFO(ymm, 1);
UPDATE_YMM_INFO(ymm, 2);
UPDATE_YMM_INFO(ymm, 3);
UPDATE_YMM_INFO(ymm, 4);
UPDATE_YMM_INFO(ymm, 5);
UPDATE_YMM_INFO(ymm, 6);
UPDATE_YMM_INFO(ymm, 7);

UPDATE_DR_INFO(0);
UPDATE_DR_INFO(1);
UPDATE_DR_INFO(2);
UPDATE_DR_INFO(3);
UPDATE_DR_INFO(4);
UPDATE_DR_INFO(5);
UPDATE_DR_INFO(6);
UPDATE_DR_INFO(7);

#undef UPDATE_GPR_INFO
#undef UPDATE_GPR_INFO_8H
#undef UPDATE_FPR_INFO
#undef UPDATE_FP_INFO
#undef UPDATE_XMM_INFO
#undef UPDATE_YMM_INFO
#undef UPDATE_DR_INFO

#endif // UPDATE_REGISTER_INFOS_I386_STRUCT_WITH_X86_64_OFFSETS

#undef GPR_OFFSET
#undef FPR_OFFSET
#undef YMM_OFFSET
