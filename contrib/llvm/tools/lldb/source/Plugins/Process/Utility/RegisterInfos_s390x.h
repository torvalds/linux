//===-- RegisterInfos_s390x.h -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <stddef.h>

#include "llvm/Support/Compiler.h"


#ifdef DECLARE_REGISTER_INFOS_S390X_STRUCT

// Computes the offset of the given GPR in the user data area.
#define GPR_OFFSET(num) (16 + 8 * num)
// Computes the offset of the given ACR in the user data area.
#define ACR_OFFSET(num) (16 + 8 * 16 + 4 * num)
// Computes the offset of the given FPR in the extended data area.
#define FPR_OFFSET(num) (8 + 8 * num)

// RegisterKind: EHFrame, DWARF, Generic, Process Plugin, LLDB

#define DEFINE_GPR(name, size, offset, alt, generic)                           \
  {                                                                            \
    #name, alt, size, offset, eEncodingUint, eFormatHex,                       \
        {dwarf_##name##_s390x, dwarf_##name##_s390x, generic,                  \
         LLDB_INVALID_REGNUM, lldb_##name##_s390x },                           \
         NULL, NULL, NULL, 0                                                   \
  }

#define DEFINE_GPR_NODWARF(name, size, offset, alt, generic)                   \
  {                                                                            \
    #name, alt, size, offset, eEncodingUint, eFormatHex,                       \
        {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, generic,                    \
         LLDB_INVALID_REGNUM, lldb_##name##_s390x },                           \
         NULL, NULL, NULL, 0                                                   \
  }

#define DEFINE_FPR(name, size, offset)                                         \
  {                                                                            \
    #name, NULL, size, offset, eEncodingUint, eFormatHex,                      \
        {dwarf_##name##_s390x, dwarf_##name##_s390x, LLDB_INVALID_REGNUM,      \
         LLDB_INVALID_REGNUM, lldb_##name##_s390x },                           \
         NULL, NULL, NULL, 0                                                   \
  }

#define DEFINE_FPR_NODWARF(name, size, offset)                                 \
  {                                                                            \
    #name, NULL, size, offset, eEncodingUint, eFormatHex,                      \
        {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,        \
         LLDB_INVALID_REGNUM, lldb_##name##_s390x },                           \
         NULL, NULL, NULL, 0                                                   \
  }

static RegisterInfo g_register_infos_s390x[] = {
    // General purpose registers.
    DEFINE_GPR(r0, 8, GPR_OFFSET(0), nullptr, LLDB_INVALID_REGNUM),
    DEFINE_GPR(r1, 8, GPR_OFFSET(1), nullptr, LLDB_INVALID_REGNUM),
    DEFINE_GPR(r2, 8, GPR_OFFSET(2), "arg1", LLDB_REGNUM_GENERIC_ARG1),
    DEFINE_GPR(r3, 8, GPR_OFFSET(3), "arg2", LLDB_REGNUM_GENERIC_ARG2),
    DEFINE_GPR(r4, 8, GPR_OFFSET(4), "arg3", LLDB_REGNUM_GENERIC_ARG3),
    DEFINE_GPR(r5, 8, GPR_OFFSET(5), "arg4", LLDB_REGNUM_GENERIC_ARG4),
    DEFINE_GPR(r6, 8, GPR_OFFSET(6), "arg5", LLDB_REGNUM_GENERIC_ARG5),
    DEFINE_GPR(r7, 8, GPR_OFFSET(7), nullptr, LLDB_INVALID_REGNUM),
    DEFINE_GPR(r8, 8, GPR_OFFSET(8), nullptr, LLDB_INVALID_REGNUM),
    DEFINE_GPR(r9, 8, GPR_OFFSET(9), nullptr, LLDB_INVALID_REGNUM),
    DEFINE_GPR(r10, 8, GPR_OFFSET(10), nullptr, LLDB_INVALID_REGNUM),
    DEFINE_GPR(r11, 8, GPR_OFFSET(11), "fp", LLDB_REGNUM_GENERIC_FP),
    DEFINE_GPR(r12, 8, GPR_OFFSET(12), nullptr, LLDB_INVALID_REGNUM),
    DEFINE_GPR(r13, 8, GPR_OFFSET(13), nullptr, LLDB_INVALID_REGNUM),
    DEFINE_GPR(r14, 8, GPR_OFFSET(14), nullptr, LLDB_INVALID_REGNUM),
    DEFINE_GPR(r15, 8, GPR_OFFSET(15), "sp", LLDB_REGNUM_GENERIC_SP),
    DEFINE_GPR(acr0, 4, ACR_OFFSET(0), nullptr, LLDB_INVALID_REGNUM),
    DEFINE_GPR(acr1, 4, ACR_OFFSET(1), nullptr, LLDB_INVALID_REGNUM),
    DEFINE_GPR(acr2, 4, ACR_OFFSET(2), nullptr, LLDB_INVALID_REGNUM),
    DEFINE_GPR(acr3, 4, ACR_OFFSET(3), nullptr, LLDB_INVALID_REGNUM),
    DEFINE_GPR(acr4, 4, ACR_OFFSET(4), nullptr, LLDB_INVALID_REGNUM),
    DEFINE_GPR(acr5, 4, ACR_OFFSET(5), nullptr, LLDB_INVALID_REGNUM),
    DEFINE_GPR(acr6, 4, ACR_OFFSET(6), nullptr, LLDB_INVALID_REGNUM),
    DEFINE_GPR(acr7, 4, ACR_OFFSET(7), nullptr, LLDB_INVALID_REGNUM),
    DEFINE_GPR(acr8, 4, ACR_OFFSET(8), nullptr, LLDB_INVALID_REGNUM),
    DEFINE_GPR(acr9, 4, ACR_OFFSET(9), nullptr, LLDB_INVALID_REGNUM),
    DEFINE_GPR(acr10, 4, ACR_OFFSET(10), nullptr, LLDB_INVALID_REGNUM),
    DEFINE_GPR(acr11, 4, ACR_OFFSET(11), nullptr, LLDB_INVALID_REGNUM),
    DEFINE_GPR(acr12, 4, ACR_OFFSET(12), nullptr, LLDB_INVALID_REGNUM),
    DEFINE_GPR(acr13, 4, ACR_OFFSET(13), nullptr, LLDB_INVALID_REGNUM),
    DEFINE_GPR(acr14, 4, ACR_OFFSET(14), nullptr, LLDB_INVALID_REGNUM),
    DEFINE_GPR(acr15, 4, ACR_OFFSET(15), nullptr, LLDB_INVALID_REGNUM),
    DEFINE_GPR(pswm, 8, 0, "flags", LLDB_REGNUM_GENERIC_FLAGS),
    DEFINE_GPR(pswa, 8, 8, "pc", LLDB_REGNUM_GENERIC_PC),

    // Floating point registers.
    DEFINE_FPR(f0, 8, FPR_OFFSET(0)), DEFINE_FPR(f1, 8, FPR_OFFSET(1)),
    DEFINE_FPR(f2, 8, FPR_OFFSET(2)), DEFINE_FPR(f3, 8, FPR_OFFSET(3)),
    DEFINE_FPR(f4, 8, FPR_OFFSET(4)), DEFINE_FPR(f5, 8, FPR_OFFSET(5)),
    DEFINE_FPR(f6, 8, FPR_OFFSET(6)), DEFINE_FPR(f7, 8, FPR_OFFSET(7)),
    DEFINE_FPR(f8, 8, FPR_OFFSET(8)), DEFINE_FPR(f9, 8, FPR_OFFSET(9)),
    DEFINE_FPR(f10, 8, FPR_OFFSET(10)), DEFINE_FPR(f11, 8, FPR_OFFSET(11)),
    DEFINE_FPR(f12, 8, FPR_OFFSET(12)), DEFINE_FPR(f13, 8, FPR_OFFSET(13)),
    DEFINE_FPR(f14, 8, FPR_OFFSET(14)), DEFINE_FPR(f15, 8, FPR_OFFSET(15)),
    DEFINE_FPR_NODWARF(fpc, 4, 0),

    // Linux operating-specific info.
    DEFINE_GPR_NODWARF(orig_r2, 8, 16 + 16 * 8 + 16 * 4, nullptr,
                       LLDB_INVALID_REGNUM),
    DEFINE_GPR_NODWARF(last_break, 8, 0, nullptr, LLDB_INVALID_REGNUM),
    DEFINE_GPR_NODWARF(system_call, 4, 0, nullptr, LLDB_INVALID_REGNUM),
};

static_assert((sizeof(g_register_infos_s390x) /
               sizeof(g_register_infos_s390x[0])) == k_num_registers_s390x,
              "g_register_infos_s390x has wrong number of register infos");

#undef GPR_OFFSET
#undef ACR_OFFSET
#undef FPR_OFFSET
#undef DEFINE_GPR
#undef DEFINE_GPR_NODWARF
#undef DEFINE_FPR
#undef DEFINE_FPR_NODWARF

#endif // DECLARE_REGISTER_INFOS_S390X_STRUCT
