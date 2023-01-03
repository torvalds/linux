/* SPDX-License-Identifier: GPL-2.0-or-later WITH GCC-exception-2.0 */
#include <linux/linkage.h>
#include <asm/asmmacro.h>
#include <asm/core.h>

#if !XCHAL_HAVE_MUL16 && !XCHAL_HAVE_MUL32 && !XCHAL_HAVE_MAC16
#define XCHAL_NO_MUL 1
#endif

ENTRY(__umulsidi3)

#ifdef __XTENSA_CALL0_ABI__
	abi_entry(32)
	s32i	a12, sp, 16
	s32i	a13, sp, 20
	s32i	a14, sp, 24
	s32i	a15, sp, 28
#elif XCHAL_NO_MUL
	/* This is not really a leaf function; allocate enough stack space
	   to allow CALL12s to a helper function.  */
	abi_entry(32)
#else
	abi_entry_default
#endif

#ifdef __XTENSA_EB__
#define wh a2
#define wl a3
#else
#define wh a3
#define wl a2
#endif /* __XTENSA_EB__ */

	/* This code is taken from the mulsf3 routine in ieee754-sf.S.
	   See more comments there.  */

#if XCHAL_HAVE_MUL32_HIGH
	mull	a6, a2, a3
	muluh	wh, a2, a3
	mov	wl, a6

#else /* ! MUL32_HIGH */

#if defined(__XTENSA_CALL0_ABI__) && XCHAL_NO_MUL
	/* a0 and a8 will be clobbered by calling the multiply function
	   but a8 is not used here and need not be saved.  */
	s32i	a0, sp, 0
#endif

#if XCHAL_HAVE_MUL16 || XCHAL_HAVE_MUL32

#define a2h a4
#define a3h a5

	/* Get the high halves of the inputs into registers.  */
	srli	a2h, a2, 16
	srli	a3h, a3, 16

#define a2l a2
#define a3l a3

#if XCHAL_HAVE_MUL32 && !XCHAL_HAVE_MUL16
	/* Clear the high halves of the inputs.  This does not matter
	   for MUL16 because the high bits are ignored.  */
	extui	a2, a2, 0, 16
	extui	a3, a3, 0, 16
#endif
#endif /* MUL16 || MUL32 */


#if XCHAL_HAVE_MUL16

#define do_mul(dst, xreg, xhalf, yreg, yhalf) \
	mul16u	dst, xreg ## xhalf, yreg ## yhalf

#elif XCHAL_HAVE_MUL32

#define do_mul(dst, xreg, xhalf, yreg, yhalf) \
	mull	dst, xreg ## xhalf, yreg ## yhalf

#elif XCHAL_HAVE_MAC16

/* The preprocessor insists on inserting a space when concatenating after
   a period in the definition of do_mul below.  These macros are a workaround
   using underscores instead of periods when doing the concatenation.  */
#define umul_aa_ll umul.aa.ll
#define umul_aa_lh umul.aa.lh
#define umul_aa_hl umul.aa.hl
#define umul_aa_hh umul.aa.hh

#define do_mul(dst, xreg, xhalf, yreg, yhalf) \
	umul_aa_ ## xhalf ## yhalf	xreg, yreg; \
	rsr	dst, ACCLO

#else /* no multiply hardware */

#define set_arg_l(dst, src) \
	extui	dst, src, 0, 16
#define set_arg_h(dst, src) \
	srli	dst, src, 16

#ifdef __XTENSA_CALL0_ABI__
#define do_mul(dst, xreg, xhalf, yreg, yhalf) \
	set_arg_ ## xhalf (a13, xreg); \
	set_arg_ ## yhalf (a14, yreg); \
	call0	.Lmul_mulsi3; \
	mov	dst, a12
#else
#define do_mul(dst, xreg, xhalf, yreg, yhalf) \
	set_arg_ ## xhalf (a14, xreg); \
	set_arg_ ## yhalf (a15, yreg); \
	call12	.Lmul_mulsi3; \
	mov	dst, a14
#endif /* __XTENSA_CALL0_ABI__ */

#endif /* no multiply hardware */

	/* Add pp1 and pp2 into a6 with carry-out in a9.  */
	do_mul(a6, a2, l, a3, h)	/* pp 1 */
	do_mul(a11, a2, h, a3, l)	/* pp 2 */
	movi	a9, 0
	add	a6, a6, a11
	bgeu	a6, a11, 1f
	addi	a9, a9, 1
1:
	/* Shift the high half of a9/a6 into position in a9.  Note that
	   this value can be safely incremented without any carry-outs.  */
	ssai	16
	src	a9, a9, a6

	/* Compute the low word into a6.  */
	do_mul(a11, a2, l, a3, l)	/* pp 0 */
	sll	a6, a6
	add	a6, a6, a11
	bgeu	a6, a11, 1f
	addi	a9, a9, 1
1:
	/* Compute the high word into wh.  */
	do_mul(wh, a2, h, a3, h)	/* pp 3 */
	add	wh, wh, a9
	mov	wl, a6

#endif /* !MUL32_HIGH */

#if defined(__XTENSA_CALL0_ABI__) && XCHAL_NO_MUL
	/* Restore the original return address.  */
	l32i	a0, sp, 0
#endif
#ifdef __XTENSA_CALL0_ABI__
	l32i	a12, sp, 16
	l32i	a13, sp, 20
	l32i	a14, sp, 24
	l32i	a15, sp, 28
	abi_ret(32)
#else
	abi_ret_default
#endif

#if XCHAL_NO_MUL

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

	/* For Xtensa processors with no multiply hardware, this simplified
	   version of _mulsi3 is used for multiplying 16-bit chunks of
	   the floating-point mantissas.  When using CALL0, this function
	   uses a custom ABI: the inputs are passed in a13 and a14, the
	   result is returned in a12, and a8 and a15 are clobbered.  */
	.align	4
.Lmul_mulsi3:
	abi_entry_default

	.macro mul_mulsi3_body dst, src1, src2, tmp1, tmp2
	movi	\dst, 0
1:	add	\tmp1, \src2, \dst
	extui	\tmp2, \src1, 0, 1
	movnez	\dst, \tmp1, \tmp2

	do_addx2 \tmp1, \src2, \dst, \tmp1
	extui	\tmp2, \src1, 1, 1
	movnez	\dst, \tmp1, \tmp2

	do_addx4 \tmp1, \src2, \dst, \tmp1
	extui	\tmp2, \src1, 2, 1
	movnez	\dst, \tmp1, \tmp2

	do_addx8 \tmp1, \src2, \dst, \tmp1
	extui	\tmp2, \src1, 3, 1
	movnez	\dst, \tmp1, \tmp2

	srli	\src1, \src1, 4
	slli	\src2, \src2, 4
	bnez	\src1, 1b
	.endm

#ifdef __XTENSA_CALL0_ABI__
	mul_mulsi3_body a12, a13, a14, a15, a8
#else
	/* The result will be written into a2, so save that argument in a4.  */
	mov	a4, a2
	mul_mulsi3_body a2, a4, a3, a5, a6
#endif
	abi_ret_default
#endif /* XCHAL_NO_MUL */

ENDPROC(__umulsidi3)
