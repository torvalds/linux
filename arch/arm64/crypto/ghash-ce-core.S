/*
 * Accelerated GHASH implementation with ARMv8 PMULL instructions.
 *
 * Copyright (C) 2014 - 2018 Linaro Ltd. <ard.biesheuvel@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include <linux/linkage.h>
#include <asm/assembler.h>

	SHASH		.req	v0
	SHASH2		.req	v1
	T1		.req	v2
	T2		.req	v3
	MASK		.req	v4
	XL		.req	v5
	XM		.req	v6
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

	KS0		.req	v12
	KS1		.req	v13
	INP0		.req	v14
	INP1		.req	v15

	.macro		load_round_keys, rounds, rk
	cmp		\rounds, #12
	blo		2222f		/* 128 bits */
	beq		1111f		/* 192 bits */
	ld1		{v17.4s-v18.4s}, [\rk], #32
1111:	ld1		{v19.4s-v20.4s}, [\rk], #32
2222:	ld1		{v21.4s-v24.4s}, [\rk], #64
	ld1		{v25.4s-v28.4s}, [\rk], #64
	ld1		{v29.4s-v31.4s}, [\rk]
	.endm

	.macro		enc_round, state, key
	aese		\state\().16b, \key\().16b
	aesmc		\state\().16b, \state\().16b
	.endm

	.macro		enc_block, state, rounds
	cmp		\rounds, #12
	b.lo		2222f		/* 128 bits */
	b.eq		1111f		/* 192 bits */
	enc_round	\state, v17
	enc_round	\state, v18
1111:	enc_round	\state, v19
	enc_round	\state, v20
2222:	.irp		key, v21, v22, v23, v24, v25, v26, v27, v28, v29
	enc_round	\state, \key
	.endr
	aese		\state\().16b, v30.16b
	eor		\state\().16b, \state\().16b, v31.16b
	.endm

	.macro		pmull_gcm_do_crypt, enc
	ld1		{SHASH.2d}, [x4], #16
	ld1		{HH.2d}, [x4]
	ld1		{XL.2d}, [x1]
	ldr		x8, [x5, #8]			// load lower counter

	movi		MASK.16b, #0xe1
	trn1		SHASH2.2d, SHASH.2d, HH.2d
	trn2		T1.2d, SHASH.2d, HH.2d
CPU_LE(	rev		x8, x8		)
	shl		MASK.2d, MASK.2d, #57
	eor		SHASH2.16b, SHASH2.16b, T1.16b

	.if		\enc == 1
	ldr		x10, [sp]
	ld1		{KS0.16b-KS1.16b}, [x10]
	.endif

	cbnz		x6, 4f

0:	ld1		{INP0.16b-INP1.16b}, [x3], #32

	rev		x9, x8
	add		x11, x8, #1
	add		x8, x8, #2

	.if		\enc == 1
	eor		INP0.16b, INP0.16b, KS0.16b	// encrypt input
	eor		INP1.16b, INP1.16b, KS1.16b
	.endif

	ld1		{KS0.8b}, [x5]			// load upper counter
	rev		x11, x11
	sub		w0, w0, #2
	mov		KS1.8b, KS0.8b
	ins		KS0.d[1], x9			// set lower counter
	ins		KS1.d[1], x11

	rev64		T1.16b, INP1.16b

	cmp		w7, #12
	b.ge		2f				// AES-192/256?

1:	enc_round	KS0, v21
	ext		IN1.16b, T1.16b, T1.16b, #8

	enc_round	KS1, v21
	pmull2		XH2.1q, SHASH.2d, IN1.2d	// a1 * b1

	enc_round	KS0, v22
	eor		T1.16b, T1.16b, IN1.16b

	enc_round	KS1, v22
	pmull		XL2.1q, SHASH.1d, IN1.1d	// a0 * b0

	enc_round	KS0, v23
	pmull		XM2.1q, SHASH2.1d, T1.1d	// (a1 + a0)(b1 + b0)

	enc_round	KS1, v23
	rev64		T1.16b, INP0.16b
	ext		T2.16b, XL.16b, XL.16b, #8

	enc_round	KS0, v24
	ext		IN1.16b, T1.16b, T1.16b, #8
	eor		T1.16b, T1.16b, T2.16b

	enc_round	KS1, v24
	eor		XL.16b, XL.16b, IN1.16b

	enc_round	KS0, v25
	eor		T1.16b, T1.16b, XL.16b

	enc_round	KS1, v25
	pmull2		XH.1q, HH.2d, XL.2d		// a1 * b1

	enc_round	KS0, v26
	pmull		XL.1q, HH.1d, XL.1d		// a0 * b0

	enc_round	KS1, v26
	pmull2		XM.1q, SHASH2.2d, T1.2d		// (a1 + a0)(b1 + b0)

	enc_round	KS0, v27
	eor		XL.16b, XL.16b, XL2.16b
	eor		XH.16b, XH.16b, XH2.16b

	enc_round	KS1, v27
	eor		XM.16b, XM.16b, XM2.16b
	ext		T1.16b, XL.16b, XH.16b, #8

	enc_round	KS0, v28
	eor		T2.16b, XL.16b, XH.16b
	eor		XM.16b, XM.16b, T1.16b

	enc_round	KS1, v28
	eor		XM.16b, XM.16b, T2.16b

	enc_round	KS0, v29
	pmull		T2.1q, XL.1d, MASK.1d

	enc_round	KS1, v29
	mov		XH.d[0], XM.d[1]
	mov		XM.d[1], XL.d[0]

	aese		KS0.16b, v30.16b
	eor		XL.16b, XM.16b, T2.16b

	aese		KS1.16b, v30.16b
	ext		T2.16b, XL.16b, XL.16b, #8

	eor		KS0.16b, KS0.16b, v31.16b
	pmull		XL.1q, XL.1d, MASK.1d
	eor		T2.16b, T2.16b, XH.16b

	eor		KS1.16b, KS1.16b, v31.16b
	eor		XL.16b, XL.16b, T2.16b

	.if		\enc == 0
	eor		INP0.16b, INP0.16b, KS0.16b
	eor		INP1.16b, INP1.16b, KS1.16b
	.endif

	st1		{INP0.16b-INP1.16b}, [x2], #32

	cbnz		w0, 0b

CPU_LE(	rev		x8, x8		)
	st1		{XL.2d}, [x1]
	str		x8, [x5, #8]			// store lower counter

	.if		\enc == 1
	st1		{KS0.16b-KS1.16b}, [x10]
	.endif

	ret

2:	b.eq		3f				// AES-192?
	enc_round	KS0, v17
	enc_round	KS1, v17
	enc_round	KS0, v18
	enc_round	KS1, v18
3:	enc_round	KS0, v19
	enc_round	KS1, v19
	enc_round	KS0, v20
	enc_round	KS1, v20
	b		1b

4:	load_round_keys	w7, x6
	b		0b
	.endm

	/*
	 * void pmull_gcm_encrypt(int blocks, u64 dg[], u8 dst[], const u8 src[],
	 *			  struct ghash_key const *k, u8 ctr[],
	 *			  int rounds, u8 ks[])
	 */
ENTRY(pmull_gcm_encrypt)
	pmull_gcm_do_crypt	1
ENDPROC(pmull_gcm_encrypt)

	/*
	 * void pmull_gcm_decrypt(int blocks, u64 dg[], u8 dst[], const u8 src[],
	 *			  struct ghash_key const *k, u8 ctr[],
	 *			  int rounds)
	 */
ENTRY(pmull_gcm_decrypt)
	pmull_gcm_do_crypt	0
ENDPROC(pmull_gcm_decrypt)

	/*
	 * void pmull_gcm_encrypt_block(u8 dst[], u8 src[], u8 rk[], int rounds)
	 */
ENTRY(pmull_gcm_encrypt_block)
	cbz		x2, 0f
	load_round_keys	w3, x2
0:	ld1		{v0.16b}, [x1]
	enc_block	v0, w3
	st1		{v0.16b}, [x0]
	ret
ENDPROC(pmull_gcm_encrypt_block)
