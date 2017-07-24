/*
 * Accelerated GHASH implementation with ARMv8 PMULL instructions.
 *
 * Copyright (C) 2014 Linaro Ltd. <ard.biesheuvel@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include <linux/linkage.h>
#include <asm/assembler.h>

	SHASH	.req	v0
	SHASH2	.req	v1
	T1	.req	v2
	T2	.req	v3
	MASK	.req	v4
	XL	.req	v5
	XM	.req	v6
	XH	.req	v7
	IN1	.req	v7

	.text
	.arch		armv8-a+crypto

	/*
	 * void pmull_ghash_update(int blocks, u64 dg[], const char *src,
	 *			   struct ghash_key const *k, const char *head)
	 */
ENTRY(pmull_ghash_update)
	ld1		{SHASH.2d}, [x3]
	ld1		{XL.2d}, [x1]
	movi		MASK.16b, #0xe1
	ext		SHASH2.16b, SHASH.16b, SHASH.16b, #8
	shl		MASK.2d, MASK.2d, #57
	eor		SHASH2.16b, SHASH2.16b, SHASH.16b

	/* do the head block first, if supplied */
	cbz		x4, 0f
	ld1		{T1.2d}, [x4]
	b		1f

0:	ld1		{T1.2d}, [x2], #16
	sub		w0, w0, #1

1:	/* multiply XL by SHASH in GF(2^128) */
CPU_LE(	rev64		T1.16b, T1.16b	)

	ext		T2.16b, XL.16b, XL.16b, #8
	ext		IN1.16b, T1.16b, T1.16b, #8
	eor		T1.16b, T1.16b, T2.16b
	eor		XL.16b, XL.16b, IN1.16b

	pmull2		XH.1q, SHASH.2d, XL.2d		// a1 * b1
	eor		T1.16b, T1.16b, XL.16b
	pmull		XL.1q, SHASH.1d, XL.1d		// a0 * b0
	pmull		XM.1q, SHASH2.1d, T1.1d		// (a1 + a0)(b1 + b0)

	ext		T1.16b, XL.16b, XH.16b, #8
	eor		T2.16b, XL.16b, XH.16b
	eor		XM.16b, XM.16b, T1.16b
	eor		XM.16b, XM.16b, T2.16b
	pmull		T2.1q, XL.1d, MASK.1d

	mov		XH.d[0], XM.d[1]
	mov		XM.d[1], XL.d[0]

	eor		XL.16b, XM.16b, T2.16b
	ext		T2.16b, XL.16b, XL.16b, #8
	pmull		XL.1q, XL.1d, MASK.1d
	eor		T2.16b, T2.16b, XH.16b
	eor		XL.16b, XL.16b, T2.16b

	cbnz		w0, 0b

	st1		{XL.2d}, [x1]
	ret
ENDPROC(pmull_ghash_update)

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
	ld1		{SHASH.2d}, [x4]
	ld1		{XL.2d}, [x1]
	ldr		x8, [x5, #8]			// load lower counter

	movi		MASK.16b, #0xe1
	ext		SHASH2.16b, SHASH.16b, SHASH.16b, #8
CPU_LE(	rev		x8, x8		)
	shl		MASK.2d, MASK.2d, #57
	eor		SHASH2.16b, SHASH2.16b, SHASH.16b

	.if		\enc == 1
	ld1		{KS.16b}, [x7]
	.endif

0:	ld1		{CTR.8b}, [x5]			// load upper counter
	ld1		{INP.16b}, [x3], #16
	rev		x9, x8
	add		x8, x8, #1
	sub		w0, w0, #1
	ins		CTR.d[1], x9			// set lower counter

	.if		\enc == 1
	eor		INP.16b, INP.16b, KS.16b	// encrypt input
	st1		{INP.16b}, [x2], #16
	.endif

	rev64		T1.16b, INP.16b

	cmp		w6, #12
	b.ge		2f				// AES-192/256?

1:	enc_round	CTR, v21

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
	st1		{INP.16b}, [x2], #16
	.endif

	cbnz		w0, 0b

CPU_LE(	rev		x8, x8		)
	st1		{XL.2d}, [x1]
	str		x8, [x5, #8]			// store lower counter

	.if		\enc == 1
	st1		{KS.16b}, [x7]
	.endif

	ret

2:	b.eq		3f				// AES-192?
	enc_round	CTR, v17
	enc_round	CTR, v18
3:	enc_round	CTR, v19
	enc_round	CTR, v20
	b		1b
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
