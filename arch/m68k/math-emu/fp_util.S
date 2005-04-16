/*
 * fp_util.S
 *
 * Copyright Roman Zippel, 1997.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, and the entire permission notice in its entirety,
 *    including the disclaimer of warranties.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior
 *    written permission.
 *
 * ALTERNATIVELY, this product may be distributed under the terms of
 * the GNU General Public License, in which case the provisions of the GPL are
 * required INSTEAD OF the above restrictions.  (This clause is
 * necessary due to a potential bad interaction between the GPL and
 * the restrictions contained in a BSD-style copyright.)
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/config.h>
#include "fp_emu.h"

/*
 * Here are lots of conversion and normalization functions mainly
 * used by fp_scan.S
 * Note that these functions are optimized for "normal" numbers,
 * these are handled first and exit as fast as possible, this is
 * especially important for fp_normalize_ext/fp_conv_ext2ext, as
 * it's called very often.
 * The register usage is optimized for fp_scan.S and which register
 * is currently at that time unused, be careful if you want change
 * something here. %d0 and %d1 is always usable, sometimes %d2 (or
 * only the lower half) most function have to return the %a0
 * unmodified, so that the caller can immediately reuse it.
 */

	.globl	fp_ill, fp_end

	| exits from fp_scan:
	| illegal instruction
fp_ill:
	printf	,"fp_illegal\n"
	rts
	| completed instruction
fp_end:
	tst.l	(TASK_MM-8,%a2)
	jmi	1f
	tst.l	(TASK_MM-4,%a2)
	jmi	1f
	tst.l	(TASK_MM,%a2)
	jpl	2f
1:	printf	,"oops:%p,%p,%p\n",3,%a2@(TASK_MM-8),%a2@(TASK_MM-4),%a2@(TASK_MM)
2:	clr.l	%d0
	rts

	.globl	fp_conv_long2ext, fp_conv_single2ext
	.globl	fp_conv_double2ext, fp_conv_ext2ext
	.globl	fp_normalize_ext, fp_normalize_double
	.globl	fp_normalize_single, fp_normalize_single_fast
	.globl	fp_conv_ext2double, fp_conv_ext2single
	.globl	fp_conv_ext2long, fp_conv_ext2short
	.globl	fp_conv_ext2byte
	.globl	fp_finalrounding_single, fp_finalrounding_single_fast
	.globl	fp_finalrounding_double
	.globl	fp_finalrounding, fp_finaltest, fp_final

/*
 * First several conversion functions from a source operand
 * into the extended format. Note, that only fp_conv_ext2ext
 * normalizes the number and is always called after the other
 * conversion functions, which only move the information into
 * fp_ext structure.
 */

	| fp_conv_long2ext:
	|
	| args:	%d0 = source (32-bit long)
	|	%a0 = destination (ptr to struct fp_ext)

fp_conv_long2ext:
	printf	PCONV,"l2e: %p -> %p(",2,%d0,%a0
	clr.l	%d1			| sign defaults to zero
	tst.l	%d0
	jeq	fp_l2e_zero		| is source zero?
	jpl	1f			| positive?
	moveq	#1,%d1
	neg.l	%d0
1:	swap	%d1
	move.w	#0x3fff+31,%d1
	move.l	%d1,(%a0)+		| set sign / exp
	move.l	%d0,(%a0)+		| set mantissa
	clr.l	(%a0)
	subq.l	#8,%a0			| restore %a0
	printx	PCONV,%a0@
	printf	PCONV,")\n"
	rts
	| source is zero
fp_l2e_zero:
	clr.l	(%a0)+
	clr.l	(%a0)+
	clr.l	(%a0)
	subq.l	#8,%a0
	printx	PCONV,%a0@
	printf	PCONV,")\n"
	rts

	| fp_conv_single2ext
	| args:	%d0 = source (single-precision fp value)
	|	%a0 = dest (struct fp_ext *)

fp_conv_single2ext:
	printf	PCONV,"s2e: %p -> %p(",2,%d0,%a0
	move.l	%d0,%d1
	lsl.l	#8,%d0			| shift mantissa
	lsr.l	#8,%d1			| exponent / sign
	lsr.l	#7,%d1
	lsr.w	#8,%d1
	jeq	fp_s2e_small		| zero / denormal?
	cmp.w	#0xff,%d1		| NaN / Inf?
	jeq	fp_s2e_large
	bset	#31,%d0			| set explizit bit
	add.w	#0x3fff-0x7f,%d1	| re-bias the exponent.
9:	move.l	%d1,(%a0)+		| fp_ext.sign, fp_ext.exp
	move.l	%d0,(%a0)+		| high lword of fp_ext.mant
	clr.l	(%a0)			| low lword = 0
	subq.l	#8,%a0
	printx	PCONV,%a0@
	printf	PCONV,")\n"
	rts
	| zeros and denormalized
fp_s2e_small:
	| exponent is zero, so explizit bit is already zero too
	tst.l	%d0
	jeq	9b
	move.w	#0x4000-0x7f,%d1
	jra	9b
	| infinities and NAN
fp_s2e_large:
	bclr	#31,%d0			| clear explizit bit
	move.w	#0x7fff,%d1
	jra	9b

fp_conv_double2ext:
#ifdef FPU_EMU_DEBUG
	getuser.l %a1@(0),%d0,fp_err_ua2,%a1
	getuser.l %a1@(4),%d1,fp_err_ua2,%a1
	printf	PCONV,"d2e: %p%p -> %p(",3,%d0,%d1,%a0
#endif
	getuser.l (%a1)+,%d0,fp_err_ua2,%a1
	move.l	%d0,%d1
	lsl.l	#8,%d0			| shift high mantissa
	lsl.l	#3,%d0
	lsr.l	#8,%d1			| exponent / sign
	lsr.l	#7,%d1
	lsr.w	#5,%d1
	jeq	fp_d2e_small		| zero / denormal?
	cmp.w	#0x7ff,%d1		| NaN / Inf?
	jeq	fp_d2e_large
	bset	#31,%d0			| set explizit bit
	add.w	#0x3fff-0x3ff,%d1	| re-bias the exponent.
9:	move.l	%d1,(%a0)+		| fp_ext.sign, fp_ext.exp
	move.l	%d0,(%a0)+
	getuser.l (%a1)+,%d0,fp_err_ua2,%a1
	move.l	%d0,%d1
	lsl.l	#8,%d0
	lsl.l	#3,%d0
	move.l	%d0,(%a0)
	moveq	#21,%d0
	lsr.l	%d0,%d1
	or.l	%d1,-(%a0)
	subq.l	#4,%a0
	printx	PCONV,%a0@
	printf	PCONV,")\n"
	rts
	| zeros and denormalized
fp_d2e_small:
	| exponent is zero, so explizit bit is already zero too
	tst.l	%d0
	jeq	9b
	move.w	#0x4000-0x3ff,%d1
	jra	9b
	| infinities and NAN
fp_d2e_large:
	bclr	#31,%d0			| clear explizit bit
	move.w	#0x7fff,%d1
	jra	9b

	| fp_conv_ext2ext:
	| originally used to get longdouble from userspace, now it's
	| called before arithmetic operations to make sure the number
	| is normalized [maybe rename it?].
	| args:	%a0 = dest (struct fp_ext *)
	| returns 0 in %d0 for a NaN, otherwise 1

fp_conv_ext2ext:
	printf	PCONV,"e2e: %p(",1,%a0
	printx	PCONV,%a0@
	printf	PCONV,"), "
	move.l	(%a0)+,%d0
	cmp.w	#0x7fff,%d0		| Inf / NaN?
	jeq	fp_e2e_large
	move.l	(%a0),%d0
	jpl	fp_e2e_small		| zero / denorm?
	| The high bit is set, so normalization is irrelevant.
fp_e2e_checkround:
	subq.l	#4,%a0
#ifdef CONFIG_M68KFPU_EMU_EXTRAPREC
	move.b	(%a0),%d0
	jne	fp_e2e_round
#endif
	printf	PCONV,"%p(",1,%a0
	printx	PCONV,%a0@
	printf	PCONV,")\n"
	moveq	#1,%d0
	rts
#ifdef CONFIG_M68KFPU_EMU_EXTRAPREC
fp_e2e_round:
	fp_set_sr FPSR_EXC_INEX2
	clr.b	(%a0)
	move.w	(FPD_RND,FPDATA),%d2
	jne	fp_e2e_roundother	| %d2 == 0, round to nearest
	tst.b	%d0			| test guard bit
	jpl	9f			| zero is closer
	btst	#0,(11,%a0)		| test lsb bit
	jne	fp_e2e_doroundup	| round to infinity
	lsl.b	#1,%d0			| check low bits
	jeq	9f			| round to zero
fp_e2e_doroundup:
	addq.l	#1,(8,%a0)
	jcc	9f
	addq.l	#1,(4,%a0)
	jcc	9f
	move.w	#0x8000,(4,%a0)
	addq.w	#1,(2,%a0)
9:	printf	PNORM,"%p(",1,%a0
	printx	PNORM,%a0@
	printf	PNORM,")\n"
	rts
fp_e2e_roundother:
	subq.w	#2,%d2
	jcs	9b			| %d2 < 2, round to zero
	jhi	1f			| %d2 > 2, round to +infinity
	tst.b	(1,%a0)			| to -inf
	jne	fp_e2e_doroundup	| negative, round to infinity
	jra	9b			| positive, round to zero
1:	tst.b	(1,%a0)			| to +inf
	jeq	fp_e2e_doroundup	| positive, round to infinity
	jra	9b			| negative, round to zero
#endif
	| zeros and subnormals:
	| try to normalize these anyway.
fp_e2e_small:
	jne	fp_e2e_small1		| high lword zero?
	move.l	(4,%a0),%d0
	jne	fp_e2e_small2
#ifdef CONFIG_M68KFPU_EMU_EXTRAPREC
	clr.l	%d0
	move.b	(-4,%a0),%d0
	jne	fp_e2e_small3
#endif
	| Genuine zero.
	clr.w	-(%a0)
	subq.l	#2,%a0
	printf	PNORM,"%p(",1,%a0
	printx	PNORM,%a0@
	printf	PNORM,")\n"
	moveq	#1,%d0
	rts
	| definitely subnormal, need to shift all 64 bits
fp_e2e_small1:
	bfffo	%d0{#0,#32},%d1
	move.w	-(%a0),%d2
	sub.w	%d1,%d2
	jcc	1f
	| Pathologically small, denormalize.
	add.w	%d2,%d1
	clr.w	%d2
1:	move.w	%d2,(%a0)+
	move.w	%d1,%d2
	jeq	fp_e2e_checkround
	| fancy 64-bit double-shift begins here
	lsl.l	%d2,%d0
	move.l	%d0,(%a0)+
	move.l	(%a0),%d0
	move.l	%d0,%d1
	lsl.l	%d2,%d0
	move.l	%d0,(%a0)
	neg.w	%d2
	and.w	#0x1f,%d2
	lsr.l	%d2,%d1
	or.l	%d1,-(%a0)
#ifdef CONFIG_M68KFPU_EMU_EXTRAPREC
fp_e2e_extra1:
	clr.l	%d0
	move.b	(-4,%a0),%d0
	neg.w	%d2
	add.w	#24,%d2
	jcc	1f
	clr.b	(-4,%a0)
	lsl.l	%d2,%d0
	or.l	%d0,(4,%a0)
	jra	fp_e2e_checkround
1:	addq.w	#8,%d2
	lsl.l	%d2,%d0
	move.b	%d0,(-4,%a0)
	lsr.l	#8,%d0
	or.l	%d0,(4,%a0)
#endif
	jra	fp_e2e_checkround
	| pathologically small subnormal
fp_e2e_small2:
	bfffo	%d0{#0,#32},%d1
	add.w	#32,%d1
	move.w	-(%a0),%d2
	sub.w	%d1,%d2
	jcc	1f
	| Beyond pathologically small, denormalize.
	add.w	%d2,%d1
	clr.w	%d2
1:	move.w	%d2,(%a0)+
	ext.l	%d1
	jeq	fp_e2e_checkround
	clr.l	(4,%a0)
	sub.w	#32,%d2
	jcs	1f
	lsl.l	%d1,%d0			| lower lword needs only to be shifted
	move.l	%d0,(%a0)		| into the higher lword
#ifdef CONFIG_M68KFPU_EMU_EXTRAPREC
	clr.l	%d0
	move.b	(-4,%a0),%d0
	clr.b	(-4,%a0)
	neg.w	%d1
	add.w	#32,%d1
	bfins	%d0,(%a0){%d1,#8}
#endif
	jra	fp_e2e_checkround
1:	neg.w	%d1			| lower lword is splitted between
	bfins	%d0,(%a0){%d1,#32}	| higher and lower lword
#ifndef CONFIG_M68KFPU_EMU_EXTRAPREC
	jra	fp_e2e_checkround
#else
	move.w	%d1,%d2
	jra	fp_e2e_extra1
	| These are extremely small numbers, that will mostly end up as zero
	| anyway, so this is only important for correct rounding.
fp_e2e_small3:
	bfffo	%d0{#24,#8},%d1
	add.w	#40,%d1
	move.w	-(%a0),%d2
	sub.w	%d1,%d2
	jcc	1f
	| Pathologically small, denormalize.
	add.w	%d2,%d1
	clr.w	%d2
1:	move.w	%d2,(%a0)+
	ext.l	%d1
	jeq	fp_e2e_checkround
	cmp.w	#8,%d1
	jcs	2f
1:	clr.b	(-4,%a0)
	sub.w	#64,%d1
	jcs	1f
	add.w	#24,%d1
	lsl.l	%d1,%d0
	move.l	%d0,(%a0)
	jra	fp_e2e_checkround
1:	neg.w	%d1
	bfins	%d0,(%a0){%d1,#8}
	jra	fp_e2e_checkround
2:	lsl.l	%d1,%d0
	move.b	%d0,(-4,%a0)
	lsr.l	#8,%d0
	move.b	%d0,(7,%a0)
	jra	fp_e2e_checkround
#endif
1:	move.l	%d0,%d1			| lower lword is splitted between
	lsl.l	%d2,%d0			| higher and lower lword
	move.l	%d0,(%a0)
	move.l	%d1,%d0
	neg.w	%d2
	add.w	#32,%d2
	lsr.l	%d2,%d0
	move.l	%d0,-(%a0)
	jra	fp_e2e_checkround
	| Infinities and NaNs
fp_e2e_large:
	move.l	(%a0)+,%d0
	jne	3f
1:	tst.l	(%a0)
	jne	4f
	moveq	#1,%d0
2:	subq.l	#8,%a0
	printf	PCONV,"%p(",1,%a0
	printx	PCONV,%a0@
	printf	PCONV,")\n"
	rts
	| we have maybe a NaN, shift off the highest bit
3:	lsl.l	#1,%d0
	jeq	1b
	| we have a NaN, clear the return value
4:	clrl	%d0
	jra	2b


/*
 * Normalization functions.  Call these on the output of general
 * FP operators, and before any conversion into the destination
 * formats. fp_normalize_ext has always to be called first, the
 * following conversion functions expect an already normalized
 * number.
 */

	| fp_normalize_ext:
	| normalize an extended in extended (unpacked) format, basically
	| it does the same as fp_conv_ext2ext, additionally it also does
	| the necessary postprocessing checks.
	| args:	%a0 (struct fp_ext *)
	| NOTE: it does _not_ modify %a0/%a1 and the upper word of %d2

fp_normalize_ext:
	printf	PNORM,"ne: %p(",1,%a0
	printx	PNORM,%a0@
	printf	PNORM,"), "
	move.l	(%a0)+,%d0
	cmp.w	#0x7fff,%d0		| Inf / NaN?
	jeq	fp_ne_large
	move.l	(%a0),%d0
	jpl	fp_ne_small		| zero / denorm?
	| The high bit is set, so normalization is irrelevant.
fp_ne_checkround:
	subq.l	#4,%a0
#ifdef CONFIG_M68KFPU_EMU_EXTRAPREC
	move.b	(%a0),%d0
	jne	fp_ne_round
#endif
	printf	PNORM,"%p(",1,%a0
	printx	PNORM,%a0@
	printf	PNORM,")\n"
	rts
#ifdef CONFIG_M68KFPU_EMU_EXTRAPREC
fp_ne_round:
	fp_set_sr FPSR_EXC_INEX2
	clr.b	(%a0)
	move.w	(FPD_RND,FPDATA),%d2
	jne	fp_ne_roundother	| %d2 == 0, round to nearest
	tst.b	%d0			| test guard bit
	jpl	9f			| zero is closer
	btst	#0,(11,%a0)		| test lsb bit
	jne	fp_ne_doroundup		| round to infinity
	lsl.b	#1,%d0			| check low bits
	jeq	9f			| round to zero
fp_ne_doroundup:
	addq.l	#1,(8,%a0)
	jcc	9f
	addq.l	#1,(4,%a0)
	jcc	9f
	addq.w	#1,(2,%a0)
	move.w	#0x8000,(4,%a0)
9:	printf	PNORM,"%p(",1,%a0
	printx	PNORM,%a0@
	printf	PNORM,")\n"
	rts
fp_ne_roundother:
	subq.w	#2,%d2
	jcs	9b			| %d2 < 2, round to zero
	jhi	1f			| %d2 > 2, round to +infinity
	tst.b	(1,%a0)			| to -inf
	jne	fp_ne_doroundup		| negative, round to infinity
	jra	9b			| positive, round to zero
1:	tst.b	(1,%a0)			| to +inf
	jeq	fp_ne_doroundup		| positive, round to infinity
	jra	9b			| negative, round to zero
#endif
	| Zeros and subnormal numbers
	| These are probably merely subnormal, rather than "denormalized"
	|  numbers, so we will try to make them normal again.
fp_ne_small:
	jne	fp_ne_small1		| high lword zero?
	move.l	(4,%a0),%d0
	jne	fp_ne_small2
#ifdef CONFIG_M68KFPU_EMU_EXTRAPREC
	clr.l	%d0
	move.b	(-4,%a0),%d0
	jne	fp_ne_small3
#endif
	| Genuine zero.
	clr.w	-(%a0)
	subq.l	#2,%a0
	printf	PNORM,"%p(",1,%a0
	printx	PNORM,%a0@
	printf	PNORM,")\n"
	rts
	| Subnormal.
fp_ne_small1:
	bfffo	%d0{#0,#32},%d1
	move.w	-(%a0),%d2
	sub.w	%d1,%d2
	jcc	1f
	| Pathologically small, denormalize.
	add.w	%d2,%d1
	clr.w	%d2
	fp_set_sr FPSR_EXC_UNFL
1:	move.w	%d2,(%a0)+
	move.w	%d1,%d2
	jeq	fp_ne_checkround
	| This is exactly the same 64-bit double shift as seen above.
	lsl.l	%d2,%d0
	move.l	%d0,(%a0)+
	move.l	(%a0),%d0
	move.l	%d0,%d1
	lsl.l	%d2,%d0
	move.l	%d0,(%a0)
	neg.w	%d2
	and.w	#0x1f,%d2
	lsr.l	%d2,%d1
	or.l	%d1,-(%a0)
#ifdef CONFIG_M68KFPU_EMU_EXTRAPREC
fp_ne_extra1:
	clr.l	%d0
	move.b	(-4,%a0),%d0
	neg.w	%d2
	add.w	#24,%d2
	jcc	1f
	clr.b	(-4,%a0)
	lsl.l	%d2,%d0
	or.l	%d0,(4,%a0)
	jra	fp_ne_checkround
1:	addq.w	#8,%d2
	lsl.l	%d2,%d0
	move.b	%d0,(-4,%a0)
	lsr.l	#8,%d0
	or.l	%d0,(4,%a0)
#endif
	jra	fp_ne_checkround
	| May or may not be subnormal, if so, only 32 bits to shift.
fp_ne_small2:
	bfffo	%d0{#0,#32},%d1
	add.w	#32,%d1
	move.w	-(%a0),%d2
	sub.w	%d1,%d2
	jcc	1f
	| Beyond pathologically small, denormalize.
	add.w	%d2,%d1
	clr.w	%d2
	fp_set_sr FPSR_EXC_UNFL
1:	move.w	%d2,(%a0)+
	ext.l	%d1
	jeq	fp_ne_checkround
	clr.l	(4,%a0)
	sub.w	#32,%d1
	jcs	1f
	lsl.l	%d1,%d0			| lower lword needs only to be shifted
	move.l	%d0,(%a0)		| into the higher lword
#ifdef CONFIG_M68KFPU_EMU_EXTRAPREC
	clr.l	%d0
	move.b	(-4,%a0),%d0
	clr.b	(-4,%a0)
	neg.w	%d1
	add.w	#32,%d1
	bfins	%d0,(%a0){%d1,#8}
#endif
	jra	fp_ne_checkround
1:	neg.w	%d1			| lower lword is splitted between
	bfins	%d0,(%a0){%d1,#32}	| higher and lower lword
#ifndef CONFIG_M68KFPU_EMU_EXTRAPREC
	jra	fp_ne_checkround
#else
	move.w	%d1,%d2
	jra	fp_ne_extra1
	| These are extremely small numbers, that will mostly end up as zero
	| anyway, so this is only important for correct rounding.
fp_ne_small3:
	bfffo	%d0{#24,#8},%d1
	add.w	#40,%d1
	move.w	-(%a0),%d2
	sub.w	%d1,%d2
	jcc	1f
	| Pathologically small, denormalize.
	add.w	%d2,%d1
	clr.w	%d2
1:	move.w	%d2,(%a0)+
	ext.l	%d1
	jeq	fp_ne_checkround
	cmp.w	#8,%d1
	jcs	2f
1:	clr.b	(-4,%a0)
	sub.w	#64,%d1
	jcs	1f
	add.w	#24,%d1
	lsl.l	%d1,%d0
	move.l	%d0,(%a0)
	jra	fp_ne_checkround
1:	neg.w	%d1
	bfins	%d0,(%a0){%d1,#8}
	jra	fp_ne_checkround
2:	lsl.l	%d1,%d0
	move.b	%d0,(-4,%a0)
	lsr.l	#8,%d0
	move.b	%d0,(7,%a0)
	jra	fp_ne_checkround
#endif
	| Infinities and NaNs, again, same as above.
fp_ne_large:
	move.l	(%a0)+,%d0
	jne	3f
1:	tst.l	(%a0)
	jne	4f
2:	subq.l	#8,%a0
	printf	PNORM,"%p(",1,%a0
	printx	PNORM,%a0@
	printf	PNORM,")\n"
	rts
	| we have maybe a NaN, shift off the highest bit
3:	move.l	%d0,%d1
	lsl.l	#1,%d1
	jne	4f
	clr.l	(-4,%a0)
	jra	1b
	| we have a NaN, test if it is signaling
4:	bset	#30,%d0
	jne	2b
	fp_set_sr FPSR_EXC_SNAN
	move.l	%d0,(-4,%a0)
	jra	2b

	| these next two do rounding as per the IEEE standard.
	| values for the rounding modes appear to be:
	| 0:	Round to nearest
	| 1:	Round to zero
	| 2:	Round to -Infinity
	| 3:	Round to +Infinity
	| both functions expect that fp_normalize was already
	| called (and extended argument is already normalized
	| as far as possible), these are used if there is different
	| rounding precision is selected and before converting
	| into single/double

	| fp_normalize_double:
	| normalize an extended with double (52-bit) precision
	| args:	 %a0 (struct fp_ext *)

fp_normalize_double:
	printf	PNORM,"nd: %p(",1,%a0
	printx	PNORM,%a0@
	printf	PNORM,"), "
	move.l	(%a0)+,%d2
	tst.w	%d2
	jeq	fp_nd_zero		| zero / denormalized
	cmp.w	#0x7fff,%d2
	jeq	fp_nd_huge		| NaN / infinitive.
	sub.w	#0x4000-0x3ff,%d2	| will the exponent fit?
	jcs	fp_nd_small		| too small.
	cmp.w	#0x7fe,%d2
	jcc	fp_nd_large		| too big.
	addq.l	#4,%a0
	move.l	(%a0),%d0		| low lword of mantissa
	| now, round off the low 11 bits.
fp_nd_round:
	moveq	#21,%d1
	lsl.l	%d1,%d0			| keep 11 low bits.
	jne	fp_nd_checkround	| Are they non-zero?
	| nothing to do here
9:	subq.l	#8,%a0
	printf	PNORM,"%p(",1,%a0
	printx	PNORM,%a0@
	printf	PNORM,")\n"
	rts
	| Be careful with the X bit! It contains the lsb
	| from the shift above, it is needed for round to nearest.
fp_nd_checkround:
	fp_set_sr FPSR_EXC_INEX2	| INEX2 bit
	and.w	#0xf800,(2,%a0)		| clear bits 0-10
	move.w	(FPD_RND,FPDATA),%d2	| rounding mode
	jne	2f			| %d2 == 0, round to nearest
	tst.l	%d0			| test guard bit
	jpl	9b			| zero is closer
	| here we test the X bit by adding it to %d2
	clr.w	%d2			| first set z bit, addx only clears it
	addx.w	%d2,%d2			| test lsb bit
	| IEEE754-specified "round to even" behaviour.  If the guard
	| bit is set, then the number is odd, so rounding works like
	| in grade-school arithmetic (i.e. 1.5 rounds to 2.0)
	| Otherwise, an equal distance rounds towards zero, so as not
	| to produce an odd number.  This is strange, but it is what
	| the standard says.
	jne	fp_nd_doroundup		| round to infinity
	lsl.l	#1,%d0			| check low bits
	jeq	9b			| round to zero
fp_nd_doroundup:
	| round (the mantissa, that is) towards infinity
	add.l	#0x800,(%a0)
	jcc	9b			| no overflow, good.
	addq.l	#1,-(%a0)		| extend to high lword
	jcc	1f			| no overflow, good.
	| Yow! we have managed to overflow the mantissa.  Since this
	| only happens when %d1 was 0xfffff800, it is now zero, so
	| reset the high bit, and increment the exponent.
	move.w	#0x8000,(%a0)
	addq.w	#1,-(%a0)
	cmp.w	#0x43ff,(%a0)+		| exponent now overflown?
	jeq	fp_nd_large		| yes, so make it infinity.
1:	subq.l	#4,%a0
	printf	PNORM,"%p(",1,%a0
	printx	PNORM,%a0@
	printf	PNORM,")\n"
	rts
2:	subq.w	#2,%d2
	jcs	9b			| %d2 < 2, round to zero
	jhi	3f			| %d2 > 2, round to +infinity
	| Round to +Inf or -Inf.  High word of %d2 contains the
	| sign of the number, by the way.
	swap	%d2			| to -inf
	tst.b	%d2
	jne	fp_nd_doroundup		| negative, round to infinity
	jra	9b			| positive, round to zero
3:	swap	%d2			| to +inf
	tst.b	%d2
	jeq	fp_nd_doroundup		| positive, round to infinity
	jra	9b			| negative, round to zero
	| Exponent underflow.  Try to make a denormal, and set it to
	| the smallest possible fraction if this fails.
fp_nd_small:
	fp_set_sr FPSR_EXC_UNFL		| set UNFL bit
	move.w	#0x3c01,(-2,%a0)	| 2**-1022
	neg.w	%d2			| degree of underflow
	cmp.w	#32,%d2			| single or double shift?
	jcc	1f
	| Again, another 64-bit double shift.
	move.l	(%a0),%d0
	move.l	%d0,%d1
	lsr.l	%d2,%d0
	move.l	%d0,(%a0)+
	move.l	(%a0),%d0
	lsr.l	%d2,%d0
	neg.w	%d2
	add.w	#32,%d2
	lsl.l	%d2,%d1
	or.l	%d1,%d0
	move.l	(%a0),%d1
	move.l	%d0,(%a0)
	| Check to see if we shifted off any significant bits
	lsl.l	%d2,%d1
	jeq	fp_nd_round		| Nope, round.
	bset	#0,%d0			| Yes, so set the "sticky bit".
	jra	fp_nd_round		| Now, round.
	| Another 64-bit single shift and store
1:	sub.w	#32,%d2
	cmp.w	#32,%d2			| Do we really need to shift?
	jcc	2f			| No, the number is too small.
	move.l	(%a0),%d0
	clr.l	(%a0)+
	move.l	%d0,%d1
	lsr.l	%d2,%d0
	neg.w	%d2
	add.w	#32,%d2
	| Again, check to see if we shifted off any significant bits.
	tst.l	(%a0)
	jeq	1f
	bset	#0,%d0			| Sticky bit.
1:	move.l	%d0,(%a0)
	lsl.l	%d2,%d1
	jeq	fp_nd_round
	bset	#0,%d0
	jra	fp_nd_round
	| Sorry, the number is just too small.
2:	clr.l	(%a0)+
	clr.l	(%a0)
	moveq	#1,%d0			| Smallest possible fraction,
	jra	fp_nd_round		| round as desired.
	| zero and denormalized
fp_nd_zero:
	tst.l	(%a0)+
	jne	1f
	tst.l	(%a0)
	jne	1f
	subq.l	#8,%a0
	printf	PNORM,"%p(",1,%a0
	printx	PNORM,%a0@
	printf	PNORM,")\n"
	rts				| zero.  nothing to do.
	| These are not merely subnormal numbers, but true denormals,
	| i.e. pathologically small (exponent is 2**-16383) numbers.
	| It is clearly impossible for even a normal extended number
	| with that exponent to fit into double precision, so just
	| write these ones off as "too darn small".
1:	fp_set_sr FPSR_EXC_UNFL		| Set UNFL bit
	clr.l	(%a0)
	clr.l	-(%a0)
	move.w	#0x3c01,-(%a0)		| i.e. 2**-1022
	addq.l	#6,%a0
	moveq	#1,%d0
	jra	fp_nd_round		| round.
	| Exponent overflow.  Just call it infinity.
fp_nd_large:
	move.w	#0x7ff,%d0
	and.w	(6,%a0),%d0
	jeq	1f
	fp_set_sr FPSR_EXC_INEX2
1:	fp_set_sr FPSR_EXC_OVFL
	move.w	(FPD_RND,FPDATA),%d2
	jne	3f			| %d2 = 0 round to nearest
1:	move.w	#0x7fff,(-2,%a0)
	clr.l	(%a0)+
	clr.l	(%a0)
2:	subq.l	#8,%a0
	printf	PNORM,"%p(",1,%a0
	printx	PNORM,%a0@
	printf	PNORM,")\n"
	rts
3:	subq.w	#2,%d2
	jcs	5f			| %d2 < 2, round to zero
	jhi	4f			| %d2 > 2, round to +infinity
	tst.b	(-3,%a0)		| to -inf
	jne	1b
	jra	5f
4:	tst.b	(-3,%a0)		| to +inf
	jeq	1b
5:	move.w	#0x43fe,(-2,%a0)
	moveq	#-1,%d0
	move.l	%d0,(%a0)+
	move.w	#0xf800,%d0
	move.l	%d0,(%a0)
	jra	2b
	| Infinities or NaNs
fp_nd_huge:
	subq.l	#4,%a0
	printf	PNORM,"%p(",1,%a0
	printx	PNORM,%a0@
	printf	PNORM,")\n"
	rts

	| fp_normalize_single:
	| normalize an extended with single (23-bit) precision
	| args:	 %a0 (struct fp_ext *)

fp_normalize_single:
	printf	PNORM,"ns: %p(",1,%a0
	printx	PNORM,%a0@
	printf	PNORM,") "
	addq.l	#2,%a0
	move.w	(%a0)+,%d2
	jeq	fp_ns_zero		| zero / denormalized
	cmp.w	#0x7fff,%d2
	jeq	fp_ns_huge		| NaN / infinitive.
	sub.w	#0x4000-0x7f,%d2	| will the exponent fit?
	jcs	fp_ns_small		| too small.
	cmp.w	#0xfe,%d2
	jcc	fp_ns_large		| too big.
	move.l	(%a0)+,%d0		| get high lword of mantissa
fp_ns_round:
	tst.l	(%a0)			| check the low lword
	jeq	1f
	| Set a sticky bit if it is non-zero.  This should only
	| affect the rounding in what would otherwise be equal-
	| distance situations, which is what we want it to do.
	bset	#0,%d0
1:	clr.l	(%a0)			| zap it from memory.
	| now, round off the low 8 bits of the hi lword.
	tst.b	%d0			| 8 low bits.
	jne	fp_ns_checkround	| Are they non-zero?
	| nothing to do here
	subq.l	#8,%a0
	printf	PNORM,"%p(",1,%a0
	printx	PNORM,%a0@
	printf	PNORM,")\n"
	rts
fp_ns_checkround:
	fp_set_sr FPSR_EXC_INEX2	| INEX2 bit
	clr.b	-(%a0)			| clear low byte of high lword
	subq.l	#3,%a0
	move.w	(FPD_RND,FPDATA),%d2	| rounding mode
	jne	2f			| %d2 == 0, round to nearest
	tst.b	%d0			| test guard bit
	jpl	9f			| zero is closer
	btst	#8,%d0			| test lsb bit
	| round to even behaviour, see above.
	jne	fp_ns_doroundup		| round to infinity
	lsl.b	#1,%d0			| check low bits
	jeq	9f			| round to zero
fp_ns_doroundup:
	| round (the mantissa, that is) towards infinity
	add.l	#0x100,(%a0)
	jcc	9f			| no overflow, good.
	| Overflow.  This means that the %d1 was 0xffffff00, so it
	| is now zero.  We will set the mantissa to reflect this, and
	| increment the exponent (checking for overflow there too)
	move.w	#0x8000,(%a0)
	addq.w	#1,-(%a0)
	cmp.w	#0x407f,(%a0)+		| exponent now overflown?
	jeq	fp_ns_large		| yes, so make it infinity.
9:	subq.l	#4,%a0
	printf	PNORM,"%p(",1,%a0
	printx	PNORM,%a0@
	printf	PNORM,")\n"
	rts
	| check nondefault rounding modes
2:	subq.w	#2,%d2
	jcs	9b			| %d2 < 2, round to zero
	jhi	3f			| %d2 > 2, round to +infinity
	tst.b	(-3,%a0)		| to -inf
	jne	fp_ns_doroundup		| negative, round to infinity
	jra	9b			| positive, round to zero
3:	tst.b	(-3,%a0)		| to +inf
	jeq	fp_ns_doroundup		| positive, round to infinity
	jra	9b			| negative, round to zero
	| Exponent underflow.  Try to make a denormal, and set it to
	| the smallest possible fraction if this fails.
fp_ns_small:
	fp_set_sr FPSR_EXC_UNFL		| set UNFL bit
	move.w	#0x3f81,(-2,%a0)	| 2**-126
	neg.w	%d2			| degree of underflow
	cmp.w	#32,%d2			| single or double shift?
	jcc	2f
	| a 32-bit shift.
	move.l	(%a0),%d0
	move.l	%d0,%d1
	lsr.l	%d2,%d0
	move.l	%d0,(%a0)+
	| Check to see if we shifted off any significant bits.
	neg.w	%d2
	add.w	#32,%d2
	lsl.l	%d2,%d1
	jeq	1f
	bset	#0,%d0			| Sticky bit.
	| Check the lower lword
1:	tst.l	(%a0)
	jeq	fp_ns_round
	clr	(%a0)
	bset	#0,%d0			| Sticky bit.
	jra	fp_ns_round
	| Sorry, the number is just too small.
2:	clr.l	(%a0)+
	clr.l	(%a0)
	moveq	#1,%d0			| Smallest possible fraction,
	jra	fp_ns_round		| round as desired.
	| Exponent overflow.  Just call it infinity.
fp_ns_large:
	tst.b	(3,%a0)
	jeq	1f
	fp_set_sr FPSR_EXC_INEX2
1:	fp_set_sr FPSR_EXC_OVFL
	move.w	(FPD_RND,FPDATA),%d2
	jne	3f			| %d2 = 0 round to nearest
1:	move.w	#0x7fff,(-2,%a0)
	clr.l	(%a0)+
	clr.l	(%a0)
2:	subq.l	#8,%a0
	printf	PNORM,"%p(",1,%a0
	printx	PNORM,%a0@
	printf	PNORM,")\n"
	rts
3:	subq.w	#2,%d2
	jcs	5f			| %d2 < 2, round to zero
	jhi	4f			| %d2 > 2, round to +infinity
	tst.b	(-3,%a0)		| to -inf
	jne	1b
	jra	5f
4:	tst.b	(-3,%a0)		| to +inf
	jeq	1b
5:	move.w	#0x407e,(-2,%a0)
	move.l	#0xffffff00,(%a0)+
	clr.l	(%a0)
	jra	2b
	| zero and denormalized
fp_ns_zero:
	tst.l	(%a0)+
	jne	1f
	tst.l	(%a0)
	jne	1f
	subq.l	#8,%a0
	printf	PNORM,"%p(",1,%a0
	printx	PNORM,%a0@
	printf	PNORM,")\n"
	rts				| zero.  nothing to do.
	| These are not merely subnormal numbers, but true denormals,
	| i.e. pathologically small (exponent is 2**-16383) numbers.
	| It is clearly impossible for even a normal extended number
	| with that exponent to fit into single precision, so just
	| write these ones off as "too darn small".
1:	fp_set_sr FPSR_EXC_UNFL		| Set UNFL bit
	clr.l	(%a0)
	clr.l	-(%a0)
	move.w	#0x3f81,-(%a0)		| i.e. 2**-126
	addq.l	#6,%a0
	moveq	#1,%d0
	jra	fp_ns_round		| round.
	| Infinities or NaNs
fp_ns_huge:
	subq.l	#4,%a0
	printf	PNORM,"%p(",1,%a0
	printx	PNORM,%a0@
	printf	PNORM,")\n"
	rts

	| fp_normalize_single_fast:
	| normalize an extended with single (23-bit) precision
	| this is only used by fsgldiv/fsgdlmul, where the
	| operand is not completly normalized.
	| args:	 %a0 (struct fp_ext *)

fp_normalize_single_fast:
	printf	PNORM,"nsf: %p(",1,%a0
	printx	PNORM,%a0@
	printf	PNORM,") "
	addq.l	#2,%a0
	move.w	(%a0)+,%d2
	cmp.w	#0x7fff,%d2
	jeq	fp_nsf_huge		| NaN / infinitive.
	move.l	(%a0)+,%d0		| get high lword of mantissa
fp_nsf_round:
	tst.l	(%a0)			| check the low lword
	jeq	1f
	| Set a sticky bit if it is non-zero.  This should only
	| affect the rounding in what would otherwise be equal-
	| distance situations, which is what we want it to do.
	bset	#0,%d0
1:	clr.l	(%a0)			| zap it from memory.
	| now, round off the low 8 bits of the hi lword.
	tst.b	%d0			| 8 low bits.
	jne	fp_nsf_checkround	| Are they non-zero?
	| nothing to do here
	subq.l	#8,%a0
	printf	PNORM,"%p(",1,%a0
	printx	PNORM,%a0@
	printf	PNORM,")\n"
	rts
fp_nsf_checkround:
	fp_set_sr FPSR_EXC_INEX2	| INEX2 bit
	clr.b	-(%a0)			| clear low byte of high lword
	subq.l	#3,%a0
	move.w	(FPD_RND,FPDATA),%d2	| rounding mode
	jne	2f			| %d2 == 0, round to nearest
	tst.b	%d0			| test guard bit
	jpl	9f			| zero is closer
	btst	#8,%d0			| test lsb bit
	| round to even behaviour, see above.
	jne	fp_nsf_doroundup		| round to infinity
	lsl.b	#1,%d0			| check low bits
	jeq	9f			| round to zero
fp_nsf_doroundup:
	| round (the mantissa, that is) towards infinity
	add.l	#0x100,(%a0)
	jcc	9f			| no overflow, good.
	| Overflow.  This means that the %d1 was 0xffffff00, so it
	| is now zero.  We will set the mantissa to reflect this, and
	| increment the exponent (checking for overflow there too)
	move.w	#0x8000,(%a0)
	addq.w	#1,-(%a0)
	cmp.w	#0x407f,(%a0)+		| exponent now overflown?
	jeq	fp_nsf_large		| yes, so make it infinity.
9:	subq.l	#4,%a0
	printf	PNORM,"%p(",1,%a0
	printx	PNORM,%a0@
	printf	PNORM,")\n"
	rts
	| check nondefault rounding modes
2:	subq.w	#2,%d2
	jcs	9b			| %d2 < 2, round to zero
	jhi	3f			| %d2 > 2, round to +infinity
	tst.b	(-3,%a0)		| to -inf
	jne	fp_nsf_doroundup	| negative, round to infinity
	jra	9b			| positive, round to zero
3:	tst.b	(-3,%a0)		| to +inf
	jeq	fp_nsf_doroundup		| positive, round to infinity
	jra	9b			| negative, round to zero
	| Exponent overflow.  Just call it infinity.
fp_nsf_large:
	tst.b	(3,%a0)
	jeq	1f
	fp_set_sr FPSR_EXC_INEX2
1:	fp_set_sr FPSR_EXC_OVFL
	move.w	(FPD_RND,FPDATA),%d2
	jne	3f			| %d2 = 0 round to nearest
1:	move.w	#0x7fff,(-2,%a0)
	clr.l	(%a0)+
	clr.l	(%a0)
2:	subq.l	#8,%a0
	printf	PNORM,"%p(",1,%a0
	printx	PNORM,%a0@
	printf	PNORM,")\n"
	rts
3:	subq.w	#2,%d2
	jcs	5f			| %d2 < 2, round to zero
	jhi	4f			| %d2 > 2, round to +infinity
	tst.b	(-3,%a0)		| to -inf
	jne	1b
	jra	5f
4:	tst.b	(-3,%a0)		| to +inf
	jeq	1b
5:	move.w	#0x407e,(-2,%a0)
	move.l	#0xffffff00,(%a0)+
	clr.l	(%a0)
	jra	2b
	| Infinities or NaNs
fp_nsf_huge:
	subq.l	#4,%a0
	printf	PNORM,"%p(",1,%a0
	printx	PNORM,%a0@
	printf	PNORM,")\n"
	rts

	| conv_ext2int (macro):
	| Generates a subroutine that converts an extended value to an
	| integer of a given size, again, with the appropriate type of
	| rounding.

	| Macro arguments:
	| s:	size, as given in an assembly instruction.
	| b:	number of bits in that size.

	| Subroutine arguments:
	| %a0:	source (struct fp_ext *)

	| Returns the integer in %d0 (like it should)

.macro conv_ext2int s,b
	.set	inf,(1<<(\b-1))-1	| i.e. MAXINT
	printf	PCONV,"e2i%d: %p(",2,#\b,%a0
	printx	PCONV,%a0@
	printf	PCONV,") "
	addq.l	#2,%a0
	move.w	(%a0)+,%d2		| exponent
	jeq	fp_e2i_zero\b		| zero / denorm (== 0, here)
	cmp.w	#0x7fff,%d2
	jeq	fp_e2i_huge\b		| Inf / NaN
	sub.w	#0x3ffe,%d2
	jcs	fp_e2i_small\b
	cmp.w	#\b,%d2
	jhi	fp_e2i_large\b
	move.l	(%a0),%d0
	move.l	%d0,%d1
	lsl.l	%d2,%d1
	jne	fp_e2i_round\b
	tst.l	(4,%a0)
	jne	fp_e2i_round\b
	neg.w	%d2
	add.w	#32,%d2
	lsr.l	%d2,%d0
9:	tst.w	(-4,%a0)
	jne	1f
	tst.\s	%d0
	jmi	fp_e2i_large\b
	printf	PCONV,"-> %p\n",1,%d0
	rts
1:	neg.\s	%d0
	jeq	1f
	jpl	fp_e2i_large\b
1:	printf	PCONV,"-> %p\n",1,%d0
	rts
fp_e2i_round\b:
	fp_set_sr FPSR_EXC_INEX2	| INEX2 bit
	neg.w	%d2
	add.w	#32,%d2
	.if	\b>16
	jeq	5f
	.endif
	lsr.l	%d2,%d0
	move.w	(FPD_RND,FPDATA),%d2	| rounding mode
	jne	2f			| %d2 == 0, round to nearest
	tst.l	%d1			| test guard bit
	jpl	9b			| zero is closer
	btst	%d2,%d0			| test lsb bit (%d2 still 0)
	jne	fp_e2i_doroundup\b
	lsl.l	#1,%d1			| check low bits
	jne	fp_e2i_doroundup\b
	tst.l	(4,%a0)
	jeq	9b
fp_e2i_doroundup\b:
	addq.l	#1,%d0
	jra	9b
	| check nondefault rounding modes
2:	subq.w	#2,%d2
	jcs	9b			| %d2 < 2, round to zero
	jhi	3f			| %d2 > 2, round to +infinity
	tst.w	(-4,%a0)		| to -inf
	jne	fp_e2i_doroundup\b	| negative, round to infinity
	jra	9b			| positive, round to zero
3:	tst.w	(-4,%a0)		| to +inf
	jeq	fp_e2i_doroundup\b	| positive, round to infinity
	jra	9b	| negative, round to zero
	| we are only want -2**127 get correctly rounded here,
	| since the guard bit is in the lower lword.
	| everything else ends up anyway as overflow.
	.if	\b>16
5:	move.w	(FPD_RND,FPDATA),%d2	| rounding mode
	jne	2b			| %d2 == 0, round to nearest
	move.l	(4,%a0),%d1		| test guard bit
	jpl	9b			| zero is closer
	lsl.l	#1,%d1			| check low bits
	jne	fp_e2i_doroundup\b
	jra	9b
	.endif
fp_e2i_zero\b:
	clr.l	%d0
	tst.l	(%a0)+
	jne	1f
	tst.l	(%a0)
	jeq	3f
1:	subq.l	#4,%a0
	fp_clr_sr FPSR_EXC_UNFL		| fp_normalize_ext has set this bit
fp_e2i_small\b:
	fp_set_sr FPSR_EXC_INEX2
	clr.l	%d0
	move.w	(FPD_RND,FPDATA),%d2	| rounding mode
	subq.w	#2,%d2
	jcs	3f			| %d2 < 2, round to nearest/zero
	jhi	2f			| %d2 > 2, round to +infinity
	tst.w	(-4,%a0)		| to -inf
	jeq	3f
	subq.\s	#1,%d0
	jra	3f
2:	tst.w	(-4,%a0)		| to +inf
	jne	3f
	addq.\s	#1,%d0
3:	printf	PCONV,"-> %p\n",1,%d0
	rts
fp_e2i_large\b:
	fp_set_sr FPSR_EXC_OPERR
	move.\s	#inf,%d0
	tst.w	(-4,%a0)
	jeq	1f
	addq.\s	#1,%d0
1:	printf	PCONV,"-> %p\n",1,%d0
	rts
fp_e2i_huge\b:
	move.\s	(%a0),%d0
	tst.l	(%a0)
	jne	1f
	tst.l	(%a0)
	jeq	fp_e2i_large\b
	| fp_normalize_ext has set this bit already
	| and made the number nonsignaling
1:	fp_tst_sr FPSR_EXC_SNAN
	jne	1f
	fp_set_sr FPSR_EXC_OPERR
1:	printf	PCONV,"-> %p\n",1,%d0
	rts
.endm

fp_conv_ext2long:
	conv_ext2int l,32

fp_conv_ext2short:
	conv_ext2int w,16

fp_conv_ext2byte:
	conv_ext2int b,8

fp_conv_ext2double:
	jsr	fp_normalize_double
	printf	PCONV,"e2d: %p(",1,%a0
	printx	PCONV,%a0@
	printf	PCONV,"), "
	move.l	(%a0)+,%d2
	cmp.w	#0x7fff,%d2
	jne	1f
	move.w	#0x7ff,%d2
	move.l	(%a0)+,%d0
	jra	2f
1:	sub.w	#0x3fff-0x3ff,%d2
	move.l	(%a0)+,%d0
	jmi	2f
	clr.w	%d2
2:	lsl.w	#5,%d2
	lsl.l	#7,%d2
	lsl.l	#8,%d2
	move.l	%d0,%d1
	lsl.l	#1,%d0
	lsr.l	#4,%d0
	lsr.l	#8,%d0
	or.l	%d2,%d0
	putuser.l %d0,(%a1)+,fp_err_ua2,%a1
	moveq	#21,%d0
	lsl.l	%d0,%d1
	move.l	(%a0),%d0
	lsr.l	#4,%d0
	lsr.l	#7,%d0
	or.l	%d1,%d0
	putuser.l %d0,(%a1),fp_err_ua2,%a1
#ifdef FPU_EMU_DEBUG
	getuser.l %a1@(-4),%d0,fp_err_ua2,%a1
	getuser.l %a1@(0),%d1,fp_err_ua2,%a1
	printf	PCONV,"%p(%08x%08x)\n",3,%a1,%d0,%d1
#endif
	rts

fp_conv_ext2single:
	jsr	fp_normalize_single
	printf	PCONV,"e2s: %p(",1,%a0
	printx	PCONV,%a0@
	printf	PCONV,"), "
	move.l	(%a0)+,%d1
	cmp.w	#0x7fff,%d1
	jne	1f
	move.w	#0xff,%d1
	move.l	(%a0)+,%d0
	jra	2f
1:	sub.w	#0x3fff-0x7f,%d1
	move.l	(%a0)+,%d0
	jmi	2f
	clr.w	%d1
2:	lsl.w	#8,%d1
	lsl.l	#7,%d1
	lsl.l	#8,%d1
	bclr	#31,%d0
	lsr.l	#8,%d0
	or.l	%d1,%d0
	printf	PCONV,"%08x\n",1,%d0
	rts

	| special return addresses for instr that
	| encode the rounding precision in the opcode
	| (e.g. fsmove,fdmove)

fp_finalrounding_single:
	addq.l	#8,%sp
	jsr	fp_normalize_ext
	jsr	fp_normalize_single
	jra	fp_finaltest

fp_finalrounding_single_fast:
	addq.l	#8,%sp
	jsr	fp_normalize_ext
	jsr	fp_normalize_single_fast
	jra	fp_finaltest

fp_finalrounding_double:
	addq.l	#8,%sp
	jsr	fp_normalize_ext
	jsr	fp_normalize_double
	jra	fp_finaltest

	| fp_finaltest:
	| set the emulated status register based on the outcome of an
	| emulated instruction.

fp_finalrounding:
	addq.l	#8,%sp
|	printf	,"f: %p\n",1,%a0
	jsr	fp_normalize_ext
	move.w	(FPD_PREC,FPDATA),%d0
	subq.w	#1,%d0
	jcs	fp_finaltest
	jne	1f
	jsr	fp_normalize_single
	jra	2f
1:	jsr	fp_normalize_double
2:|	printf	,"f: %p\n",1,%a0
fp_finaltest:
	| First, we do some of the obvious tests for the exception
	| status byte and condition code bytes of fp_sr here, so that
	| they do not have to be handled individually by every
	| emulated instruction.
	clr.l	%d0
	addq.l	#1,%a0
	tst.b	(%a0)+			| sign
	jeq	1f
	bset	#FPSR_CC_NEG-24,%d0	| N bit
1:	cmp.w	#0x7fff,(%a0)+		| exponent
	jeq	2f
	| test for zero
	moveq	#FPSR_CC_Z-24,%d1
	tst.l	(%a0)+
	jne	9f
	tst.l	(%a0)
	jne	9f
	jra	8f
	| infinitiv and NAN
2:	moveq	#FPSR_CC_NAN-24,%d1
	move.l	(%a0)+,%d2
	lsl.l	#1,%d2			| ignore high bit
	jne	8f
	tst.l	(%a0)
	jne	8f
	moveq	#FPSR_CC_INF-24,%d1
8:	bset	%d1,%d0
9:	move.b	%d0,(FPD_FPSR+0,FPDATA)	| set condition test result
	| move instructions enter here
	| Here, we test things in the exception status byte, and set
	| other things in the accrued exception byte accordingly.
	| Emulated instructions can set various things in the former,
	| as defined in fp_emu.h.
fp_final:
	move.l	(FPD_FPSR,FPDATA),%d0
#if 0
	btst	#FPSR_EXC_SNAN,%d0	| EXC_SNAN
	jne	1f
	btst	#FPSR_EXC_OPERR,%d0	| EXC_OPERR
	jeq	2f
1:	bset	#FPSR_AEXC_IOP,%d0	| set IOP bit
2:	btst	#FPSR_EXC_OVFL,%d0	| EXC_OVFL
	jeq	1f
	bset	#FPSR_AEXC_OVFL,%d0	| set OVFL bit
1:	btst	#FPSR_EXC_UNFL,%d0	| EXC_UNFL
	jeq	1f
	btst	#FPSR_EXC_INEX2,%d0	| EXC_INEX2
	jeq	1f
	bset	#FPSR_AEXC_UNFL,%d0	| set UNFL bit
1:	btst	#FPSR_EXC_DZ,%d0	| EXC_INEX1
	jeq	1f
	bset	#FPSR_AEXC_DZ,%d0	| set DZ bit
1:	btst	#FPSR_EXC_OVFL,%d0	| EXC_OVFL
	jne	1f
	btst	#FPSR_EXC_INEX2,%d0	| EXC_INEX2
	jne	1f
	btst	#FPSR_EXC_INEX1,%d0	| EXC_INEX1
	jeq	2f
1:	bset	#FPSR_AEXC_INEX,%d0	| set INEX bit
2:	move.l	%d0,(FPD_FPSR,FPDATA)
#else
	| same as above, greatly optimized, but untested (yet)
	move.l	%d0,%d2
	lsr.l	#5,%d0
	move.l	%d0,%d1
	lsr.l	#4,%d1
	or.l	%d0,%d1
	and.b	#0x08,%d1
	move.l	%d2,%d0
	lsr.l	#6,%d0
	or.l	%d1,%d0
	move.l	%d2,%d1
	lsr.l	#4,%d1
	or.b	#0xdf,%d1
	and.b	%d1,%d0
	move.l	%d2,%d1
	lsr.l	#7,%d1
	and.b	#0x80,%d1
	or.b	%d1,%d0
	and.b	#0xf8,%d0
	or.b	%d0,%d2
	move.l	%d2,(FPD_FPSR,FPDATA)
#endif
	move.b	(FPD_FPSR+2,FPDATA),%d0
	and.b	(FPD_FPCR+2,FPDATA),%d0
	jeq	1f
	printf	,"send signal!!!\n"
1:	jra	fp_end
