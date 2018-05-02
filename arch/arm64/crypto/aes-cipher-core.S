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
	do_crypt	iround, crypto_it_tab, __aes_arm64_inverse_sbox, 0
ENDPROC(__aes_arm64_decrypt)

	.section	".rodata", "a"
	.align		L1_CACHE_SHIFT
	.type		__aes_arm64_inverse_sbox, %object
__aes_arm64_inverse_sbox:
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
	.size		__aes_arm64_inverse_sbox, . - __aes_arm64_inverse_sbox
