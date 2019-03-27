//===-- aeabi_memset.S - EABI memset implementation -----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "../assembly.h"

//  void __aeabi_memset(void *dest, size_t n, int c) { memset(dest, c, n); }
//  void __aeabi_memclr(void *dest, size_t n) { __aeabi_memset(dest, n, 0); }

        .syntax unified
        .p2align 2
DEFINE_COMPILERRT_FUNCTION(__aeabi_memset)
        mov     r3, r1
        mov     r1, r2
        mov     r2, r3
#ifdef USE_THUMB_1
        push    {r7, lr}
        bl      memset
        pop     {r7, pc}
#else
        b       memset
#endif
END_COMPILERRT_FUNCTION(__aeabi_memset)

DEFINE_AEABI_FUNCTION_ALIAS(__aeabi_memset4, __aeabi_memset)
DEFINE_AEABI_FUNCTION_ALIAS(__aeabi_memset8, __aeabi_memset)

        .p2align 2
DEFINE_COMPILERRT_FUNCTION(__aeabi_memclr)
        mov     r2, r1
        movs    r1, #0
#ifdef USE_THUMB_1
        push    {r7, lr}
        bl      memset
        pop     {r7, pc}
#else
        b       memset
#endif
END_COMPILERRT_FUNCTION(__aeabi_memclr)

DEFINE_AEABI_FUNCTION_ALIAS(__aeabi_memclr4, __aeabi_memclr)
DEFINE_AEABI_FUNCTION_ALIAS(__aeabi_memclr8, __aeabi_memclr)

NO_EXEC_STACK_DIRECTIVE

