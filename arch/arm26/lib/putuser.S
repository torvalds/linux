/*
 *  linux/arch/arm26/lib/putuser.S
 *
 *  Copyright (C) 2001 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Idea from x86 version, (C) Copyright 1998 Linus Torvalds
 *
 * These functions have a non-standard call interface to make
 * them more efficient, especially as they return an error
 * value in addition to the "real" return value.
 *
 * __put_user_X
 *
 * Inputs:	r0 contains the address
 *		r1, r2 contains the value
 * Outputs:	r0 is the error code
 *		lr corrupted
 *
 * No other registers must be altered.  (see include/asm-arm/uaccess.h
 * for specific ASM register usage).
 *
 * Note that ADDR_LIMIT is either 0 or 0xc0000000
 * Note also that it is intended that __put_user_bad is not global.
 */
#include <asm/asm_offsets.h>
#include <asm/thread_info.h>
#include <asm/errno.h>

        .global __put_user_1
__put_user_1:
        bic     r2, sp, #0x1f00
        bic     r2, r2, #0x00ff
        str     lr, [sp, #-4]!
        ldr     r2, [r2, #TI_ADDR_LIMIT]
        sub     r2, r2, #1
        cmp     r0, r2
        bge     __put_user_bad
1:      cmp     r0, #0x02000000
        strlsbt r1, [r0]
        strgeb  r1, [r0]
        mov     r0, #0
        ldmfd   sp!, {pc}^

        .global __put_user_2
__put_user_2:
        bic     r2, sp, #0x1f00
        bic     r2, r2, #0x00ff
        str     lr, [sp, #-4]!
        ldr     r2, [r2, #TI_ADDR_LIMIT]
        sub     r2, r2, #2
        cmp     r0, r2
        bge     __put_user_bad
2:      cmp     r0, #0x02000000
        strlsbt r1, [r0], #1
        strgeb  r1, [r0], #1
        mov     r1, r1, lsr #8
3:      strlsbt r1, [r0]
        strgeb  r1, [r0]
        mov     r0, #0
        ldmfd   sp!, {pc}^

        .global __put_user_4
__put_user_4:
        bic     r2, sp, #0x1f00
        bic     r2, r2, #0x00ff
        str     lr, [sp, #-4]!
        ldr     r2, [r2, #TI_ADDR_LIMIT]
        sub     r2, r2, #4
        cmp     r0, r2
4:      bge     __put_user_bad
        cmp     r0, #0x02000000
        strlst  r1, [r0]
        strge   r1, [r0]
        mov     r0, #0
        ldmfd   sp!, {pc}^

        .global __put_user_8
__put_user_8:
        bic     ip, sp, #0x1f00
        bic     ip, ip, #0x00ff
        str     lr, [sp, #-4]!
        ldr     ip, [ip, #TI_ADDR_LIMIT]
        sub     ip, ip, #8
        cmp     r0, ip
        bge     __put_user_bad
        cmp     r0, #0x02000000
5:      strlst  r1, [r0], #4
6:      strlst  r2, [r0]
        strge   r1, [r0], #4
        strge   r2, [r0]
        mov     r0, #0
        ldmfd   sp!, {pc}^

__put_user_bad:
	mov	r0, #-EFAULT
	mov	pc, lr

.section __ex_table, "a"
	.long	1b, __put_user_bad
	.long	2b, __put_user_bad
	.long	3b, __put_user_bad
	.long	4b, __put_user_bad
	.long	5b, __put_user_bad
	.long	6b, __put_user_bad
.previous
