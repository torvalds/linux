/*
 *  linux/arch/arm/lib/strncpy_from_user.S
 *
 *  Copyright (C) 1995-2000 Russell King
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
1:	subs	r2, r2, #1
USER(	ldrplbt	r3, [r1], #1)
	bmi	2f
	strb	r3, [r0], #1
	teq	r3, #0
	bne	1b
	sub	r1, r1, #1	@ take NUL character out of count
2:	sub	r0, r1, ip
	mov	pc, lr

	.section .fixup,"ax"
	.align	0
9001:	mov	r3, #0
	strb	r3, [r0, #0]	@ null terminate
	mov	r0, #-EFAULT
	mov	pc, lr
	.previous

