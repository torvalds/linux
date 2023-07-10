/* SPDX-License-Identifier: GPL-2.0-or-later WITH GCC-exception-2.0 */
#include <linux/linkage.h>
#include <asm/asmmacro.h>
#include <asm/core.h>

ENTRY(__umodsi3)

	abi_entry_default
#if XCHAL_HAVE_DIV32
	remu	a2, a2, a3
#else
	bltui	a3, 2, .Lle_one	/* check if the divisor is <= 1 */

	do_nsau	a5, a2, a6, a7	/* dividend_shift = nsau (dividend) */
	do_nsau	a4, a3, a6, a7	/* divisor_shift = nsau (divisor) */
	bgeu	a5, a4, .Lspecial

	sub	a4, a4, a5	/* count = divisor_shift - dividend_shift */
	ssl	a4
	sll	a3, a3		/* divisor <<= count */

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
	sub	a2, a2, a3	/* subtract once more if dividend >= divisor */
.Lreturn:
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

ENDPROC(__umodsi3)
EXPORT_SYMBOL(__umodsi3)
