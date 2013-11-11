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
 * Copy to user space from a kernel buffer (alignment handled by the hardware)
 *
 * Parameters:
 *	x0 - to
 *	x1 - from
 *	x2 - n
 * Returns:
 *	x0 - bytes not copied
 */
ENTRY(__copy_to_user)
	add	x4, x0, x2			// upper user buffer boundary
	subs	x2, x2, #8
	b.mi	2f
1:
	ldr	x3, [x1], #8
	subs	x2, x2, #8
USER(9f, str	x3, [x0], #8	)
	b.pl	1b
2:	adds	x2, x2, #4
	b.mi	3f
	ldr	w3, [x1], #4
	sub	x2, x2, #4
USER(9f, str	w3, [x0], #4	)
3:	adds	x2, x2, #2
	b.mi	4f
	ldrh	w3, [x1], #2
	sub	x2, x2, #2
USER(9f, strh	w3, [x0], #2	)
4:	adds	x2, x2, #1
	b.mi	5f
	ldrb	w3, [x1]
USER(9f, strb	w3, [x0]	)
5:	mov	x0, #0
	ret
ENDPROC(__copy_to_user)

	.section .fixup,"ax"
	.align	2
9:	sub	x0, x4, x0			// bytes not copied
	ret
	.previous
