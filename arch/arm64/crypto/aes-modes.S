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
	 * aes_ctr_encrypt(u8 out[], u8 const in[], u8 const rk[], int rounds,
	 *		   int bytes, u8 ctr[], u8 finalbuf[])
	 */

AES_FUNC_START(aes_ctr_encrypt)
	stp		x29, x30, [sp, #-16]!
	mov		x29, sp

	enc_prepare	w3, x2, x12
	ld1		{vctr.16b}, [x5]

	umov		x12, vctr.d[1]		/* keep swabbed ctr in reg */
	rev		x12, x12

.LctrloopNx:
	add		w7, w4, #15
	sub		w4, w4, #MAX_STRIDE << 4
	lsr		w7, w7, #4
	mov		w8, #MAX_STRIDE
	cmp		w7, w8
	csel		w7, w7, w8, lt
	adds		x12, x12, x7

	mov		v0.16b, vctr.16b
	mov		v1.16b, vctr.16b
	mov		v2.16b, vctr.16b
	mov		v3.16b, vctr.16b
ST5(	mov		v4.16b, vctr.16b		)
	bcs		0f

	.subsection	1
	/* apply carry to outgoing counter */
0:	umov		x8, vctr.d[0]
	rev		x8, x8
	add		x8, x8, #1
	rev		x8, x8
	ins		vctr.d[0], x8

	/* apply carry to N counter blocks for N := x12 */
	cbz		x12, 2f
	adr		x16, 1f
	sub		x16, x16, x12, lsl #3
	br		x16
	hint		34			// bti c
	mov		v0.d[0], vctr.d[0]
	hint		34			// bti c
	mov		v1.d[0], vctr.d[0]
	hint		34			// bti c
	mov		v2.d[0], vctr.d[0]
	hint		34			// bti c
	mov		v3.d[0], vctr.d[0]
ST5(	hint		34				)
ST5(	mov		v4.d[0], vctr.d[0]		)
1:	b		2f
	.previous

2:	rev		x7, x12
	ins		vctr.d[1], x7
	sub		x7, x12, #MAX_STRIDE - 1
	sub		x8, x12, #MAX_STRIDE - 2
	sub		x9, x12, #MAX_STRIDE - 3
	rev		x7, x7
	rev		x8, x8
	mov		v1.d[1], x7
	rev		x9, x9
ST5(	sub		x10, x12, #MAX_STRIDE - 4	)
	mov		v2.d[1], x8
ST5(	rev		x10, x10			)
	mov		v3.d[1], x9
ST5(	mov		v4.d[1], x10			)
	tbnz		w4, #31, .Lctrtail
	ld1		{v5.16b-v7.16b}, [x1], #48
ST4(	bl		aes_encrypt_block4x		)
ST5(	bl		aes_encrypt_block5x		)
	eor		v0.16b, v5.16b, v0.16b
ST4(	ld1		{v5.16b}, [x1], #16		)
	eor		v1.16b, v6.16b, v1.16b
ST5(	ld1		{v5.16b-v6.16b}, [x1], #32	)
	eor		v2.16b, v7.16b, v2.16b
	eor		v3.16b, v5.16b, v3.16b
ST5(	eor		v4.16b, v6.16b, v4.16b		)
	st1		{v0.16b-v3.16b}, [x0], #64
ST5(	st1		{v4.16b}, [x0], #16		)
	cbz		w4, .Lctrout
	b		.LctrloopNx

.Lctrout:
	st1		{vctr.16b}, [x5]	/* return next CTR value */
	ldp		x29, x30, [sp], #16
	ret

.Lctrtail:
	/* XOR up to MAX_STRIDE * 16 - 1 bytes of in/output with v0 ... v3/v4 */
	mov		x16, #16
	ands		x13, x4, #0xf
	csel		x13, x13, x16, ne

ST5(	cmp		w4, #64 - (MAX_STRIDE << 4)	)
ST5(	csel		x14, x16, xzr, gt		)
	cmp		w4, #48 - (MAX_STRIDE << 4)
	csel		x15, x16, xzr, gt
	cmp		w4, #32 - (MAX_STRIDE << 4)
	csel		x16, x16, xzr, gt
	cmp		w4, #16 - (MAX_STRIDE << 4)
	ble		.Lctrtail1x

	adr_l		x12, .Lcts_permute_table
	add		x12, x12, x13

ST5(	ld1		{v5.16b}, [x1], x14		)
	ld1		{v6.16b}, [x1], x15
	ld1		{v7.16b}, [x1], x16

ST4(	bl		aes_encrypt_block4x		)
ST5(	bl		aes_encrypt_block5x		)

	ld1		{v8.16b}, [x1], x13
	ld1		{v9.16b}, [x1]
	ld1		{v10.16b}, [x12]

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

ST5(	st1		{v5.16b}, [x0], x14		)
	st1		{v6.16b}, [x0], x15
	st1		{v7.16b}, [x0], x16
	add		x13, x13, x0
	st1		{v9.16b}, [x13]		// overlapping stores
	st1		{v8.16b}, [x0]
	b		.Lctrout

.Lctrtail1x:
	csel		x0, x0, x6, eq		// use finalbuf if less than a full block
	ld1		{v5.16b}, [x1]
ST5(	mov		v3.16b, v4.16b			)
	encrypt_block	v3, w3, x2, x8, w7
	eor		v5.16b, v5.16b, v3.16b
	st1		{v5.16b}, [x0]
	b		.Lctrout
AES_FUNC_END(aes_ctr_encrypt)


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
	cond_yield	.Lmacout, x7
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
