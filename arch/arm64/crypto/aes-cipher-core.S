/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Scalar AES core transform
 *
 * Copyright (C) 2017 Linaro Ltd <ard.biesheuvel@linaro.org>
 */

#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/cache.h>

	.text

	rk		.req	x0
	out		.req	x1
	in		.req	x2
	rounds		.req	x3
	tt		.req	x2

	.macro		__pair1, sz, op, reg0, reg1, in0, in1e, in1d, shift
	.ifc		\op\shift, b0
	ubfiz		\reg0, \in0, #2, #8
	ubfiz		\reg1, \in1e, #2, #8
	.else
	ubfx		\reg0, \in0, #\shift, #8
	ubfx		\reg1, \in1e, #\shift, #8
	.endif

	/*
	 * AArch64 cannot do byte size indexed loads from a table containing
	 * 32-bit quantities, i.e., 'ldrb w12, [tt, w12, uxtw #2]' is not a
	 * valid instruction. So perform the shift explicitly first for the
	 * high bytes (the low byte is shifted implicitly by using ubfiz rather
	 * than ubfx above)
	 */
	.ifnc		\op, b
	ldr		\reg0, [tt, \reg0, uxtw #2]
	ldr		\reg1, [tt, \reg1, uxtw #2]
	.else
	.if		\shift > 0
	lsl		\reg0, \reg0, #2
	lsl		\reg1, \reg1, #2
	.endif
	ldrb		\reg0, [tt, \reg0, uxtw]
	ldrb		\reg1, [tt, \reg1, uxtw]
	.endif
	.endm

	.macro		__pair0, sz, op, reg0, reg1, in0, in1e, in1d, shift
	ubfx		\reg0, \in0, #\shift, #8
	ubfx		\reg1, \in1d, #\shift, #8
	ldr\op		\reg0, [tt, \reg0, uxtw #\sz]
	ldr\op		\reg1, [tt, \reg1, uxtw #\sz]
	.endm

	.macro		__hround, out0, out1, in0, in1, in2, in3, t0, t1, enc, sz, op
	ldp		\out0, \out1, [rk], #8

	__pair\enc	\sz, \op, w12, w13, \in0, \in1, \in3, 0
	__pair\enc	\sz, \op, w14, w15, \in1, \in2, \in0, 8
	__pair\enc	\sz, \op, w16, w17, \in2, \in3, \in1, 16
	__pair\enc	\sz, \op, \t0, \t1, \in3, \in0, \in2, 24

	eor		\out0, \out0, w12
	eor		\out1, \out1, w13
	eor		\out0, \out0, w14, ror #24
	eor		\out1, \out1, w15, ror #24
	eor		\out0, \out0, w16, ror #16
	eor		\out1, \out1, w17, ror #16
	eor		\out0, \out0, \t0, ror #8
	eor		\out1, \out1, \t1, ror #8
	.endm

	.macro		fround, out0, out1, out2, out3, in0, in1, in2, in3, sz=2, op
	__hround	\out0, \out1, \in0, \in1, \in2, \in3, \out2, \out3, 1, \sz, \op
	__hround	\out2, \out3, \in2, \in3, \in0, \in1, \in1, \in2, 1, \sz, \op
	.endm

	.macro		iround, out0, out1, out2, out3, in0, in1, in2, in3, sz=2, op
	__hround	\out0, \out1, \in0, \in3, \in2, \in1, \out2, \out3, 0, \sz, \op
	__hround	\out2, \out3, \in2, \in1, \in0, \in3, \in1, \in0, 0, \sz, \op
	.endm

	.macro		do_crypt, round, ttab, ltab, bsz
	ldp		w4, w5, [in]
	ldp		w6, w7, [in, #8]
	ldp		w8, w9, [rk], #16
	ldp		w10, w11, [rk, #-8]

CPU_BE(	rev		w4, w4		)
CPU_BE(	rev		w5, w5		)
CPU_BE(	rev		w6, w6		)
CPU_BE(	rev		w7, w7		)

	eor		w4, w4, w8
	eor		w5, w5, w9
	eor		w6, w6, w10
	eor		w7, w7, w11

	adr_l		tt, \ttab

	tbnz		rounds, #1, 1f

0:	\round		w8, w9, w10, w11, w4, w5, w6, w7
	\round		w4, w5, w6, w7, w8, w9, w10, w11

1:	subs		rounds, rounds, #4
	\round		w8, w9, w10, w11, w4, w5, w6, w7
	b.ls		3f
2:	\round		w4, w5, w6, w7, w8, w9, w10, w11
	b		0b
3:	adr_l		tt, \ltab
	\round		w4, w5, w6, w7, w8, w9, w10, w11, \bsz, b

CPU_BE(	rev		w4, w4		)
CPU_BE(	rev		w5, w5		)
CPU_BE(	rev		w6, w6		)
CPU_BE(	rev		w7, w7		)

	stp		w4, w5, [out]
	stp		w6, w7, [out, #8]
	ret
	.endm

ENTRY(__aes_arm64_encrypt)
	do_crypt	fround, crypto_ft_tab, crypto_ft_tab + 1, 2
ENDPROC(__aes_arm64_encrypt)

	.align		5
ENTRY(__aes_arm64_decrypt)
	do_crypt	iround, crypto_it_tab, crypto_aes_inv_sbox, 0
ENDPROC(__aes_arm64_decrypt)
