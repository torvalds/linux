/*
 * linux/arch/unicore32/lib/strncpy_from_user.S
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
#include <asm/errno.h>

	.text
	.align	5

/*
 * Copy a string from user space to kernel space.
 *  r0 = dst, r1 = src, r2 = byte length
 * returns the number of characters copied (strlen of copied string),
 *  -EFAULT on exception, or "len" if we fill the whole buffer
 */
ENTRY(__strncpy_from_user)
	mov	ip, r1
1:	sub.a	r2, r2, #1
	ldrusr	r3, r1, 1, ns
	bfs	2f
	stb.w	r3, [r0]+, #1
	cxor.a	r3, #0
	bne	1b
	sub	r1, r1, #1	@ take NUL character out of count
2:	sub	r0, r1, ip
	mov	pc, lr
ENDPROC(__strncpy_from_user)

	.pushsection .fixup,"ax"
	.align	0
9001:	mov	r3, #0
	stb	r3, [r0+], #0	@ null terminate
	mov	r0, #-EFAULT
	mov	pc, lr
	.popsection

