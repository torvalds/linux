/*
 * Based on arch/arm/lib/strnlen_user.S
 *
 * Copyright (C) 1995-2000 Russell King
 * Copyright (C) 2012 ARM Ltd.
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
#include <asm/errno.h>

	.text
	.align	5

/* Prototype: unsigned long __strnlen_user(const char *str, long n)
 * Purpose  : get length of a string in user memory
 * Params   : str - address of string in user memory
 * Returns  : length of string *including terminator*
 *	      or zero on exception, or n if too long
 */
ENTRY(__strnlen_user)
	mov	x2, x0
1:	subs	x1, x1, #1
	b.mi	2f
USER(9f, ldrb	w3, [x0], #1	)
	cbnz	w3, 1b
2:	sub	x0, x0, x2
	ret
ENDPROC(__strnlen_user)

	.section .fixup,"ax"
	.align	0
9:	mov	x0, #0
	ret
	.previous
