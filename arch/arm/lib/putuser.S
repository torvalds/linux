/*
 *  linux/arch/arm/lib/putuser.S
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
 *		r2, r3 contains the value
 * Outputs:	r0 is the error code
 *		lr corrupted
 *
 * No other registers must be altered.  (see <asm/uaccess.h>
 * for specific ASM register usage).
 *
 * Note that ADDR_LIMIT is either 0 or 0xc0000000
 * Note also that it is intended that __put_user_bad is not global.
 */
#include <linux/linkage.h>
#include <asm/errno.h>
#include <asm/domain.h>

ENTRY(__put_user_1)
1: TUSER(strb)	r2, [r0]
	mov	r0, #0
	mov	pc, lr
ENDPROC(__put_user_1)

ENTRY(__put_user_2)
	mov	ip, r2, lsr #8
#ifdef CONFIG_THUMB2_KERNEL
#ifndef __ARMEB__
2: TUSER(strb)	r2, [r0]
3: TUSER(strb)	ip, [r0, #1]
#else
2: TUSER(strb)	ip, [r0]
3: TUSER(strb)	r2, [r0, #1]
#endif
#else	/* !CONFIG_THUMB2_KERNEL */
#ifndef __ARMEB__
2: TUSER(strb)	r2, [r0], #1
3: TUSER(strb)	ip, [r0]
#else
2: TUSER(strb)	ip, [r0], #1
3: TUSER(strb)	r2, [r0]
#endif
#endif	/* CONFIG_THUMB2_KERNEL */
	mov	r0, #0
	mov	pc, lr
ENDPROC(__put_user_2)

ENTRY(__put_user_4)
4: TUSER(str)	r2, [r0]
	mov	r0, #0
	mov	pc, lr
ENDPROC(__put_user_4)

ENTRY(__put_user_8)
#ifdef CONFIG_THUMB2_KERNEL
5: TUSER(str)	r2, [r0]
6: TUSER(str)	r3, [r0, #4]
#else
5: TUSER(str)	r2, [r0], #4
6: TUSER(str)	r3, [r0]
#endif
	mov	r0, #0
	mov	pc, lr
ENDPROC(__put_user_8)

__put_user_bad:
	mov	r0, #-EFAULT
	mov	pc, lr
ENDPROC(__put_user_bad)

.pushsection __ex_table, "a"
	.long	1b, __put_user_bad
	.long	2b, __put_user_bad
	.long	3b, __put_user_bad
	.long	4b, __put_user_bad
	.long	5b, __put_user_bad
	.long	6b, __put_user_bad
.popsection
