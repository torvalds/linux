/*
 * Based on arch/arm/lib/strncpy_from_user.S
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

/*
 * Copy a string from user space to kernel space.
 *  x0 = dst, x1 = src, x2 = byte length
 * returns the number of characters copied (strlen of copied string),
 *  -EFAULT on exception, or "len" if we fill the whole buffer
 */
ENTRY(__strncpy_from_user)
	mov	x4, x1
1:	subs	x2, x2, #1
	bmi	2f
USER(9f, ldrb	w3, [x1], #1	)
	strb	w3, [x0], #1
	cbnz	w3, 1b
	sub	x1, x1, #1	// take NUL character out of count
2:	sub	x0, x1, x4
	ret
ENDPROC(__strncpy_from_user)

	.section .fixup,"ax"
	.align	0
9:	strb	wzr, [x0]	// null terminate
	mov	x0, #-EFAULT
	ret
	.previous
