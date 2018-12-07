/*
 * Based on arch/arm/lib/memchr.S
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
 * Find a character in an area of memory.
 *
 * Parameters:
 *	x0 - buf
 *	x1 - c
 *	x2 - n
 * Returns:
 *	x0 - address of first occurrence of 'c' or 0
 */
WEAK(memchr)
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
ENDPIPROC(memchr)
EXPORT_SYMBOL_NOKASAN(memchr)
