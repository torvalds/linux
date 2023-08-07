/* SPDX-License-Identifier: GPL-2.0-or-later WITH GCC-exception-2.0 */
#include <linux/linkage.h>
#include <asm/asmmacro.h>
#include <asm/core.h>

ENTRY(__divsi3)

	abi_entry_default
#if XCHAL_HAVE_DIV32
	quos	a2, a2, a3
#else
	xor	a7, a2, a3	/* sign = dividend ^ divisor */
	do_abs	a6, a2, a4	/* udividend = abs (dividend) */
	do_abs	a3, a3, a4	/* udivisor = abs (divisor) */
	bltui	a3, 2, .Lle_one	/* check if udivisor <= 1 */
	do_nsau	a5, a6, a2, a8	/* udividend_shift = nsau (udividend) */
	do_nsau	a4, a3, a2, a8	/* udivisor_shift = nsau (udivisor) */
	bgeu	a5, a4, .Lspecial

	sub	a4, a4, a5	/* count = udivisor_shift - udividend_shift */
	ssl	a4
	sll	a3, a3		/* udivisor <<= count */
	movi	a2, 0		/* quotient = 0 */

	/* test-subtract-and-shift loop; one quotient bit on each iteration */
#if XCHAL_HAVE_LOOPS
	loopnez	a4, .Lloopend
#endif /* XCHAL_HAVE_LOOPS */
.Lloop:
	bltu	a6, a3, .Lzerobit
	sub	a6, a6, a3
	addi	a2, a2, 1
.Lzerobit:
	slli	a2, a2, 1
	srli	a3, a3, 1
#if !XCHAL_HAVE_LOOPS
	addi	a4, a4, -1
	bnez	a4, .Lloop
#endif /* !XCHAL_HAVE_LOOPS */
.Lloopend:

	bltu	a6, a3, .Lreturn
	addi	a2, a2, 1	/* increment if udividend >= udivisor */
.Lreturn:
	neg	a5, a2
	movltz	a2, a5, a7	/* return (sign < 0) ? -quotient : quotient */
	abi_ret_default

.Lle_one:
	beqz	a3, .Lerror
	neg	a2, a6		/* if udivisor == 1, then return... */
	movgez	a2, a6, a7	/* (sign < 0) ? -udividend : udividend */
	abi_ret_default

.Lspecial:
	bltu	a6, a3, .Lreturn0 /* if dividend < divisor, return 0 */
	movi	a2, 1
	movi	a4, -1
	movltz	a2, a4, a7	/* else return (sign < 0) ? -1 : 1 */
	abi_ret_default

.Lerror:
	/* Divide by zero: Use an illegal instruction to force an exception.
	   The subsequent "DIV0" string can be recognized by the exception
	   handler to identify the real cause of the exception.  */
	ill
	.ascii	"DIV0"

.Lreturn0:
	movi	a2, 0
#endif /* XCHAL_HAVE_DIV32 */
	abi_ret_default

ENDPROC(__divsi3)
EXPORT_SYMBOL(__divsi3)
