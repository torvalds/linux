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

		.text

/* Prototype: int __arch_clear_user(void *addr, size_t sz)
 * Purpose  : clear some user memory
 * Params   : addr - user memory address to clear
 *          : sz   - number of bytes to clear
 * Returns  : number of bytes NOT cleared
 */
ENTRY(__arch_clear_user)
		stmfd	sp!, {r1, lr}
		mov	r2, #0
		cmp	r1, #4
		blt	2f
		ands	ip, r0, #3
		beq	1f
		cmp	ip, #2
USER(		strbt	r2, [r0], #1)
USER(		strlebt	r2, [r0], #1)
USER(		strltbt	r2, [r0], #1)
		rsb	ip, ip, #4
		sub	r1, r1, ip		@  7  6  5  4  3  2  1
1:		subs	r1, r1, #8		@ -1 -2 -3 -4 -5 -6 -7
USER(		strplt	r2, [r0], #4)
USER(		strplt	r2, [r0], #4)
		bpl	1b
		adds	r1, r1, #4		@  3  2  1  0 -1 -2 -3
USER(		strplt	r2, [r0], #4)
2:		tst	r1, #2			@ 1x 1x 0x 0x 1x 1x 0x
USER(		strnebt	r2, [r0], #1)
USER(		strnebt	r2, [r0], #1)
		tst	r1, #1			@ x1 x0 x1 x0 x1 x0 x1
USER(		strnebt	r2, [r0], #1)
		mov	r0, #0
		LOADREGS(fd,sp!, {r1, pc})

		.section .fixup,"ax"
		.align	0
9001:		LOADREGS(fd,sp!, {r0, pc})
		.previous

