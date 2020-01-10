/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Userland implementation of getcpu() for 64 bits processes in a
 * s390 kernel for use in the vDSO
 *
 *  Copyright IBM Corp. 2016
 *  Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 */
#include <asm/vdso.h>
#include <asm/asm-offsets.h>
#include <asm/dwarf.h>

	.text
	.align 4
	.globl __kernel_getcpu
	.type  __kernel_getcpu,@function
__kernel_getcpu:
	CFI_STARTPROC
	sacf	256
	lm	%r4,%r5,__VDSO_GETCPU_VAL(%r0)
	sacf	0
	ltgr	%r2,%r2
	jz	2f
	st	%r5,0(%r2)
2:	ltgr	%r3,%r3
	jz	3f
	st	%r4,0(%r3)
3:	lghi	%r2,0
	br	%r14
	CFI_ENDPROC
	.size	__kernel_getcpu,.-__kernel_getcpu
