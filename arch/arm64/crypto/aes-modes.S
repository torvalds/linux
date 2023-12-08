/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * linux/arch/arm64/crypto/aes-modes.S - chaining mode wrappers for AES
 *
 * Copyright (C) 2013 - 2017 Linaro Ltd <ard.biesheuvel@linaro.org>
 */

/* included by aes-ce.S and aes-neon.S */

	.text
	.align		4

#ifndef MAX_STRIDE
#define MAX_STRIDE	4
#endif

#if MAX_STRIDE == 4
#define ST4(x...) x
#define ST5(x...)
#else
#define ST4(x...)
#define ST5(x...) x
#endif

SYM_FUNC_START_LOCAL(aes_encrypt_block4x)
	encrypt_block4x	v0, v1, v2, v3, w3, x2, x8, w7
	ret
SYM_FUNC_END(aes_encrypt_block4x)

SYM_FUNC_START_LOCAL(aes_decrypt_block4x)
	decrypt_block4x	v0, v1, v2, v3, w3, x2, x8, w7
	ret
SYM_FUNC_END(aes_decrypt_block4x)

#if MAX_STRIDE == 5
SYM_FUNC_START_LOCAL(aes_encrypt_block5x)
	encrypt_block5x	v0, v1, v2, v3, v4, w3, x2, x8, w7
	ret
SYM_FUNC_END(aes_encrypt_block5x)

SYM_FUNC_START_LOCAL(aes_decrypt_block5x)
	decrypt_block5x	v0, v1, v2, v3, v4, w3, x2, x8, w7
	ret
SYM_FUNC_END(aes_decrypt_block5x)
#endif

	/*
	 * aes_ecb_encrypt(u8 out[], u8 const in[], u8 const rk[], int rounds,
	 *		   int blocks)
	 * aes_ecb_decrypt(u8 out[], u8 const in[], u8 const rk[], int rounds,
	 *		   int blocks)
	 */

AES_FUNC_START(aes_ecb_encrypt)
	stp		x29, x30, [sp, #-16]!
	mov		x29, sp

	enc_prepare	w3, x2, x5

.LecbencloopNx:
	subs		w4, w4, #MAX_STRIDE
	bmi		.Lecbenc1x
	ld1		{v0.16b-v3.16b}, [x1], #64	/* get 4 pt blocks */
ST4(	bl		aes_encrypt_block4x		)
ST5(	ld1		{v4.16b}, [x1], #16		)
ST5(	bl		aes_encrypt_block5x		)
	st1		{v0.16b-v3.16b}, [x0], #64
ST5(	st1		{v4.16b}, [x0], #16		)
	b		.LecbencloopNx
.Lecbenc1x:
	adds		w4, w4, #MAX_STRIDE
	beq		.Lecbencout
.Lecbencloop:
	ld1		{v0.16b}, [x1], #16		/* get next pt block */
	encrypt_block	v0, w3, x2, x5, w6
	st1		{v0.16b}, [x0], #16
	subs		w4, w4, #1
	bne		.Lecbencloop
.Lecbencout:
	ldp		x29, x30, [sp], #16
	ret
AES_FUNC_END(aes_ecb_encrypt)


AES_FUNC_START(aes_ecb_decrypt)
	stp		x29, x30, [sp, #-16]!
	mov		x29, sp

	dec_prepare	w3, x2, x5

.LecbdecloopNx:
	subs		w4, w4, #MAX_STRIDE
	bmi		.Lecbdec1x
	ld1		{v0.16b-v3.16b}, [x1], #64	/* get 4 ct blocks */
ST4(	bl		aes_decrypt_block4x		)
ST5(	ld1		{v4.16b}, [x1], #16		)
ST5(	bl		aes_decrypt_block5x		)
	st1		{v0.16b-v3.16b}, [x0], #64
ST5(	st1		{v4.16b}, [x0], #16		)
	b		.LecbdecloopNx
.Lecbdec1x:
	adds		w4, w4, #MAX_STRIDE
	beq		.Lecbdecout
.Lecbdecloop:
	ld1		{v0.16b}, [x1], #16		/* get next ct block */
	decrypt_block	v0, w3, x2, x5, w6
	st1		{v0.16b}, [x0], #16
	subs		w4, w4, #1
	bne		.Lecbdecloop
.Lecbdecout:
	ldp		x29, x30, [sp], #16
	ret
AES_FUNC_END(aes_ecb_decrypt)


	/*
	 * aes_cbc_encrypt(u8 out[], u8 const in[], u8 const rk[], int rounds,
	 *		   int blocks, u8 iv[])
	 * aes_cbc_decrypt(u8 out[], u8 const in[], u8 const rk[], int rounds,
	 *		   int blocks, u8 iv[])
	 * aes_essiv_cbc_encrypt(u8 out[], u8 const in[], u32 const rk1[],
	 *			 int rounds, int blocks, u8 iv[],
	 *			 u32 const rk2[]);
	 * aes_essiv_cbc_decrypt(u8 out[], u8 const in[], u32 const rk1[],
	 *			 int rounds, int blocks, u8 iv[],
	 *			 u32 const rk2[]);
	 */

AES_FUNC_START(aes_essiv_cbc_encrypt)
	ld1		{v4.16b}, [x5]			/* get iv */

	mov		w8, #14				/* AES-256: 14 rounds */
	enc_prepare	w8, x6, x7
	encrypt_block	v4, w8, x6, x7, w9
	enc_switch_key	w3, x2, x6
	b		.Lcbcencloop4x

AES_FUNC_START(aes_cbc_encrypt)
	ld1		{v4.16b}, [x5]			/* get iv */
	enc_prepare	w3, x2, x6

.Lcbcencloop4x:
	subs		w4, w4, #4
	bmi		.Lcbcenc1x
	ld1		{v0.16b-v3.16b}, [x1], #64	/* get 4 pt blocks */
	eor		v0.16b, v0.16b, v4.16b		/* ..and xor with iv */
	encrypt_block	v0, w3, x2, x6, w7
	eor		v1.16b, v1.16b, v0.16b
	encrypt_block	v1, w3, x2, x6, w7
	eor		v2.16b, v2.16b, v1.16b
	encrypt_block	v2, w3, x2, x6, w7
	eor		v3.16b, v3.16b, v2.16b
	encrypt_block	v3, w3, x2, x6, w7
	st1		{v0.16b-v3.16b}, [x0], #64
	mov		v4.16b, v3.16b
	b		.Lcbcencloop4x
.Lcbcenc1x:
	adds		w4, w4, #4
	beq		.Lcbcencout
.Lcbcencloop:
	ld1		{v0.16b}, [x1], #16		/* get next pt block */
	eor		v4.16b, v4.16b, v0.16b		/* ..and xor with iv */
	encrypt_block	v4, w3, x2, x6, w7
	st1		{v4.16b}, [x0], #16
	subs		w4, w4, #1
	bne		.Lcbcencloop
.Lcbcencout:
	st1		{v4.16b}, [x5]			/* return iv */
	ret
AES_FUNC_END(aes_cbc_encrypt)
AES_FUNC_END(aes_essiv_cbc_encrypt)

AES_FUNC_START(aes_essiv_cbc_decrypt)
	stp		x29, x30, [sp, #-16]!
	mov		x29, sp

	ld1		{cbciv.16b}, [x5]		/* get iv */

	mov		w8, #14				/* AES-256: 14 rounds */
	enc_prepare	w8, x6, x7
	encrypt_block	cbciv, w8, x6, x7, w9
	b		.Lessivcbcdecstart

AES_FUNC_START(aes_cbc_decrypt)
	stp		x29, x30, [sp, #-16]!
	mov		x29, sp

	ld1		{cbciv.16b}, [x5]		/* get iv */
.Lessivcbcdecstart:
	dec_prepare	w3, x2, x6

.LcbcdecloopNx:
	subs		w4, w4, #MAX_STRIDE
	bmi		.Lcbcdec1x
	ld1		{v0.16b-v3.16b}, [x1], #64	/* get 4 ct blocks */
#if MAX_STRIDE == 5
	ld1		{v4.16b}, [x1], #16		/* get 1 ct block */
	mov		v5.16b, v0.16b
	mov		v6.16b, v1.16b
	mov		v7.16b, v2.16b
	bl		aes_decrypt_block5x
	sub		x1, x1, #32
	eor		v0.16b, v0.16b, cbciv.16b
	eor		v1.16b, v1.16b, v5.16b
	ld1		{v5.16b}, [x1], #16		/* reload 1 ct block */
	ld1		{cbciv.16b}, [x1], #16		/* reload 1 ct block */
	eor		v2.16b, v2.16b, v6.16b
	eor		v3.16b, v3.16b, v7.16b
	eor		v4.16b, v4.16b, v5.16b
#else
	mov		v4.16b, v0.16b
	mov		v5.16b, v1.16b
	mov		v6.16b, v2.16b
	bl		aes_decrypt_block4x
	sub		x1, x1, #16
	eor		v0.16b, v0.16b, cbciv.16b
	eor		v1.16b, v1.16b, v4.16b
	ld1		{cbciv.16b}, [x1], #16		/* reload 1 ct block */
	eor		v2.16b, v2.16b, v5.16b
	eor		v3.16b, v3.16b, v6.16b
#endif
	st1		{v0.16b-v3.16b}, [x0], #64
ST5(	st1		{v4.16b}, [x0], #16		)
	b		.LcbcdecloopNx
.Lcbcdec1x:
	adds		w4, w4, #MAX_STRIDE
	beq		.Lcbcdecout
.Lcbcdecloop:
	ld1		{v1.16b}, [x1], #16		/* get next ct block */
	mov		v0.16b, v1.16b			/* ...and copy to v0 */
	decrypt_block	v0, w3, x2, x6, w7
	eor		v0.16b, v0.16b, cbciv.16b	/* xor with iv => pt */
	mov		cbciv.16b, v1.16b		/* ct is next iv */
	st1		{v0.16b}, [x0], #16
	subs		w4, w4, #1
	bne		.Lcbcdecloop
.Lcbcdecout:
	st1		{cbciv.16b}, [x5]		/* return iv */
	ldp		x29, x30, [sp], #16
	ret
AES_FUNC_END(aes_cbc_decrypt)
AES_FUNC_END(aes_essiv_cbc_decrypt)


	/*
	 * aes_cbc_cts_encrypt(u8 out[], u8 const in[], u32 const rk[],
	 *		       int rounds, int bytes, u8 const iv[])
	 * aes_cbc_cts_decrypt(u8 out[], u8 const in[], u32 const rk[],
	 *		       int rounds, int bytes, u8 const iv[])
	 */

AES_FUNC_START(aes_cbc_cts_encrypt)
	adr_l		x8, .Lcts_permute_table
	sub		x4, x4, #16
	add		x9, x8, #32
	add		x8, x8, x4
	sub		x9, x9, x4
	ld1		{v3.16b}, [x8]
	ld1		{v4.16b}, [x9]

	ld1		{v0.16b}, [x1], x4		/* overlapping loads */
	ld1		{v1.16b}, [x1]

	ld1		{v5.16b}, [x5]			/* get iv */
	enc_prepare	w3, x2, x6

	eor		v0.16b, v0.16b, v5.16b		/* xor with iv */
	tbl		v1.16b, {v1.16b}, v4.16b
	encrypt_block	v0, w3, x2, x6, w7

	eor		v1.16b, v1.16b, v0.16b
	tbl		v0.16b, {v0.16b}, v3.16b
	encrypt_block	v1, w3, x2, x6, w7

	add		x4, x0, x4
	st1		{v0.16b}, [x4]			/* overlapping stores */
	st1		{v1.16b}, [x0]
	ret
AES_FUNC_END(aes_cbc_cts_encrypt)

AES_FUNC_START(aes_cbc_cts_decrypt)
	adr_l		x8, .Lcts_permute_table
	sub		x4, x4, #16
	add		x9, x8, #32
	add		x8, x8, x4
	sub		x9, x9, x4
	ld1		{v3.16b}, [x8]
	ld1		{v4.16b}, [x9]

	ld1		{v0.16b}, [x1], x4		/* overlapping loads */
	ld1		{v1.16b}, [x1]

	ld1		{v5.16b}, [x5]			/* get iv */
	dec_prepare	w3, x2, x6

	decrypt_block	v0, w3, x2, x6, w7
	tbl		v2.16b, {v0.16b}, v3.16b
	eor		v2.16b, v2.16b, v1.16b

	tbx		v0.16b, {v1.16b}, v4.16b
	decrypt_block	v0, w3, x2, x6, w7
	eor		v0.16b, v0.16b, v5.16b		/* xor with iv */

	add		x4, x0, x4
	st1		{v2.16b}, [x4]			/* overlapping stores */
	st1		{v0.16b}, [x0]
	ret
AES_FUNC_END(aes_cbc_cts_decrypt)

	.section	".rodata", "a"
	.align		6
.Lcts_permute_table:
	.byte		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
	.byte		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
	.byte		 0x0,  0x1,  0x2,  0x3,  0x4,  0x5,  0x6,  0x7
	.byte		 0x8,  0x9,  0xa,  0xb,  0xc,  0xd,  0xe,  0xf
	.byte		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
	.byte		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
	.previous

	/*
	 * This macro generates the code for CTR and XCTR mode.
	 */
.macro ctr_encrypt xctr
	// Arguments
	OUT		.req x0
	IN		.req x1
	KEY		.req x2
	ROUNDS_W	.req w3
	BYTES_W		.req w4
	IV		.req x5
	BYTE_CTR_W 	.req w6		// XCTR only
	// Intermediate values
	CTR_W		.req w11	// XCTR only
	CTR		.req x11	// XCTR only
	IV_PART		.req x12
	BLOCKS		.req x13
	BLOCKS_W	.req w13

	stp		x29, x30, [sp, #-16]!
	mov		x29, sp

	enc_prepare	ROUNDS_W, KEY, IV_PART
	ld1		{vctr.16b}, [IV]

	/*
	 * Keep 64 bits of the IV in a register.  For CTR mode this lets us
	 * easily increment the IV.  For XCTR mode this lets us efficiently XOR
	 * the 64-bit counter with the IV.
	 */
	.if \xctr
		umov		IV_PART, vctr.d[0]
		lsr		CTR_W, BYTE_CTR_W, #4
	.else
		umov		IV_PART, vctr.d[1]
		rev		IV_PART, IV_PART
	.endif

.LctrloopNx\xctr:
	add		BLOCKS_W, BYTES_W, #15
	sub		BYTES_W, BYTES_W, #MAX_STRIDE << 4
	lsr		BLOCKS_W, BLOCKS_W, #4
	mov		w8, #MAX_STRIDE
	cmp		BLOCKS_W, w8
	csel		BLOCKS_W, BLOCKS_W, w8, lt

	/*
	 * Set up the counter values in v0-v{MAX_STRIDE-1}.
	 *
	 * If we are encrypting less than MAX_STRIDE blocks, the tail block
	 * handling code expects the last keystream block to be in
	 * v{MAX_STRIDE-1}.  For example: if encrypting two blocks with
	 * MAX_STRIDE=5, then v3 and v4 should have the next two counter blocks.
	 */
	.if \xctr
		add		CTR, CTR, BLOCKS
	.else
		adds		IV_PART, IV_PART, BLOCKS
	.endif
	mov		v0.16b, vctr.16b
	mov		v1.16b, vctr.16b
	mov		v2.16b, vctr.16b
	mov		v3.16b, vctr.16b
ST5(	mov		v4.16b, vctr.16b		)
	.if \xctr
		sub		x6, CTR, #MAX_STRIDE - 1
		sub		x7, CTR, #MAX_STRIDE - 2
		sub		x8, CTR, #MAX_STRIDE - 3
		sub		x9, CTR, #MAX_STRIDE - 4
ST5(		sub		x10, CTR, #MAX_STRIDE - 5	)
		eor		x6, x6, IV_PART
		eor		x7, x7, IV_PART
		eor		x8, x8, IV_PART
		eor		x9, x9, IV_PART
ST5(		eor		x10, x10, IV_PART		)
		mov		v0.d[0], x6
		mov		v1.d[0], x7
		mov		v2.d[0], x8
		mov		v3.d[0], x9
ST5(		mov		v4.d[0], x10			)
	.else
		bcs		0f
		.subsection	1
		/*
		 * This subsection handles carries.
		 *
		 * Conditional branching here is allowed with respect to time
		 * invariance since the branches are dependent on the IV instead
		 * of the plaintext or key.  This code is rarely executed in
		 * practice anyway.
		 */

		/* Apply carry to outgoing counter. */
0:		umov		x8, vctr.d[0]
		rev		x8, x8
		add		x8, x8, #1
		rev		x8, x8
		ins		vctr.d[0], x8

		/*
		 * Apply carry to counter blocks if needed.
		 *
		 * Since the carry flag was set, we know 0 <= IV_PART <
		 * MAX_STRIDE.  Using the value of IV_PART we can determine how
		 * many counter blocks need to be updated.
		 */
		cbz		IV_PART, 2f
		adr		x16, 1f
		sub		x16, x16, IV_PART, lsl #3
		br		x16
		bti		c
		mov		v0.d[0], vctr.d[0]
		bti		c
		mov		v1.d[0], vctr.d[0]
		bti		c
		mov		v2.d[0], vctr.d[0]
		bti		c
		mov		v3.d[0], vctr.d[0]
ST5(		bti		c				)
ST5(		mov		v4.d[0], vctr.d[0]		)
1:		b		2f
		.previous

2:		rev		x7, IV_PART
		ins		vctr.d[1], x7
		sub		x7, IV_PART, #MAX_STRIDE - 1
		sub		x8, IV_PART, #MAX_STRIDE - 2
		sub		x9, IV_PART, #MAX_STRIDE - 3
		rev		x7, x7
		rev		x8, x8
		mov		v1.d[1], x7
		rev		x9, x9
ST5(		sub		x10, IV_PART, #MAX_STRIDE - 4	)
		mov		v2.d[1], x8
ST5(		rev		x10, x10			)
		mov		v3.d[1], x9
ST5(		mov		v4.d[1], x10			)
	.endif

	/*
	 * If there are at least MAX_STRIDE blocks left, XOR the data with
	 * keystream and store.  Otherwise jump to tail handling.
	 */
	tbnz		BYTES_W, #31, .Lctrtail\xctr
	ld1		{v5.16b-v7.16b}, [IN], #48
ST4(	bl		aes_encrypt_block4x		)
ST5(	bl		aes_encrypt_block5x		)
	eor		v0.16b, v5.16b, v0.16b
ST4(	ld1		{v5.16b}, [IN], #16		)
	eor		v1.16b, v6.16b, v1.16b
ST5(	ld1		{v5.16b-v6.16b}, [IN], #32	)
	eor		v2.16b, v7.16b, v2.16b
	eor		v3.16b, v5.16b, v3.16b
ST5(	eor		v4.16b, v6.16b, v4.16b		)
	st1		{v0.16b-v3.16b}, [OUT], #64
ST5(	st1		{v4.16b}, [OUT], #16		)
	cbz		BYTES_W, .Lctrout\xctr
	b		.LctrloopNx\xctr

.Lctrout\xctr:
	.if !\xctr
		st1		{vctr.16b}, [IV] /* return next CTR value */
	.endif
	ldp		x29, x30, [sp], #16
	ret

.Lctrtail\xctr:
	/*
	 * Handle up to MAX_STRIDE * 16 - 1 bytes of plaintext
	 *
	 * This code expects the last keystream block to be in v{MAX_STRIDE-1}.
	 * For example: if encrypting two blocks with MAX_STRIDE=5, then v3 and
	 * v4 should have the next two counter blocks.
	 *
	 * This allows us to store the ciphertext by writing to overlapping
	 * regions of memory.  Any invalid ciphertext blocks get overwritten by
	 * correctly computed blocks.  This approach greatly simplifies the
	 * logic for storing the ciphertext.
	 */
	mov		x16, #16
	ands		w7, BYTES_W, #0xf
	csel		x13, x7, x16, ne

ST5(	cmp		BYTES_W, #64 - (MAX_STRIDE << 4))
ST5(	csel		x14, x16, xzr, gt		)
	cmp		BYTES_W, #48 - (MAX_STRIDE << 4)
	csel		x15, x16, xzr, gt
	cmp		BYTES_W, #32 - (MAX_STRIDE << 4)
	csel		x16, x16, xzr, gt
	cmp		BYTES_W, #16 - (MAX_STRIDE << 4)

	adr_l		x9, .Lcts_permute_table
	add		x9, x9, x13
	ble		.Lctrtail1x\xctr

ST5(	ld1		{v5.16b}, [IN], x14		)
	ld1		{v6.16b}, [IN], x15
	ld1		{v7.16b}, [IN], x16

ST4(	bl		aes_encrypt_block4x		)
ST5(	bl		aes_encrypt_block5x		)

	ld1		{v8.16b}, [IN], x13
	ld1		{v9.16b}, [IN]
	ld1		{v10.16b}, [x9]

ST4(	eor		v6.16b, v6.16b, v0.16b		)
ST4(	eor		v7.16b, v7.16b, v1.16b		)
ST4(	tbl		v3.16b, {v3.16b}, v10.16b	)
ST4(	eor		v8.16b, v8.16b, v2.16b		)
ST4(	eor		v9.16b, v9.16b, v3.16b		)

ST5(	eor		v5.16b, v5.16b, v0.16b		)
ST5(	eor		v6.16b, v6.16b, v1.16b		)
ST5(	tbl		v4.16b, {v4.16b}, v10.16b	)
ST5(	eor		v7.16b, v7.16b, v2.16b		)
ST5(	eor		v8.16b, v8.16b, v3.16b		)
ST5(	eor		v9.16b, v9.16b, v4.16b		)

ST5(	st1		{v5.16b}, [OUT], x14		)
	st1		{v6.16b}, [OUT], x15
	st1		{v7.16b}, [OUT], x16
	add		x13, x13, OUT
	st1		{v9.16b}, [x13]		// overlapping stores
	st1		{v8.16b}, [OUT]
	b		.Lctrout\xctr

.Lctrtail1x\xctr:
	/*
	 * Handle <= 16 bytes of plaintext
	 *
	 * This code always reads and writes 16 bytes.  To avoid out of bounds
	 * accesses, XCTR and CTR modes must use a temporary buffer when
	 * encrypting/decrypting less than 16 bytes.
	 *
	 * This code is unusual in that it loads the input and stores the output
	 * relative to the end of the buffers rather than relative to the start.
	 * This causes unusual behaviour when encrypting/decrypting less than 16
	 * bytes; the end of the data is expected to be at the end of the
	 * temporary buffer rather than the start of the data being at the start
	 * of the temporary buffer.
	 */
	sub		x8, x7, #16
	csel		x7, x7, x8, eq
	add		IN, IN, x7
	add		OUT, OUT, x7
	ld1		{v5.16b}, [IN]
	ld1		{v6.16b}, [OUT]
ST5(	mov		v3.16b, v4.16b			)
	encrypt_block	v3, ROUNDS_W, KEY, x8, w7
	ld1		{v10.16b-v11.16b}, [x9]
	tbl		v3.16b, {v3.16b}, v10.16b
	sshr		v11.16b, v11.16b, #7
	eor		v5.16b, v5.16b, v3.16b
	bif		v5.16b, v6.16b, v11.16b
	st1		{v5.16b}, [OUT]
	b		.Lctrout\xctr

	// Arguments
	.unreq OUT
	.unreq IN
	.unreq KEY
	.unreq ROUNDS_W
	.unreq BYTES_W
	.unreq IV
	.unreq BYTE_CTR_W	// XCTR only
	// Intermediate values
	.unreq CTR_W		// XCTR only
	.unreq CTR		// XCTR only
	.unreq IV_PART
	.unreq BLOCKS
	.unreq BLOCKS_W
.endm

	/*
	 * aes_ctr_encrypt(u8 out[], u8 const in[], u8 const rk[], int rounds,
	 *		   int bytes, u8 ctr[])
	 *
	 * The input and output buffers must always be at least 16 bytes even if
	 * encrypting/decrypting less than 16 bytes.  Otherwise out of bounds
	 * accesses will occur.  The data to be encrypted/decrypted is expected
	 * to be at the end of this 16-byte temporary buffer rather than the
	 * start.
	 */

AES_FUNC_START(aes_ctr_encrypt)
	ctr_encrypt 0
AES_FUNC_END(aes_ctr_encrypt)

	/*
	 * aes_xctr_encrypt(u8 out[], u8 const in[], u8 const rk[], int rounds,
	 *		   int bytes, u8 const iv[], int byte_ctr)
	 *
	 * The input and output buffers must always be at least 16 bytes even if
	 * encrypting/decrypting less than 16 bytes.  Otherwise out of bounds
	 * accesses will occur.  The data to be encrypted/decrypted is expected
	 * to be at the end of this 16-byte temporary buffer rather than the
	 * start.
	 */

AES_FUNC_START(aes_xctr_encrypt)
	ctr_encrypt 1
AES_FUNC_END(aes_xctr_encrypt)


	/*
	 * aes_xts_encrypt(u8 out[], u8 const in[], u8 const rk1[], int rounds,
	 *		   int bytes, u8 const rk2[], u8 iv[], int first)
	 * aes_xts_decrypt(u8 out[], u8 const in[], u8 const rk1[], int rounds,
	 *		   int bytes, u8 const rk2[], u8 iv[], int first)
	 */

	.macro		next_tweak, out, in, tmp
	sshr		\tmp\().2d,  \in\().2d,   #63
	and		\tmp\().16b, \tmp\().16b, xtsmask.16b
	add		\out\().2d,  \in\().2d,   \in\().2d
	ext		\tmp\().16b, \tmp\().16b, \tmp\().16b, #8
	eor		\out\().16b, \out\().16b, \tmp\().16b
	.endm

	.macro		xts_load_mask, tmp
	movi		xtsmask.2s, #0x1
	movi		\tmp\().2s, #0x87
	uzp1		xtsmask.4s, xtsmask.4s, \tmp\().4s
	.endm

AES_FUNC_START(aes_xts_encrypt)
	stp		x29, x30, [sp, #-16]!
	mov		x29, sp

	ld1		{v4.16b}, [x6]
	xts_load_mask	v8
	cbz		w7, .Lxtsencnotfirst

	enc_prepare	w3, x5, x8
	xts_cts_skip_tw	w7, .LxtsencNx
	encrypt_block	v4, w3, x5, x8, w7		/* first tweak */
	enc_switch_key	w3, x2, x8
	b		.LxtsencNx

.Lxtsencnotfirst:
	enc_prepare	w3, x2, x8
.LxtsencloopNx:
	next_tweak	v4, v4, v8
.LxtsencNx:
	subs		w4, w4, #64
	bmi		.Lxtsenc1x
	ld1		{v0.16b-v3.16b}, [x1], #64	/* get 4 pt blocks */
	next_tweak	v5, v4, v8
	eor		v0.16b, v0.16b, v4.16b
	next_tweak	v6, v5, v8
	eor		v1.16b, v1.16b, v5.16b
	eor		v2.16b, v2.16b, v6.16b
	next_tweak	v7, v6, v8
	eor		v3.16b, v3.16b, v7.16b
	bl		aes_encrypt_block4x
	eor		v3.16b, v3.16b, v7.16b
	eor		v0.16b, v0.16b, v4.16b
	eor		v1.16b, v1.16b, v5.16b
	eor		v2.16b, v2.16b, v6.16b
	st1		{v0.16b-v3.16b}, [x0], #64
	mov		v4.16b, v7.16b
	cbz		w4, .Lxtsencret
	xts_reload_mask	v8
	b		.LxtsencloopNx
.Lxtsenc1x:
	adds		w4, w4, #64
	beq		.Lxtsencout
	subs		w4, w4, #16
	bmi		.LxtsencctsNx
.Lxtsencloop:
	ld1		{v0.16b}, [x1], #16
.Lxtsencctsout:
	eor		v0.16b, v0.16b, v4.16b
	encrypt_block	v0, w3, x2, x8, w7
	eor		v0.16b, v0.16b, v4.16b
	cbz		w4, .Lxtsencout
	subs		w4, w4, #16
	next_tweak	v4, v4, v8
	bmi		.Lxtsenccts
	st1		{v0.16b}, [x0], #16
	b		.Lxtsencloop
.Lxtsencout:
	st1		{v0.16b}, [x0]
.Lxtsencret:
	st1		{v4.16b}, [x6]
	ldp		x29, x30, [sp], #16
	ret

.LxtsencctsNx:
	mov		v0.16b, v3.16b
	sub		x0, x0, #16
.Lxtsenccts:
	adr_l		x8, .Lcts_permute_table

	add		x1, x1, w4, sxtw	/* rewind input pointer */
	add		w4, w4, #16		/* # bytes in final block */
	add		x9, x8, #32
	add		x8, x8, x4
	sub		x9, x9, x4
	add		x4, x0, x4		/* output address of final block */

	ld1		{v1.16b}, [x1]		/* load final block */
	ld1		{v2.16b}, [x8]
	ld1		{v3.16b}, [x9]

	tbl		v2.16b, {v0.16b}, v2.16b
	tbx		v0.16b, {v1.16b}, v3.16b
	st1		{v2.16b}, [x4]			/* overlapping stores */
	mov		w4, wzr
	b		.Lxtsencctsout
AES_FUNC_END(aes_xts_encrypt)

AES_FUNC_START(aes_xts_decrypt)
	stp		x29, x30, [sp, #-16]!
	mov		x29, sp

	/* subtract 16 bytes if we are doing CTS */
	sub		w8, w4, #0x10
	tst		w4, #0xf
	csel		w4, w4, w8, eq

	ld1		{v4.16b}, [x6]
	xts_load_mask	v8
	xts_cts_skip_tw	w7, .Lxtsdecskiptw
	cbz		w7, .Lxtsdecnotfirst

	enc_prepare	w3, x5, x8
	encrypt_block	v4, w3, x5, x8, w7		/* first tweak */
.Lxtsdecskiptw:
	dec_prepare	w3, x2, x8
	b		.LxtsdecNx

.Lxtsdecnotfirst:
	dec_prepare	w3, x2, x8
.LxtsdecloopNx:
	next_tweak	v4, v4, v8
.LxtsdecNx:
	subs		w4, w4, #64
	bmi		.Lxtsdec1x
	ld1		{v0.16b-v3.16b}, [x1], #64	/* get 4 ct blocks */
	next_tweak	v5, v4, v8
	eor		v0.16b, v0.16b, v4.16b
	next_tweak	v6, v5, v8
	eor		v1.16b, v1.16b, v5.16b
	eor		v2.16b, v2.16b, v6.16b
	next_tweak	v7, v6, v8
	eor		v3.16b, v3.16b, v7.16b
	bl		aes_decrypt_block4x
	eor		v3.16b, v3.16b, v7.16b
	eor		v0.16b, v0.16b, v4.16b
	eor		v1.16b, v1.16b, v5.16b
	eor		v2.16b, v2.16b, v6.16b
	st1		{v0.16b-v3.16b}, [x0], #64
	mov		v4.16b, v7.16b
	cbz		w4, .Lxtsdecout
	xts_reload_mask	v8
	b		.LxtsdecloopNx
.Lxtsdec1x:
	adds		w4, w4, #64
	beq		.Lxtsdecout
	subs		w4, w4, #16
.Lxtsdecloop:
	ld1		{v0.16b}, [x1], #16
	bmi		.Lxtsdeccts
.Lxtsdecctsout:
	eor		v0.16b, v0.16b, v4.16b
	decrypt_block	v0, w3, x2, x8, w7
	eor		v0.16b, v0.16b, v4.16b
	st1		{v0.16b}, [x0], #16
	cbz		w4, .Lxtsdecout
	subs		w4, w4, #16
	next_tweak	v4, v4, v8
	b		.Lxtsdecloop
.Lxtsdecout:
	st1		{v4.16b}, [x6]
	ldp		x29, x30, [sp], #16
	ret

.Lxtsdeccts:
	adr_l		x8, .Lcts_permute_table

	add		x1, x1, w4, sxtw	/* rewind input pointer */
	add		w4, w4, #16		/* # bytes in final block */
	add		x9, x8, #32
	add		x8, x8, x4
	sub		x9, x9, x4
	add		x4, x0, x4		/* output address of final block */

	next_tweak	v5, v4, v8

	ld1		{v1.16b}, [x1]		/* load final block */
	ld1		{v2.16b}, [x8]
	ld1		{v3.16b}, [x9]

	eor		v0.16b, v0.16b, v5.16b
	decrypt_block	v0, w3, x2, x8, w7
	eor		v0.16b, v0.16b, v5.16b

	tbl		v2.16b, {v0.16b}, v2.16b
	tbx		v0.16b, {v1.16b}, v3.16b

	st1		{v2.16b}, [x4]			/* overlapping stores */
	mov		w4, wzr
	b		.Lxtsdecctsout
AES_FUNC_END(aes_xts_decrypt)

	/*
	 * aes_mac_update(u8 const in[], u32 const rk[], int rounds,
	 *		  int blocks, u8 dg[], int enc_before, int enc_after)
	 */
AES_FUNC_START(aes_mac_update)
	ld1		{v0.16b}, [x4]			/* get dg */
	enc_prepare	w2, x1, x7
	cbz		w5, .Lmacloop4x

	encrypt_block	v0, w2, x1, x7, w8

.Lmacloop4x:
	subs		w3, w3, #4
	bmi		.Lmac1x
	ld1		{v1.16b-v4.16b}, [x0], #64	/* get next pt block */
	eor		v0.16b, v0.16b, v1.16b		/* ..and xor with dg */
	encrypt_block	v0, w2, x1, x7, w8
	eor		v0.16b, v0.16b, v2.16b
	encrypt_block	v0, w2, x1, x7, w8
	eor		v0.16b, v0.16b, v3.16b
	encrypt_block	v0, w2, x1, x7, w8
	eor		v0.16b, v0.16b, v4.16b
	cmp		w3, wzr
	csinv		x5, x6, xzr, eq
	cbz		w5, .Lmacout
	encrypt_block	v0, w2, x1, x7, w8
	st1		{v0.16b}, [x4]			/* return dg */
	cond_yield	.Lmacout, x7, x8
	b		.Lmacloop4x
.Lmac1x:
	add		w3, w3, #4
.Lmacloop:
	cbz		w3, .Lmacout
	ld1		{v1.16b}, [x0], #16		/* get next pt block */
	eor		v0.16b, v0.16b, v1.16b		/* ..and xor with dg */

	subs		w3, w3, #1
	csinv		x5, x6, xzr, eq
	cbz		w5, .Lmacout

.Lmacenc:
	encrypt_block	v0, w2, x1, x7, w8
	b		.Lmacloop

.Lmacout:
	st1		{v0.16b}, [x4]			/* return dg */
	mov		w0, w3
	ret
AES_FUNC_END(aes_mac_update)
