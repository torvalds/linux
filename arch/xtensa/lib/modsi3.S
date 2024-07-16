/* SPDX-License-Identifier: GPL-2.0-or-later WITH GCC-exception-2.0 */
#include <linux/linkage.h>
#include <asm/asmmacro.h>
#include <asm/core.h>

ENTRY(__modsi3)

	abi_entry_default
#if XCHAL_HAVE_DIV32
	rems	a2, a2, a3
#else
	mov	a7, a2		/* save original (signed) dividend */
	do_abs	a2, a2, a4	/* udividend = abs (dividend) */
	do_abs	a3, a3, a4	/* udivisor = abs (divisor) */
	bltui	a3, 2, .Lle_one	/* check if udivisor <= 1 */
	do_nsau	a5, a2, a6, a8	/* udividend_shift = nsau (udividend) */
	do_nsau	a4, a3, a6, a8	/* udivisor_shift = nsau (udivisor) */
	bgeu	a5, a4, .Lspecial

	sub	a4, a4, a5	/* count = udivisor_shift - udividend_shift */
	ssl	a4
	sll	a3, a3		/* udivisor <<= count */

	/* test-subtract-and-shift loop */
#if XCHAL_HAVE_LOOPS
	loopnez	a4, .Lloopend
#endif /* XCHAL_HAVE_LOOPS */
.Lloop:
	bltu	a2, a3, .Lzerobit
	sub	a2, a2, a3
.Lzerobit:
	srli	a3, a3, 1
#if !XCHAL_HAVE_LOOPS
	addi	a4, a4, -1
	bnez	a4, .Lloop
#endif /* !XCHAL_HAVE_LOOPS */
.Lloopend:

.Lspecial:
	bltu	a2, a3, .Lreturn
	sub	a2, a2, a3	/* subtract again if udividend >= udivisor */
.Lreturn:
	bgez	a7, .Lpositive
	neg	a2, a2		/* if (dividend < 0), return -udividend */
.Lpositive:
	abi_ret_default

.Lle_one:
	bnez	a3, .Lreturn0

	/* Divide by zero: Use an illegal instruction to force an exception.
	   The subsequent "DIV0" string can be recognized by the exception
	   handler to identify the real cause of the exception.  */
	ill
	.ascii	"DIV0"

.Lreturn0:
	movi	a2, 0
#endif /* XCHAL_HAVE_DIV32 */
	abi_ret_default

ENDPROC(__modsi3)

#if !XCHAL_HAVE_NSA
	.section .rodata
	.align	4
	.global	__nsau_data
	.type	__nsau_data, @object
__nsau_data:
	.byte	8, 7, 6, 6, 5, 5, 5, 5, 4, 4, 4, 4, 4, 4, 4, 4
	.byte	3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3
	.byte	2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2
	.byte	2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2
	.byte	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1
	.byte	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1
	.byte	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1
	.byte	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1
	.byte	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
	.byte	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
	.byte	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
	.byte	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
	.byte	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
	.byte	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
	.byte	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
	.byte	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
	.size	__nsau_data, . - __nsau_data
#endif /* !XCHAL_HAVE_NSA */
