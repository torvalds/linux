/*
 * linux/arch/arm64/crypto/aes-modes.S - chaining mode wrappers for AES
 *
 * Copyright (C) 2013 - 2017 Linaro Ltd <ard.biesheuvel@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/* included by aes-ce.S and aes-neon.S */

	.text
	.align		4

aes_encrypt_block4x:
	encrypt_block4x	v0, v1, v2, v3, w3, x2, x8, w7
	ret
ENDPROC(aes_encrypt_block4x)

aes_decrypt_block4x:
	decrypt_block4x	v0, v1, v2, v3, w3, x2, x8, w7
	ret
ENDPROC(aes_decrypt_block4x)

	/*
	 * aes_ecb_encrypt(u8 out[], u8 const in[], u8 const rk[], int rounds,
	 *		   int blocks)
	 * aes_ecb_decrypt(u8 out[], u8 const in[], u8 const rk[], int rounds,
	 *		   int blocks)
	 */

AES_ENTRY(aes_ecb_encrypt)
	stp		x29, x30, [sp, #-16]!
	mov		x29, sp

	enc_prepare	w3, x2, x5

.LecbencloopNx:
	subs		w4, w4, #4
	bmi		.Lecbenc1x
	ld1		{v0.16b-v3.16b}, [x1], #64	/* get 4 pt blocks */
	bl		aes_encrypt_block4x
	st1		{v0.16b-v3.16b}, [x0], #64
	b		.LecbencloopNx
.Lecbenc1x:
	adds		w4, w4, #4
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
AES_ENDPROC(aes_ecb_encrypt)


AES_ENTRY(aes_ecb_decrypt)
	stp		x29, x30, [sp, #-16]!
	mov		x29, sp

	dec_prepare	w3, x2, x5

.LecbdecloopNx:
	subs		w4, w4, #4
	bmi		.Lecbdec1x
	ld1		{v0.16b-v3.16b}, [x1], #64	/* get 4 ct blocks */
	bl		aes_decrypt_block4x
	st1		{v0.16b-v3.16b}, [x0], #64
	b		.LecbdecloopNx
.Lecbdec1x:
	adds		w4, w4, #4
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
AES_ENDPROC(aes_ecb_decrypt)


	/*
	 * aes_cbc_encrypt(u8 out[], u8 const in[], u8 const rk[], int rounds,
	 *		   int blocks, u8 iv[])
	 * aes_cbc_decrypt(u8 out[], u8 const in[], u8 const rk[], int rounds,
	 *		   int blocks, u8 iv[])
	 */

AES_ENTRY(aes_cbc_encrypt)
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
AES_ENDPROC(aes_cbc_encrypt)


AES_ENTRY(aes_cbc_decrypt)
	stp		x29, x30, [sp, #-16]!
	mov		x29, sp

	ld1		{v7.16b}, [x5]			/* get iv */
	dec_prepare	w3, x2, x6

.LcbcdecloopNx:
	subs		w4, w4, #4
	bmi		.Lcbcdec1x
	ld1		{v0.16b-v3.16b}, [x1], #64	/* get 4 ct blocks */
	mov		v4.16b, v0.16b
	mov		v5.16b, v1.16b
	mov		v6.16b, v2.16b
	bl		aes_decrypt_block4x
	sub		x1, x1, #16
	eor		v0.16b, v0.16b, v7.16b
	eor		v1.16b, v1.16b, v4.16b
	ld1		{v7.16b}, [x1], #16		/* reload 1 ct block */
	eor		v2.16b, v2.16b, v5.16b
	eor		v3.16b, v3.16b, v6.16b
	st1		{v0.16b-v3.16b}, [x0], #64
	b		.LcbcdecloopNx
.Lcbcdec1x:
	adds		w4, w4, #4
	beq		.Lcbcdecout
.Lcbcdecloop:
	ld1		{v1.16b}, [x1], #16		/* get next ct block */
	mov		v0.16b, v1.16b			/* ...and copy to v0 */
	decrypt_block	v0, w3, x2, x6, w7
	eor		v0.16b, v0.16b, v7.16b		/* xor with iv => pt */
	mov		v7.16b, v1.16b			/* ct is next iv */
	st1		{v0.16b}, [x0], #16
	subs		w4, w4, #1
	bne		.Lcbcdecloop
.Lcbcdecout:
	st1		{v7.16b}, [x5]			/* return iv */
	ldp		x29, x30, [sp], #16
	ret
AES_ENDPROC(aes_cbc_decrypt)


	/*
	 * aes_cbc_cts_encrypt(u8 out[], u8 const in[], u32 const rk[],
	 *		       int rounds, int bytes, u8 const iv[])
	 * aes_cbc_cts_decrypt(u8 out[], u8 const in[], u32 const rk[],
	 *		       int rounds, int bytes, u8 const iv[])
	 */

AES_ENTRY(aes_cbc_cts_encrypt)
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
AES_ENDPROC(aes_cbc_cts_encrypt)

AES_ENTRY(aes_cbc_cts_decrypt)
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

	tbl		v2.16b, {v1.16b}, v4.16b
	decrypt_block	v0, w3, x2, x6, w7
	eor		v2.16b, v2.16b, v0.16b

	tbx		v0.16b, {v1.16b}, v4.16b
	tbl		v2.16b, {v2.16b}, v3.16b
	decrypt_block	v0, w3, x2, x6, w7
	eor		v0.16b, v0.16b, v5.16b		/* xor with iv */

	add		x4, x0, x4
	st1		{v2.16b}, [x4]			/* overlapping stores */
	st1		{v0.16b}, [x0]
	ret
AES_ENDPROC(aes_cbc_cts_decrypt)

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
	 *		   int blocks, u8 ctr[])
	 */

AES_ENTRY(aes_ctr_encrypt)
	stp		x29, x30, [sp, #-16]!
	mov		x29, sp

	enc_prepare	w3, x2, x6
	ld1		{v4.16b}, [x5]

	umov		x6, v4.d[1]		/* keep swabbed ctr in reg */
	rev		x6, x6
	cmn		w6, w4			/* 32 bit overflow? */
	bcs		.Lctrloop
.LctrloopNx:
	subs		w4, w4, #4
	bmi		.Lctr1x
	add		w7, w6, #1
	mov		v0.16b, v4.16b
	add		w8, w6, #2
	mov		v1.16b, v4.16b
	add		w9, w6, #3
	mov		v2.16b, v4.16b
	rev		w7, w7
	mov		v3.16b, v4.16b
	rev		w8, w8
	mov		v1.s[3], w7
	rev		w9, w9
	mov		v2.s[3], w8
	mov		v3.s[3], w9
	ld1		{v5.16b-v7.16b}, [x1], #48	/* get 3 input blocks */
	bl		aes_encrypt_block4x
	eor		v0.16b, v5.16b, v0.16b
	ld1		{v5.16b}, [x1], #16		/* get 1 input block  */
	eor		v1.16b, v6.16b, v1.16b
	eor		v2.16b, v7.16b, v2.16b
	eor		v3.16b, v5.16b, v3.16b
	st1		{v0.16b-v3.16b}, [x0], #64
	add		x6, x6, #4
	rev		x7, x6
	ins		v4.d[1], x7
	cbz		w4, .Lctrout
	b		.LctrloopNx
.Lctr1x:
	adds		w4, w4, #4
	beq		.Lctrout
.Lctrloop:
	mov		v0.16b, v4.16b
	encrypt_block	v0, w3, x2, x8, w7

	adds		x6, x6, #1		/* increment BE ctr */
	rev		x7, x6
	ins		v4.d[1], x7
	bcs		.Lctrcarry		/* overflow? */

.Lctrcarrydone:
	subs		w4, w4, #1
	bmi		.Lctrtailblock		/* blocks <0 means tail block */
	ld1		{v3.16b}, [x1], #16
	eor		v3.16b, v0.16b, v3.16b
	st1		{v3.16b}, [x0], #16
	bne		.Lctrloop

.Lctrout:
	st1		{v4.16b}, [x5]		/* return next CTR value */
	ldp		x29, x30, [sp], #16
	ret

.Lctrtailblock:
	st1		{v0.16b}, [x0]
	ldp		x29, x30, [sp], #16
	ret

.Lctrcarry:
	umov		x7, v4.d[0]		/* load upper word of ctr  */
	rev		x7, x7			/* ... to handle the carry */
	add		x7, x7, #1
	rev		x7, x7
	ins		v4.d[0], x7
	b		.Lctrcarrydone
AES_ENDPROC(aes_ctr_encrypt)


	/*
	 * aes_xts_decrypt(u8 out[], u8 const in[], u8 const rk1[], int rounds,
	 *		   int blocks, u8 const rk2[], u8 iv[], int first)
	 * aes_xts_decrypt(u8 out[], u8 const in[], u8 const rk1[], int rounds,
	 *		   int blocks, u8 const rk2[], u8 iv[], int first)
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

AES_ENTRY(aes_xts_encrypt)
	stp		x29, x30, [sp, #-16]!
	mov		x29, sp

	ld1		{v4.16b}, [x6]
	cbz		w7, .Lxtsencnotfirst

	enc_prepare	w3, x5, x8
	encrypt_block	v4, w3, x5, x8, w7		/* first tweak */
	enc_switch_key	w3, x2, x8
	xts_load_mask	v8
	b		.LxtsencNx

.Lxtsencnotfirst:
	enc_prepare	w3, x2, x8
.LxtsencloopNx:
	xts_reload_mask	v8
	next_tweak	v4, v4, v8
.LxtsencNx:
	subs		w4, w4, #4
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
	cbz		w4, .Lxtsencout
	b		.LxtsencloopNx
.Lxtsenc1x:
	adds		w4, w4, #4
	beq		.Lxtsencout
.Lxtsencloop:
	ld1		{v1.16b}, [x1], #16
	eor		v0.16b, v1.16b, v4.16b
	encrypt_block	v0, w3, x2, x8, w7
	eor		v0.16b, v0.16b, v4.16b
	st1		{v0.16b}, [x0], #16
	subs		w4, w4, #1
	beq		.Lxtsencout
	next_tweak	v4, v4, v8
	b		.Lxtsencloop
.Lxtsencout:
	st1		{v4.16b}, [x6]
	ldp		x29, x30, [sp], #16
	ret
AES_ENDPROC(aes_xts_encrypt)


AES_ENTRY(aes_xts_decrypt)
	stp		x29, x30, [sp, #-16]!
	mov		x29, sp

	ld1		{v4.16b}, [x6]
	cbz		w7, .Lxtsdecnotfirst

	enc_prepare	w3, x5, x8
	encrypt_block	v4, w3, x5, x8, w7		/* first tweak */
	dec_prepare	w3, x2, x8
	xts_load_mask	v8
	b		.LxtsdecNx

.Lxtsdecnotfirst:
	dec_prepare	w3, x2, x8
.LxtsdecloopNx:
	xts_reload_mask	v8
	next_tweak	v4, v4, v8
.LxtsdecNx:
	subs		w4, w4, #4
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
	b		.LxtsdecloopNx
.Lxtsdec1x:
	adds		w4, w4, #4
	beq		.Lxtsdecout
.Lxtsdecloop:
	ld1		{v1.16b}, [x1], #16
	eor		v0.16b, v1.16b, v4.16b
	decrypt_block	v0, w3, x2, x8, w7
	eor		v0.16b, v0.16b, v4.16b
	st1		{v0.16b}, [x0], #16
	subs		w4, w4, #1
	beq		.Lxtsdecout
	next_tweak	v4, v4, v8
	b		.Lxtsdecloop
.Lxtsdecout:
	st1		{v4.16b}, [x6]
	ldp		x29, x30, [sp], #16
	ret
AES_ENDPROC(aes_xts_decrypt)

	/*
	 * aes_mac_update(u8 const in[], u32 const rk[], int rounds,
	 *		  int blocks, u8 dg[], int enc_before, int enc_after)
	 */
AES_ENTRY(aes_mac_update)
	frame_push	6

	mov		x19, x0
	mov		x20, x1
	mov		x21, x2
	mov		x22, x3
	mov		x23, x4
	mov		x24, x6

	ld1		{v0.16b}, [x23]			/* get dg */
	enc_prepare	w2, x1, x7
	cbz		w5, .Lmacloop4x

	encrypt_block	v0, w2, x1, x7, w8

.Lmacloop4x:
	subs		w22, w22, #4
	bmi		.Lmac1x
	ld1		{v1.16b-v4.16b}, [x19], #64	/* get next pt block */
	eor		v0.16b, v0.16b, v1.16b		/* ..and xor with dg */
	encrypt_block	v0, w21, x20, x7, w8
	eor		v0.16b, v0.16b, v2.16b
	encrypt_block	v0, w21, x20, x7, w8
	eor		v0.16b, v0.16b, v3.16b
	encrypt_block	v0, w21, x20, x7, w8
	eor		v0.16b, v0.16b, v4.16b
	cmp		w22, wzr
	csinv		x5, x24, xzr, eq
	cbz		w5, .Lmacout
	encrypt_block	v0, w21, x20, x7, w8
	st1		{v0.16b}, [x23]			/* return dg */
	cond_yield_neon	.Lmacrestart
	b		.Lmacloop4x
.Lmac1x:
	add		w22, w22, #4
.Lmacloop:
	cbz		w22, .Lmacout
	ld1		{v1.16b}, [x19], #16		/* get next pt block */
	eor		v0.16b, v0.16b, v1.16b		/* ..and xor with dg */

	subs		w22, w22, #1
	csinv		x5, x24, xzr, eq
	cbz		w5, .Lmacout

.Lmacenc:
	encrypt_block	v0, w21, x20, x7, w8
	b		.Lmacloop

.Lmacout:
	st1		{v0.16b}, [x23]			/* return dg */
	frame_pop
	ret

.Lmacrestart:
	ld1		{v0.16b}, [x23]			/* get dg */
	enc_prepare	w21, x20, x0
	b		.Lmacloop4x
AES_ENDPROC(aes_mac_update)
