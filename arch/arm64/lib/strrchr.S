/*
 * Based on arch/arm/lib/strrchr.S
 *
 * Copyright (C) 1995-2000 Russell King
 * Copyright (C) 2013 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
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
WEAK(strrchr)
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
ENDPIPROC(strrchr)
