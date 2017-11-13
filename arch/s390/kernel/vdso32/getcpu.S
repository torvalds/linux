/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Userland implementation of getcpu() for 32 bits processes in a
 * s390 kernel for use in the vDSO
 *
 *  Copyright IBM Corp. 2016
 *  Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 */
#include <asm/vdso.h>
#include <asm/asm-offsets.h>

	.text
	.align 4
	.globl __kernel_getcpu
	.type  __kernel_getcpu,@function
__kernel_getcpu:
	.cfi_startproc
	ear	%r1,%a4
	lhi	%r4,1
	sll	%r4,24
	sar	%a4,%r4
	la	%r4,0
	epsw	%r0,0
	sacf	512
	l	%r5,__VDSO_CPU_NR(%r4)
	l	%r4,__VDSO_NODE_ID(%r4)
	tml	%r0,0x4000
	jo	1f
	tml	%r0,0x8000
	jno	0f
	sacf	256
	j	1f
0:	sacf	0
1:	sar	%a4,%r1
	ltr	%r2,%r2
	jz	2f
	st	%r5,0(%r2)
2:	ltr	%r3,%r3
	jz	3f
	st	%r4,0(%r3)
3:	lhi	%r2,0
	br	%r14
	.cfi_endproc
	.size	__kernel_getcpu,.-__kernel_getcpu
