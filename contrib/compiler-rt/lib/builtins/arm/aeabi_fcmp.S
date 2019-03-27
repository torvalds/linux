//===-- aeabi_fcmp.S - EABI fcmp* implementation ---------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "../assembly.h"

// int __aeabi_fcmp{eq,lt,le,ge,gt}(float a, float b) {
//   int result = __{eq,lt,le,ge,gt}sf2(a, b);
//   if (result {==,<,<=,>=,>} 0) {
//     return 1;
//   } else {
//     return 0;
//   }
// }

#if defined(COMPILER_RT_ARMHF_TARGET)
#  define CONVERT_FCMP_ARGS_TO_SF2_ARGS                    \
        vmov      s0, r0                         SEPARATOR \
        vmov      s1, r1
#else
#  define CONVERT_FCMP_ARGS_TO_SF2_ARGS
#endif

#define DEFINE_AEABI_FCMP(cond)                            \
        .syntax unified                          SEPARATOR \
        .p2align 2                               SEPARATOR \
DEFINE_COMPILERRT_FUNCTION(__aeabi_fcmp ## cond)           \
        push      { r4, lr }                     SEPARATOR \
        CONVERT_FCMP_ARGS_TO_SF2_ARGS            SEPARATOR \
        bl        SYMBOL_NAME(__ ## cond ## sf2) SEPARATOR \
        cmp       r0, #0                         SEPARATOR \
        b ## cond 1f                             SEPARATOR \
        movs      r0, #0                         SEPARATOR \
        pop       { r4, pc }                     SEPARATOR \
1:                                               SEPARATOR \
        movs      r0, #1                         SEPARATOR \
        pop       { r4, pc }                     SEPARATOR \
END_COMPILERRT_FUNCTION(__aeabi_fcmp ## cond)

DEFINE_AEABI_FCMP(eq)
DEFINE_AEABI_FCMP(lt)
DEFINE_AEABI_FCMP(le)
DEFINE_AEABI_FCMP(ge)
DEFINE_AEABI_FCMP(gt)

NO_EXEC_STACK_DIRECTIVE

