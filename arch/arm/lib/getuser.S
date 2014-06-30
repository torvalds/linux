/*
 *  linux/arch/arm/lib/getuser.S
 *
 *  Copyright (C) 2001 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Idea from x86 version, (C) Copyright 1998 Linus Torvalds
 *
 * These functions have a non-standard call interface to make them more
 * efficient, especially as they return an error value in addition to
 * the "real" return value.
 *
 * __get_user_X
 *
 * Inputs:	r0 contains the address
 *		r1 contains the address limit, which must be preserved
 * Outputs:	r0 is the error code
 *		r2 contains the zero-extended value
 *		lr corrupted
 *
 * No other registers must be altered.  (see <asm/uaccess.h>
 * for specific ASM register usage).
 *
 * Note that ADDR_LIMIT is either 0 or 0xc0000000.
 * Note also that it is intended that __get_user_bad is not global.
 */
#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/errno.h>
#include <asm/domain.h>

ENTRY(__get_user_1)
	check_uaccess r0, 1, r1, r2, __get_user_bad
1: TUSER(ldrb)	r2, [r0]
	mov	r0, #0
	ret	lr
ENDPROC(__get_user_1)

ENTRY(__get_user_2)
	check_uaccess r0, 2, r1, r2, __get_user_bad
#ifdef CONFIG_CPU_USE_DOMAINS
rb	.req	ip
2:	ldrbt	r2, [r0], #1
3:	ldrbt	rb, [r0], #0
#else
rb	.req	r0
2:	ldrb	r2, [r0]
3:	ldrb	rb, [r0, #1]
#endif
#ifndef __ARMEB__
	orr	r2, r2, rb, lsl #8
#else
	orr	r2, rb, r2, lsl #8
#endif
	mov	r0, #0
	ret	lr
ENDPROC(__get_user_2)

ENTRY(__get_user_4)
	check_uaccess r0, 4, r1, r2, __get_user_bad
4: TUSER(ldr)	r2, [r0]
	mov	r0, #0
	ret	lr
ENDPROC(__get_user_4)

__get_user_bad:
	mov	r2, #0
	mov	r0, #-EFAULT
	ret	lr
ENDPROC(__get_user_bad)

.pushsection __ex_table, "a"
	.long	1b, __get_user_bad
	.long	2b, __get_user_bad
	.long	3b, __get_user_bad
	.long	4b, __get_user_bad
.popsection
