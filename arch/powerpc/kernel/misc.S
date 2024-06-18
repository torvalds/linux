/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * This file contains miscellaneous low-level functions.
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 * Largely rewritten by Cort Dougan (cort@cs.nmt.edu)
 * and Paul Mackerras.
 *
 * PPC64 updates by Dave Engebretsen (engebret@us.ibm.com)
 *
 * setjmp/longjmp code by Paul Mackerras.
 */
#include <linux/export.h>
#include <asm/ppc_asm.h>
#include <asm/unistd.h>
#include <asm/asm-compat.h>
#include <asm/asm-offsets.h>

	.text

/*
 * Returns (address we are running at) - (address we were linked at)
 * for use before the text and data are mapped to KERNELBASE.

 * add_reloc_offset(x) returns x + reloc_offset().
 */

_GLOBAL(reloc_offset)
	li	r3, 0
_GLOBAL(add_reloc_offset)
	mflr	r0
	bcl	20,31,$+4
1:	mflr	r5
	PPC_LL	r4,(2f-1b)(r5)
	subf	r5,r4,r5
	add	r3,r3,r5
	mtlr	r0
	blr
_ASM_NOKPROBE_SYMBOL(reloc_offset)
_ASM_NOKPROBE_SYMBOL(add_reloc_offset)

	.align	3
2:	PPC_LONG 1b

_GLOBAL(setjmp)
	mflr	r0
	PPC_STL	r0,0(r3)
	PPC_STL	r1,SZL(r3)
	PPC_STL	r2,2*SZL(r3)
#ifdef CONFIG_PPC32
	mfcr	r12
	stmw	r12, 3*SZL(r3)
#else
	mfcr	r0
	PPC_STL	r0,3*SZL(r3)
	PPC_STL	r13,4*SZL(r3)
	PPC_STL	r14,5*SZL(r3)
	PPC_STL	r15,6*SZL(r3)
	PPC_STL	r16,7*SZL(r3)
	PPC_STL	r17,8*SZL(r3)
	PPC_STL	r18,9*SZL(r3)
	PPC_STL	r19,10*SZL(r3)
	PPC_STL	r20,11*SZL(r3)
	PPC_STL	r21,12*SZL(r3)
	PPC_STL	r22,13*SZL(r3)
	PPC_STL	r23,14*SZL(r3)
	PPC_STL	r24,15*SZL(r3)
	PPC_STL	r25,16*SZL(r3)
	PPC_STL	r26,17*SZL(r3)
	PPC_STL	r27,18*SZL(r3)
	PPC_STL	r28,19*SZL(r3)
	PPC_STL	r29,20*SZL(r3)
	PPC_STL	r30,21*SZL(r3)
	PPC_STL	r31,22*SZL(r3)
#endif
	li	r3,0
	blr

_GLOBAL(longjmp)
#ifdef CONFIG_PPC32
	lmw	r12, 3*SZL(r3)
	mtcrf	0x38, r12
#else
	PPC_LL	r13,4*SZL(r3)
	PPC_LL	r14,5*SZL(r3)
	PPC_LL	r15,6*SZL(r3)
	PPC_LL	r16,7*SZL(r3)
	PPC_LL	r17,8*SZL(r3)
	PPC_LL	r18,9*SZL(r3)
	PPC_LL	r19,10*SZL(r3)
	PPC_LL	r20,11*SZL(r3)
	PPC_LL	r21,12*SZL(r3)
	PPC_LL	r22,13*SZL(r3)
	PPC_LL	r23,14*SZL(r3)
	PPC_LL	r24,15*SZL(r3)
	PPC_LL	r25,16*SZL(r3)
	PPC_LL	r26,17*SZL(r3)
	PPC_LL	r27,18*SZL(r3)
	PPC_LL	r28,19*SZL(r3)
	PPC_LL	r29,20*SZL(r3)
	PPC_LL	r30,21*SZL(r3)
	PPC_LL	r31,22*SZL(r3)
	PPC_LL	r0,3*SZL(r3)
	mtcrf	0x38,r0
#endif
	PPC_LL	r0,0(r3)
	PPC_LL	r1,SZL(r3)
	PPC_LL	r2,2*SZL(r3)
	mtlr	r0
	mr.	r3, r4
	bnelr
	li	r3, 1
	blr

_GLOBAL(current_stack_frame)
	PPC_LL	r3,0(r1)
	blr
EXPORT_SYMBOL(current_stack_frame)
