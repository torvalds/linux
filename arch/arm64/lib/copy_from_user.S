/*
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

/*
 * Copy from user space to a kernel buffer (alignment handled by the hardware)
 *
 * Parameters:
 *	x0 - to
 *	x1 - from
 *	x2 - n
 * Returns:
 *	x0 - bytes not copied
 */
ENTRY(__copy_from_user)
	add	x4, x1, x2			// upper user buffer boundary
	subs	x2, x2, #8
	b.mi	2f
1:
USER(9f, ldr	x3, [x1], #8	)
	subs	x2, x2, #8
	str	x3, [x0], #8
	b.pl	1b
2:	adds	x2, x2, #4
	b.mi	3f
USER(9f, ldr	w3, [x1], #4	)
	sub	x2, x2, #4
	str	w3, [x0], #4
3:	adds	x2, x2, #2
	b.mi	4f
USER(9f, ldrh	w3, [x1], #2	)
	sub	x2, x2, #2
	strh	w3, [x0], #2
4:	adds	x2, x2, #1
	b.mi	5f
USER(9f, ldrb	w3, [x1]	)
	strb	w3, [x0]
5:	mov	x0, #0
	ret
ENDPROC(__copy_from_user)

	.section .fixup,"ax"
	.align	2
9:	sub	x2, x4, x1
	mov	x3, x2
10:	strb	wzr, [x0], #1			// zero remaining buffer space
	subs	x3, x3, #1
	b.ne	10b
	mov	x0, x2				// bytes not copied
	ret
	.previous
