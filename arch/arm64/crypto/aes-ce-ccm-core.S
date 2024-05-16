/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * aes-ce-ccm-core.S - AES-CCM transform for ARMv8 with Crypto Extensions
 *
 * Copyright (C) 2013 - 2017 Linaro Ltd.
 * Copyright (C) 2024 Google LLC
 *
 * Author: Ard Biesheuvel <ardb@kernel.org>
 */

#include <linux/linkage.h>
#include <asm/assembler.h>

	.text
	.arch	armv8-a+crypto

	.macro	load_round_keys, rk, nr, tmp
	sub	w\tmp, \nr, #10
	add	\tmp, \rk, w\tmp, sxtw #4
	ld1	{v10.4s-v13.4s}, [\rk]
	ld1	{v14.4s-v17.4s}, [\tmp], #64
	ld1	{v18.4s-v21.4s}, [\tmp], #64
	ld1	{v3.4s-v5.4s}, [\tmp]
	.endm

	.macro	dround, va, vb, vk
	aese	\va\().16b, \vk\().16b
	aesmc	\va\().16b, \va\().16b
	aese	\vb\().16b, \vk\().16b
	aesmc	\vb\().16b, \vb\().16b
	.endm

	.macro	aes_encrypt, va, vb, nr
	tbz	\nr, #2, .L\@
	dround	\va, \vb, v10
	dround	\va, \vb, v11
	tbz	\nr, #1, .L\@
	dround	\va, \vb, v12
	dround	\va, \vb, v13
.L\@:	.irp	v, v14, v15, v16, v17, v18, v19, v20, v21, v3
	dround	\va, \vb, \v
	.endr
	aese	\va\().16b, v4.16b
	aese	\vb\().16b, v4.16b
	.endm

	.macro	aes_ccm_do_crypt,enc
	load_round_keys	x3, w4, x10

	ld1	{v0.16b}, [x5]			/* load mac */
	cbz	x2, ce_aes_ccm_final
	ldr	x8, [x6, #8]			/* load lower ctr */
CPU_LE(	rev	x8, x8			)	/* keep swabbed ctr in reg */
0:	/* outer loop */
	ld1	{v1.8b}, [x6]			/* load upper ctr */
	prfm	pldl1strm, [x1]
	add	x8, x8, #1
	rev	x9, x8
	ins	v1.d[1], x9			/* no carry in lower ctr */

	aes_encrypt	v0, v1, w4

	subs	w2, w2, #16
	bmi	ce_aes_ccm_crypt_tail
	ld1	{v2.16b}, [x1], #16		/* load next input block */
	.if	\enc == 1
	eor	v2.16b, v2.16b, v5.16b		/* final round enc+mac */
	eor	v6.16b, v1.16b, v2.16b		/* xor with crypted ctr */
	.else
	eor	v2.16b, v2.16b, v1.16b		/* xor with crypted ctr */
	eor	v6.16b, v2.16b, v5.16b		/* final round enc */
	.endif
	eor	v0.16b, v0.16b, v2.16b		/* xor mac with pt ^ rk[last] */
	st1	{v6.16b}, [x0], #16		/* write output block */
	bne	0b
CPU_LE(	rev	x8, x8			)
	str	x8, [x6, #8]			/* store lsb end of ctr (BE) */
	cbnz	x7, ce_aes_ccm_final
	st1	{v0.16b}, [x5]			/* store mac */
	ret
	.endm

SYM_FUNC_START_LOCAL(ce_aes_ccm_crypt_tail)
	eor	v0.16b, v0.16b, v5.16b		/* final round mac */
	eor	v1.16b, v1.16b, v5.16b		/* final round enc */

	add	x1, x1, w2, sxtw		/* rewind the input pointer (w2 < 0) */
	add	x0, x0, w2, sxtw		/* rewind the output pointer */

	adr_l	x8, .Lpermute			/* load permute vectors */
	add	x9, x8, w2, sxtw
	sub	x8, x8, w2, sxtw
	ld1	{v7.16b-v8.16b}, [x9]
	ld1	{v9.16b}, [x8]

	ld1	{v2.16b}, [x1]			/* load a full block of input */
	tbl	v1.16b, {v1.16b}, v7.16b	/* move keystream to end of register */
	eor	v7.16b, v2.16b, v1.16b		/* encrypt partial input block */
	bif	v2.16b, v7.16b, v22.16b		/* select plaintext */
	tbx	v7.16b, {v6.16b}, v8.16b	/* insert output from previous iteration */
	tbl	v2.16b, {v2.16b}, v9.16b	/* copy plaintext to start of v2 */
	eor	v0.16b, v0.16b, v2.16b		/* fold plaintext into mac */

	st1	{v7.16b}, [x0]			/* store output block */
	cbz	x7, 0f

SYM_INNER_LABEL(ce_aes_ccm_final, SYM_L_LOCAL)
	ld1	{v1.16b}, [x7]			/* load 1st ctriv */

	aes_encrypt	v0, v1, w4

	/* final round key cancels out */
	eor	v0.16b, v0.16b, v1.16b		/* en-/decrypt the mac */
0:	st1	{v0.16b}, [x5]			/* store result */
	ret
SYM_FUNC_END(ce_aes_ccm_crypt_tail)

	/*
	 * void ce_aes_ccm_encrypt(u8 out[], u8 const in[], u32 cbytes,
	 * 			   u8 const rk[], u32 rounds, u8 mac[],
	 * 			   u8 ctr[], u8 const final_iv[]);
	 * void ce_aes_ccm_decrypt(u8 out[], u8 const in[], u32 cbytes,
	 * 			   u8 const rk[], u32 rounds, u8 mac[],
	 * 			   u8 ctr[], u8 const final_iv[]);
	 */
SYM_FUNC_START(ce_aes_ccm_encrypt)
	movi	v22.16b, #255
	aes_ccm_do_crypt	1
SYM_FUNC_END(ce_aes_ccm_encrypt)

SYM_FUNC_START(ce_aes_ccm_decrypt)
	movi	v22.16b, #0
	aes_ccm_do_crypt	0
SYM_FUNC_END(ce_aes_ccm_decrypt)

	.section ".rodata", "a"
	.align	6
	.fill	15, 1, 0xff
.Lpermute:
	.byte	0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7
	.byte	0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf
	.fill	15, 1, 0xff
