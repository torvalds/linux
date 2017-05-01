/*
 * Scalar AES core transform
 *
 * Copyright (C) 2017 Linaro Ltd.
 * Author: Ard Biesheuvel <ard.biesheuvel@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/linkage.h>

	.text
	.align		5

	rk		.req	r0
	rounds		.req	r1
	in		.req	r2
	out		.req	r3
	ttab		.req	ip

	t0		.req	lr
	t1		.req	r2
	t2		.req	r3

	.macro		__select, out, in, idx
	.if		__LINUX_ARM_ARCH__ < 7
	and		\out, \in, #0xff << (8 * \idx)
	.else
	ubfx		\out, \in, #(8 * \idx), #8
	.endif
	.endm

	.macro		__load, out, in, idx
	.if		__LINUX_ARM_ARCH__ < 7 && \idx > 0
	ldr		\out, [ttab, \in, lsr #(8 * \idx) - 2]
	.else
	ldr		\out, [ttab, \in, lsl #2]
	.endif
	.endm

	.macro		__hround, out0, out1, in0, in1, in2, in3, t3, t4, enc
	__select	\out0, \in0, 0
	__select	t0, \in1, 1
	__load		\out0, \out0, 0
	__load		t0, t0, 1

	.if		\enc
	__select	\out1, \in1, 0
	__select	t1, \in2, 1
	.else
	__select	\out1, \in3, 0
	__select	t1, \in0, 1
	.endif
	__load		\out1, \out1, 0
	__select	t2, \in2, 2
	__load		t1, t1, 1
	__load		t2, t2, 2

	eor		\out0, \out0, t0, ror #24

	__select	t0, \in3, 3
	.if		\enc
	__select	\t3, \in3, 2
	__select	\t4, \in0, 3
	.else
	__select	\t3, \in1, 2
	__select	\t4, \in2, 3
	.endif
	__load		\t3, \t3, 2
	__load		t0, t0, 3
	__load		\t4, \t4, 3

	eor		\out1, \out1, t1, ror #24
	eor		\out0, \out0, t2, ror #16
	ldm		rk!, {t1, t2}
	eor		\out1, \out1, \t3, ror #16
	eor		\out0, \out0, t0, ror #8
	eor		\out1, \out1, \t4, ror #8
	eor		\out0, \out0, t1
	eor		\out1, \out1, t2
	.endm

	.macro		fround, out0, out1, out2, out3, in0, in1, in2, in3
	__hround	\out0, \out1, \in0, \in1, \in2, \in3, \out2, \out3, 1
	__hround	\out2, \out3, \in2, \in3, \in0, \in1, \in1, \in2, 1
	.endm

	.macro		iround, out0, out1, out2, out3, in0, in1, in2, in3
	__hround	\out0, \out1, \in0, \in3, \in2, \in1, \out2, \out3, 0
	__hround	\out2, \out3, \in2, \in1, \in0, \in3, \in1, \in0, 0
	.endm

	.macro		__rev, out, in
	.if		__LINUX_ARM_ARCH__ < 6
	lsl		t0, \in, #24
	and		t1, \in, #0xff00
	and		t2, \in, #0xff0000
	orr		\out, t0, \in, lsr #24
	orr		\out, \out, t1, lsl #8
	orr		\out, \out, t2, lsr #8
	.else
	rev		\out, \in
	.endif
	.endm

	.macro		__adrl, out, sym, c
	.if		__LINUX_ARM_ARCH__ < 7
	ldr\c		\out, =\sym
	.else
	movw\c		\out, #:lower16:\sym
	movt\c		\out, #:upper16:\sym
	.endif
	.endm

	.macro		do_crypt, round, ttab, ltab
	push		{r3-r11, lr}

	ldr		r4, [in]
	ldr		r5, [in, #4]
	ldr		r6, [in, #8]
	ldr		r7, [in, #12]

	ldm		rk!, {r8-r11}

#ifdef CONFIG_CPU_BIG_ENDIAN
	__rev		r4, r4
	__rev		r5, r5
	__rev		r6, r6
	__rev		r7, r7
#endif

	eor		r4, r4, r8
	eor		r5, r5, r9
	eor		r6, r6, r10
	eor		r7, r7, r11

	__adrl		ttab, \ttab

	tst		rounds, #2
	bne		1f

0:	\round		r8, r9, r10, r11, r4, r5, r6, r7
	\round		r4, r5, r6, r7, r8, r9, r10, r11

1:	subs		rounds, rounds, #4
	\round		r8, r9, r10, r11, r4, r5, r6, r7
	__adrl		ttab, \ltab, ls
	\round		r4, r5, r6, r7, r8, r9, r10, r11
	bhi		0b

#ifdef CONFIG_CPU_BIG_ENDIAN
	__rev		r4, r4
	__rev		r5, r5
	__rev		r6, r6
	__rev		r7, r7
#endif

	ldr		out, [sp]

	str		r4, [out]
	str		r5, [out, #4]
	str		r6, [out, #8]
	str		r7, [out, #12]

	pop		{r3-r11, pc}

	.align		3
	.ltorg
	.endm

ENTRY(__aes_arm_encrypt)
	do_crypt	fround, crypto_ft_tab, crypto_fl_tab
ENDPROC(__aes_arm_encrypt)

ENTRY(__aes_arm_decrypt)
	do_crypt	iround, crypto_it_tab, crypto_il_tab
ENDPROC(__aes_arm_decrypt)
