/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Accelerated GHASH implementation with ARMv8 PMULL instructions.
 *
 * Copyright (C) 2014 - 2018 Linaro Ltd. <ard.biesheuvel@linaro.org>
 */

#include <linux/linkage.h>
#include <asm/assembler.h>

	SHASH		.req	v0
	SHASH2		.req	v1
	T1		.req	v2
	T2		.req	v3
	MASK		.req	v4
	XM		.req	v5
	XL		.req	v6
	XH		.req	v7
	IN1		.req	v7

	k00_16		.req	v8
	k32_48		.req	v9

	t3		.req	v10
	t4		.req	v11
	t5		.req	v12
	t6		.req	v13
	t7		.req	v14
	t8		.req	v15
	t9		.req	v16

	perm1		.req	v17
	perm2		.req	v18
	perm3		.req	v19

	sh1		.req	v20
	sh2		.req	v21
	sh3		.req	v22
	sh4		.req	v23

	ss1		.req	v24
	ss2		.req	v25
	ss3		.req	v26
	ss4		.req	v27

	XL2		.req	v8
	XM2		.req	v9
	XH2		.req	v10
	XL3		.req	v11
	XM3		.req	v12
	XH3		.req	v13
	TT3		.req	v14
	TT4		.req	v15
	HH		.req	v16
	HH3		.req	v17
	HH4		.req	v18
	HH34		.req	v19

	.text
	.arch		armv8-a+crypto

	.macro		__pmull_p64, rd, rn, rm
	pmull		\rd\().1q, \rn\().1d, \rm\().1d
	.endm

	.macro		__pmull2_p64, rd, rn, rm
	pmull2		\rd\().1q, \rn\().2d, \rm\().2d
	.endm

	.macro		__pmull_p8, rq, ad, bd
	ext		t3.8b, \ad\().8b, \ad\().8b, #1		// A1
	ext		t5.8b, \ad\().8b, \ad\().8b, #2		// A2
	ext		t7.8b, \ad\().8b, \ad\().8b, #3		// A3

	__pmull_p8_\bd	\rq, \ad
	.endm

	.macro		__pmull2_p8, rq, ad, bd
	tbl		t3.16b, {\ad\().16b}, perm1.16b		// A1
	tbl		t5.16b, {\ad\().16b}, perm2.16b		// A2
	tbl		t7.16b, {\ad\().16b}, perm3.16b		// A3

	__pmull2_p8_\bd	\rq, \ad
	.endm

	.macro		__pmull_p8_SHASH, rq, ad
	__pmull_p8_tail	\rq, \ad\().8b, SHASH.8b, 8b,, sh1, sh2, sh3, sh4
	.endm

	.macro		__pmull_p8_SHASH2, rq, ad
	__pmull_p8_tail	\rq, \ad\().8b, SHASH2.8b, 8b,, ss1, ss2, ss3, ss4
	.endm

	.macro		__pmull2_p8_SHASH, rq, ad
	__pmull_p8_tail	\rq, \ad\().16b, SHASH.16b, 16b, 2, sh1, sh2, sh3, sh4
	.endm

	.macro		__pmull_p8_tail, rq, ad, bd, nb, t, b1, b2, b3, b4
	pmull\t		t3.8h, t3.\nb, \bd			// F = A1*B
	pmull\t		t4.8h, \ad, \b1\().\nb			// E = A*B1
	pmull\t		t5.8h, t5.\nb, \bd			// H = A2*B
	pmull\t		t6.8h, \ad, \b2\().\nb			// G = A*B2
	pmull\t		t7.8h, t7.\nb, \bd			// J = A3*B
	pmull\t		t8.8h, \ad, \b3\().\nb			// I = A*B3
	pmull\t		t9.8h, \ad, \b4\().\nb			// K = A*B4
	pmull\t		\rq\().8h, \ad, \bd			// D = A*B

	eor		t3.16b, t3.16b, t4.16b			// L = E + F
	eor		t5.16b, t5.16b, t6.16b			// M = G + H
	eor		t7.16b, t7.16b, t8.16b			// N = I + J

	uzp1		t4.2d, t3.2d, t5.2d
	uzp2		t3.2d, t3.2d, t5.2d
	uzp1		t6.2d, t7.2d, t9.2d
	uzp2		t7.2d, t7.2d, t9.2d

	// t3 = (L) (P0 + P1) << 8
	// t5 = (M) (P2 + P3) << 16
	eor		t4.16b, t4.16b, t3.16b
	and		t3.16b, t3.16b, k32_48.16b

	// t7 = (N) (P4 + P5) << 24
	// t9 = (K) (P6 + P7) << 32
	eor		t6.16b, t6.16b, t7.16b
	and		t7.16b, t7.16b, k00_16.16b

	eor		t4.16b, t4.16b, t3.16b
	eor		t6.16b, t6.16b, t7.16b

	zip2		t5.2d, t4.2d, t3.2d
	zip1		t3.2d, t4.2d, t3.2d
	zip2		t9.2d, t6.2d, t7.2d
	zip1		t7.2d, t6.2d, t7.2d

	ext		t3.16b, t3.16b, t3.16b, #15
	ext		t5.16b, t5.16b, t5.16b, #14
	ext		t7.16b, t7.16b, t7.16b, #13
	ext		t9.16b, t9.16b, t9.16b, #12

	eor		t3.16b, t3.16b, t5.16b
	eor		t7.16b, t7.16b, t9.16b
	eor		\rq\().16b, \rq\().16b, t3.16b
	eor		\rq\().16b, \rq\().16b, t7.16b
	.endm

	.macro		__pmull_pre_p64
	add		x8, x3, #16
	ld1		{HH.2d-HH4.2d}, [x8]

	trn1		SHASH2.2d, SHASH.2d, HH.2d
	trn2		T1.2d, SHASH.2d, HH.2d
	eor		SHASH2.16b, SHASH2.16b, T1.16b

	trn1		HH34.2d, HH3.2d, HH4.2d
	trn2		T1.2d, HH3.2d, HH4.2d
	eor		HH34.16b, HH34.16b, T1.16b

	movi		MASK.16b, #0xe1
	shl		MASK.2d, MASK.2d, #57
	.endm

	.macro		__pmull_pre_p8
	ext		SHASH2.16b, SHASH.16b, SHASH.16b, #8
	eor		SHASH2.16b, SHASH2.16b, SHASH.16b

	// k00_16 := 0x0000000000000000_000000000000ffff
	// k32_48 := 0x00000000ffffffff_0000ffffffffffff
	movi		k32_48.2d, #0xffffffff
	mov		k32_48.h[2], k32_48.h[0]
	ushr		k00_16.2d, k32_48.2d, #32

	// prepare the permutation vectors
	mov_q		x5, 0x080f0e0d0c0b0a09
	movi		T1.8b, #8
	dup		perm1.2d, x5
	eor		perm1.16b, perm1.16b, T1.16b
	ushr		perm2.2d, perm1.2d, #8
	ushr		perm3.2d, perm1.2d, #16
	ushr		T1.2d, perm1.2d, #24
	sli		perm2.2d, perm1.2d, #56
	sli		perm3.2d, perm1.2d, #48
	sli		T1.2d, perm1.2d, #40

	// precompute loop invariants
	tbl		sh1.16b, {SHASH.16b}, perm1.16b
	tbl		sh2.16b, {SHASH.16b}, perm2.16b
	tbl		sh3.16b, {SHASH.16b}, perm3.16b
	tbl		sh4.16b, {SHASH.16b}, T1.16b
	ext		ss1.8b, SHASH2.8b, SHASH2.8b, #1
	ext		ss2.8b, SHASH2.8b, SHASH2.8b, #2
	ext		ss3.8b, SHASH2.8b, SHASH2.8b, #3
	ext		ss4.8b, SHASH2.8b, SHASH2.8b, #4
	.endm

	//
	// PMULL (64x64->128) based reduction for CPUs that can do
	// it in a single instruction.
	//
	.macro		__pmull_reduce_p64
	pmull		T2.1q, XL.1d, MASK.1d
	eor		XM.16b, XM.16b, T1.16b

	mov		XH.d[0], XM.d[1]
	mov		XM.d[1], XL.d[0]

	eor		XL.16b, XM.16b, T2.16b
	ext		T2.16b, XL.16b, XL.16b, #8
	pmull		XL.1q, XL.1d, MASK.1d
	.endm

	//
	// Alternative reduction for CPUs that lack support for the
	// 64x64->128 PMULL instruction
	//
	.macro		__pmull_reduce_p8
	eor		XM.16b, XM.16b, T1.16b

	mov		XL.d[1], XM.d[0]
	mov		XH.d[0], XM.d[1]

	shl		T1.2d, XL.2d, #57
	shl		T2.2d, XL.2d, #62
	eor		T2.16b, T2.16b, T1.16b
	shl		T1.2d, XL.2d, #63
	eor		T2.16b, T2.16b, T1.16b
	ext		T1.16b, XL.16b, XH.16b, #8
	eor		T2.16b, T2.16b, T1.16b

	mov		XL.d[1], T2.d[0]
	mov		XH.d[0], T2.d[1]

	ushr		T2.2d, XL.2d, #1
	eor		XH.16b, XH.16b, XL.16b
	eor		XL.16b, XL.16b, T2.16b
	ushr		T2.2d, T2.2d, #6
	ushr		XL.2d, XL.2d, #1
	.endm

	.macro		__pmull_ghash, pn
	ld1		{SHASH.2d}, [x3]
	ld1		{XL.2d}, [x1]

	__pmull_pre_\pn

	/* do the head block first, if supplied */
	cbz		x4, 0f
	ld1		{T1.2d}, [x4]
	mov		x4, xzr
	b		3f

0:	.ifc		\pn, p64
	tbnz		w0, #0, 2f		// skip until #blocks is a
	tbnz		w0, #1, 2f		// round multiple of 4

1:	ld1		{XM3.16b-TT4.16b}, [x2], #64

	sub		w0, w0, #4

	rev64		T1.16b, XM3.16b
	rev64		T2.16b, XH3.16b
	rev64		TT4.16b, TT4.16b
	rev64		TT3.16b, TT3.16b

	ext		IN1.16b, TT4.16b, TT4.16b, #8
	ext		XL3.16b, TT3.16b, TT3.16b, #8

	eor		TT4.16b, TT4.16b, IN1.16b
	pmull2		XH2.1q, SHASH.2d, IN1.2d	// a1 * b1
	pmull		XL2.1q, SHASH.1d, IN1.1d	// a0 * b0
	pmull		XM2.1q, SHASH2.1d, TT4.1d	// (a1 + a0)(b1 + b0)

	eor		TT3.16b, TT3.16b, XL3.16b
	pmull2		XH3.1q, HH.2d, XL3.2d		// a1 * b1
	pmull		XL3.1q, HH.1d, XL3.1d		// a0 * b0
	pmull2		XM3.1q, SHASH2.2d, TT3.2d	// (a1 + a0)(b1 + b0)

	ext		IN1.16b, T2.16b, T2.16b, #8
	eor		XL2.16b, XL2.16b, XL3.16b
	eor		XH2.16b, XH2.16b, XH3.16b
	eor		XM2.16b, XM2.16b, XM3.16b

	eor		T2.16b, T2.16b, IN1.16b
	pmull2		XH3.1q, HH3.2d, IN1.2d		// a1 * b1
	pmull		XL3.1q, HH3.1d, IN1.1d		// a0 * b0
	pmull		XM3.1q, HH34.1d, T2.1d		// (a1 + a0)(b1 + b0)

	eor		XL2.16b, XL2.16b, XL3.16b
	eor		XH2.16b, XH2.16b, XH3.16b
	eor		XM2.16b, XM2.16b, XM3.16b

	ext		IN1.16b, T1.16b, T1.16b, #8
	ext		TT3.16b, XL.16b, XL.16b, #8
	eor		XL.16b, XL.16b, IN1.16b
	eor		T1.16b, T1.16b, TT3.16b

	pmull2		XH.1q, HH4.2d, XL.2d		// a1 * b1
	eor		T1.16b, T1.16b, XL.16b
	pmull		XL.1q, HH4.1d, XL.1d		// a0 * b0
	pmull2		XM.1q, HH34.2d, T1.2d		// (a1 + a0)(b1 + b0)

	eor		XL.16b, XL.16b, XL2.16b
	eor		XH.16b, XH.16b, XH2.16b
	eor		XM.16b, XM.16b, XM2.16b

	eor		T2.16b, XL.16b, XH.16b
	ext		T1.16b, XL.16b, XH.16b, #8
	eor		XM.16b, XM.16b, T2.16b

	__pmull_reduce_p64

	eor		T2.16b, T2.16b, XH.16b
	eor		XL.16b, XL.16b, T2.16b

	cbz		w0, 5f
	b		1b
	.endif

2:	ld1		{T1.2d}, [x2], #16
	sub		w0, w0, #1

3:	/* multiply XL by SHASH in GF(2^128) */
CPU_LE(	rev64		T1.16b, T1.16b	)

	ext		T2.16b, XL.16b, XL.16b, #8
	ext		IN1.16b, T1.16b, T1.16b, #8
	eor		T1.16b, T1.16b, T2.16b
	eor		XL.16b, XL.16b, IN1.16b

	__pmull2_\pn	XH, XL, SHASH			// a1 * b1
	eor		T1.16b, T1.16b, XL.16b
	__pmull_\pn 	XL, XL, SHASH			// a0 * b0
	__pmull_\pn	XM, T1, SHASH2			// (a1 + a0)(b1 + b0)

4:	eor		T2.16b, XL.16b, XH.16b
	ext		T1.16b, XL.16b, XH.16b, #8
	eor		XM.16b, XM.16b, T2.16b

	__pmull_reduce_\pn

	eor		T2.16b, T2.16b, XH.16b
	eor		XL.16b, XL.16b, T2.16b

	cbnz		w0, 0b

5:	st1		{XL.2d}, [x1]
	ret
	.endm

	/*
	 * void pmull_ghash_update(int blocks, u64 dg[], const char *src,
	 *			   struct ghash_key const *k, const char *head)
	 */
ENTRY(pmull_ghash_update_p64)
	__pmull_ghash	p64
ENDPROC(pmull_ghash_update_p64)

ENTRY(pmull_ghash_update_p8)
	__pmull_ghash	p8
ENDPROC(pmull_ghash_update_p8)

	KS0		.req	v8
	KS1		.req	v9
	KS2		.req	v10
	KS3		.req	v11

	INP0		.req	v21
	INP1		.req	v22
	INP2		.req	v23
	INP3		.req	v24

	K0		.req	v25
	K1		.req	v26
	K2		.req	v27
	K3		.req	v28
	K4		.req	v12
	K5		.req	v13
	K6		.req	v4
	K7		.req	v5
	K8		.req	v14
	K9		.req	v15
	KK		.req	v29
	KL		.req	v30
	KM		.req	v31

	.macro		load_round_keys, rounds, rk, tmp
	add		\tmp, \rk, #64
	ld1		{K0.4s-K3.4s}, [\rk]
	ld1		{K4.4s-K5.4s}, [\tmp]
	add		\tmp, \rk, \rounds, lsl #4
	sub		\tmp, \tmp, #32
	ld1		{KK.4s-KM.4s}, [\tmp]
	.endm

	.macro		enc_round, state, key
	aese		\state\().16b, \key\().16b
	aesmc		\state\().16b, \state\().16b
	.endm

	.macro		enc_qround, s0, s1, s2, s3, key
	enc_round	\s0, \key
	enc_round	\s1, \key
	enc_round	\s2, \key
	enc_round	\s3, \key
	.endm

	.macro		enc_block, state, rounds, rk, tmp
	add		\tmp, \rk, #96
	ld1		{K6.4s-K7.4s}, [\tmp], #32
	.irp		key, K0, K1, K2, K3, K4 K5
	enc_round	\state, \key
	.endr

	tbnz		\rounds, #2, .Lnot128_\@
.Lout256_\@:
	enc_round	\state, K6
	enc_round	\state, K7

.Lout192_\@:
	enc_round	\state, KK
	aese		\state\().16b, KL.16b
	eor		\state\().16b, \state\().16b, KM.16b

	.subsection	1
.Lnot128_\@:
	ld1		{K8.4s-K9.4s}, [\tmp], #32
	enc_round	\state, K6
	enc_round	\state, K7
	ld1		{K6.4s-K7.4s}, [\tmp]
	enc_round	\state, K8
	enc_round	\state, K9
	tbz		\rounds, #1, .Lout192_\@
	b		.Lout256_\@
	.previous
	.endm

	.align		6
	.macro		pmull_gcm_do_crypt, enc
	stp		x29, x30, [sp, #-32]!
	mov		x29, sp
	str		x19, [sp, #24]

	load_round_keys	x7, x6, x8

	ld1		{SHASH.2d}, [x3], #16
	ld1		{HH.2d-HH4.2d}, [x3]

	trn1		SHASH2.2d, SHASH.2d, HH.2d
	trn2		T1.2d, SHASH.2d, HH.2d
	eor		SHASH2.16b, SHASH2.16b, T1.16b

	trn1		HH34.2d, HH3.2d, HH4.2d
	trn2		T1.2d, HH3.2d, HH4.2d
	eor		HH34.16b, HH34.16b, T1.16b

	ld1		{XL.2d}, [x4]

	cbz		x0, 3f				// tag only?

	ldr		w8, [x5, #12]			// load lower counter
CPU_LE(	rev		w8, w8		)

0:	mov		w9, #4				// max blocks per round
	add		x10, x0, #0xf
	lsr		x10, x10, #4			// remaining blocks

	subs		x0, x0, #64
	csel		w9, w10, w9, mi
	add		w8, w8, w9

	bmi		1f
	ld1		{INP0.16b-INP3.16b}, [x2], #64
	.subsection	1
	/*
	 * Populate the four input registers right to left with up to 63 bytes
	 * of data, using overlapping loads to avoid branches.
	 *
	 *                INP0     INP1     INP2     INP3
	 *  1 byte     |        |        |        |x       |
	 * 16 bytes    |        |        |        |xxxxxxxx|
	 * 17 bytes    |        |        |xxxxxxxx|x       |
	 * 47 bytes    |        |xxxxxxxx|xxxxxxxx|xxxxxxx |
	 * etc etc
	 *
	 * Note that this code may read up to 15 bytes before the start of
	 * the input. It is up to the calling code to ensure this is safe if
	 * this happens in the first iteration of the loop (i.e., when the
	 * input size is < 16 bytes)
	 */
1:	mov		x15, #16
	ands		x19, x0, #0xf
	csel		x19, x19, x15, ne
	adr_l		x17, .Lpermute_table + 16

	sub		x11, x15, x19
	add		x12, x17, x11
	sub		x17, x17, x11
	ld1		{T1.16b}, [x12]
	sub		x10, x1, x11
	sub		x11, x2, x11

	cmp		x0, #-16
	csel		x14, x15, xzr, gt
	cmp		x0, #-32
	csel		x15, x15, xzr, gt
	cmp		x0, #-48
	csel		x16, x19, xzr, gt
	csel		x1, x1, x10, gt
	csel		x2, x2, x11, gt

	ld1		{INP0.16b}, [x2], x14
	ld1		{INP1.16b}, [x2], x15
	ld1		{INP2.16b}, [x2], x16
	ld1		{INP3.16b}, [x2]
	tbl		INP3.16b, {INP3.16b}, T1.16b
	b		2f
	.previous

2:	.if		\enc == 0
	bl		pmull_gcm_ghash_4x
	.endif

	bl		pmull_gcm_enc_4x

	tbnz		x0, #63, 6f
	st1		{INP0.16b-INP3.16b}, [x1], #64
	.if		\enc == 1
	bl		pmull_gcm_ghash_4x
	.endif
	bne		0b

3:	ldp		x19, x10, [sp, #24]
	cbz		x10, 5f				// output tag?

	ld1		{INP3.16b}, [x10]		// load lengths[]
	mov		w9, #1
	bl		pmull_gcm_ghash_4x

	mov		w11, #(0x1 << 24)		// BE '1U'
	ld1		{KS0.16b}, [x5]
	mov		KS0.s[3], w11

	enc_block	KS0, x7, x6, x12

	ext		XL.16b, XL.16b, XL.16b, #8
	rev64		XL.16b, XL.16b
	eor		XL.16b, XL.16b, KS0.16b
	st1		{XL.16b}, [x10]			// store tag

4:	ldp		x29, x30, [sp], #32
	ret

5:
CPU_LE(	rev		w8, w8		)
	str		w8, [x5, #12]			// store lower counter
	st1		{XL.2d}, [x4]
	b		4b

6:	ld1		{T1.16b-T2.16b}, [x17], #32	// permute vectors
	sub		x17, x17, x19, lsl #1

	cmp		w9, #1
	beq		7f
	.subsection	1
7:	ld1		{INP2.16b}, [x1]
	tbx		INP2.16b, {INP3.16b}, T1.16b
	mov		INP3.16b, INP2.16b
	b		8f
	.previous

	st1		{INP0.16b}, [x1], x14
	st1		{INP1.16b}, [x1], x15
	st1		{INP2.16b}, [x1], x16
	tbl		INP3.16b, {INP3.16b}, T1.16b
	tbx		INP3.16b, {INP2.16b}, T2.16b
8:	st1		{INP3.16b}, [x1]

	.if		\enc == 1
	ld1		{T1.16b}, [x17]
	tbl		INP3.16b, {INP3.16b}, T1.16b	// clear non-data bits
	bl		pmull_gcm_ghash_4x
	.endif
	b		3b
	.endm

	/*
	 * void pmull_gcm_encrypt(int blocks, u8 dst[], const u8 src[],
	 *			  struct ghash_key const *k, u64 dg[], u8 ctr[],
	 *			  int rounds, u8 tag)
	 */
ENTRY(pmull_gcm_encrypt)
	pmull_gcm_do_crypt	1
ENDPROC(pmull_gcm_encrypt)

	/*
	 * void pmull_gcm_decrypt(int blocks, u8 dst[], const u8 src[],
	 *			  struct ghash_key const *k, u64 dg[], u8 ctr[],
	 *			  int rounds, u8 tag)
	 */
ENTRY(pmull_gcm_decrypt)
	pmull_gcm_do_crypt	0
ENDPROC(pmull_gcm_decrypt)

pmull_gcm_ghash_4x:
	movi		MASK.16b, #0xe1
	shl		MASK.2d, MASK.2d, #57

	rev64		T1.16b, INP0.16b
	rev64		T2.16b, INP1.16b
	rev64		TT3.16b, INP2.16b
	rev64		TT4.16b, INP3.16b

	ext		XL.16b, XL.16b, XL.16b, #8

	tbz		w9, #2, 0f			// <4 blocks?
	.subsection	1
0:	movi		XH2.16b, #0
	movi		XM2.16b, #0
	movi		XL2.16b, #0

	tbz		w9, #0, 1f			// 2 blocks?
	tbz		w9, #1, 2f			// 1 block?

	eor		T2.16b, T2.16b, XL.16b
	ext		T1.16b, T2.16b, T2.16b, #8
	b		.Lgh3

1:	eor		TT3.16b, TT3.16b, XL.16b
	ext		T2.16b, TT3.16b, TT3.16b, #8
	b		.Lgh2

2:	eor		TT4.16b, TT4.16b, XL.16b
	ext		IN1.16b, TT4.16b, TT4.16b, #8
	b		.Lgh1
	.previous

	eor		T1.16b, T1.16b, XL.16b
	ext		IN1.16b, T1.16b, T1.16b, #8

	pmull2		XH2.1q, HH4.2d, IN1.2d		// a1 * b1
	eor		T1.16b, T1.16b, IN1.16b
	pmull		XL2.1q, HH4.1d, IN1.1d		// a0 * b0
	pmull2		XM2.1q, HH34.2d, T1.2d		// (a1 + a0)(b1 + b0)

	ext		T1.16b, T2.16b, T2.16b, #8
.Lgh3:	eor		T2.16b, T2.16b, T1.16b
	pmull2		XH.1q, HH3.2d, T1.2d		// a1 * b1
	pmull		XL.1q, HH3.1d, T1.1d		// a0 * b0
	pmull		XM.1q, HH34.1d, T2.1d		// (a1 + a0)(b1 + b0)

	eor		XH2.16b, XH2.16b, XH.16b
	eor		XL2.16b, XL2.16b, XL.16b
	eor		XM2.16b, XM2.16b, XM.16b

	ext		T2.16b, TT3.16b, TT3.16b, #8
.Lgh2:	eor		TT3.16b, TT3.16b, T2.16b
	pmull2		XH.1q, HH.2d, T2.2d		// a1 * b1
	pmull		XL.1q, HH.1d, T2.1d		// a0 * b0
	pmull2		XM.1q, SHASH2.2d, TT3.2d	// (a1 + a0)(b1 + b0)

	eor		XH2.16b, XH2.16b, XH.16b
	eor		XL2.16b, XL2.16b, XL.16b
	eor		XM2.16b, XM2.16b, XM.16b

	ext		IN1.16b, TT4.16b, TT4.16b, #8
.Lgh1:	eor		TT4.16b, TT4.16b, IN1.16b
	pmull		XL.1q, SHASH.1d, IN1.1d		// a0 * b0
	pmull2		XH.1q, SHASH.2d, IN1.2d		// a1 * b1
	pmull		XM.1q, SHASH2.1d, TT4.1d	// (a1 + a0)(b1 + b0)

	eor		XH.16b, XH.16b, XH2.16b
	eor		XL.16b, XL.16b, XL2.16b
	eor		XM.16b, XM.16b, XM2.16b

	eor		T2.16b, XL.16b, XH.16b
	ext		T1.16b, XL.16b, XH.16b, #8
	eor		XM.16b, XM.16b, T2.16b

	__pmull_reduce_p64

	eor		T2.16b, T2.16b, XH.16b
	eor		XL.16b, XL.16b, T2.16b

	ret
ENDPROC(pmull_gcm_ghash_4x)

pmull_gcm_enc_4x:
	ld1		{KS0.16b}, [x5]			// load upper counter
	sub		w10, w8, #4
	sub		w11, w8, #3
	sub		w12, w8, #2
	sub		w13, w8, #1
	rev		w10, w10
	rev		w11, w11
	rev		w12, w12
	rev		w13, w13
	mov		KS1.16b, KS0.16b
	mov		KS2.16b, KS0.16b
	mov		KS3.16b, KS0.16b
	ins		KS0.s[3], w10			// set lower counter
	ins		KS1.s[3], w11
	ins		KS2.s[3], w12
	ins		KS3.s[3], w13

	add		x10, x6, #96			// round key pointer
	ld1		{K6.4s-K7.4s}, [x10], #32
	.irp		key, K0, K1, K2, K3, K4, K5
	enc_qround	KS0, KS1, KS2, KS3, \key
	.endr

	tbnz		x7, #2, .Lnot128
	.subsection	1
.Lnot128:
	ld1		{K8.4s-K9.4s}, [x10], #32
	.irp		key, K6, K7
	enc_qround	KS0, KS1, KS2, KS3, \key
	.endr
	ld1		{K6.4s-K7.4s}, [x10]
	.irp		key, K8, K9
	enc_qround	KS0, KS1, KS2, KS3, \key
	.endr
	tbz		x7, #1, .Lout192
	b		.Lout256
	.previous

.Lout256:
	.irp		key, K6, K7
	enc_qround	KS0, KS1, KS2, KS3, \key
	.endr

.Lout192:
	enc_qround	KS0, KS1, KS2, KS3, KK

	aese		KS0.16b, KL.16b
	aese		KS1.16b, KL.16b
	aese		KS2.16b, KL.16b
	aese		KS3.16b, KL.16b

	eor		KS0.16b, KS0.16b, KM.16b
	eor		KS1.16b, KS1.16b, KM.16b
	eor		KS2.16b, KS2.16b, KM.16b
	eor		KS3.16b, KS3.16b, KM.16b

	eor		INP0.16b, INP0.16b, KS0.16b
	eor		INP1.16b, INP1.16b, KS1.16b
	eor		INP2.16b, INP2.16b, KS2.16b
	eor		INP3.16b, INP3.16b, KS3.16b

	ret
ENDPROC(pmull_gcm_enc_4x)

	.section	".rodata", "a"
	.align		6
.Lpermute_table:
	.byte		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
	.byte		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
	.byte		 0x0,  0x1,  0x2,  0x3,  0x4,  0x5,  0x6,  0x7
	.byte		 0x8,  0x9,  0xa,  0xb,  0xc,  0xd,  0xe,  0xf
	.byte		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
	.byte		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
	.byte		 0x0,  0x1,  0x2,  0x3,  0x4,  0x5,  0x6,  0x7
	.byte		 0x8,  0x9,  0xa,  0xb,  0xc,  0xd,  0xe,  0xf
	.previous
