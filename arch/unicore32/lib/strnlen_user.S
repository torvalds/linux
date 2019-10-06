/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * linux/arch/unicore32/lib/strnlen_user.S
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 */
#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/errno.h>

	.text
	.align	5

/* Prototype: unsigned long __strnlen_user(const char *str, long n)
 * Purpose  : get length of a string in user memory
 * Params   : str - address of string in user memory
 * Returns  : length of string *including terminator*
 *	      or zero on exception, or n + 1 if too long
 */
ENTRY(__strnlen_user)
	mov	r2, r0
1:
	ldrusr	r3, r0, 1
	cxor.a	r3, #0
	beq	2f
	sub.a	r1, r1, #1
	bne	1b
	add	r0, r0, #1
2:	sub	r0, r0, r2
	mov	pc, lr
ENDPROC(__strnlen_user)

	.pushsection .fixup,"ax"
	.align	0
9001:	mov	r0, #0
	mov	pc, lr
	.popsection
