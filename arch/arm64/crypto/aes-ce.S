/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * linux/arch/arm64/crypto/aes-ce.S - AES cipher for ARMv8 with
 *                                    Crypto Extensions
 *
 * Copyright (C) 2013 - 2017 Linaro Ltd <ard.biesheuvel@linaro.org>
 */

#include <linux/linkage.h>
#include <asm/assembler.h>

#define AES_ENTRY(func)		ENTRY(ce_ ## func)
#define AES_ENDPROC(func)	ENDPROC(ce_ ## func)

	.arch		armv8-a+crypto

	xtsmask		.req	v16
	cbciv		.req	v16
	vctr		.req	v16

	.macro		xts_reload_mask, tmp
	.endm

	.macro		xts_cts_skip_tw, reg, lbl
	.endm

	/* preload all round keys */
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

	/* prepare for encryption with key in rk[] */
	.macro		enc_prepare, rounds, rk, temp
	mov		\temp, \rk
	load_round_keys	\rounds, \temp
	.endm

	/* prepare for encryption (again) but with new key in rk[] */
	.macro		enc_switch_key, rounds, rk, temp
	mov		\temp, \rk
	load_round_keys	\rounds, \temp
	.endm

	/* prepare for decryption with key in rk[] */
	.macro		dec_prepare, rounds, rk, temp
	mov		\temp, \rk
	load_round_keys	\rounds, \temp
	.endm

	.macro		do_enc_Nx, de, mc, k, i0, i1, i2, i3, i4
	aes\de		\i0\().16b, \k\().16b
	aes\mc		\i0\().16b, \i0\().16b
	.ifnb		\i1
	aes\de		\i1\().16b, \k\().16b
	aes\mc		\i1\().16b, \i1\().16b
	.ifnb		\i3
	aes\de		\i2\().16b, \k\().16b
	aes\mc		\i2\().16b, \i2\().16b
	aes\de		\i3\().16b, \k\().16b
	aes\mc		\i3\().16b, \i3\().16b
	.ifnb		\i4
	aes\de		\i4\().16b, \k\().16b
	aes\mc		\i4\().16b, \i4\().16b
	.endif
	.endif
	.endif
	.endm

	/* up to 5 interleaved encryption rounds with the same round key */
	.macro		round_Nx, enc, k, i0, i1, i2, i3, i4
	.ifc		\enc, e
	do_enc_Nx	e, mc, \k, \i0, \i1, \i2, \i3, \i4
	.else
	do_enc_Nx	d, imc, \k, \i0, \i1, \i2, \i3, \i4
	.endif
	.endm

	/* up to 5 interleaved final rounds */
	.macro		fin_round_Nx, de, k, k2, i0, i1, i2, i3, i4
	aes\de		\i0\().16b, \k\().16b
	.ifnb		\i1
	aes\de		\i1\().16b, \k\().16b
	.ifnb		\i3
	aes\de		\i2\().16b, \k\().16b
	aes\de		\i3\().16b, \k\().16b
	.ifnb		\i4
	aes\de		\i4\().16b, \k\().16b
	.endif
	.endif
	.endif
	eor		\i0\().16b, \i0\().16b, \k2\().16b
	.ifnb		\i1
	eor		\i1\().16b, \i1\().16b, \k2\().16b
	.ifnb		\i3
	eor		\i2\().16b, \i2\().16b, \k2\().16b
	eor		\i3\().16b, \i3\().16b, \k2\().16b
	.ifnb		\i4
	eor		\i4\().16b, \i4\().16b, \k2\().16b
	.endif
	.endif
	.endif
	.endm

	/* up to 5 interleaved blocks */
	.macro		do_block_Nx, enc, rounds, i0, i1, i2, i3, i4
	cmp		\rounds, #12
	blo		2222f		/* 128 bits */
	beq		1111f		/* 192 bits */
	round_Nx	\enc, v17, \i0, \i1, \i2, \i3, \i4
	round_Nx	\enc, v18, \i0, \i1, \i2, \i3, \i4
1111:	round_Nx	\enc, v19, \i0, \i1, \i2, \i3, \i4
	round_Nx	\enc, v20, \i0, \i1, \i2, \i3, \i4
2222:	.irp		key, v21, v22, v23, v24, v25, v26, v27, v28, v29
	round_Nx	\enc, \key, \i0, \i1, \i2, \i3, \i4
	.endr
	fin_round_Nx	\enc, v30, v31, \i0, \i1, \i2, \i3, \i4
	.endm

	.macro		encrypt_block, in, rounds, t0, t1, t2
	do_block_Nx	e, \rounds, \in
	.endm

	.macro		encrypt_block4x, i0, i1, i2, i3, rounds, t0, t1, t2
	do_block_Nx	e, \rounds, \i0, \i1, \i2, \i3
	.endm

	.macro		encrypt_block5x, i0, i1, i2, i3, i4, rounds, t0, t1, t2
	do_block_Nx	e, \rounds, \i0, \i1, \i2, \i3, \i4
	.endm

	.macro		decrypt_block, in, rounds, t0, t1, t2
	do_block_Nx	d, \rounds, \in
	.endm

	.macro		decrypt_block4x, i0, i1, i2, i3, rounds, t0, t1, t2
	do_block_Nx	d, \rounds, \i0, \i1, \i2, \i3
	.endm

	.macro		decrypt_block5x, i0, i1, i2, i3, i4, rounds, t0, t1, t2
	do_block_Nx	d, \rounds, \i0, \i1, \i2, \i3, \i4
	.endm

#define MAX_STRIDE	5

#include "aes-modes.S"
