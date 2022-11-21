/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Based on arch/arm/lib/strchr.S
 *
 * Copyright (C) 1995-2000 Russell King
 * Copyright (C) 2013 ARM Ltd.
 */

#include <linux/linkage.h>
#include <asm/assembler.h>

/*
 * Find the first occurrence of a character in a string.
 *
 * Parameters:
 *	x0 - str
 *	x1 - c
 * Returns:
 *	x0 - address of first occurrence of 'c' or 0
 */
SYM_FUNC_START(__pi_strchr)
	and	w1, w1, #0xff
1:	ldrb	w2, [x0], #1
	cmp	w2, w1
	ccmp	w2, wzr, #4, ne
	b.ne	1b
	sub	x0, x0, #1
	cmp	w2, w1
	csel	x0, x0, xzr, eq
	ret
SYM_FUNC_END(__pi_strchr)

SYM_FUNC_ALIAS_WEAK(strchr, __pi_strchr)
EXPORT_SYMBOL_NOKASAN(strchr)
