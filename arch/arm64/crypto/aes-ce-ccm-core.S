/*
 * aesce-ccm-core.S - AES-CCM transform for ARMv8 with Crypto Extensions
 *
 * Copyright (C) 2013 - 2017 Linaro Ltd <ard.biesheuvel@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/linkage.h>
#include <asm/assembler.h>

	.text
	.arch	armv8-a+crypto

	/*
	 * void ce_aes_ccm_auth_data(u8 mac[], u8 const in[], u32 abytes,
	 *			     u32 *macp, u8 const rk[], u32 rounds);
	 */
ENTRY(ce_aes_ccm_auth_data)
	frame_push	7

	mov	x19, x0
	mov	x20, x1
	mov	x21, x2
	mov	x22, x3
	mov	x23, x4
	mov	x24, x5

	ldr	w25, [x22]			/* leftover from prev round? */
	ld1	{v0.16b}, [x0]			/* load mac */
	cbz	w25, 1f
	sub	w25, w25, #16
	eor	v1.16b, v1.16b, v1.16b
0:	ldrb	w7, [x20], #1			/* get 1 byte of input */
	subs	w21, w21, #1
	add	w25, w25, #1
	ins	v1.b[0], w7
	ext	v1.16b, v1.16b, v1.16b, #1	/* rotate in the input bytes */
	beq	8f				/* out of input? */
	cbnz	w25, 0b
	eor	v0.16b, v0.16b, v1.16b
1:	ld1	{v3.4s}, [x23]			/* load first round key */
	prfm	pldl1strm, [x20]
	cmp	w24, #12			/* which key size? */
	add	x6, x23, #16
	sub	w7, w24, #2			/* modified # of rounds */
	bmi	2f
	bne	5f
	mov	v5.16b, v3.16b
	b	4f
2:	mov	v4.16b, v3.16b
	ld1	{v5.4s}, [x6], #16		/* load 2nd round key */
3:	aese	v0.16b, v4.16b
	aesmc	v0.16b, v0.16b
4:	ld1	{v3.4s}, [x6], #16		/* load next round key */
	aese	v0.16b, v5.16b
	aesmc	v0.16b, v0.16b
5:	ld1	{v4.4s}, [x6], #16		/* load next round key */
	subs	w7, w7, #3
	aese	v0.16b, v3.16b
	aesmc	v0.16b, v0.16b
	ld1	{v5.4s}, [x6], #16		/* load next round key */
	bpl	3b
	aese	v0.16b, v4.16b
	subs	w21, w21, #16			/* last data? */
	eor	v0.16b, v0.16b, v5.16b		/* final round */
	bmi	6f
	ld1	{v1.16b}, [x20], #16		/* load next input block */
	eor	v0.16b, v0.16b, v1.16b		/* xor with mac */
	beq	6f

	if_will_cond_yield_neon
	st1	{v0.16b}, [x19]			/* store mac */
	do_cond_yield_neon
	ld1	{v0.16b}, [x19]			/* reload mac */
	endif_yield_neon

	b	1b
6:	st1	{v0.16b}, [x19]			/* store mac */
	beq	10f
	adds	w21, w21, #16
	beq	10f
	mov	w25, w21
7:	ldrb	w7, [x20], #1
	umov	w6, v0.b[0]
	eor	w6, w6, w7
	strb	w6, [x19], #1
	subs	w21, w21, #1
	beq	10f
	ext	v0.16b, v0.16b, v0.16b, #1	/* rotate out the mac bytes */
	b	7b
8:	mov	w7, w25
	add	w25, w25, #16
9:	ext	v1.16b, v1.16b, v1.16b, #1
	adds	w7, w7, #1
	bne	9b
	eor	v0.16b, v0.16b, v1.16b
	st1	{v0.16b}, [x19]
10:	str	w25, [x22]

	frame_pop
	ret
ENDPROC(ce_aes_ccm_auth_data)

	/*
	 * void ce_aes_ccm_final(u8 mac[], u8 const ctr[], u8 const rk[],
	 * 			 u32 rounds);
	 */
ENTRY(ce_aes_ccm_final)
	ld1	{v3.4s}, [x2], #16		/* load first round key */
	ld1	{v0.16b}, [x0]			/* load mac */
	cmp	w3, #12				/* which key size? */
	sub	w3, w3, #2			/* modified # of rounds */
	ld1	{v1.16b}, [x1]			/* load 1st ctriv */
	bmi	0f
	bne	3f
	mov	v5.16b, v3.16b
	b	2f
0:	mov	v4.16b, v3.16b
1:	ld1	{v5.4s}, [x2], #16		/* load next round key */
	aese	v0.16b, v4.16b
	aesmc	v0.16b, v0.16b
	aese	v1.16b, v4.16b
	aesmc	v1.16b, v1.16b
2:	ld1	{v3.4s}, [x2], #16		/* load next round key */
	aese	v0.16b, v5.16b
	aesmc	v0.16b, v0.16b
	aese	v1.16b, v5.16b
	aesmc	v1.16b, v1.16b
3:	ld1	{v4.4s}, [x2], #16		/* load next round key */
	subs	w3, w3, #3
	aese	v0.16b, v3.16b
	aesmc	v0.16b, v0.16b
	aese	v1.16b, v3.16b
	aesmc	v1.16b, v1.16b
	bpl	1b
	aese	v0.16b, v4.16b
	aese	v1.16b, v4.16b
	/* final round key cancels out */
	eor	v0.16b, v0.16b, v1.16b		/* en-/decrypt the mac */
	st1	{v0.16b}, [x0]			/* store result */
	ret
ENDPROC(ce_aes_ccm_final)

	.macro	aes_ccm_do_crypt,enc
	frame_push	8

	mov	x19, x0
	mov	x20, x1
	mov	x21, x2
	mov	x22, x3
	mov	x23, x4
	mov	x24, x5
	mov	x25, x6

	ldr	x26, [x25, #8]			/* load lower ctr */
	ld1	{v0.16b}, [x24]			/* load mac */
CPU_LE(	rev	x26, x26		)	/* keep swabbed ctr in reg */
0:	/* outer loop */
	ld1	{v1.8b}, [x25]			/* load upper ctr */
	prfm	pldl1strm, [x20]
	add	x26, x26, #1
	rev	x9, x26
	cmp	w23, #12			/* which key size? */
	sub	w7, w23, #2			/* get modified # of rounds */
	ins	v1.d[1], x9			/* no carry in lower ctr */
	ld1	{v3.4s}, [x22]			/* load first round key */
	add	x10, x22, #16
	bmi	1f
	bne	4f
	mov	v5.16b, v3.16b
	b	3f
1:	mov	v4.16b, v3.16b
	ld1	{v5.4s}, [x10], #16		/* load 2nd round key */
2:	/* inner loop: 3 rounds, 2x interleaved */
	aese	v0.16b, v4.16b
	aesmc	v0.16b, v0.16b
	aese	v1.16b, v4.16b
	aesmc	v1.16b, v1.16b
3:	ld1	{v3.4s}, [x10], #16		/* load next round key */
	aese	v0.16b, v5.16b
	aesmc	v0.16b, v0.16b
	aese	v1.16b, v5.16b
	aesmc	v1.16b, v1.16b
4:	ld1	{v4.4s}, [x10], #16		/* load next round key */
	subs	w7, w7, #3
	aese	v0.16b, v3.16b
	aesmc	v0.16b, v0.16b
	aese	v1.16b, v3.16b
	aesmc	v1.16b, v1.16b
	ld1	{v5.4s}, [x10], #16		/* load next round key */
	bpl	2b
	aese	v0.16b, v4.16b
	aese	v1.16b, v4.16b
	subs	w21, w21, #16
	bmi	7f				/* partial block? */
	ld1	{v2.16b}, [x20], #16		/* load next input block */
	.if	\enc == 1
	eor	v2.16b, v2.16b, v5.16b		/* final round enc+mac */
	eor	v1.16b, v1.16b, v2.16b		/* xor with crypted ctr */
	.else
	eor	v2.16b, v2.16b, v1.16b		/* xor with crypted ctr */
	eor	v1.16b, v2.16b, v5.16b		/* final round enc */
	.endif
	eor	v0.16b, v0.16b, v2.16b		/* xor mac with pt ^ rk[last] */
	st1	{v1.16b}, [x19], #16		/* write output block */
	beq	5f

	if_will_cond_yield_neon
	st1	{v0.16b}, [x24]			/* store mac */
	do_cond_yield_neon
	ld1	{v0.16b}, [x24]			/* reload mac */
	endif_yield_neon

	b	0b
5:
CPU_LE(	rev	x26, x26			)
	st1	{v0.16b}, [x24]			/* store mac */
	str	x26, [x25, #8]			/* store lsb end of ctr (BE) */

6:	frame_pop
	ret

7:	eor	v0.16b, v0.16b, v5.16b		/* final round mac */
	eor	v1.16b, v1.16b, v5.16b		/* final round enc */
	st1	{v0.16b}, [x24]			/* store mac */
	add	w21, w21, #16			/* process partial tail block */
8:	ldrb	w9, [x20], #1			/* get 1 byte of input */
	umov	w6, v1.b[0]			/* get top crypted ctr byte */
	umov	w7, v0.b[0]			/* get top mac byte */
	.if	\enc == 1
	eor	w7, w7, w9
	eor	w9, w9, w6
	.else
	eor	w9, w9, w6
	eor	w7, w7, w9
	.endif
	strb	w9, [x19], #1			/* store out byte */
	strb	w7, [x24], #1			/* store mac byte */
	subs	w21, w21, #1
	beq	6b
	ext	v0.16b, v0.16b, v0.16b, #1	/* shift out mac byte */
	ext	v1.16b, v1.16b, v1.16b, #1	/* shift out ctr byte */
	b	8b
	.endm

	/*
	 * void ce_aes_ccm_encrypt(u8 out[], u8 const in[], u32 cbytes,
	 * 			   u8 const rk[], u32 rounds, u8 mac[],
	 * 			   u8 ctr[]);
	 * void ce_aes_ccm_decrypt(u8 out[], u8 const in[], u32 cbytes,
	 * 			   u8 const rk[], u32 rounds, u8 mac[],
	 * 			   u8 ctr[]);
	 */
ENTRY(ce_aes_ccm_encrypt)
	aes_ccm_do_crypt	1
ENDPROC(ce_aes_ccm_encrypt)

ENTRY(ce_aes_ccm_decrypt)
	aes_ccm_do_crypt	0
ENDPROC(ce_aes_ccm_decrypt)
