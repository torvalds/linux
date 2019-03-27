// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.

#include "../assembly.h"

// __chkstk routine
// This routine is windows specific.
// http://msdn.microsoft.com/en-us/library/ms648426.aspx

// This clobbers the register r12, and the condition codes, and uses r5 and r6
// as temporaries by backing them up and restoring them afterwards.
// Does not modify any memory or the stack pointer.

//      movw    r4,  #256 // Number of bytes of stack, in units of 4 byte
//      bl      __chkstk
//      sub.w   sp, sp, r4

#define PAGE_SIZE 4096

        .p2align 2
DEFINE_COMPILERRT_FUNCTION(__chkstk)
        lsl    r4,  r4,  #2
        mov    r12, sp
        push   {r5, r6}
        mov    r5,  r4
1:
        sub    r12, r12, #PAGE_SIZE
        subs   r5,  r5,  #PAGE_SIZE
        ldr    r6,  [r12]
        bgt    1b

        pop    {r5, r6}
        bx     lr
END_COMPILERRT_FUNCTION(__chkstk)
