/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Based on arch/arm/lib/memchr.S
 *
 * Copyright (C) 1995-2000 Russell King
 * Copyright (C) 2013 ARM Ltd.
 */

#include <linux/linkage.h>
#include <asm/assembler.h>

/*
 * Find a character in an area of memory.
 *
 * Parameters:
 *	x0 - buf
 *	x1 - c
 *	x2 - n
 * Returns:
 *	x0 - address of first occurrence of 'c' or 0
 */
SYM_FUNC_START_WEAK_PI(memchr)
	and	w1, w1, #0xff
1:	subs	x2, x2, #1
	b.mi	2f
	ldrb	w3, [x0], #1
	cmp	w3, w1
	b.ne	1b
	sub	x0, x0, #1
	ret
2:	mov	x0, #0
	ret
SYM_FUNC_END_PI(memchr)
EXPORT_SYMBOL_NOKASAN(memchr)
