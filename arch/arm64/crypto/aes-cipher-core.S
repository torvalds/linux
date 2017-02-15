/*
 * Scalar AES core transform
 *
 * Copyright (C) 2017 Linaro Ltd <ard.biesheuvel@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/linkage.h>
#include <asm/assembler.h>

	.text

	rk		.req	x0
	out		.req	x1
	in		.req	x2
	rounds		.req	x3
	tt		.req	x4
	lt		.req	x2

	.macro		__pair, enc, reg0, reg1, in0, in1e, in1d, shift
	ubfx		\reg0, \in0, #\shift, #8
	.if		\enc
	ubfx		\reg1, \in1e, #\shift, #8
	.else
	ubfx		\reg1, \in1d, #\shift, #8
	.endif
	ldr		\reg0, [tt, \reg0, uxtw #2]
	ldr		\reg1, [tt, \reg1, uxtw #2]
	.endm

	.macro		__hround, out0, out1, in0, in1, in2, in3, t0, t1, enc
	ldp		\out0, \out1, [rk], #8

	__pair		\enc, w13, w14, \in0, \in1, \in3, 0
	__pair		\enc, w15, w16, \in1, \in2, \in0, 8
	__pair		\enc, w17, w18, \in2, \in3, \in1, 16
	__pair		\enc, \t0, \t1, \in3, \in0, \in2, 24

	eor		\out0, \out0, w13
	eor		\out1, \out1, w14
	eor		\out0, \out0, w15, ror #24
	eor		\out1, \out1, w16, ror #24
	eor		\out0, \out0, w17, ror #16
	eor		\out1, \out1, w18, ror #16
	eor		\out0, \out0, \t0, ror #8
	eor		\out1, \out1, \t1, ror #8
	.endm

	.macro		fround, out0, out1, out2, out3, in0, in1, in2, in3
	__hround	\out0, \out1, \in0, \in1, \in2, \in3, \out2, \out3, 1
	__hround	\out2, \out3, \in2, \in3, \in0, \in1, \in1, \in2, 1
	.endm

	.macro		iround, out0, out1, out2, out3, in0, in1, in2, in3
	__hround	\out0, \out1, \in0, \in3, \in2, \in1, \out2, \out3, 0
	__hround	\out2, \out3, \in2, \in1, \in0, \in3, \in1, \in0, 0
	.endm

	.macro		do_crypt, round, ttab, ltab
	ldp		w5, w6, [in]
	ldp		w7, w8, [in, #8]
	ldp		w9, w10, [rk], #16
	ldp		w11, w12, [rk, #-8]

CPU_BE(	rev		w5, w5		)
CPU_BE(	rev		w6, w6		)
CPU_BE(	rev		w7, w7		)
CPU_BE(	rev		w8, w8		)

	eor		w5, w5, w9
	eor		w6, w6, w10
	eor		w7, w7, w11
	eor		w8, w8, w12

	adr_l		tt, \ttab
	adr_l		lt, \ltab

	tbnz		rounds, #1, 1f

0:	\round		w9, w10, w11, w12, w5, w6, w7, w8
	\round		w5, w6, w7, w8, w9, w10, w11, w12

1:	subs		rounds, rounds, #4
	\round		w9, w10, w11, w12, w5, w6, w7, w8
	csel		tt, tt, lt, hi
	\round		w5, w6, w7, w8, w9, w10, w11, w12
	b.hi		0b

CPU_BE(	rev		w5, w5		)
CPU_BE(	rev		w6, w6		)
CPU_BE(	rev		w7, w7		)
CPU_BE(	rev		w8, w8		)

	stp		w5, w6, [out]
	stp		w7, w8, [out, #8]
	ret
	.endm

	.align		5
ENTRY(__aes_arm64_encrypt)
	do_crypt	fround, crypto_ft_tab, crypto_fl_tab
ENDPROC(__aes_arm64_encrypt)

	.align		5
ENTRY(__aes_arm64_decrypt)
	do_crypt	iround, crypto_it_tab, crypto_il_tab
ENDPROC(__aes_arm64_decrypt)
