/*
 * Coherency fabric: low level functions
 *
 * Copyright (C) 2012 Marvell
 *
 * Gregory CLEMENT <gregory.clement@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 *
 * This file implements the assembly function to add a CPU to the
 * coherency fabric. This function is called by each of the secondary
 * CPUs during their early boot in an SMP kernel, this why this
 * function have to callable from assembly. It can also be called by a
 * primary CPU from C code during its boot.
 */

#include <linux/linkage.h>
#define ARMADA_XP_CFB_CTL_REG_OFFSET 0x0
#define ARMADA_XP_CFB_CFG_REG_OFFSET 0x4

#include <asm/assembler.h>
#include <asm/cp15.h>

	.text

ENTRY(ll_set_cpu_coherent)
	mrc	p15, 0, r1, c1, c0, 0
	tst	r1, #CR_M @ Check MMU bit enabled
	bne	1f

	/* use physical address of the coherency register*/
	adr	r0, 3f
	ldr	r3, [r0]
	ldr	r0, [r0, r3]
	b	2f
1:
	/* use virtual address of the coherency register*/
	ldr	r0, =coherency_base
	ldr	r0, [r0]
2:
	/* Create bit by cpu index */
	mrc	15, 0, r1, cr0, cr0, 5
	and	r1, r1, #15
	mov	r2, #(1 << 24)
	lsl	r1, r2, r1
ARM_BE8(rev	r1, r1)

	/* Add CPU to SMP group - Atomic */
	add	r3, r0, #ARMADA_XP_CFB_CTL_REG_OFFSET
1:
	ldrex	r2, [r3]
	orr	r2, r2, r1
	strex 	r0, r2, [r3]
	cmp	r0, #0
	bne 1b

	/* Enable coherency on CPU - Atomic */
	add	r3, r3, #ARMADA_XP_CFB_CFG_REG_OFFSET
1:
	ldrex	r2, [r3]
	orr	r2, r2, r1
	strex	r0, r2, [r3]
	cmp	r0, #0
	bne 1b

	dsb

	mov	r0, #0
	mov	pc, lr
ENDPROC(ll_set_cpu_coherent)

	.align 2
3:
	.long	coherency_phys_base - .
