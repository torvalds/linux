/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Bit sliced AES using NEON instructions
 *
 * Copyright (C) 2016 Linaro Ltd <ard.biesheuvel@linaro.org>
 */

/*
 * The algorithm implemented here is described in detail by the paper
 * 'Faster and Timing-Attack Resistant AES-GCM' by Emilia Kaesper and
 * Peter Schwabe (https://eprint.iacr.org/2009/129.pdf)
 *
 * This implementation is based primarily on the OpenSSL implementation
 * for 32-bit ARM written by Andy Polyakov <appro@openssl.org>
 */

#include <linux/linkage.h>
#include <asm/assembler.h>

	.text

	rounds		.req	x11
	bskey		.req	x12

	.macro		in_bs_ch, b0, b1, b2, b3, b4, b5, b6, b7
	eor		\b2, \b2, \b1
	eor		\b5, \b5, \b6
	eor		\b3, \b3, \b0
	eor		\b6, \b6, \b2
	eor		\b5, \b5, \b0
	eor		\b6, \b6, \b3
	eor		\b3, \b3, \b7
	eor		\b7, \b7, \b5
	eor		\b3, \b3, \b4
	eor		\b4, \b4, \b5
	eor		\b2, \b2, \b7
	eor		\b3, \b3, \b1
	eor		\b1, \b1, \b5
	.endm

	.macro		out_bs_ch, b0, b1, b2, b3, b4, b5, b6, b7
	eor		\b0, \b0, \b6
	eor		\b1, \b1, \b4
	eor		\b4, \b4, \b6
	eor		\b2, \b2, \b0
	eor		\b6, \b6, \b1
	eor		\b1, \b1, \b5
	eor		\b5, \b5, \b3
	eor		\b3, \b3, \b7
	eor		\b7, \b7, \b5
	eor		\b2, \b2, \b5
	eor		\b4, \b4, \b7
	.endm

	.macro		inv_in_bs_ch, b6, b1, b2, b4, b7, b0, b3, b5
	eor		\b1, \b1, \b7
	eor		\b4, \b4, \b7
	eor		\b7, \b7, \b5
	eor		\b1, \b1, \b3
	eor		\b2, \b2, \b5
	eor		\b3, \b3, \b7
	eor		\b6, \b6, \b1
	eor		\b2, \b2, \b0
	eor		\b5, \b5, \b3
	eor		\b4, \b4, \b6
	eor		\b0, \b0, \b6
	eor		\b1, \b1, \b4
	.endm

	.macro		inv_out_bs_ch, b6, b5, b0, b3, b7, b1, b4, b2
	eor		\b1, \b1, \b5
	eor		\b2, \b2, \b7
	eor		\b3, \b3, \b1
	eor		\b4, \b4, \b5
	eor		\b7, \b7, \b5
	eor		\b3, \b3, \b4
	eor 		\b5, \b5, \b0
	eor		\b3, \b3, \b7
	eor		\b6, \b6, \b2
	eor		\b2, \b2, \b1
	eor		\b6, \b6, \b3
	eor		\b3, \b3, \b0
	eor		\b5, \b5, \b6
	.endm

	.macro		mul_gf4, x0, x1, y0, y1, t0, t1
	eor 		\t0, \y0, \y1
	and		\t0, \t0, \x0
	eor		\x0, \x0, \x1
	and		\t1, \x1, \y0
	and		\x0, \x0, \y1
	eor		\x1, \t1, \t0
	eor		\x0, \x0, \t1
	.endm

	.macro		mul_gf4_n_gf4, x0, x1, y0, y1, t0, x2, x3, y2, y3, t1
	eor		\t0, \y0, \y1
	eor 		\t1, \y2, \y3
	and		\t0, \t0, \x0
	and		\t1, \t1, \x2
	eor		\x0, \x0, \x1
	eor		\x2, \x2, \x3
	and		\x1, \x1, \y0
	and		\x3, \x3, \y2
	and		\x0, \x0, \y1
	and		\x2, \x2, \y3
	eor		\x1, \x1, \x0
	eor		\x2, \x2, \x3
	eor		\x0, \x0, \t0
	eor		\x3, \x3, \t1
	.endm

	.macro		mul_gf16_2, x0, x1, x2, x3, x4, x5, x6, x7, \
				    y0, y1, y2, y3, t0, t1, t2, t3
	eor		\t0, \x0, \x2
	eor		\t1, \x1, \x3
	mul_gf4  	\x0, \x1, \y0, \y1, \t2, \t3
	eor		\y0, \y0, \y2
	eor		\y1, \y1, \y3
	mul_gf4_n_gf4	\t0, \t1, \y0, \y1, \t3, \x2, \x3, \y2, \y3, \t2
	eor		\x0, \x0, \t0
	eor		\x2, \x2, \t0
	eor		\x1, \x1, \t1
	eor		\x3, \x3, \t1
	eor		\t0, \x4, \x6
	eor		\t1, \x5, \x7
	mul_gf4_n_gf4	\t0, \t1, \y0, \y1, \t3, \x6, \x7, \y2, \y3, \t2
	eor		\y0, \y0, \y2
	eor		\y1, \y1, \y3
	mul_gf4  	\x4, \x5, \y0, \y1, \t2, \t3
	eor		\x4, \x4, \t0
	eor		\x6, \x6, \t0
	eor		\x5, \x5, \t1
	eor		\x7, \x7, \t1
	.endm

	.macro		inv_gf256, x0, x1, x2, x3, x4, x5, x6, x7, \
				   t0, t1, t2, t3, s0, s1, s2, s3
	eor		\t3, \x4, \x6
	eor		\t0, \x5, \x7
	eor		\t1, \x1, \x3
	eor		\s1, \x7, \x6
	eor		\s0, \x0, \x2
	eor		\s3, \t3, \t0
	orr		\t2, \t0, \t1
	and		\s2, \t3, \s0
	orr		\t3, \t3, \s0
	eor		\s0, \s0, \t1
	and		\t0, \t0, \t1
	eor		\t1, \x3, \x2
	and		\s3, \s3, \s0
	and		\s1, \s1, \t1
	eor		\t1, \x4, \x5
	eor		\s0, \x1, \x0
	eor		\t3, \t3, \s1
	eor		\t2, \t2, \s1
	and		\s1, \t1, \s0
	orr		\t1, \t1, \s0
	eor		\t3, \t3, \s3
	eor		\t0, \t0, \s1
	eor		\t2, \t2, \s2
	eor		\t1, \t1, \s3
	eor		\t0, \t0, \s2
	and		\s0, \x7, \x3
	eor		\t1, \t1, \s2
	and		\s1, \x6, \x2
	and		\s2, \x5, \x1
	orr		\s3, \x4, \x0
	eor		\t3, \t3, \s0
	eor		\t1, \t1, \s2
	eor		\s0, \t0, \s3
	eor		\t2, \t2, \s1
	and		\s2, \t3, \t1
	eor		\s1, \t2, \s2
	eor		\s3, \s0, \s2
	bsl		\s1, \t1, \s0
	not		\t0, \s0
	bsl		\s0, \s1, \s3
	bsl		\t0, \s1, \s3
	bsl		\s3, \t3, \t2
	eor		\t3, \t3, \t2
	and		\s2, \s0, \s3
	eor		\t1, \t1, \t0
	eor		\s2, \s2, \t3
	mul_gf16_2	\x0, \x1, \x2, \x3, \x4, \x5, \x6, \x7, \
			\s3, \s2, \s1, \t1, \s0, \t0, \t2, \t3
	.endm

	.macro		sbox, b0, b1, b2, b3, b4, b5, b6, b7, \
			      t0, t1, t2, t3, s0, s1, s2, s3
	in_bs_ch	\b0\().16b, \b1\().16b, \b2\().16b, \b3\().16b, \
			\b4\().16b, \b5\().16b, \b6\().16b, \b7\().16b
	inv_gf256	\b6\().16b, \b5\().16b, \b0\().16b, \b3\().16b, \
			\b7\().16b, \b1\().16b, \b4\().16b, \b2\().16b, \
			\t0\().16b, \t1\().16b, \t2\().16b, \t3\().16b, \
			\s0\().16b, \s1\().16b, \s2\().16b, \s3\().16b
	out_bs_ch	\b7\().16b, \b1\().16b, \b4\().16b, \b2\().16b, \
			\b6\().16b, \b5\().16b, \b0\().16b, \b3\().16b
	.endm

	.macro		inv_sbox, b0, b1, b2, b3, b4, b5, b6, b7, \
				  t0, t1, t2, t3, s0, s1, s2, s3
	inv_in_bs_ch	\b0\().16b, \b1\().16b, \b2\().16b, \b3\().16b, \
			\b4\().16b, \b5\().16b, \b6\().16b, \b7\().16b
	inv_gf256	\b5\().16b, \b1\().16b, \b2\().16b, \b6\().16b, \
			\b3\().16b, \b7\().16b, \b0\().16b, \b4\().16b, \
			\t0\().16b, \t1\().16b, \t2\().16b, \t3\().16b, \
			\s0\().16b, \s1\().16b, \s2\().16b, \s3\().16b
	inv_out_bs_ch	\b3\().16b, \b7\().16b, \b0\().16b, \b4\().16b, \
			\b5\().16b, \b1\().16b, \b2\().16b, \b6\().16b
	.endm

	.macro		enc_next_rk
	ldp		q16, q17, [bskey], #128
	ldp		q18, q19, [bskey, #-96]
	ldp		q20, q21, [bskey, #-64]
	ldp		q22, q23, [bskey, #-32]
	.endm

	.macro		dec_next_rk
	ldp		q16, q17, [bskey, #-128]!
	ldp		q18, q19, [bskey, #32]
	ldp		q20, q21, [bskey, #64]
	ldp		q22, q23, [bskey, #96]
	.endm

	.macro		add_round_key, x0, x1, x2, x3, x4, x5, x6, x7
	eor		\x0\().16b, \x0\().16b, v16.16b
	eor		\x1\().16b, \x1\().16b, v17.16b
	eor		\x2\().16b, \x2\().16b, v18.16b
	eor		\x3\().16b, \x3\().16b, v19.16b
	eor		\x4\().16b, \x4\().16b, v20.16b
	eor		\x5\().16b, \x5\().16b, v21.16b
	eor		\x6\().16b, \x6\().16b, v22.16b
	eor		\x7\().16b, \x7\().16b, v23.16b
	.endm

	.macro		shift_rows, x0, x1, x2, x3, x4, x5, x6, x7, mask
	tbl		\x0\().16b, {\x0\().16b}, \mask\().16b
	tbl		\x1\().16b, {\x1\().16b}, \mask\().16b
	tbl		\x2\().16b, {\x2\().16b}, \mask\().16b
	tbl		\x3\().16b, {\x3\().16b}, \mask\().16b
	tbl		\x4\().16b, {\x4\().16b}, \mask\().16b
	tbl		\x5\().16b, {\x5\().16b}, \mask\().16b
	tbl		\x6\().16b, {\x6\().16b}, \mask\().16b
	tbl		\x7\().16b, {\x7\().16b}, \mask\().16b
	.endm

	.macro		mix_cols, x0, x1, x2, x3, x4, x5, x6, x7, \
				  t0, t1, t2, t3, t4, t5, t6, t7, inv
	ext		\t0\().16b, \x0\().16b, \x0\().16b, #12
	ext		\t1\().16b, \x1\().16b, \x1\().16b, #12
	eor		\x0\().16b, \x0\().16b, \t0\().16b
	ext		\t2\().16b, \x2\().16b, \x2\().16b, #12
	eor		\x1\().16b, \x1\().16b, \t1\().16b
	ext		\t3\().16b, \x3\().16b, \x3\().16b, #12
	eor		\x2\().16b, \x2\().16b, \t2\().16b
	ext		\t4\().16b, \x4\().16b, \x4\().16b, #12
	eor		\x3\().16b, \x3\().16b, \t3\().16b
	ext		\t5\().16b, \x5\().16b, \x5\().16b, #12
	eor		\x4\().16b, \x4\().16b, \t4\().16b
	ext		\t6\().16b, \x6\().16b, \x6\().16b, #12
	eor		\x5\().16b, \x5\().16b, \t5\().16b
	ext		\t7\().16b, \x7\().16b, \x7\().16b, #12
	eor		\x6\().16b, \x6\().16b, \t6\().16b
	eor		\t1\().16b, \t1\().16b, \x0\().16b
	eor		\x7\().16b, \x7\().16b, \t7\().16b
	ext		\x0\().16b, \x0\().16b, \x0\().16b, #8
	eor		\t2\().16b, \t2\().16b, \x1\().16b
	eor		\t0\().16b, \t0\().16b, \x7\().16b
	eor		\t1\().16b, \t1\().16b, \x7\().16b
	ext		\x1\().16b, \x1\().16b, \x1\().16b, #8
	eor		\t5\().16b, \t5\().16b, \x4\().16b
	eor		\x0\().16b, \x0\().16b, \t0\().16b
	eor		\t6\().16b, \t6\().16b, \x5\().16b
	eor		\x1\().16b, \x1\().16b, \t1\().16b
	ext		\t0\().16b, \x4\().16b, \x4\().16b, #8
	eor		\t4\().16b, \t4\().16b, \x3\().16b
	ext		\t1\().16b, \x5\().16b, \x5\().16b, #8
	eor		\t7\().16b, \t7\().16b, \x6\().16b
	ext		\x4\().16b, \x3\().16b, \x3\().16b, #8
	eor		\t3\().16b, \t3\().16b, \x2\().16b
	ext		\x5\().16b, \x7\().16b, \x7\().16b, #8
	eor		\t4\().16b, \t4\().16b, \x7\().16b
	ext		\x3\().16b, \x6\().16b, \x6\().16b, #8
	eor		\t3\().16b, \t3\().16b, \x7\().16b
	ext		\x6\().16b, \x2\().16b, \x2\().16b, #8
	eor		\x7\().16b, \t1\().16b, \t5\().16b
	.ifb		\inv
	eor		\x2\().16b, \t0\().16b, \t4\().16b
	eor		\x4\().16b, \x4\().16b, \t3\().16b
	eor		\x5\().16b, \x5\().16b, \t7\().16b
	eor		\x3\().16b, \x3\().16b, \t6\().16b
	eor		\x6\().16b, \x6\().16b, \t2\().16b
	.else
	eor		\t3\().16b, \t3\().16b, \x4\().16b
	eor		\x5\().16b, \x5\().16b, \t7\().16b
	eor		\x2\().16b, \x3\().16b, \t6\().16b
	eor		\x3\().16b, \t0\().16b, \t4\().16b
	eor		\x4\().16b, \x6\().16b, \t2\().16b
	mov		\x6\().16b, \t3\().16b
	.endif
	.endm

	.macro		inv_mix_cols, x0, x1, x2, x3, x4, x5, x6, x7, \
				      t0, t1, t2, t3, t4, t5, t6, t7
	ext		\t0\().16b, \x0\().16b, \x0\().16b, #8
	ext		\t6\().16b, \x6\().16b, \x6\().16b, #8
	ext		\t7\().16b, \x7\().16b, \x7\().16b, #8
	eor		\t0\().16b, \t0\().16b, \x0\().16b
	ext		\t1\().16b, \x1\().16b, \x1\().16b, #8
	eor		\t6\().16b, \t6\().16b, \x6\().16b
	ext		\t2\().16b, \x2\().16b, \x2\().16b, #8
	eor		\t7\().16b, \t7\().16b, \x7\().16b
	ext		\t3\().16b, \x3\().16b, \x3\().16b, #8
	eor		\t1\().16b, \t1\().16b, \x1\().16b
	ext		\t4\().16b, \x4\().16b, \x4\().16b, #8
	eor		\t2\().16b, \t2\().16b, \x2\().16b
	ext		\t5\().16b, \x5\().16b, \x5\().16b, #8
	eor		\t3\().16b, \t3\().16b, \x3\().16b
	eor		\t4\().16b, \t4\().16b, \x4\().16b
	eor		\t5\().16b, \t5\().16b, \x5\().16b
	eor		\x0\().16b, \x0\().16b, \t6\().16b
	eor		\x1\().16b, \x1\().16b, \t6\().16b
	eor		\x2\().16b, \x2\().16b, \t0\().16b
	eor		\x4\().16b, \x4\().16b, \t2\().16b
	eor		\x3\().16b, \x3\().16b, \t1\().16b
	eor		\x1\().16b, \x1\().16b, \t7\().16b
	eor		\x2\().16b, \x2\().16b, \t7\().16b
	eor		\x4\().16b, \x4\().16b, \t6\().16b
	eor		\x5\().16b, \x5\().16b, \t3\().16b
	eor		\x3\().16b, \x3\().16b, \t6\().16b
	eor		\x6\().16b, \x6\().16b, \t4\().16b
	eor		\x4\().16b, \x4\().16b, \t7\().16b
	eor		\x5\().16b, \x5\().16b, \t7\().16b
	eor		\x7\().16b, \x7\().16b, \t5\().16b
	mix_cols	\x0, \x1, \x2, \x3, \x4, \x5, \x6, \x7, \
			\t0, \t1, \t2, \t3, \t4, \t5, \t6, \t7, 1
	.endm

	.macro		swapmove_2x, a0, b0, a1, b1, n, mask, t0, t1
	ushr		\t0\().2d, \b0\().2d, #\n
	ushr		\t1\().2d, \b1\().2d, #\n
	eor		\t0\().16b, \t0\().16b, \a0\().16b
	eor		\t1\().16b, \t1\().16b, \a1\().16b
	and		\t0\().16b, \t0\().16b, \mask\().16b
	and		\t1\().16b, \t1\().16b, \mask\().16b
	eor		\a0\().16b, \a0\().16b, \t0\().16b
	shl		\t0\().2d, \t0\().2d, #\n
	eor		\a1\().16b, \a1\().16b, \t1\().16b
	shl		\t1\().2d, \t1\().2d, #\n
	eor		\b0\().16b, \b0\().16b, \t0\().16b
	eor		\b1\().16b, \b1\().16b, \t1\().16b
	.endm

	.macro		bitslice, x7, x6, x5, x4, x3, x2, x1, x0, t0, t1, t2, t3
	movi		\t0\().16b, #0x55
	movi		\t1\().16b, #0x33
	swapmove_2x	\x0, \x1, \x2, \x3, 1, \t0, \t2, \t3
	swapmove_2x	\x4, \x5, \x6, \x7, 1, \t0, \t2, \t3
	movi		\t0\().16b, #0x0f
	swapmove_2x	\x0, \x2, \x1, \x3, 2, \t1, \t2, \t3
	swapmove_2x	\x4, \x6, \x5, \x7, 2, \t1, \t2, \t3
	swapmove_2x	\x0, \x4, \x1, \x5, 4, \t0, \t2, \t3
	swapmove_2x	\x2, \x6, \x3, \x7, 4, \t0, \t2, \t3
	.endm


	.align		6
M0:	.octa		0x0004080c0105090d02060a0e03070b0f

M0SR:	.octa		0x0004080c05090d010a0e02060f03070b
SR:	.octa		0x0f0e0d0c0a09080b0504070600030201
SRM0:	.octa		0x01060b0c0207080d0304090e00050a0f

M0ISR:	.octa		0x0004080c0d0105090a0e0206070b0f03
ISR:	.octa		0x0f0e0d0c080b0a090504070602010003
ISRM0:	.octa		0x0306090c00070a0d01040b0e0205080f

	/*
	 * void aesbs_convert_key(u8 out[], u32 const rk[], int rounds)
	 */
ENTRY(aesbs_convert_key)
	ld1		{v7.4s}, [x1], #16		// load round 0 key
	ld1		{v17.4s}, [x1], #16		// load round 1 key

	movi		v8.16b,  #0x01			// bit masks
	movi		v9.16b,  #0x02
	movi		v10.16b, #0x04
	movi		v11.16b, #0x08
	movi		v12.16b, #0x10
	movi		v13.16b, #0x20
	movi		v14.16b, #0x40
	movi		v15.16b, #0x80
	ldr		q16, M0

	sub		x2, x2, #1
	str		q7, [x0], #16		// save round 0 key

.Lkey_loop:
	tbl		v7.16b ,{v17.16b}, v16.16b
	ld1		{v17.4s}, [x1], #16		// load next round key

	cmtst		v0.16b, v7.16b, v8.16b
	cmtst		v1.16b, v7.16b, v9.16b
	cmtst		v2.16b, v7.16b, v10.16b
	cmtst		v3.16b, v7.16b, v11.16b
	cmtst		v4.16b, v7.16b, v12.16b
	cmtst		v5.16b, v7.16b, v13.16b
	cmtst		v6.16b, v7.16b, v14.16b
	cmtst		v7.16b, v7.16b, v15.16b
	not		v0.16b, v0.16b
	not		v1.16b, v1.16b
	not		v5.16b, v5.16b
	not		v6.16b, v6.16b

	subs		x2, x2, #1
	stp		q0, q1, [x0], #128
	stp		q2, q3, [x0, #-96]
	stp		q4, q5, [x0, #-64]
	stp		q6, q7, [x0, #-32]
	b.ne		.Lkey_loop

	movi		v7.16b, #0x63			// compose .L63
	eor		v17.16b, v17.16b, v7.16b
	str		q17, [x0]
	ret
ENDPROC(aesbs_convert_key)

	.align		4
aesbs_encrypt8:
	ldr		q9, [bskey], #16		// round 0 key
	ldr		q8, M0SR
	ldr		q24, SR

	eor		v10.16b, v0.16b, v9.16b		// xor with round0 key
	eor		v11.16b, v1.16b, v9.16b
	tbl		v0.16b, {v10.16b}, v8.16b
	eor		v12.16b, v2.16b, v9.16b
	tbl		v1.16b, {v11.16b}, v8.16b
	eor		v13.16b, v3.16b, v9.16b
	tbl		v2.16b, {v12.16b}, v8.16b
	eor		v14.16b, v4.16b, v9.16b
	tbl		v3.16b, {v13.16b}, v8.16b
	eor		v15.16b, v5.16b, v9.16b
	tbl		v4.16b, {v14.16b}, v8.16b
	eor		v10.16b, v6.16b, v9.16b
	tbl		v5.16b, {v15.16b}, v8.16b
	eor		v11.16b, v7.16b, v9.16b
	tbl		v6.16b, {v10.16b}, v8.16b
	tbl		v7.16b, {v11.16b}, v8.16b

	bitslice	v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11

	sub		rounds, rounds, #1
	b		.Lenc_sbox

.Lenc_loop:
	shift_rows	v0, v1, v2, v3, v4, v5, v6, v7, v24
.Lenc_sbox:
	sbox		v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, \
								v13, v14, v15
	subs		rounds, rounds, #1
	b.cc		.Lenc_done

	enc_next_rk

	mix_cols	v0, v1, v4, v6, v3, v7, v2, v5, v8, v9, v10, v11, v12, \
								v13, v14, v15

	add_round_key	v0, v1, v2, v3, v4, v5, v6, v7

	b.ne		.Lenc_loop
	ldr		q24, SRM0
	b		.Lenc_loop

.Lenc_done:
	ldr		q12, [bskey]			// last round key

	bitslice	v0, v1, v4, v6, v3, v7, v2, v5, v8, v9, v10, v11

	eor		v0.16b, v0.16b, v12.16b
	eor		v1.16b, v1.16b, v12.16b
	eor		v4.16b, v4.16b, v12.16b
	eor		v6.16b, v6.16b, v12.16b
	eor		v3.16b, v3.16b, v12.16b
	eor		v7.16b, v7.16b, v12.16b
	eor		v2.16b, v2.16b, v12.16b
	eor		v5.16b, v5.16b, v12.16b
	ret
ENDPROC(aesbs_encrypt8)

	.align		4
aesbs_decrypt8:
	lsl		x9, rounds, #7
	add		bskey, bskey, x9

	ldr		q9, [bskey, #-112]!		// round 0 key
	ldr		q8, M0ISR
	ldr		q24, ISR

	eor		v10.16b, v0.16b, v9.16b		// xor with round0 key
	eor		v11.16b, v1.16b, v9.16b
	tbl		v0.16b, {v10.16b}, v8.16b
	eor		v12.16b, v2.16b, v9.16b
	tbl		v1.16b, {v11.16b}, v8.16b
	eor		v13.16b, v3.16b, v9.16b
	tbl		v2.16b, {v12.16b}, v8.16b
	eor		v14.16b, v4.16b, v9.16b
	tbl		v3.16b, {v13.16b}, v8.16b
	eor		v15.16b, v5.16b, v9.16b
	tbl		v4.16b, {v14.16b}, v8.16b
	eor		v10.16b, v6.16b, v9.16b
	tbl		v5.16b, {v15.16b}, v8.16b
	eor		v11.16b, v7.16b, v9.16b
	tbl		v6.16b, {v10.16b}, v8.16b
	tbl		v7.16b, {v11.16b}, v8.16b

	bitslice	v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11

	sub		rounds, rounds, #1
	b		.Ldec_sbox

.Ldec_loop:
	shift_rows	v0, v1, v2, v3, v4, v5, v6, v7, v24
.Ldec_sbox:
	inv_sbox	v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, \
								v13, v14, v15
	subs		rounds, rounds, #1
	b.cc		.Ldec_done

	dec_next_rk

	add_round_key	v0, v1, v6, v4, v2, v7, v3, v5

	inv_mix_cols	v0, v1, v6, v4, v2, v7, v3, v5, v8, v9, v10, v11, v12, \
								v13, v14, v15

	b.ne		.Ldec_loop
	ldr		q24, ISRM0
	b		.Ldec_loop
.Ldec_done:
	ldr		q12, [bskey, #-16]		// last round key

	bitslice	v0, v1, v6, v4, v2, v7, v3, v5, v8, v9, v10, v11

	eor		v0.16b, v0.16b, v12.16b
	eor		v1.16b, v1.16b, v12.16b
	eor		v6.16b, v6.16b, v12.16b
	eor		v4.16b, v4.16b, v12.16b
	eor		v2.16b, v2.16b, v12.16b
	eor		v7.16b, v7.16b, v12.16b
	eor		v3.16b, v3.16b, v12.16b
	eor		v5.16b, v5.16b, v12.16b
	ret
ENDPROC(aesbs_decrypt8)

	/*
	 * aesbs_ecb_encrypt(u8 out[], u8 const in[], u8 const rk[], int rounds,
	 *		     int blocks)
	 * aesbs_ecb_decrypt(u8 out[], u8 const in[], u8 const rk[], int rounds,
	 *		     int blocks)
	 */
	.macro		__ecb_crypt, do8, o0, o1, o2, o3, o4, o5, o6, o7
	frame_push	5

	mov		x19, x0
	mov		x20, x1
	mov		x21, x2
	mov		x22, x3
	mov		x23, x4

99:	mov		x5, #1
	lsl		x5, x5, x23
	subs		w23, w23, #8
	csel		x23, x23, xzr, pl
	csel		x5, x5, xzr, mi

	ld1		{v0.16b}, [x20], #16
	tbnz		x5, #1, 0f
	ld1		{v1.16b}, [x20], #16
	tbnz		x5, #2, 0f
	ld1		{v2.16b}, [x20], #16
	tbnz		x5, #3, 0f
	ld1		{v3.16b}, [x20], #16
	tbnz		x5, #4, 0f
	ld1		{v4.16b}, [x20], #16
	tbnz		x5, #5, 0f
	ld1		{v5.16b}, [x20], #16
	tbnz		x5, #6, 0f
	ld1		{v6.16b}, [x20], #16
	tbnz		x5, #7, 0f
	ld1		{v7.16b}, [x20], #16

0:	mov		bskey, x21
	mov		rounds, x22
	bl		\do8

	st1		{\o0\().16b}, [x19], #16
	tbnz		x5, #1, 1f
	st1		{\o1\().16b}, [x19], #16
	tbnz		x5, #2, 1f
	st1		{\o2\().16b}, [x19], #16
	tbnz		x5, #3, 1f
	st1		{\o3\().16b}, [x19], #16
	tbnz		x5, #4, 1f
	st1		{\o4\().16b}, [x19], #16
	tbnz		x5, #5, 1f
	st1		{\o5\().16b}, [x19], #16
	tbnz		x5, #6, 1f
	st1		{\o6\().16b}, [x19], #16
	tbnz		x5, #7, 1f
	st1		{\o7\().16b}, [x19], #16

	cbz		x23, 1f
	cond_yield_neon
	b		99b

1:	frame_pop
	ret
	.endm

	.align		4
ENTRY(aesbs_ecb_encrypt)
	__ecb_crypt	aesbs_encrypt8, v0, v1, v4, v6, v3, v7, v2, v5
ENDPROC(aesbs_ecb_encrypt)

	.align		4
ENTRY(aesbs_ecb_decrypt)
	__ecb_crypt	aesbs_decrypt8, v0, v1, v6, v4, v2, v7, v3, v5
ENDPROC(aesbs_ecb_decrypt)

	/*
	 * aesbs_cbc_decrypt(u8 out[], u8 const in[], u8 const rk[], int rounds,
	 *		     int blocks, u8 iv[])
	 */
	.align		4
ENTRY(aesbs_cbc_decrypt)
	frame_push	6

	mov		x19, x0
	mov		x20, x1
	mov		x21, x2
	mov		x22, x3
	mov		x23, x4
	mov		x24, x5

99:	mov		x6, #1
	lsl		x6, x6, x23
	subs		w23, w23, #8
	csel		x23, x23, xzr, pl
	csel		x6, x6, xzr, mi

	ld1		{v0.16b}, [x20], #16
	mov		v25.16b, v0.16b
	tbnz		x6, #1, 0f
	ld1		{v1.16b}, [x20], #16
	mov		v26.16b, v1.16b
	tbnz		x6, #2, 0f
	ld1		{v2.16b}, [x20], #16
	mov		v27.16b, v2.16b
	tbnz		x6, #3, 0f
	ld1		{v3.16b}, [x20], #16
	mov		v28.16b, v3.16b
	tbnz		x6, #4, 0f
	ld1		{v4.16b}, [x20], #16
	mov		v29.16b, v4.16b
	tbnz		x6, #5, 0f
	ld1		{v5.16b}, [x20], #16
	mov		v30.16b, v5.16b
	tbnz		x6, #6, 0f
	ld1		{v6.16b}, [x20], #16
	mov		v31.16b, v6.16b
	tbnz		x6, #7, 0f
	ld1		{v7.16b}, [x20]

0:	mov		bskey, x21
	mov		rounds, x22
	bl		aesbs_decrypt8

	ld1		{v24.16b}, [x24]		// load IV

	eor		v1.16b, v1.16b, v25.16b
	eor		v6.16b, v6.16b, v26.16b
	eor		v4.16b, v4.16b, v27.16b
	eor		v2.16b, v2.16b, v28.16b
	eor		v7.16b, v7.16b, v29.16b
	eor		v0.16b, v0.16b, v24.16b
	eor		v3.16b, v3.16b, v30.16b
	eor		v5.16b, v5.16b, v31.16b

	st1		{v0.16b}, [x19], #16
	mov		v24.16b, v25.16b
	tbnz		x6, #1, 1f
	st1		{v1.16b}, [x19], #16
	mov		v24.16b, v26.16b
	tbnz		x6, #2, 1f
	st1		{v6.16b}, [x19], #16
	mov		v24.16b, v27.16b
	tbnz		x6, #3, 1f
	st1		{v4.16b}, [x19], #16
	mov		v24.16b, v28.16b
	tbnz		x6, #4, 1f
	st1		{v2.16b}, [x19], #16
	mov		v24.16b, v29.16b
	tbnz		x6, #5, 1f
	st1		{v7.16b}, [x19], #16
	mov		v24.16b, v30.16b
	tbnz		x6, #6, 1f
	st1		{v3.16b}, [x19], #16
	mov		v24.16b, v31.16b
	tbnz		x6, #7, 1f
	ld1		{v24.16b}, [x20], #16
	st1		{v5.16b}, [x19], #16
1:	st1		{v24.16b}, [x24]		// store IV

	cbz		x23, 2f
	cond_yield_neon
	b		99b

2:	frame_pop
	ret
ENDPROC(aesbs_cbc_decrypt)

	.macro		next_tweak, out, in, const, tmp
	sshr		\tmp\().2d,  \in\().2d,   #63
	and		\tmp\().16b, \tmp\().16b, \const\().16b
	add		\out\().2d,  \in\().2d,   \in\().2d
	ext		\tmp\().16b, \tmp\().16b, \tmp\().16b, #8
	eor		\out\().16b, \out\().16b, \tmp\().16b
	.endm

	.align		4
.Lxts_mul_x:
CPU_LE(	.quad		1, 0x87		)
CPU_BE(	.quad		0x87, 1		)

	/*
	 * aesbs_xts_encrypt(u8 out[], u8 const in[], u8 const rk[], int rounds,
	 *		     int blocks, u8 iv[])
	 * aesbs_xts_decrypt(u8 out[], u8 const in[], u8 const rk[], int rounds,
	 *		     int blocks, u8 iv[])
	 */
__xts_crypt8:
	mov		x6, #1
	lsl		x6, x6, x23
	subs		w23, w23, #8
	csel		x23, x23, xzr, pl
	csel		x6, x6, xzr, mi

	ld1		{v0.16b}, [x20], #16
	next_tweak	v26, v25, v30, v31
	eor		v0.16b, v0.16b, v25.16b
	tbnz		x6, #1, 0f

	ld1		{v1.16b}, [x20], #16
	next_tweak	v27, v26, v30, v31
	eor		v1.16b, v1.16b, v26.16b
	tbnz		x6, #2, 0f

	ld1		{v2.16b}, [x20], #16
	next_tweak	v28, v27, v30, v31
	eor		v2.16b, v2.16b, v27.16b
	tbnz		x6, #3, 0f

	ld1		{v3.16b}, [x20], #16
	next_tweak	v29, v28, v30, v31
	eor		v3.16b, v3.16b, v28.16b
	tbnz		x6, #4, 0f

	ld1		{v4.16b}, [x20], #16
	str		q29, [sp, #.Lframe_local_offset]
	eor		v4.16b, v4.16b, v29.16b
	next_tweak	v29, v29, v30, v31
	tbnz		x6, #5, 0f

	ld1		{v5.16b}, [x20], #16
	str		q29, [sp, #.Lframe_local_offset + 16]
	eor		v5.16b, v5.16b, v29.16b
	next_tweak	v29, v29, v30, v31
	tbnz		x6, #6, 0f

	ld1		{v6.16b}, [x20], #16
	str		q29, [sp, #.Lframe_local_offset + 32]
	eor		v6.16b, v6.16b, v29.16b
	next_tweak	v29, v29, v30, v31
	tbnz		x6, #7, 0f

	ld1		{v7.16b}, [x20], #16
	str		q29, [sp, #.Lframe_local_offset + 48]
	eor		v7.16b, v7.16b, v29.16b
	next_tweak	v29, v29, v30, v31

0:	mov		bskey, x21
	mov		rounds, x22
	br		x7
ENDPROC(__xts_crypt8)

	.macro		__xts_crypt, do8, o0, o1, o2, o3, o4, o5, o6, o7
	frame_push	6, 64

	mov		x19, x0
	mov		x20, x1
	mov		x21, x2
	mov		x22, x3
	mov		x23, x4
	mov		x24, x5

0:	ldr		q30, .Lxts_mul_x
	ld1		{v25.16b}, [x24]

99:	adr		x7, \do8
	bl		__xts_crypt8

	ldp		q16, q17, [sp, #.Lframe_local_offset]
	ldp		q18, q19, [sp, #.Lframe_local_offset + 32]

	eor		\o0\().16b, \o0\().16b, v25.16b
	eor		\o1\().16b, \o1\().16b, v26.16b
	eor		\o2\().16b, \o2\().16b, v27.16b
	eor		\o3\().16b, \o3\().16b, v28.16b

	st1		{\o0\().16b}, [x19], #16
	mov		v25.16b, v26.16b
	tbnz		x6, #1, 1f
	st1		{\o1\().16b}, [x19], #16
	mov		v25.16b, v27.16b
	tbnz		x6, #2, 1f
	st1		{\o2\().16b}, [x19], #16
	mov		v25.16b, v28.16b
	tbnz		x6, #3, 1f
	st1		{\o3\().16b}, [x19], #16
	mov		v25.16b, v29.16b
	tbnz		x6, #4, 1f

	eor		\o4\().16b, \o4\().16b, v16.16b
	eor		\o5\().16b, \o5\().16b, v17.16b
	eor		\o6\().16b, \o6\().16b, v18.16b
	eor		\o7\().16b, \o7\().16b, v19.16b

	st1		{\o4\().16b}, [x19], #16
	tbnz		x6, #5, 1f
	st1		{\o5\().16b}, [x19], #16
	tbnz		x6, #6, 1f
	st1		{\o6\().16b}, [x19], #16
	tbnz		x6, #7, 1f
	st1		{\o7\().16b}, [x19], #16

	cbz		x23, 1f
	st1		{v25.16b}, [x24]

	cond_yield_neon	0b
	b		99b

1:	st1		{v25.16b}, [x24]
	frame_pop
	ret
	.endm

ENTRY(aesbs_xts_encrypt)
	__xts_crypt	aesbs_encrypt8, v0, v1, v4, v6, v3, v7, v2, v5
ENDPROC(aesbs_xts_encrypt)

ENTRY(aesbs_xts_decrypt)
	__xts_crypt	aesbs_decrypt8, v0, v1, v6, v4, v2, v7, v3, v5
ENDPROC(aesbs_xts_decrypt)

	.macro		next_ctr, v
	mov		\v\().d[1], x8
	adds		x8, x8, #1
	mov		\v\().d[0], x7
	adc		x7, x7, xzr
	rev64		\v\().16b, \v\().16b
	.endm

	/*
	 * aesbs_ctr_encrypt(u8 out[], u8 const in[], u8 const rk[],
	 *		     int rounds, int blocks, u8 iv[], u8 final[])
	 */
ENTRY(aesbs_ctr_encrypt)
	frame_push	8

	mov		x19, x0
	mov		x20, x1
	mov		x21, x2
	mov		x22, x3
	mov		x23, x4
	mov		x24, x5
	mov		x25, x6

	cmp		x25, #0
	cset		x26, ne
	add		x23, x23, x26		// do one extra block if final

98:	ldp		x7, x8, [x24]
	ld1		{v0.16b}, [x24]
CPU_LE(	rev		x7, x7		)
CPU_LE(	rev		x8, x8		)
	adds		x8, x8, #1
	adc		x7, x7, xzr

99:	mov		x9, #1
	lsl		x9, x9, x23
	subs		w23, w23, #8
	csel		x23, x23, xzr, pl
	csel		x9, x9, xzr, le

	tbnz		x9, #1, 0f
	next_ctr	v1
	tbnz		x9, #2, 0f
	next_ctr	v2
	tbnz		x9, #3, 0f
	next_ctr	v3
	tbnz		x9, #4, 0f
	next_ctr	v4
	tbnz		x9, #5, 0f
	next_ctr	v5
	tbnz		x9, #6, 0f
	next_ctr	v6
	tbnz		x9, #7, 0f
	next_ctr	v7

0:	mov		bskey, x21
	mov		rounds, x22
	bl		aesbs_encrypt8

	lsr		x9, x9, x26		// disregard the extra block
	tbnz		x9, #0, 0f

	ld1		{v8.16b}, [x20], #16
	eor		v0.16b, v0.16b, v8.16b
	st1		{v0.16b}, [x19], #16
	tbnz		x9, #1, 1f

	ld1		{v9.16b}, [x20], #16
	eor		v1.16b, v1.16b, v9.16b
	st1		{v1.16b}, [x19], #16
	tbnz		x9, #2, 2f

	ld1		{v10.16b}, [x20], #16
	eor		v4.16b, v4.16b, v10.16b
	st1		{v4.16b}, [x19], #16
	tbnz		x9, #3, 3f

	ld1		{v11.16b}, [x20], #16
	eor		v6.16b, v6.16b, v11.16b
	st1		{v6.16b}, [x19], #16
	tbnz		x9, #4, 4f

	ld1		{v12.16b}, [x20], #16
	eor		v3.16b, v3.16b, v12.16b
	st1		{v3.16b}, [x19], #16
	tbnz		x9, #5, 5f

	ld1		{v13.16b}, [x20], #16
	eor		v7.16b, v7.16b, v13.16b
	st1		{v7.16b}, [x19], #16
	tbnz		x9, #6, 6f

	ld1		{v14.16b}, [x20], #16
	eor		v2.16b, v2.16b, v14.16b
	st1		{v2.16b}, [x19], #16
	tbnz		x9, #7, 7f

	ld1		{v15.16b}, [x20], #16
	eor		v5.16b, v5.16b, v15.16b
	st1		{v5.16b}, [x19], #16

8:	next_ctr	v0
	st1		{v0.16b}, [x24]
	cbz		x23, .Lctr_done

	cond_yield_neon	98b
	b		99b

.Lctr_done:
	frame_pop
	ret

	/*
	 * If we are handling the tail of the input (x6 != NULL), return the
	 * final keystream block back to the caller.
	 */
0:	cbz		x25, 8b
	st1		{v0.16b}, [x25]
	b		8b
1:	cbz		x25, 8b
	st1		{v1.16b}, [x25]
	b		8b
2:	cbz		x25, 8b
	st1		{v4.16b}, [x25]
	b		8b
3:	cbz		x25, 8b
	st1		{v6.16b}, [x25]
	b		8b
4:	cbz		x25, 8b
	st1		{v3.16b}, [x25]
	b		8b
5:	cbz		x25, 8b
	st1		{v7.16b}, [x25]
	b		8b
6:	cbz		x25, 8b
	st1		{v2.16b}, [x25]
	b		8b
7:	cbz		x25, 8b
	st1		{v5.16b}, [x25]
	b		8b
ENDPROC(aesbs_ctr_encrypt)
