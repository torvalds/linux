/*
 * linux/arch/unicore32/lib/clear_user.S
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/linkage.h>
#include <asm/assembler.h>

		.text

/* Prototype: int __clear_user(void *addr, size_t sz)
 * Purpose  : clear some user memory
 * Params   : addr - user memory address to clear
 *          : sz   - number of bytes to clear
 * Returns  : number of bytes NOT cleared
 */
WEAK(__clear_user)
		stm.w	(lr), [sp-]
		stm.w	(r1), [sp-]
		mov	r2, #0
		csub.a	r1, #4
		bsl	2f
		and.a	ip, r0, #3
		beq	1f
		csub.a	ip, #2
		strusr	r2, r0, 1
		strusr	r2, r0, 1, el
		strusr	r2, r0, 1, sl
		rsub	ip, ip, #4
		sub	r1, r1, ip		@  7  6  5  4  3  2  1
1:		sub.a	r1, r1, #8		@ -1 -2 -3 -4 -5 -6 -7
		strusr	r2, r0, 4, ns, rept=2
		bns	1b
		add.a	r1, r1, #4		@  3  2  1  0 -1 -2 -3
		strusr	r2, r0, 4, ns
2:		cand.a	r1, #2			@ 1x 1x 0x 0x 1x 1x 0x
		strusr	r2, r0, 1, ne, rept=2
		cand.a	r1, #1			@ x1 x0 x1 x0 x1 x0 x1
		beq	3f
USER(		stb.u	r2, [r0])
3:		mov	r0, #0
		ldm.w	(r1), [sp]+
		ldm.w	(pc), [sp]+
ENDPROC(__clear_user)

		.pushsection .fixup,"ax"
		.align	0
9001:		ldm.w	(r0), [sp]+
		ldm.w	(pc), [sp]+
		.popsection

