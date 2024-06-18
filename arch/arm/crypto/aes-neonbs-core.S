/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Bit sliced AES using NEON instructions
 *
 * Copyright (C) 2017 Linaro Ltd.
 * Author: Ard Biesheuvel <ard.biesheuvel@linaro.org>
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
	.fpu		neon

	rounds		.req	ip
	bskey		.req	r4

	q0l		.req	d0
	q0h		.req	d1
	q1l		.req	d2
	q1h		.req	d3
	q2l		.req	d4
	q2h		.req	d5
	q3l		.req	d6
	q3h		.req	d7
	q4l		.req	d8
	q4h		.req	d9
	q5l		.req	d10
	q5h		.req	d11
	q6l		.req	d12
	q6h		.req	d13
	q7l		.req	d14
	q7h		.req	d15
	q8l		.req	d16
	q8h		.req	d17
	q9l		.req	d18
	q9h		.req	d19
	q10l		.req	d20
	q10h		.req	d21
	q11l		.req	d22
	q11h		.req	d23
	q12l		.req	d24
	q12h		.req	d25
	q13l		.req	d26
	q13h		.req	d27
	q14l		.req	d28
	q14h		.req	d29
	q15l		.req	d30
	q15h		.req	d31

	.macro		__tbl, out, tbl, in, tmp
	.ifc		\out, \tbl
	.ifb		\tmp
	.error		__tbl needs temp register if out == tbl
	.endif
	vmov		\tmp, \out
	.endif
	vtbl.8		\out\()l, {\tbl}, \in\()l
	.ifc		\out, \tbl
	vtbl.8		\out\()h, {\tmp}, \in\()h
	.else
	vtbl.8		\out\()h, {\tbl}, \in\()h
	.endif
	.endm

	.macro		__ldr, out, sym
	vldr		\out\()l, \sym
	vldr		\out\()h, \sym + 8
	.endm

	.macro		in_bs_ch, b0, b1, b2, b3, b4, b5, b6, b7
	veor		\b2, \b2, \b1
	veor		\b5, \b5, \b6
	veor		\b3, \b3, \b0
	veor		\b6, \b6, \b2
	veor		\b5, \b5, \b0
	veor		\b6, \b6, \b3
	veor		\b3, \b3, \b7
	veor		\b7, \b7, \b5
	veor		\b3, \b3, \b4
	veor		\b4, \b4, \b5
	veor		\b2, \b2, \b7
	veor		\b3, \b3, \b1
	veor		\b1, \b1, \b5
	.endm

	.macro		out_bs_ch, b0, b1, b2, b3, b4, b5, b6, b7
	veor		\b0, \b0, \b6
	veor		\b1, \b1, \b4
	veor		\b4, \b4, \b6
	veor		\b2, \b2, \b0
	veor		\b6, \b6, \b1
	veor		\b1, \b1, \b5
	veor		\b5, \b5, \b3
	veor		\b3, \b3, \b7
	veor		\b7, \b7, \b5
	veor		\b2, \b2, \b5
	veor		\b4, \b4, \b7
	.endm

	.macro		inv_in_bs_ch, b6, b1, b2, b4, b7, b0, b3, b5
	veor		\b1, \b1, \b7
	veor		\b4, \b4, \b7
	veor		\b7, \b7, \b5
	veor		\b1, \b1, \b3
	veor		\b2, \b2, \b5
	veor		\b3, \b3, \b7
	veor		\b6, \b6, \b1
	veor		\b2, \b2, \b0
	veor		\b5, \b5, \b3
	veor		\b4, \b4, \b6
	veor		\b0, \b0, \b6
	veor		\b1, \b1, \b4
	.endm

	.macro		inv_out_bs_ch, b6, b5, b0, b3, b7, b1, b4, b2
	veor		\b1, \b1, \b5
	veor		\b2, \b2, \b7
	veor		\b3, \b3, \b1
	veor		\b4, \b4, \b5
	veor		\b7, \b7, \b5
	veor		\b3, \b3, \b4
	veor 		\b5, \b5, \b0
	veor		\b3, \b3, \b7
	veor		\b6, \b6, \b2
	veor		\b2, \b2, \b1
	veor		\b6, \b6, \b3
	veor		\b3, \b3, \b0
	veor		\b5, \b5, \b6
	.endm

	.macro		mul_gf4, x0, x1, y0, y1, t0, t1
	veor 		\t0, \y0, \y1
	vand		\t0, \t0, \x0
	veor		\x0, \x0, \x1
	vand		\t1, \x1, \y0
	vand		\x0, \x0, \y1
	veor		\x1, \t1, \t0
	veor		\x0, \x0, \t1
	.endm

	.macro		mul_gf4_n_gf4, x0, x1, y0, y1, t0, x2, x3, y2, y3, t1
	veor		\t0, \y0, \y1
	veor 		\t1, \y2, \y3
	vand		\t0, \t0, \x0
	vand		\t1, \t1, \x2
	veor		\x0, \x0, \x1
	veor		\x2, \x2, \x3
	vand		\x1, \x1, \y0
	vand		\x3, \x3, \y2
	vand		\x0, \x0, \y1
	vand		\x2, \x2, \y3
	veor		\x1, \x1, \x0
	veor		\x2, \x2, \x3
	veor		\x0, \x0, \t0
	veor		\x3, \x3, \t1
	.endm

	.macro		mul_gf16_2, x0, x1, x2, x3, x4, x5, x6, x7, \
				    y0, y1, y2, y3, t0, t1, t2, t3
	veor		\t0, \x0, \x2
	veor		\t1, \x1, \x3
	mul_gf4  	\x0, \x1, \y0, \y1, \t2, \t3
	veor		\y0, \y0, \y2
	veor		\y1, \y1, \y3
	mul_gf4_n_gf4	\t0, \t1, \y0, \y1, \t3, \x2, \x3, \y2, \y3, \t2
	veor		\x0, \x0, \t0
	veor		\x2, \x2, \t0
	veor		\x1, \x1, \t1
	veor		\x3, \x3, \t1
	veor		\t0, \x4, \x6
	veor		\t1, \x5, \x7
	mul_gf4_n_gf4	\t0, \t1, \y0, \y1, \t3, \x6, \x7, \y2, \y3, \t2
	veor		\y0, \y0, \y2
	veor		\y1, \y1, \y3
	mul_gf4  	\x4, \x5, \y0, \y1, \t2, \t3
	veor		\x4, \x4, \t0
	veor		\x6, \x6, \t0
	veor		\x5, \x5, \t1
	veor		\x7, \x7, \t1
	.endm

	.macro		inv_gf256, x0, x1, x2, x3, x4, x5, x6, x7, \
				   t0, t1, t2, t3, s0, s1, s2, s3
	veor		\t3, \x4, \x6
	veor		\t0, \x5, \x7
	veor		\t1, \x1, \x3
	veor		\s1, \x7, \x6
	veor		\s0, \x0, \x2
	veor		\s3, \t3, \t0
	vorr		\t2, \t0, \t1
	vand		\s2, \t3, \s0
	vorr		\t3, \t3, \s0
	veor		\s0, \s0, \t1
	vand		\t0, \t0, \t1
	veor		\t1, \x3, \x2
	vand		\s3, \s3, \s0
	vand		\s1, \s1, \t1
	veor		\t1, \x4, \x5
	veor		\s0, \x1, \x0
	veor		\t3, \t3, \s1
	veor		\t2, \t2, \s1
	vand		\s1, \t1, \s0
	vorr		\t1, \t1, \s0
	veor		\t3, \t3, \s3
	veor		\t0, \t0, \s1
	veor		\t2, \t2, \s2
	veor		\t1, \t1, \s3
	veor		\t0, \t0, \s2
	vand		\s0, \x7, \x3
	veor		\t1, \t1, \s2
	vand		\s1, \x6, \x2
	vand		\s2, \x5, \x1
	vorr		\s3, \x4, \x0
	veor		\t3, \t3, \s0
	veor		\t1, \t1, \s2
	veor		\s0, \t0, \s3
	veor		\t2, \t2, \s1
	vand		\s2, \t3, \t1
	veor		\s1, \t2, \s2
	veor		\s3, \s0, \s2
	vbsl		\s1, \t1, \s0
	vmvn		\t0, \s0
	vbsl		\s0, \s1, \s3
	vbsl		\t0, \s1, \s3
	vbsl		\s3, \t3, \t2
	veor		\t3, \t3, \t2
	vand		\s2, \s0, \s3
	veor		\t1, \t1, \t0
	veor		\s2, \s2, \t3
	mul_gf16_2	\x0, \x1, \x2, \x3, \x4, \x5, \x6, \x7, \
			\s3, \s2, \s1, \t1, \s0, \t0, \t2, \t3
	.endm

	.macro		sbox, b0, b1, b2, b3, b4, b5, b6, b7, \
			      t0, t1, t2, t3, s0, s1, s2, s3
	in_bs_ch	\b0, \b1, \b2, \b3, \b4, \b5, \b6, \b7
	inv_gf256	\b6, \b5, \b0, \b3, \b7, \b1, \b4, \b2, \
			\t0, \t1, \t2, \t3, \s0, \s1, \s2, \s3
	out_bs_ch	\b7, \b1, \b4, \b2, \b6, \b5, \b0, \b3
	.endm

	.macro		inv_sbox, b0, b1, b2, b3, b4, b5, b6, b7, \
				  t0, t1, t2, t3, s0, s1, s2, s3
	inv_in_bs_ch	\b0, \b1, \b2, \b3, \b4, \b5, \b6, \b7
	inv_gf256	\b5, \b1, \b2, \b6, \b3, \b7, \b0, \b4, \
			\t0, \t1, \t2, \t3, \s0, \s1, \s2, \s3
	inv_out_bs_ch	\b3, \b7, \b0, \b4, \b5, \b1, \b2, \b6
	.endm

	.macro		shift_rows, x0, x1, x2, x3, x4, x5, x6, x7, \
				    t0, t1, t2, t3, mask
	vld1.8		{\t0-\t1}, [bskey, :256]!
	veor		\t0, \t0, \x0
	vld1.8		{\t2-\t3}, [bskey, :256]!
	veor		\t1, \t1, \x1
	__tbl		\x0, \t0, \mask
	veor		\t2, \t2, \x2
	__tbl		\x1, \t1, \mask
	vld1.8		{\t0-\t1}, [bskey, :256]!
	veor		\t3, \t3, \x3
	__tbl		\x2, \t2, \mask
	__tbl		\x3, \t3, \mask
	vld1.8		{\t2-\t3}, [bskey, :256]!
	veor		\t0, \t0, \x4
	veor		\t1, \t1, \x5
	__tbl		\x4, \t0, \mask
	veor		\t2, \t2, \x6
	__tbl		\x5, \t1, \mask
	veor		\t3, \t3, \x7
	__tbl		\x6, \t2, \mask
	__tbl		\x7, \t3, \mask
	.endm

	.macro		inv_shift_rows, x0, x1, x2, x3, x4, x5, x6, x7, \
					t0, t1, t2, t3, mask
	__tbl		\x0, \x0, \mask, \t0
	__tbl		\x1, \x1, \mask, \t1
	__tbl		\x2, \x2, \mask, \t2
	__tbl		\x3, \x3, \mask, \t3
	__tbl		\x4, \x4, \mask, \t0
	__tbl		\x5, \x5, \mask, \t1
	__tbl		\x6, \x6, \mask, \t2
	__tbl		\x7, \x7, \mask, \t3
	.endm

	.macro		mix_cols, x0, x1, x2, x3, x4, x5, x6, x7, \
				  t0, t1, t2, t3, t4, t5, t6, t7, inv
	vext.8		\t0, \x0, \x0, #12
	vext.8		\t1, \x1, \x1, #12
	veor		\x0, \x0, \t0
	vext.8		\t2, \x2, \x2, #12
	veor		\x1, \x1, \t1
	vext.8		\t3, \x3, \x3, #12
	veor		\x2, \x2, \t2
	vext.8		\t4, \x4, \x4, #12
	veor		\x3, \x3, \t3
	vext.8		\t5, \x5, \x5, #12
	veor		\x4, \x4, \t4
	vext.8		\t6, \x6, \x6, #12
	veor		\x5, \x5, \t5
	vext.8		\t7, \x7, \x7, #12
	veor		\x6, \x6, \t6
	veor		\t1, \t1, \x0
	veor.8		\x7, \x7, \t7
	vext.8		\x0, \x0, \x0, #8
	veor		\t2, \t2, \x1
	veor		\t0, \t0, \x7
	veor		\t1, \t1, \x7
	vext.8		\x1, \x1, \x1, #8
	veor		\t5, \t5, \x4
	veor		\x0, \x0, \t0
	veor		\t6, \t6, \x5
	veor		\x1, \x1, \t1
	vext.8		\t0, \x4, \x4, #8
	veor		\t4, \t4, \x3
	vext.8		\t1, \x5, \x5, #8
	veor		\t7, \t7, \x6
	vext.8		\x4, \x3, \x3, #8
	veor		\t3, \t3, \x2
	vext.8		\x5, \x7, \x7, #8
	veor		\t4, \t4, \x7
	vext.8		\x3, \x6, \x6, #8
	veor		\t3, \t3, \x7
	vext.8		\x6, \x2, \x2, #8
	veor		\x7, \t1, \t5
	.ifb		\inv
	veor		\x2, \t0, \t4
	veor		\x4, \x4, \t3
	veor		\x5, \x5, \t7
	veor		\x3, \x3, \t6
	veor		\x6, \x6, \t2
	.else
	veor		\t3, \t3, \x4
	veor		\x5, \x5, \t7
	veor		\x2, \x3, \t6
	veor		\x3, \t0, \t4
	veor		\x4, \x6, \t2
	vmov		\x6, \t3
	.endif
	.endm

	.macro		inv_mix_cols, x0, x1, x2, x3, x4, x5, x6, x7, \
				      t0, t1, t2, t3, t4, t5, t6, t7
	vld1.8		{\t0-\t1}, [bskey, :256]!
	veor		\x0, \x0, \t0
	vld1.8		{\t2-\t3}, [bskey, :256]!
	veor		\x1, \x1, \t1
	vld1.8		{\t4-\t5}, [bskey, :256]!
	veor		\x2, \x2, \t2
	vld1.8		{\t6-\t7}, [bskey, :256]
	sub		bskey, bskey, #224
	veor		\x3, \x3, \t3
	veor		\x4, \x4, \t4
	veor		\x5, \x5, \t5
	veor		\x6, \x6, \t6
	veor		\x7, \x7, \t7
	vext.8		\t0, \x0, \x0, #8
	vext.8		\t6, \x6, \x6, #8
	vext.8		\t7, \x7, \x7, #8
	veor		\t0, \t0, \x0
	vext.8		\t1, \x1, \x1, #8
	veor		\t6, \t6, \x6
	vext.8		\t2, \x2, \x2, #8
	veor		\t7, \t7, \x7
	vext.8		\t3, \x3, \x3, #8
	veor		\t1, \t1, \x1
	vext.8		\t4, \x4, \x4, #8
	veor		\t2, \t2, \x2
	vext.8		\t5, \x5, \x5, #8
	veor		\t3, \t3, \x3
	veor		\t4, \t4, \x4
	veor		\t5, \t5, \x5
	veor		\x0, \x0, \t6
	veor		\x1, \x1, \t6
	veor		\x2, \x2, \t0
	veor		\x4, \x4, \t2
	veor		\x3, \x3, \t1
	veor		\x1, \x1, \t7
	veor		\x2, \x2, \t7
	veor		\x4, \x4, \t6
	veor		\x5, \x5, \t3
	veor		\x3, \x3, \t6
	veor		\x6, \x6, \t4
	veor		\x4, \x4, \t7
	veor		\x5, \x5, \t7
	veor		\x7, \x7, \t5
	mix_cols	\x0, \x1, \x2, \x3, \x4, \x5, \x6, \x7, \
			\t0, \t1, \t2, \t3, \t4, \t5, \t6, \t7, 1
	.endm

	.macro		swapmove_2x, a0, b0, a1, b1, n, mask, t0, t1
	vshr.u64	\t0, \b0, #\n
	vshr.u64	\t1, \b1, #\n
	veor		\t0, \t0, \a0
	veor		\t1, \t1, \a1
	vand		\t0, \t0, \mask
	vand		\t1, \t1, \mask
	veor		\a0, \a0, \t0
	vshl.s64	\t0, \t0, #\n
	veor		\a1, \a1, \t1
	vshl.s64	\t1, \t1, #\n
	veor		\b0, \b0, \t0
	veor		\b1, \b1, \t1
	.endm

	.macro		bitslice, x7, x6, x5, x4, x3, x2, x1, x0, t0, t1, t2, t3
	vmov.i8		\t0, #0x55
	vmov.i8		\t1, #0x33
	swapmove_2x	\x0, \x1, \x2, \x3, 1, \t0, \t2, \t3
	swapmove_2x	\x4, \x5, \x6, \x7, 1, \t0, \t2, \t3
	vmov.i8		\t0, #0x0f
	swapmove_2x	\x0, \x2, \x1, \x3, 2, \t1, \t2, \t3
	swapmove_2x	\x4, \x6, \x5, \x7, 2, \t1, \t2, \t3
	swapmove_2x	\x0, \x4, \x1, \x5, 4, \t0, \t2, \t3
	swapmove_2x	\x2, \x6, \x3, \x7, 4, \t0, \t2, \t3
	.endm

	.align		4
M0:	.quad		0x02060a0e03070b0f, 0x0004080c0105090d

	/*
	 * void aesbs_convert_key(u8 out[], u32 const rk[], int rounds)
	 */
ENTRY(aesbs_convert_key)
	vld1.32		{q7}, [r1]!		// load round 0 key
	vld1.32		{q15}, [r1]!		// load round 1 key

	vmov.i8		q8,  #0x01		// bit masks
	vmov.i8		q9,  #0x02
	vmov.i8		q10, #0x04
	vmov.i8		q11, #0x08
	vmov.i8		q12, #0x10
	vmov.i8		q13, #0x20
	__ldr		q14, M0

	sub		r2, r2, #1
	vst1.8		{q7}, [r0, :128]!	// save round 0 key

.Lkey_loop:
	__tbl		q7, q15, q14
	vmov.i8		q6, #0x40
	vmov.i8		q15, #0x80

	vtst.8		q0, q7, q8
	vtst.8		q1, q7, q9
	vtst.8		q2, q7, q10
	vtst.8		q3, q7, q11
	vtst.8		q4, q7, q12
	vtst.8		q5, q7, q13
	vtst.8		q6, q7, q6
	vtst.8		q7, q7, q15
	vld1.32		{q15}, [r1]!		// load next round key
	vmvn		q0, q0
	vmvn		q1, q1
	vmvn		q5, q5
	vmvn		q6, q6

	subs		r2, r2, #1
	vst1.8		{q0-q1}, [r0, :256]!
	vst1.8		{q2-q3}, [r0, :256]!
	vst1.8		{q4-q5}, [r0, :256]!
	vst1.8		{q6-q7}, [r0, :256]!
	bne		.Lkey_loop

	vmov.i8		q7, #0x63		// compose .L63
	veor		q15, q15, q7
	vst1.8		{q15}, [r0, :128]
	bx		lr
ENDPROC(aesbs_convert_key)

	.align		4
M0SR:	.quad		0x0a0e02060f03070b, 0x0004080c05090d01

aesbs_encrypt8:
	vld1.8		{q9}, [bskey, :128]!	// round 0 key
	__ldr		q8, M0SR

	veor		q10, q0, q9		// xor with round0 key
	veor		q11, q1, q9
	__tbl		q0, q10, q8
	veor		q12, q2, q9
	__tbl		q1, q11, q8
	veor		q13, q3, q9
	__tbl		q2, q12, q8
	veor		q14, q4, q9
	__tbl		q3, q13, q8
	veor		q15, q5, q9
	__tbl		q4, q14, q8
	veor		q10, q6, q9
	__tbl		q5, q15, q8
	veor		q11, q7, q9
	__tbl		q6, q10, q8
	__tbl		q7, q11, q8

	bitslice	q0, q1, q2, q3, q4, q5, q6, q7, q8, q9, q10, q11

	sub		rounds, rounds, #1
	b		.Lenc_sbox

	.align		5
SR:	.quad		0x0504070600030201, 0x0f0e0d0c0a09080b
SRM0:	.quad		0x0304090e00050a0f, 0x01060b0c0207080d

.Lenc_last:
	__ldr		q12, SRM0
.Lenc_loop:
	shift_rows	q0, q1, q2, q3, q4, q5, q6, q7, q8, q9, q10, q11, q12
.Lenc_sbox:
	sbox		q0, q1, q2, q3, q4, q5, q6, q7, q8, q9, q10, q11, q12, \
								q13, q14, q15
	subs		rounds, rounds, #1
	bcc		.Lenc_done

	mix_cols	q0, q1, q4, q6, q3, q7, q2, q5, q8, q9, q10, q11, q12, \
								q13, q14, q15

	beq		.Lenc_last
	__ldr		q12, SR
	b		.Lenc_loop

.Lenc_done:
	vld1.8		{q12}, [bskey, :128]	// last round key

	bitslice	q0, q1, q4, q6, q3, q7, q2, q5, q8, q9, q10, q11

	veor		q0, q0, q12
	veor		q1, q1, q12
	veor		q4, q4, q12
	veor		q6, q6, q12
	veor		q3, q3, q12
	veor		q7, q7, q12
	veor		q2, q2, q12
	veor		q5, q5, q12
	bx		lr
ENDPROC(aesbs_encrypt8)

	.align		4
M0ISR:	.quad		0x0a0e0206070b0f03, 0x0004080c0d010509

aesbs_decrypt8:
	add		bskey, bskey, rounds, lsl #7
	sub		bskey, bskey, #112
	vld1.8		{q9}, [bskey, :128]	// round 0 key
	sub		bskey, bskey, #128
	__ldr		q8, M0ISR

	veor		q10, q0, q9		// xor with round0 key
	veor		q11, q1, q9
	__tbl		q0, q10, q8
	veor		q12, q2, q9
	__tbl		q1, q11, q8
	veor		q13, q3, q9
	__tbl		q2, q12, q8
	veor		q14, q4, q9
	__tbl		q3, q13, q8
	veor		q15, q5, q9
	__tbl		q4, q14, q8
	veor		q10, q6, q9
	__tbl		q5, q15, q8
	veor		q11, q7, q9
	__tbl		q6, q10, q8
	__tbl		q7, q11, q8

	bitslice	q0, q1, q2, q3, q4, q5, q6, q7, q8, q9, q10, q11

	sub		rounds, rounds, #1
	b		.Ldec_sbox

	.align		5
ISR:	.quad		0x0504070602010003, 0x0f0e0d0c080b0a09
ISRM0:	.quad		0x01040b0e0205080f, 0x0306090c00070a0d

.Ldec_last:
	__ldr		q12, ISRM0
.Ldec_loop:
	inv_shift_rows	q0, q1, q2, q3, q4, q5, q6, q7, q8, q9, q10, q11, q12
.Ldec_sbox:
	inv_sbox	q0, q1, q2, q3, q4, q5, q6, q7, q8, q9, q10, q11, q12, \
								q13, q14, q15
	subs		rounds, rounds, #1
	bcc		.Ldec_done

	inv_mix_cols	q0, q1, q6, q4, q2, q7, q3, q5, q8, q9, q10, q11, q12, \
								q13, q14, q15

	beq		.Ldec_last
	__ldr		q12, ISR
	b		.Ldec_loop

.Ldec_done:
	add		bskey, bskey, #112
	vld1.8		{q12}, [bskey, :128]	// last round key

	bitslice	q0, q1, q6, q4, q2, q7, q3, q5, q8, q9, q10, q11

	veor		q0, q0, q12
	veor		q1, q1, q12
	veor		q6, q6, q12
	veor		q4, q4, q12
	veor		q2, q2, q12
	veor		q7, q7, q12
	veor		q3, q3, q12
	veor		q5, q5, q12
	bx		lr
ENDPROC(aesbs_decrypt8)

	/*
	 * aesbs_ecb_encrypt(u8 out[], u8 const in[], u8 const rk[], int rounds,
	 *		     int blocks)
	 * aesbs_ecb_decrypt(u8 out[], u8 const in[], u8 const rk[], int rounds,
	 *		     int blocks)
	 */
	.macro		__ecb_crypt, do8, o0, o1, o2, o3, o4, o5, o6, o7
	push		{r4-r6, lr}
	ldr		r5, [sp, #16]		// number of blocks

99:	adr		ip, 0f
	and		lr, r5, #7
	cmp		r5, #8
	sub		ip, ip, lr, lsl #2
	movlt		pc, ip			// computed goto if blocks < 8

	vld1.8		{q0}, [r1]!
	vld1.8		{q1}, [r1]!
	vld1.8		{q2}, [r1]!
	vld1.8		{q3}, [r1]!
	vld1.8		{q4}, [r1]!
	vld1.8		{q5}, [r1]!
	vld1.8		{q6}, [r1]!
	vld1.8		{q7}, [r1]!

0:	mov		bskey, r2
	mov		rounds, r3
	bl		\do8

	adr		ip, 1f
	and		lr, r5, #7
	cmp		r5, #8
	sub		ip, ip, lr, lsl #2
	movlt		pc, ip			// computed goto if blocks < 8

	vst1.8		{\o0}, [r0]!
	vst1.8		{\o1}, [r0]!
	vst1.8		{\o2}, [r0]!
	vst1.8		{\o3}, [r0]!
	vst1.8		{\o4}, [r0]!
	vst1.8		{\o5}, [r0]!
	vst1.8		{\o6}, [r0]!
	vst1.8		{\o7}, [r0]!

1:	subs		r5, r5, #8
	bgt		99b

	pop		{r4-r6, pc}
	.endm

	.align		4
ENTRY(aesbs_ecb_encrypt)
	__ecb_crypt	aesbs_encrypt8, q0, q1, q4, q6, q3, q7, q2, q5
ENDPROC(aesbs_ecb_encrypt)

	.align		4
ENTRY(aesbs_ecb_decrypt)
	__ecb_crypt	aesbs_decrypt8, q0, q1, q6, q4, q2, q7, q3, q5
ENDPROC(aesbs_ecb_decrypt)

	/*
	 * aesbs_cbc_decrypt(u8 out[], u8 const in[], u8 const rk[],
	 *		     int rounds, int blocks, u8 iv[])
	 */
	.align		4
ENTRY(aesbs_cbc_decrypt)
	mov		ip, sp
	push		{r4-r6, lr}
	ldm		ip, {r5-r6}		// load args 4-5

99:	adr		ip, 0f
	and		lr, r5, #7
	cmp		r5, #8
	sub		ip, ip, lr, lsl #2
	mov		lr, r1
	movlt		pc, ip			// computed goto if blocks < 8

	vld1.8		{q0}, [lr]!
	vld1.8		{q1}, [lr]!
	vld1.8		{q2}, [lr]!
	vld1.8		{q3}, [lr]!
	vld1.8		{q4}, [lr]!
	vld1.8		{q5}, [lr]!
	vld1.8		{q6}, [lr]!
	vld1.8		{q7}, [lr]

0:	mov		bskey, r2
	mov		rounds, r3
	bl		aesbs_decrypt8

	vld1.8		{q8}, [r6]
	vmov		q9, q8
	vmov		q10, q8
	vmov		q11, q8
	vmov		q12, q8
	vmov		q13, q8
	vmov		q14, q8
	vmov		q15, q8

	adr		ip, 1f
	and		lr, r5, #7
	cmp		r5, #8
	sub		ip, ip, lr, lsl #2
	movlt		pc, ip			// computed goto if blocks < 8

	vld1.8		{q9}, [r1]!
	vld1.8		{q10}, [r1]!
	vld1.8		{q11}, [r1]!
	vld1.8		{q12}, [r1]!
	vld1.8		{q13}, [r1]!
	vld1.8		{q14}, [r1]!
	vld1.8		{q15}, [r1]!
	W(nop)

1:	adr		ip, 2f
	sub		ip, ip, lr, lsl #3
	movlt		pc, ip			// computed goto if blocks < 8

	veor		q0, q0, q8
	vst1.8		{q0}, [r0]!
	veor		q1, q1, q9
	vst1.8		{q1}, [r0]!
	veor		q6, q6, q10
	vst1.8		{q6}, [r0]!
	veor		q4, q4, q11
	vst1.8		{q4}, [r0]!
	veor		q2, q2, q12
	vst1.8		{q2}, [r0]!
	veor		q7, q7, q13
	vst1.8		{q7}, [r0]!
	veor		q3, q3, q14
	vst1.8		{q3}, [r0]!
	veor		q5, q5, q15
	vld1.8		{q8}, [r1]!		// load next round's iv
2:	vst1.8		{q5}, [r0]!

	subs		r5, r5, #8
	vst1.8		{q8}, [r6]		// store next round's iv
	bgt		99b

	pop		{r4-r6, pc}
ENDPROC(aesbs_cbc_decrypt)

	.macro		next_ctr, q
	vmov		\q\()h, r9, r10
	adds		r10, r10, #1
	adcs		r9, r9, #0
	vmov		\q\()l, r7, r8
	adcs		r8, r8, #0
	adc		r7, r7, #0
	vrev32.8	\q, \q
	.endm

	/*
	 * aesbs_ctr_encrypt(u8 out[], u8 const in[], u8 const rk[],
	 *		     int rounds, int bytes, u8 ctr[])
	 */
ENTRY(aesbs_ctr_encrypt)
	mov		ip, sp
	push		{r4-r10, lr}

	ldm		ip, {r5, r6}		// load args 4-5
	vld1.8		{q0}, [r6]		// load counter
	vrev32.8	q1, q0
	vmov		r9, r10, d3
	vmov		r7, r8, d2

	adds		r10, r10, #1
	adcs		r9, r9, #0
	adcs		r8, r8, #0
	adc		r7, r7, #0

99:	vmov		q1, q0
	sub		lr, r5, #1
	vmov		q2, q0
	adr		ip, 0f
	vmov		q3, q0
	and		lr, lr, #112
	vmov		q4, q0
	cmp		r5, #112
	vmov		q5, q0
	sub		ip, ip, lr, lsl #1
	vmov		q6, q0
	add		ip, ip, lr, lsr #2
	vmov		q7, q0
	movle		pc, ip			// computed goto if bytes < 112

	next_ctr	q1
	next_ctr	q2
	next_ctr	q3
	next_ctr	q4
	next_ctr	q5
	next_ctr	q6
	next_ctr	q7

0:	mov		bskey, r2
	mov		rounds, r3
	bl		aesbs_encrypt8

	adr		ip, 1f
	sub		lr, r5, #1
	cmp		r5, #128
	bic		lr, lr, #15
	ands		r4, r5, #15		// preserves C flag
	teqcs		r5, r5			// set Z flag if not last iteration
	sub		ip, ip, lr, lsr #2
	rsb		r4, r4, #16
	movcc		pc, ip			// computed goto if bytes < 128

	vld1.8		{q8}, [r1]!
	vld1.8		{q9}, [r1]!
	vld1.8		{q10}, [r1]!
	vld1.8		{q11}, [r1]!
	vld1.8		{q12}, [r1]!
	vld1.8		{q13}, [r1]!
	vld1.8		{q14}, [r1]!
1:	subne		r1, r1, r4
	vld1.8		{q15}, [r1]!

	add		ip, ip, #2f - 1b

	veor		q0, q0, q8
	veor		q1, q1, q9
	veor		q4, q4, q10
	veor		q6, q6, q11
	veor		q3, q3, q12
	veor		q7, q7, q13
	veor		q2, q2, q14
	bne		3f
	veor		q5, q5, q15

	movcc		pc, ip			// computed goto if bytes < 128

	vst1.8		{q0}, [r0]!
	vst1.8		{q1}, [r0]!
	vst1.8		{q4}, [r0]!
	vst1.8		{q6}, [r0]!
	vst1.8		{q3}, [r0]!
	vst1.8		{q7}, [r0]!
	vst1.8		{q2}, [r0]!
2:	subne		r0, r0, r4
	vst1.8		{q5}, [r0]!

	next_ctr	q0

	subs		r5, r5, #128
	bgt		99b

	vst1.8		{q0}, [r6]
	pop		{r4-r10, pc}

3:	adr		lr, .Lpermute_table + 16
	cmp		r5, #16			// Z flag remains cleared
	sub		lr, lr, r4
	vld1.8		{q8-q9}, [lr]
	vtbl.8		d16, {q5}, d16
	vtbl.8		d17, {q5}, d17
	veor		q5, q8, q15
	bcc		4f			// have to reload prev if R5 < 16
	vtbx.8		d10, {q2}, d18
	vtbx.8		d11, {q2}, d19
	mov		pc, ip			// branch back to VST sequence

4:	sub		r0, r0, r4
	vshr.s8		q9, q9, #7		// create mask for VBIF
	vld1.8		{q8}, [r0]		// reload
	vbif		q5, q8, q9
	vst1.8		{q5}, [r0]
	pop		{r4-r10, pc}
ENDPROC(aesbs_ctr_encrypt)

	.align		6
.Lpermute_table:
	.byte		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
	.byte		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
	.byte		0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07
	.byte		0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
	.byte		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
	.byte		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff

	.macro		next_tweak, out, in, const, tmp
	vshr.s64	\tmp, \in, #63
	vand		\tmp, \tmp, \const
	vadd.u64	\out, \in, \in
	vext.8		\tmp, \tmp, \tmp, #8
	veor		\out, \out, \tmp
	.endm

	/*
	 * aesbs_xts_encrypt(u8 out[], u8 const in[], u8 const rk[], int rounds,
	 *		     int blocks, u8 iv[], int reorder_last_tweak)
	 * aesbs_xts_decrypt(u8 out[], u8 const in[], u8 const rk[], int rounds,
	 *		     int blocks, u8 iv[], int reorder_last_tweak)
	 */
	.align		6
__xts_prepare8:
	vld1.8		{q14}, [r7]		// load iv
	vmov.i32	d30, #0x87		// compose tweak mask vector
	vmovl.u32	q15, d30
	vshr.u64	d30, d31, #7
	vmov		q12, q14

	adr		ip, 0f
	and		r4, r6, #7
	cmp		r6, #8
	sub		ip, ip, r4, lsl #5
	mov		r4, sp
	movlt		pc, ip			// computed goto if blocks < 8

	vld1.8		{q0}, [r1]!
	next_tweak	q12, q14, q15, q13
	veor		q0, q0, q14
	vst1.8		{q14}, [r4, :128]!

	vld1.8		{q1}, [r1]!
	next_tweak	q14, q12, q15, q13
	veor		q1, q1, q12
	vst1.8		{q12}, [r4, :128]!

	vld1.8		{q2}, [r1]!
	next_tweak	q12, q14, q15, q13
	veor		q2, q2, q14
	vst1.8		{q14}, [r4, :128]!

	vld1.8		{q3}, [r1]!
	next_tweak	q14, q12, q15, q13
	veor		q3, q3, q12
	vst1.8		{q12}, [r4, :128]!

	vld1.8		{q4}, [r1]!
	next_tweak	q12, q14, q15, q13
	veor		q4, q4, q14
	vst1.8		{q14}, [r4, :128]!

	vld1.8		{q5}, [r1]!
	next_tweak	q14, q12, q15, q13
	veor		q5, q5, q12
	vst1.8		{q12}, [r4, :128]!

	vld1.8		{q6}, [r1]!
	next_tweak	q12, q14, q15, q13
	veor		q6, q6, q14
	vst1.8		{q14}, [r4, :128]!

	vld1.8		{q7}, [r1]!
	next_tweak	q14, q12, q15, q13
THUMB(	itt		le		)
	W(cmple)	r8, #0
	ble		1f
0:	veor		q7, q7, q12
	vst1.8		{q12}, [r4, :128]

	vst1.8		{q14}, [r7]		// store next iv
	bx		lr

1:	vswp		q12, q14
	b		0b
ENDPROC(__xts_prepare8)

	.macro		__xts_crypt, do8, o0, o1, o2, o3, o4, o5, o6, o7
	push		{r4-r8, lr}
	mov		r5, sp			// preserve sp
	ldrd		r6, r7, [sp, #24]	// get blocks and iv args
	rsb		r8, ip, #1
	sub		ip, sp, #128		// make room for 8x tweak
	bic		ip, ip, #0xf		// align sp to 16 bytes
	mov		sp, ip

99:	bl		__xts_prepare8

	mov		bskey, r2
	mov		rounds, r3
	bl		\do8

	adr		ip, 0f
	and		lr, r6, #7
	cmp		r6, #8
	sub		ip, ip, lr, lsl #2
	mov		r4, sp
	movlt		pc, ip			// computed goto if blocks < 8

	vld1.8		{q8}, [r4, :128]!
	vld1.8		{q9}, [r4, :128]!
	vld1.8		{q10}, [r4, :128]!
	vld1.8		{q11}, [r4, :128]!
	vld1.8		{q12}, [r4, :128]!
	vld1.8		{q13}, [r4, :128]!
	vld1.8		{q14}, [r4, :128]!
	vld1.8		{q15}, [r4, :128]

0:	adr		ip, 1f
	sub		ip, ip, lr, lsl #3
	movlt		pc, ip			// computed goto if blocks < 8

	veor		\o0, \o0, q8
	vst1.8		{\o0}, [r0]!
	veor		\o1, \o1, q9
	vst1.8		{\o1}, [r0]!
	veor		\o2, \o2, q10
	vst1.8		{\o2}, [r0]!
	veor		\o3, \o3, q11
	vst1.8		{\o3}, [r0]!
	veor		\o4, \o4, q12
	vst1.8		{\o4}, [r0]!
	veor		\o5, \o5, q13
	vst1.8		{\o5}, [r0]!
	veor		\o6, \o6, q14
	vst1.8		{\o6}, [r0]!
	veor		\o7, \o7, q15
	vst1.8		{\o7}, [r0]!

1:	subs		r6, r6, #8
	bgt		99b

	mov		sp, r5
	pop		{r4-r8, pc}
	.endm

ENTRY(aesbs_xts_encrypt)
	mov		ip, #0			// never reorder final tweak
	__xts_crypt	aesbs_encrypt8, q0, q1, q4, q6, q3, q7, q2, q5
ENDPROC(aesbs_xts_encrypt)

ENTRY(aesbs_xts_decrypt)
	ldr		ip, [sp, #8]		// reorder final tweak?
	__xts_crypt	aesbs_decrypt8, q0, q1, q6, q4, q2, q7, q3, q5
ENDPROC(aesbs_xts_decrypt)
