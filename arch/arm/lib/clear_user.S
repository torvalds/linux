/*
 *  linux/arch/arm/lib/clear_user.S
 *
 *  Copyright (C) 1995, 1996,1997,1998 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/unwind.h>

		.text

/* Prototype: unsigned long arm_clear_user(void *addr, size_t sz)
 * Purpose  : clear some user memory
 * Params   : addr - user memory address to clear
 *          : sz   - number of bytes to clear
 * Returns  : number of bytes NOT cleared
 */
ENTRY(__clear_user_std)
WEAK(arm_clear_user)
UNWIND(.fnstart)
UNWIND(.save {r1, lr})
		stmfd	sp!, {r1, lr}
		mov	r2, #0
		cmp	r1, #4
		blt	2f
		ands	ip, r0, #3
		beq	1f
		cmp	ip, #2
		strusr	r2, r0, 1
		strusr	r2, r0, 1, le
		strusr	r2, r0, 1, lt
		rsb	ip, ip, #4
		sub	r1, r1, ip		@  7  6  5  4  3  2  1
1:		subs	r1, r1, #8		@ -1 -2 -3 -4 -5 -6 -7
		strusr	r2, r0, 4, pl, rept=2
		bpl	1b
		adds	r1, r1, #4		@  3  2  1  0 -1 -2 -3
		strusr	r2, r0, 4, pl
2:		tst	r1, #2			@ 1x 1x 0x 0x 1x 1x 0x
		strusr	r2, r0, 1, ne, rept=2
		tst	r1, #1			@ x1 x0 x1 x0 x1 x0 x1
		it	ne			@ explicit IT needed for the label
USER(		strnebt	r2, [r0])
		mov	r0, #0
		ldmfd	sp!, {r1, pc}
UNWIND(.fnend)
ENDPROC(arm_clear_user)
ENDPROC(__clear_user_std)

		.pushsection .text.fixup,"ax"
		.align	0
9001:		ldmfd	sp!, {r0, pc}
		.popsection

