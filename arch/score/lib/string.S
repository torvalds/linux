/*
 * arch/score/lib/string.S
 *
 * Score Processor version.
 *
 * Copyright (C) 2009 Sunplus Core Technology Co., Ltd.
 *  Chen Liqin <liqin.chen@sunplusct.com>
 *  Lennox Wu <lennox.wu@sunplusct.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/linkage.h>
#include <asm-generic/errno.h>

	.text
	.align 2
ENTRY(__strncpy_from_user)
	cmpi.c	r6, 0
	mv	r9, r6
	ble	.L2
0:	lbu	r7, [r5]
	ldi	r8, 0
1:	sb	r7, [r4]
2:	lb	r6, [r5]
	cmp.c	r6, r8
	beq	.L2

.L5:
	addi	r8, 1
	cmp.c	r8, r9
	beq	.L7
3:	lbu	r6, [r5, 1]+
4:	sb	r6, [r4, 1]+
5:	lb	r7, [r5]
	cmpi.c	r7, 0
	bne	.L5
.L7:
	mv	r4, r8
	br	r3
.L2:
	ldi	r8, 0
	mv	r4, r8
	br	r3
	.section .fixup, "ax"
99:
	ldi	r4, -EFAULT
	br	r3
	.previous
	.section __ex_table, "a"
	.align	2
	.word	0b ,99b
	.word	1b ,99b
	.word	2b ,99b
	.word	3b ,99b
	.word	4b ,99b
	.word	5b ,99b
	.previous

	.align 2
ENTRY(__strnlen_user)
	cmpi.c	r5, 0
	ble	.L11
0:	lb	r6, [r4]
	ldi	r7, 0
	cmp.c	r6, r7
	beq	.L11
.L15:
	addi	r7, 1
	cmp.c	r7, r5
	beq	.L23
1:	lb	r6, [r4,1]+
	cmpi.c	r6, 0
	bne	.L15
.L23:
	addri	r4, r7, 1
	br	r3

.L11:
	ldi	r4, 1
	br	r3
	.section .fixup, "ax"
99:
	ldi	r4, 0
	br	r3

	.section __ex_table,"a"
	.align 2
	.word	0b, 99b
	.word	1b, 99b
	.previous

	.align 2
ENTRY(__strlen_user)
0:	lb	r6, [r4]
	mv	r7, r4
	extsb	r6, r6
	cmpi.c	r6, 0
	mv	r4, r6
	beq	.L27
.L28:
1:	lb	r6, [r7, 1]+
	addi	r6, 1
	cmpi.c	r6, 0
	bne	.L28
.L27:
	br	r3
	.section .fixup, "ax"
	ldi	r4, 0x0
	br	r3
99:
	ldi	r4, 0
	br	r3
	.previous
	.section __ex_table, "a"
	.align	2
	.word	0b ,99b
	.word	1b ,99b
	.previous

	.align 2
ENTRY(__copy_tofrom_user)
	cmpi.c	r6, 0
	mv	r10,r6
	beq	.L32
	ldi	r9, 0
.L34:
	add	r6, r5, r9
0:	lbu	r8, [r6]
	add	r7, r4, r9
1:	sb	r8, [r7]
	addi	r9, 1
	cmp.c	r9, r10
	bne	.L34
.L32:
	ldi	r4, 0
	br	r3
	.section .fixup, "ax"
99:
	sub	r4, r10, r9
	br	r3
	.previous
	.section __ex_table, "a"
	.align	2
	.word	0b, 99b
	.word	1b, 99b
	.previous

	.align 2
ENTRY(__clear_user)
	cmpi.c	r5, 0
	beq	.L38
	ldi	r6, 0
	mv	r7, r6
.L40:
	addi	r6, 1
0:	sb	r7, [r4]+, 1
	cmp.c	r6, r5
	bne	.L40
.L38:
	ldi	r4, 0
	br	r3

	.section .fixup, "ax"
	br	r3
	.previous
	.section __ex_table, "a"
	.align	2
99:
	.word	0b, 99b
	.previous
