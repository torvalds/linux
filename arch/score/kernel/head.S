/*
 * arch/score/kernel/head.S
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
#include <linux/init.h>
#include <linux/linkage.h>

#include <asm/asm-offsets.h>

	.extern start_kernel
	.global init_thread_union
	.global	kernelsp

__INIT
ENTRY(_stext)
	la	r30, __bss_start	/* initialize BSS segment. */
	la	r31, _end
	xor	r8, r8, r8

1:	cmp.c	r31, r30
	beq	2f

	sw	r8, [r30]		/* clean memory. */
	addi	r30, 4
	b	1b

2:	la	r28, init_thread_union	/* set kernel stack. */
	mv	r0, r28
	addi	r0, KERNEL_STACK_SIZE - 32
	la	r30, kernelsp
	sw	r0, [r30]
	subi	r0, 4*4
	xor	r30, r30, r30
	ori	r30, 0x02		/* enable MMU. */
	mtcr	r30, cr4
	nop
	nop
	nop
	nop
	nop
	nop
	nop

	/* there is no parameter */
	xor	r4, r4, r4
	xor	r5, r5, r5
	xor	r6, r6, r6
	xor	r7, r7, r7
	la	r30, start_kernel	/* jump to init_arch */
	br	r30
