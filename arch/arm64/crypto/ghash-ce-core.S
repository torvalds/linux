/*
 * Accelerated GHASH implementation with ARMv8 PMULL instructions.
 *
 * Copyright (C) 2014 - 2017 Linaro Ltd. <ard.biesheuvel@linaro.org>
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
	movi		MASK.16b, #0xe1
	shl		MASK.2d, MASK.2d, #57
	.endm

	.macro		__pmull_pre_p8
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
	frame_push	5

	mov		x19, x0
	mov		x20, x1
	mov		x21, x2
	mov		x22, x3
	mov		x23, x4

0:	ld1		{SHASH.2d}, [x22]
	ld1		{XL.2d}, [x20]
	ext		SHASH2.16b, SHASH.16b, SHASH.16b, #8
	eor		SHASH2.16b, SHASH2.16b, SHASH.16b

	__pmull_pre_\pn

	/* do the head block first, if supplied */
	cbz		x23, 1f
	ld1		{T1.2d}, [x23]
	mov		x23, xzr
	b		2f

1:	ld1		{T1.2d}, [x21], #16
	sub		w19, w19, #1

2:	/* multiply XL by SHASH in GF(2^128) */
CPU_LE(	rev64		T1.16b, T1.16b	)

	ext		T2.16b, XL.16b, XL.16b, #8
	ext		IN1.16b, T1.16b, T1.16b, #8
	eor		T1.16b, T1.16b, T2.16b
	eor		XL.16b, XL.16b, IN1.16b

	__pmull2_\pn	XH, XL, SHASH			// a1 * b1
	eor		T1.16b, T1.16b, XL.16b
	__pmull_\pn 	XL, XL, SHASH			// a0 * b0
	__pmull_\pn	XM, T1, SHASH2			// (a1 + a0)(b1 + b0)

	eor		T2.16b, XL.16b, XH.16b
	ext		T1.16b, XL.16b, XH.16b, #8
	eor		XM.16b, XM.16b, T2.16b

	__pmull_reduce_\pn

	eor		T2.16b, T2.16b, XH.16b
	eor		XL.16b, XL.16b, T2.16b

	cbz		w19, 3f

	if_will_cond_yield_neon
	st1		{XL.2d}, [x20]
	do_cond_yield_neon
	b		0b
	endif_yield_neon

	b		1b

3:	st1		{XL.2d}, [x20]
	frame_pop
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

	KS		.req	v8
	CTR		.req	v9
	INP		.req	v10

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
	frame_push	10

	mov		x19, x0
	mov		x20, x1
	mov		x21, x2
	mov		x22, x3
	mov		x23, x4
	mov		x24, x5
	mov		x25, x6
	mov		x26, x7
	.if		\enc == 1
	ldr		x27, [sp, #96]			// first stacked arg
	.endif

	ldr		x28, [x24, #8]			// load lower counter
CPU_LE(	rev		x28, x28	)

0:	mov		x0, x25
	load_round_keys	w26, x0
	ld1		{SHASH.2d}, [x23]
	ld1		{XL.2d}, [x20]

	movi		MASK.16b, #0xe1
	ext		SHASH2.16b, SHASH.16b, SHASH.16b, #8
	shl		MASK.2d, MASK.2d, #57
	eor		SHASH2.16b, SHASH2.16b, SHASH.16b

	.if		\enc == 1
	ld1		{KS.16b}, [x27]
	.endif

1:	ld1		{CTR.8b}, [x24]			// load upper counter
	ld1		{INP.16b}, [x22], #16
	rev		x9, x28
	add		x28, x28, #1
	sub		w19, w19, #1
	ins		CTR.d[1], x9			// set lower counter

	.if		\enc == 1
	eor		INP.16b, INP.16b, KS.16b	// encrypt input
	st1		{INP.16b}, [x21], #16
	.endif

	rev64		T1.16b, INP.16b

	cmp		w26, #12
	b.ge		4f				// AES-192/256?

2:	enc_round	CTR, v21

	ext		T2.16b, XL.16b, XL.16b, #8
	ext		IN1.16b, T1.16b, T1.16b, #8

	enc_round	CTR, v22

	eor		T1.16b, T1.16b, T2.16b
	eor		XL.16b, XL.16b, IN1.16b

	enc_round	CTR, v23

	pmull2		XH.1q, SHASH.2d, XL.2d		// a1 * b1
	eor		T1.16b, T1.16b, XL.16b

	enc_round	CTR, v24

	pmull		XL.1q, SHASH.1d, XL.1d		// a0 * b0
	pmull		XM.1q, SHASH2.1d, T1.1d		// (a1 + a0)(b1 + b0)

	enc_round	CTR, v25

	ext		T1.16b, XL.16b, XH.16b, #8
	eor		T2.16b, XL.16b, XH.16b
	eor		XM.16b, XM.16b, T1.16b

	enc_round	CTR, v26

	eor		XM.16b, XM.16b, T2.16b
	pmull		T2.1q, XL.1d, MASK.1d

	enc_round	CTR, v27

	mov		XH.d[0], XM.d[1]
	mov		XM.d[1], XL.d[0]

	enc_round	CTR, v28

	eor		XL.16b, XM.16b, T2.16b

	enc_round	CTR, v29

	ext		T2.16b, XL.16b, XL.16b, #8

	aese		CTR.16b, v30.16b

	pmull		XL.1q, XL.1d, MASK.1d
	eor		T2.16b, T2.16b, XH.16b

	eor		KS.16b, CTR.16b, v31.16b

	eor		XL.16b, XL.16b, T2.16b

	.if		\enc == 0
	eor		INP.16b, INP.16b, KS.16b
	st1		{INP.16b}, [x21], #16
	.endif

	cbz		w19, 3f

	if_will_cond_yield_neon
	st1		{XL.2d}, [x20]
	.if		\enc == 1
	st1		{KS.16b}, [x27]
	.endif
	do_cond_yield_neon
	b		0b
	endif_yield_neon

	b		1b

3:	st1		{XL.2d}, [x20]
	.if		\enc == 1
	st1		{KS.16b}, [x27]
	.endif

CPU_LE(	rev		x28, x28	)
	str		x28, [x24, #8]			// store lower counter

	frame_pop
	ret

4:	b.eq		5f				// AES-192?
	enc_round	CTR, v17
	enc_round	CTR, v18
5:	enc_round	CTR, v19
	enc_round	CTR, v20
	b		2b
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
