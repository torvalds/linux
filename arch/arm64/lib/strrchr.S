/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Based on arch/arm/lib/strrchr.S
 *
 * Copyright (C) 1995-2000 Russell King
 * Copyright (C) 2013 ARM Ltd.
 */

#include <linux/linkage.h>
#include <asm/assembler.h>

/*
 * Find the last occurrence of a character in a string.
 *
 * Parameters:
 *	x0 - str
 *	x1 - c
 * Returns:
 *	x0 - address of last occurrence of 'c' or 0
 */
SYM_FUNC_START(__pi_strrchr)
	mov	x3, #0
	and	w1, w1, #0xff
1:	ldrb	w2, [x0], #1
	cbz	w2, 2f
	cmp	w2, w1
	b.ne	1b
	sub	x3, x0, #1
	b	1b
2:	mov	x0, x3
	ret
SYM_FUNC_END(__pi_strrchr)
SYM_FUNC_ALIAS_WEAK(strrchr, __pi_strrchr)
EXPORT_SYMBOL_NOKASAN(strrchr)
