/* SPDX-License-Identifier: GPL-2.0-or-later WITH GCC-exception-2.0 */
#include <linux/linkage.h>
#include <asm/asmmacro.h>
#include <asm/core.h>

ENTRY(__udivsi3)

	abi_entry_default
#if XCHAL_HAVE_DIV32
	quou	a2, a2, a3
#else
	bltui	a3, 2, .Lle_one	/* check if the divisor <= 1 */

	mov	a6, a2		/* keep dividend in a6 */
	do_nsau	a5, a6, a2, a7	/* dividend_shift = nsau (dividend) */
	do_nsau	a4, a3, a2, a7	/* divisor_shift = nsau (divisor) */
	bgeu	a5, a4, .Lspecial

	sub	a4, a4, a5	/* count = divisor_shift - dividend_shift */
	ssl	a4
	sll	a3, a3		/* divisor <<= count */
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
	addi	a2, a2, 1	/* increment quotient if dividend >= divisor */
.Lreturn:
	abi_ret_default

.Lle_one:
	beqz	a3, .Lerror	/* if divisor == 1, return the dividend */
	abi_ret_default

.Lspecial:
	/* return dividend >= divisor */
	bltu	a6, a3, .Lreturn0
	movi	a2, 1
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

ENDPROC(__udivsi3)
