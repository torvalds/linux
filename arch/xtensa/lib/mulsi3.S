/* SPDX-License-Identifier: GPL-2.0-or-later WITH GCC-exception-2.0 */
#include <linux/linkage.h>
#include <asm/asmmacro.h>
#include <asm/core.h>

	.macro	do_addx2 dst, as, at, tmp
#if XCHAL_HAVE_ADDX
	addx2	\dst, \as, \at
#else
	slli	\tmp, \as, 1
	add	\dst, \tmp, \at
#endif
	.endm

	.macro	do_addx4 dst, as, at, tmp
#if XCHAL_HAVE_ADDX
	addx4	\dst, \as, \at
#else
	slli	\tmp, \as, 2
	add	\dst, \tmp, \at
#endif
	.endm

	.macro	do_addx8 dst, as, at, tmp
#if XCHAL_HAVE_ADDX
	addx8	\dst, \as, \at
#else
	slli	\tmp, \as, 3
	add	\dst, \tmp, \at
#endif
	.endm

ENTRY(__mulsi3)

	abi_entry_default

#if XCHAL_HAVE_MUL32
	mull	a2, a2, a3

#elif XCHAL_HAVE_MUL16
	or	a4, a2, a3
	srai	a4, a4, 16
	bnez	a4, .LMUL16
	mul16u	a2, a2, a3
	abi_ret_default
.LMUL16:
	srai	a4, a2, 16
	srai	a5, a3, 16
	mul16u	a7, a4, a3
	mul16u	a6, a5, a2
	mul16u	a4, a2, a3
	add	a7, a7, a6
	slli	a7, a7, 16
	add	a2, a7, a4

#elif XCHAL_HAVE_MAC16
	mul.aa.hl a2, a3
	mula.aa.lh a2, a3
	rsr	a5, ACCLO
	umul.aa.ll a2, a3
	rsr	a4, ACCLO
	slli	a5, a5, 16
	add	a2, a4, a5

#else /* !MUL32 && !MUL16 && !MAC16 */

	/* Multiply one bit at a time, but unroll the loop 4x to better
	   exploit the addx instructions and avoid overhead.
	   Peel the first iteration to save a cycle on init.  */

	/* Avoid negative numbers.  */
	xor	a5, a2, a3	/* Top bit is 1 if one input is negative.  */
	do_abs	a3, a3, a6
	do_abs	a2, a2, a6

	/* Swap so the second argument is smaller.  */
	sub	a7, a2, a3
	mov	a4, a3
	movgez	a4, a2, a7	/* a4 = max (a2, a3) */
	movltz	a3, a2, a7	/* a3 = min (a2, a3) */

	movi	a2, 0
	extui	a6, a3, 0, 1
	movnez	a2, a4, a6

	do_addx2 a7, a4, a2, a7
	extui	a6, a3, 1, 1
	movnez	a2, a7, a6

	do_addx4 a7, a4, a2, a7
	extui	a6, a3, 2, 1
	movnez	a2, a7, a6

	do_addx8 a7, a4, a2, a7
	extui	a6, a3, 3, 1
	movnez	a2, a7, a6

	bgeui	a3, 16, .Lmult_main_loop
	neg	a3, a2
	movltz	a2, a3, a5
	abi_ret_default

	.align	4
.Lmult_main_loop:
	srli	a3, a3, 4
	slli	a4, a4, 4

	add	a7, a4, a2
	extui	a6, a3, 0, 1
	movnez	a2, a7, a6

	do_addx2 a7, a4, a2, a7
	extui	a6, a3, 1, 1
	movnez	a2, a7, a6

	do_addx4 a7, a4, a2, a7
	extui	a6, a3, 2, 1
	movnez	a2, a7, a6

	do_addx8 a7, a4, a2, a7
	extui	a6, a3, 3, 1
	movnez	a2, a7, a6

	bgeui	a3, 16, .Lmult_main_loop

	neg	a3, a2
	movltz	a2, a3, a5

#endif /* !MUL32 && !MUL16 && !MAC16 */

	abi_ret_default

ENDPROC(__mulsi3)
EXPORT_SYMBOL(__mulsi3)
