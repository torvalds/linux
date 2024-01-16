/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ashldi3.S:	GCC emits these for certain drivers playing
 *		with long longs.
 *
 * Copyright (C) 1999 David S. Miller (davem@redhat.com)
 */

#include <linux/linkage.h>
#include <asm/export.h>

	.text
ENTRY(__ashldi3)
	cmp	%o2, 0
	be	9f
	 mov	0x20, %g2

	sub	%g2, %o2, %g2
	cmp	%g2, 0
	bg	7f
	 sll	%o0, %o2, %g3

	neg	%g2
	clr	%o5
	b	8f
	 sll	%o1, %g2, %o4
7:
	srl	%o1, %g2, %g2
	sll	%o1, %o2, %o5
	or	%g3, %g2, %o4
8:
	mov	%o4, %o0
	mov	%o5, %o1
9:
	retl
	 nop
ENDPROC(__ashldi3)
EXPORT_SYMBOL(__ashldi3)
