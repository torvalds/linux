/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#include <linux/linkage.h>
#include <asm/errno.h>
#include <asm/cache.h>
#include <arch/chip.h>

/* Access user memory, but use MMU to avoid propagating kernel exceptions. */

/*
 * strnlen_user_asm takes the pointer in r0, and the length bound in r1.
 * It returns the length, including the terminating NUL, or zero on exception.
 * If length is greater than the bound, returns one plus the bound.
 */
STD_ENTRY(strnlen_user_asm)
	{ bz r1, 2f; addi r3, r0, -1 }  /* bias down to include NUL */
1:      { lb_u r4, r0; addi r1, r1, -1 }
	bz r4, 2f
	{ bnzt r1, 1b; addi r0, r0, 1 }
2:      { sub r0, r0, r3; jrp lr }
	STD_ENDPROC(strnlen_user_asm)
	.pushsection .fixup,"ax"
strnlen_user_fault:
	{ move r0, zero; jrp lr }
	ENDPROC(strnlen_user_fault)
	.section __ex_table,"a"
	.align 4
	.word 1b, strnlen_user_fault
	.popsection

/*
 * strncpy_from_user_asm takes the kernel target pointer in r0,
 * the userspace source pointer in r1, and the length bound (including
 * the trailing NUL) in r2.  On success, it returns the string length
 * (not including the trailing NUL), or -EFAULT on failure.
 */
STD_ENTRY(strncpy_from_user_asm)
	{ bz r2, 2f; move r3, r0 }
1:      { lb_u r4, r1; addi r1, r1, 1; addi r2, r2, -1 }
	{ sb r0, r4; addi r0, r0, 1 }
	bz r2, 2f
	bnzt r4, 1b
	addi r0, r0, -1   /* don't count the trailing NUL */
2:      { sub r0, r0, r3; jrp lr }
	STD_ENDPROC(strncpy_from_user_asm)
	.pushsection .fixup,"ax"
strncpy_from_user_fault:
	{ movei r0, -EFAULT; jrp lr }
	ENDPROC(strncpy_from_user_fault)
	.section __ex_table,"a"
	.align 4
	.word 1b, strncpy_from_user_fault
	.popsection

/*
 * clear_user_asm takes the user target address in r0 and the
 * number of bytes to zero in r1.
 * It returns the number of uncopiable bytes (hopefully zero) in r0.
 * Note that we don't use a separate .fixup section here since we fall
 * through into the "fixup" code as the last straight-line bundle anyway.
 */
STD_ENTRY(clear_user_asm)
	{ bz r1, 2f; or r2, r0, r1 }
	andi r2, r2, 3
	bzt r2, .Lclear_aligned_user_asm
1:      { sb r0, zero; addi r0, r0, 1; addi r1, r1, -1 }
	bnzt r1, 1b
2:      { move r0, r1; jrp lr }
	.pushsection __ex_table,"a"
	.align 4
	.word 1b, 2b
	.popsection

.Lclear_aligned_user_asm:
1:      { sw r0, zero; addi r0, r0, 4; addi r1, r1, -4 }
	bnzt r1, 1b
2:      { move r0, r1; jrp lr }
	STD_ENDPROC(clear_user_asm)
	.pushsection __ex_table,"a"
	.align 4
	.word 1b, 2b
	.popsection

/*
 * flush_user_asm takes the user target address in r0 and the
 * number of bytes to flush in r1.
 * It returns the number of unflushable bytes (hopefully zero) in r0.
 */
STD_ENTRY(flush_user_asm)
	bz r1, 2f
	{ movei r2, L2_CACHE_BYTES; add r1, r0, r1 }
	{ sub r2, zero, r2; addi r1, r1, L2_CACHE_BYTES-1 }
	{ and r0, r0, r2; and r1, r1, r2 }
	{ sub r1, r1, r0 }
1:      { flush r0; addi r1, r1, -CHIP_FLUSH_STRIDE() }
	{ addi r0, r0, CHIP_FLUSH_STRIDE(); bnzt r1, 1b }
2:      { move r0, r1; jrp lr }
	STD_ENDPROC(flush_user_asm)
	.pushsection __ex_table,"a"
	.align 4
	.word 1b, 2b
	.popsection

/*
 * finv_user_asm takes the user target address in r0 and the
 * number of bytes to flush-invalidate in r1.
 * It returns the number of not finv'able bytes (hopefully zero) in r0.
 */
STD_ENTRY(finv_user_asm)
	bz r1, 2f
	{ movei r2, L2_CACHE_BYTES; add r1, r0, r1 }
	{ sub r2, zero, r2; addi r1, r1, L2_CACHE_BYTES-1 }
	{ and r0, r0, r2; and r1, r1, r2 }
	{ sub r1, r1, r0 }
1:      { finv r0; addi r1, r1, -CHIP_FINV_STRIDE() }
	{ addi r0, r0, CHIP_FINV_STRIDE(); bnzt r1, 1b }
2:      { move r0, r1; jrp lr }
	STD_ENDPROC(finv_user_asm)
	.pushsection __ex_table,"a"
	.align 4
	.word 1b, 2b
	.popsection
