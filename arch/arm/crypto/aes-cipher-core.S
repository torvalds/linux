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
#include <asm/cache.h>

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

	.macro		__load, out, in, idx, sz, op
	.if		__LINUX_ARM_ARCH__ < 7 && \idx > 0
	ldr\op		\out, [ttab, \in, lsr #(8 * \idx) - \sz]
	.else
	ldr\op		\out, [ttab, \in, lsl #\sz]
	.endif
	.endm

	.macro		__hround, out0, out1, in0, in1, in2, in3, t3, t4, enc, sz, op
	__select	\out0, \in0, 0
	__select	t0, \in1, 1
	__load		\out0, \out0, 0, \sz, \op
	__load		t0, t0, 1, \sz, \op

	.if		\enc
	__select	\out1, \in1, 0
	__select	t1, \in2, 1
	.else
	__select	\out1, \in3, 0
	__select	t1, \in0, 1
	.endif
	__load		\out1, \out1, 0, \sz, \op
	__select	t2, \in2, 2
	__load		t1, t1, 1, \sz, \op
	__load		t2, t2, 2, \sz, \op

	eor		\out0, \out0, t0, ror #24

	__select	t0, \in3, 3
	.if		\enc
	__select	\t3, \in3, 2
	__select	\t4, \in0, 3
	.else
	__select	\t3, \in1, 2
	__select	\t4, \in2, 3
	.endif
	__load		\t3, \t3, 2, \sz, \op
	__load		t0, t0, 3, \sz, \op
	__load		\t4, \t4, 3, \sz, \op

	eor		\out1, \out1, t1, ror #24
	eor		\out0, \out0, t2, ror #16
	ldm		rk!, {t1, t2}
	eor		\out1, \out1, \t3, ror #16
	eor		\out0, \out0, t0, ror #8
	eor		\out1, \out1, \t4, ror #8
	eor		\out0, \out0, t1
	eor		\out1, \out1, t2
	.endm

	.macro		fround, out0, out1, out2, out3, in0, in1, in2, in3, sz=2, op
	__hround	\out0, \out1, \in0, \in1, \in2, \in3, \out2, \out3, 1, \sz, \op
	__hround	\out2, \out3, \in2, \in3, \in0, \in1, \in1, \in2, 1, \sz, \op
	.endm

	.macro		iround, out0, out1, out2, out3, in0, in1, in2, in3, sz=2, op
	__hround	\out0, \out1, \in0, \in3, \in2, \in1, \out2, \out3, 0, \sz, \op
	__hround	\out2, \out3, \in2, \in1, \in0, \in3, \in1, \in0, 0, \sz, \op
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

	.macro		do_crypt, round, ttab, ltab, bsz
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
	bls		2f
	\round		r4, r5, r6, r7, r8, r9, r10, r11
	b		0b

2:	__adrl		ttab, \ltab
	\round		r4, r5, r6, r7, r8, r9, r10, r11, \bsz, b

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

	.align		L1_CACHE_SHIFT
	.type		__aes_arm_inverse_sbox, %object
__aes_arm_inverse_sbox:
	.byte		0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38
	.byte		0xbf, 0x40, 0xa3, 0x9e, 0x81, 0xf3, 0xd7, 0xfb
	.byte		0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87
	.byte		0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb
	.byte		0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2, 0x23, 0x3d
	.byte		0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e
	.byte		0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2
	.byte		0x76, 0x5b, 0xa2, 0x49, 0x6d, 0x8b, 0xd1, 0x25
	.byte		0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16
	.byte		0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92
	.byte		0x6c, 0x70, 0x48, 0x50, 0xfd, 0xed, 0xb9, 0xda
	.byte		0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84
	.byte		0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a
	.byte		0xf7, 0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06
	.byte		0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02
	.byte		0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b
	.byte		0x3a, 0x91, 0x11, 0x41, 0x4f, 0x67, 0xdc, 0xea
	.byte		0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73
	.byte		0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85
	.byte		0xe2, 0xf9, 0x37, 0xe8, 0x1c, 0x75, 0xdf, 0x6e
	.byte		0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89
	.byte		0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b
	.byte		0xfc, 0x56, 0x3e, 0x4b, 0xc6, 0xd2, 0x79, 0x20
	.byte		0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4
	.byte		0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07, 0xc7, 0x31
	.byte		0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f
	.byte		0x60, 0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d
	.byte		0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef
	.byte		0xa0, 0xe0, 0x3b, 0x4d, 0xae, 0x2a, 0xf5, 0xb0
	.byte		0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61
	.byte		0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26
	.byte		0xe1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0c, 0x7d
	.size		__aes_arm_inverse_sbox, . - __aes_arm_inverse_sbox

ENTRY(__aes_arm_encrypt)
	do_crypt	fround, crypto_ft_tab, crypto_ft_tab + 1, 2
ENDPROC(__aes_arm_encrypt)

	.align		5
ENTRY(__aes_arm_decrypt)
	do_crypt	iround, crypto_it_tab, __aes_arm_inverse_sbox, 0
ENDPROC(__aes_arm_decrypt)
