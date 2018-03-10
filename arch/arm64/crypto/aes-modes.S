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

/*
 * There are several ways to instantiate this code:
 * - no interleave, all inline
 * - 2-way interleave, 2x calls out of line (-DINTERLEAVE=2)
 * - 2-way interleave, all inline (-DINTERLEAVE=2 -DINTERLEAVE_INLINE)
 * - 4-way interleave, 4x calls out of line (-DINTERLEAVE=4)
 * - 4-way interleave, all inline (-DINTERLEAVE=4 -DINTERLEAVE_INLINE)
 *
 * Macros imported by this code:
 * - enc_prepare	- setup NEON registers for encryption
 * - dec_prepare	- setup NEON registers for decryption
 * - enc_switch_key	- change to new key after having prepared for encryption
 * - encrypt_block	- encrypt a single block
 * - decrypt block	- decrypt a single block
 * - encrypt_block2x	- encrypt 2 blocks in parallel (if INTERLEAVE == 2)
 * - decrypt_block2x	- decrypt 2 blocks in parallel (if INTERLEAVE == 2)
 * - encrypt_block4x	- encrypt 4 blocks in parallel (if INTERLEAVE == 4)
 * - decrypt_block4x	- decrypt 4 blocks in parallel (if INTERLEAVE == 4)
 */

#if defined(INTERLEAVE) && !defined(INTERLEAVE_INLINE)
#define FRAME_PUSH	stp x29, x30, [sp,#-16]! ; mov x29, sp
#define FRAME_POP	ldp x29, x30, [sp],#16

#if INTERLEAVE == 2

aes_encrypt_block2x:
	encrypt_block2x	v0, v1, w3, x2, x8, w7
	ret
ENDPROC(aes_encrypt_block2x)

aes_decrypt_block2x:
	decrypt_block2x	v0, v1, w3, x2, x8, w7
	ret
ENDPROC(aes_decrypt_block2x)

#elif INTERLEAVE == 4

aes_encrypt_block4x:
	encrypt_block4x	v0, v1, v2, v3, w3, x2, x8, w7
	ret
ENDPROC(aes_encrypt_block4x)

aes_decrypt_block4x:
	decrypt_block4x	v0, v1, v2, v3, w3, x2, x8, w7
	ret
ENDPROC(aes_decrypt_block4x)

#else
#error INTERLEAVE should equal 2 or 4
#endif

	.macro		do_encrypt_block2x
	bl		aes_encrypt_block2x
	.endm

	.macro		do_decrypt_block2x
	bl		aes_decrypt_block2x
	.endm

	.macro		do_encrypt_block4x
	bl		aes_encrypt_block4x
	.endm

	.macro		do_decrypt_block4x
	bl		aes_decrypt_block4x
	.endm

#else
#define FRAME_PUSH
#define FRAME_POP

	.macro		do_encrypt_block2x
	encrypt_block2x	v0, v1, w3, x2, x8, w7
	.endm

	.macro		do_decrypt_block2x
	decrypt_block2x	v0, v1, w3, x2, x8, w7
	.endm

	.macro		do_encrypt_block4x
	encrypt_block4x	v0, v1, v2, v3, w3, x2, x8, w7
	.endm

	.macro		do_decrypt_block4x
	decrypt_block4x	v0, v1, v2, v3, w3, x2, x8, w7
	.endm

#endif

	/*
	 * aes_ecb_encrypt(u8 out[], u8 const in[], u8 const rk[], int rounds,
	 *		   int blocks)
	 * aes_ecb_decrypt(u8 out[], u8 const in[], u8 const rk[], int rounds,
	 *		   int blocks)
	 */

AES_ENTRY(aes_ecb_encrypt)
	FRAME_PUSH

	enc_prepare	w3, x2, x5

.LecbencloopNx:
#if INTERLEAVE >= 2
	subs		w4, w4, #INTERLEAVE
	bmi		.Lecbenc1x
#if INTERLEAVE == 2
	ld1		{v0.16b-v1.16b}, [x1], #32	/* get 2 pt blocks */
	do_encrypt_block2x
	st1		{v0.16b-v1.16b}, [x0], #32
#else
	ld1		{v0.16b-v3.16b}, [x1], #64	/* get 4 pt blocks */
	do_encrypt_block4x
	st1		{v0.16b-v3.16b}, [x0], #64
#endif
	b		.LecbencloopNx
.Lecbenc1x:
	adds		w4, w4, #INTERLEAVE
	beq		.Lecbencout
#endif
.Lecbencloop:
	ld1		{v0.16b}, [x1], #16		/* get next pt block */
	encrypt_block	v0, w3, x2, x5, w6
	st1		{v0.16b}, [x0], #16
	subs		w4, w4, #1
	bne		.Lecbencloop
.Lecbencout:
	FRAME_POP
	ret
AES_ENDPROC(aes_ecb_encrypt)


AES_ENTRY(aes_ecb_decrypt)
	FRAME_PUSH

	dec_prepare	w3, x2, x5

.LecbdecloopNx:
#if INTERLEAVE >= 2
	subs		w4, w4, #INTERLEAVE
	bmi		.Lecbdec1x
#if INTERLEAVE == 2
	ld1		{v0.16b-v1.16b}, [x1], #32	/* get 2 ct blocks */
	do_decrypt_block2x
	st1		{v0.16b-v1.16b}, [x0], #32
#else
	ld1		{v0.16b-v3.16b}, [x1], #64	/* get 4 ct blocks */
	do_decrypt_block4x
	st1		{v0.16b-v3.16b}, [x0], #64
#endif
	b		.LecbdecloopNx
.Lecbdec1x:
	adds		w4, w4, #INTERLEAVE
	beq		.Lecbdecout
#endif
.Lecbdecloop:
	ld1		{v0.16b}, [x1], #16		/* get next ct block */
	decrypt_block	v0, w3, x2, x5, w6
	st1		{v0.16b}, [x0], #16
	subs		w4, w4, #1
	bne		.Lecbdecloop
.Lecbdecout:
	FRAME_POP
	ret
AES_ENDPROC(aes_ecb_decrypt)


	/*
	 * aes_cbc_encrypt(u8 out[], u8 const in[], u8 const rk[], int rounds,
	 *		   int blocks, u8 iv[])
	 * aes_cbc_decrypt(u8 out[], u8 const in[], u8 const rk[], int rounds,
	 *		   int blocks, u8 iv[])
	 */

AES_ENTRY(aes_cbc_encrypt)
	ld1		{v0.16b}, [x5]			/* get iv */
	enc_prepare	w3, x2, x6

.Lcbcencloop:
	ld1		{v1.16b}, [x1], #16		/* get next pt block */
	eor		v0.16b, v0.16b, v1.16b		/* ..and xor with iv */
	encrypt_block	v0, w3, x2, x6, w7
	st1		{v0.16b}, [x0], #16
	subs		w4, w4, #1
	bne		.Lcbcencloop
	st1		{v0.16b}, [x5]			/* return iv */
	ret
AES_ENDPROC(aes_cbc_encrypt)


AES_ENTRY(aes_cbc_decrypt)
	FRAME_PUSH

	ld1		{v7.16b}, [x5]			/* get iv */
	dec_prepare	w3, x2, x6

.LcbcdecloopNx:
#if INTERLEAVE >= 2
	subs		w4, w4, #INTERLEAVE
	bmi		.Lcbcdec1x
#if INTERLEAVE == 2
	ld1		{v0.16b-v1.16b}, [x1], #32	/* get 2 ct blocks */
	mov		v2.16b, v0.16b
	mov		v3.16b, v1.16b
	do_decrypt_block2x
	eor		v0.16b, v0.16b, v7.16b
	eor		v1.16b, v1.16b, v2.16b
	mov		v7.16b, v3.16b
	st1		{v0.16b-v1.16b}, [x0], #32
#else
	ld1		{v0.16b-v3.16b}, [x1], #64	/* get 4 ct blocks */
	mov		v4.16b, v0.16b
	mov		v5.16b, v1.16b
	mov		v6.16b, v2.16b
	do_decrypt_block4x
	sub		x1, x1, #16
	eor		v0.16b, v0.16b, v7.16b
	eor		v1.16b, v1.16b, v4.16b
	ld1		{v7.16b}, [x1], #16		/* reload 1 ct block */
	eor		v2.16b, v2.16b, v5.16b
	eor		v3.16b, v3.16b, v6.16b
	st1		{v0.16b-v3.16b}, [x0], #64
#endif
	b		.LcbcdecloopNx
.Lcbcdec1x:
	adds		w4, w4, #INTERLEAVE
	beq		.Lcbcdecout
#endif
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
	FRAME_POP
	st1		{v7.16b}, [x5]			/* return iv */
	ret
AES_ENDPROC(aes_cbc_decrypt)


	/*
	 * aes_ctr_encrypt(u8 out[], u8 const in[], u8 const rk[], int rounds,
	 *		   int blocks, u8 ctr[])
	 */

AES_ENTRY(aes_ctr_encrypt)
	FRAME_PUSH

	enc_prepare	w3, x2, x6
	ld1		{v4.16b}, [x5]

	umov		x6, v4.d[1]		/* keep swabbed ctr in reg */
	rev		x6, x6
#if INTERLEAVE >= 2
	cmn		w6, w4			/* 32 bit overflow? */
	bcs		.Lctrloop
.LctrloopNx:
	subs		w4, w4, #INTERLEAVE
	bmi		.Lctr1x
#if INTERLEAVE == 2
	mov		v0.8b, v4.8b
	mov		v1.8b, v4.8b
	rev		x7, x6
	add		x6, x6, #1
	ins		v0.d[1], x7
	rev		x7, x6
	add		x6, x6, #1
	ins		v1.d[1], x7
	ld1		{v2.16b-v3.16b}, [x1], #32	/* get 2 input blocks */
	do_encrypt_block2x
	eor		v0.16b, v0.16b, v2.16b
	eor		v1.16b, v1.16b, v3.16b
	st1		{v0.16b-v1.16b}, [x0], #32
#else
	ldr		q8, =0x30000000200000001	/* addends 1,2,3[,0] */
	dup		v7.4s, w6
	mov		v0.16b, v4.16b
	add		v7.4s, v7.4s, v8.4s
	mov		v1.16b, v4.16b
	rev32		v8.16b, v7.16b
	mov		v2.16b, v4.16b
	mov		v3.16b, v4.16b
	mov		v1.s[3], v8.s[0]
	mov		v2.s[3], v8.s[1]
	mov		v3.s[3], v8.s[2]
	ld1		{v5.16b-v7.16b}, [x1], #48	/* get 3 input blocks */
	do_encrypt_block4x
	eor		v0.16b, v5.16b, v0.16b
	ld1		{v5.16b}, [x1], #16		/* get 1 input block  */
	eor		v1.16b, v6.16b, v1.16b
	eor		v2.16b, v7.16b, v2.16b
	eor		v3.16b, v5.16b, v3.16b
	st1		{v0.16b-v3.16b}, [x0], #64
	add		x6, x6, #INTERLEAVE
#endif
	rev		x7, x6
	ins		v4.d[1], x7
	cbz		w4, .Lctrout
	b		.LctrloopNx
.Lctr1x:
	adds		w4, w4, #INTERLEAVE
	beq		.Lctrout
#endif
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
	FRAME_POP
	ret

.Lctrtailblock:
	st1		{v0.16b}, [x0]
	FRAME_POP
	ret

.Lctrcarry:
	umov		x7, v4.d[0]		/* load upper word of ctr  */
	rev		x7, x7			/* ... to handle the carry */
	add		x7, x7, #1
	rev		x7, x7
	ins		v4.d[0], x7
	b		.Lctrcarrydone
AES_ENDPROC(aes_ctr_encrypt)
	.ltorg


	/*
	 * aes_xts_decrypt(u8 out[], u8 const in[], u8 const rk1[], int rounds,
	 *		   int blocks, u8 const rk2[], u8 iv[], int first)
	 * aes_xts_decrypt(u8 out[], u8 const in[], u8 const rk1[], int rounds,
	 *		   int blocks, u8 const rk2[], u8 iv[], int first)
	 */

	.macro		next_tweak, out, in, const, tmp
	sshr		\tmp\().2d,  \in\().2d,   #63
	and		\tmp\().16b, \tmp\().16b, \const\().16b
	add		\out\().2d,  \in\().2d,   \in\().2d
	ext		\tmp\().16b, \tmp\().16b, \tmp\().16b, #8
	eor		\out\().16b, \out\().16b, \tmp\().16b
	.endm

.Lxts_mul_x:
CPU_LE(	.quad		1, 0x87		)
CPU_BE(	.quad		0x87, 1		)

AES_ENTRY(aes_xts_encrypt)
	FRAME_PUSH
	ld1		{v4.16b}, [x6]
	cbz		w7, .Lxtsencnotfirst

	enc_prepare	w3, x5, x8
	encrypt_block	v4, w3, x5, x8, w7		/* first tweak */
	enc_switch_key	w3, x2, x8
	ldr		q7, .Lxts_mul_x
	b		.LxtsencNx

.Lxtsencnotfirst:
	enc_prepare	w3, x2, x8
.LxtsencloopNx:
	ldr		q7, .Lxts_mul_x
	next_tweak	v4, v4, v7, v8
.LxtsencNx:
#if INTERLEAVE >= 2
	subs		w4, w4, #INTERLEAVE
	bmi		.Lxtsenc1x
#if INTERLEAVE == 2
	ld1		{v0.16b-v1.16b}, [x1], #32	/* get 2 pt blocks */
	next_tweak	v5, v4, v7, v8
	eor		v0.16b, v0.16b, v4.16b
	eor		v1.16b, v1.16b, v5.16b
	do_encrypt_block2x
	eor		v0.16b, v0.16b, v4.16b
	eor		v1.16b, v1.16b, v5.16b
	st1		{v0.16b-v1.16b}, [x0], #32
	cbz		w4, .LxtsencoutNx
	next_tweak	v4, v5, v7, v8
	b		.LxtsencNx
.LxtsencoutNx:
	mov		v4.16b, v5.16b
	b		.Lxtsencout
#else
	ld1		{v0.16b-v3.16b}, [x1], #64	/* get 4 pt blocks */
	next_tweak	v5, v4, v7, v8
	eor		v0.16b, v0.16b, v4.16b
	next_tweak	v6, v5, v7, v8
	eor		v1.16b, v1.16b, v5.16b
	eor		v2.16b, v2.16b, v6.16b
	next_tweak	v7, v6, v7, v8
	eor		v3.16b, v3.16b, v7.16b
	do_encrypt_block4x
	eor		v3.16b, v3.16b, v7.16b
	eor		v0.16b, v0.16b, v4.16b
	eor		v1.16b, v1.16b, v5.16b
	eor		v2.16b, v2.16b, v6.16b
	st1		{v0.16b-v3.16b}, [x0], #64
	mov		v4.16b, v7.16b
	cbz		w4, .Lxtsencout
	b		.LxtsencloopNx
#endif
.Lxtsenc1x:
	adds		w4, w4, #INTERLEAVE
	beq		.Lxtsencout
#endif
.Lxtsencloop:
	ld1		{v1.16b}, [x1], #16
	eor		v0.16b, v1.16b, v4.16b
	encrypt_block	v0, w3, x2, x8, w7
	eor		v0.16b, v0.16b, v4.16b
	st1		{v0.16b}, [x0], #16
	subs		w4, w4, #1
	beq		.Lxtsencout
	next_tweak	v4, v4, v7, v8
	b		.Lxtsencloop
.Lxtsencout:
	st1		{v4.16b}, [x6]
	FRAME_POP
	ret
AES_ENDPROC(aes_xts_encrypt)


AES_ENTRY(aes_xts_decrypt)
	FRAME_PUSH
	ld1		{v4.16b}, [x6]
	cbz		w7, .Lxtsdecnotfirst

	enc_prepare	w3, x5, x8
	encrypt_block	v4, w3, x5, x8, w7		/* first tweak */
	dec_prepare	w3, x2, x8
	ldr		q7, .Lxts_mul_x
	b		.LxtsdecNx

.Lxtsdecnotfirst:
	dec_prepare	w3, x2, x8
.LxtsdecloopNx:
	ldr		q7, .Lxts_mul_x
	next_tweak	v4, v4, v7, v8
.LxtsdecNx:
#if INTERLEAVE >= 2
	subs		w4, w4, #INTERLEAVE
	bmi		.Lxtsdec1x
#if INTERLEAVE == 2
	ld1		{v0.16b-v1.16b}, [x1], #32	/* get 2 ct blocks */
	next_tweak	v5, v4, v7, v8
	eor		v0.16b, v0.16b, v4.16b
	eor		v1.16b, v1.16b, v5.16b
	do_decrypt_block2x
	eor		v0.16b, v0.16b, v4.16b
	eor		v1.16b, v1.16b, v5.16b
	st1		{v0.16b-v1.16b}, [x0], #32
	cbz		w4, .LxtsdecoutNx
	next_tweak	v4, v5, v7, v8
	b		.LxtsdecNx
.LxtsdecoutNx:
	mov		v4.16b, v5.16b
	b		.Lxtsdecout
#else
	ld1		{v0.16b-v3.16b}, [x1], #64	/* get 4 ct blocks */
	next_tweak	v5, v4, v7, v8
	eor		v0.16b, v0.16b, v4.16b
	next_tweak	v6, v5, v7, v8
	eor		v1.16b, v1.16b, v5.16b
	eor		v2.16b, v2.16b, v6.16b
	next_tweak	v7, v6, v7, v8
	eor		v3.16b, v3.16b, v7.16b
	do_decrypt_block4x
	eor		v3.16b, v3.16b, v7.16b
	eor		v0.16b, v0.16b, v4.16b
	eor		v1.16b, v1.16b, v5.16b
	eor		v2.16b, v2.16b, v6.16b
	st1		{v0.16b-v3.16b}, [x0], #64
	mov		v4.16b, v7.16b
	cbz		w4, .Lxtsdecout
	b		.LxtsdecloopNx
#endif
.Lxtsdec1x:
	adds		w4, w4, #INTERLEAVE
	beq		.Lxtsdecout
#endif
.Lxtsdecloop:
	ld1		{v1.16b}, [x1], #16
	eor		v0.16b, v1.16b, v4.16b
	decrypt_block	v0, w3, x2, x8, w7
	eor		v0.16b, v0.16b, v4.16b
	st1		{v0.16b}, [x0], #16
	subs		w4, w4, #1
	beq		.Lxtsdecout
	next_tweak	v4, v4, v7, v8
	b		.Lxtsdecloop
.Lxtsdecout:
	st1		{v4.16b}, [x6]
	FRAME_POP
	ret
AES_ENDPROC(aes_xts_decrypt)

	/*
	 * aes_mac_update(u8 const in[], u32 const rk[], int rounds,
	 *		  int blocks, u8 dg[], int enc_before, int enc_after)
	 */
AES_ENTRY(aes_mac_update)
	ld1		{v0.16b}, [x4]			/* get dg */
	enc_prepare	w2, x1, x7
	cbnz		w5, .Lmacenc

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
	ret
AES_ENDPROC(aes_mac_update)
