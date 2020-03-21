/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * linux/arch/arm64/crypto/aes-neon.S - AES cipher for ARMv8 NEON
 *
 * Copyright (C) 2013 - 2017 Linaro Ltd. <ard.biesheuvel@linaro.org>
 */

#include <linux/linkage.h>
#include <asm/assembler.h>

#define AES_ENTRY(func)		SYM_FUNC_START(neon_ ## func)
#define AES_ENDPROC(func)	SYM_FUNC_END(neon_ ## func)

	xtsmask		.req	v7
	cbciv		.req	v7
	vctr		.req	v4

	.macro		xts_reload_mask, tmp
	xts_load_mask	\tmp
	.endm

	/* special case for the neon-bs driver calling into this one for CTS */
	.macro		xts_cts_skip_tw, reg, lbl
	tbnz		\reg, #1, \lbl
	.endm

	/* multiply by polynomial 'x' in GF(2^8) */
	.macro		mul_by_x, out, in, temp, const
	sshr		\temp, \in, #7
	shl		\out, \in, #1
	and		\temp, \temp, \const
	eor		\out, \out, \temp
	.endm

	/* multiply by polynomial 'x^2' in GF(2^8) */
	.macro		mul_by_x2, out, in, temp, const
	ushr		\temp, \in, #6
	shl		\out, \in, #2
	pmul		\temp, \temp, \const
	eor		\out, \out, \temp
	.endm

	/* preload the entire Sbox */
	.macro		prepare, sbox, shiftrows, temp
	movi		v12.16b, #0x1b
	ldr_l		q13, \shiftrows, \temp
	ldr_l		q14, .Lror32by8, \temp
	adr_l		\temp, \sbox
	ld1		{v16.16b-v19.16b}, [\temp], #64
	ld1		{v20.16b-v23.16b}, [\temp], #64
	ld1		{v24.16b-v27.16b}, [\temp], #64
	ld1		{v28.16b-v31.16b}, [\temp]
	.endm

	/* do preload for encryption */
	.macro		enc_prepare, ignore0, ignore1, temp
	prepare		crypto_aes_sbox, .LForward_ShiftRows, \temp
	.endm

	.macro		enc_switch_key, ignore0, ignore1, temp
	/* do nothing */
	.endm

	/* do preload for decryption */
	.macro		dec_prepare, ignore0, ignore1, temp
	prepare		crypto_aes_inv_sbox, .LReverse_ShiftRows, \temp
	.endm

	/* apply SubBytes transformation using the the preloaded Sbox */
	.macro		sub_bytes, in
	sub		v9.16b, \in\().16b, v15.16b
	tbl		\in\().16b, {v16.16b-v19.16b}, \in\().16b
	sub		v10.16b, v9.16b, v15.16b
	tbx		\in\().16b, {v20.16b-v23.16b}, v9.16b
	sub		v11.16b, v10.16b, v15.16b
	tbx		\in\().16b, {v24.16b-v27.16b}, v10.16b
	tbx		\in\().16b, {v28.16b-v31.16b}, v11.16b
	.endm

	/* apply MixColumns transformation */
	.macro		mix_columns, in, enc
	.if		\enc == 0
	/* Inverse MixColumns: pre-multiply by { 5, 0, 4, 0 } */
	mul_by_x2	v8.16b, \in\().16b, v9.16b, v12.16b
	eor		\in\().16b, \in\().16b, v8.16b
	rev32		v8.8h, v8.8h
	eor		\in\().16b, \in\().16b, v8.16b
	.endif

	mul_by_x	v9.16b, \in\().16b, v8.16b, v12.16b
	rev32		v8.8h, \in\().8h
	eor		v8.16b, v8.16b, v9.16b
	eor		\in\().16b, \in\().16b, v8.16b
	tbl		\in\().16b, {\in\().16b}, v14.16b
	eor		\in\().16b, \in\().16b, v8.16b
	.endm

	.macro		do_block, enc, in, rounds, rk, rkp, i
	ld1		{v15.4s}, [\rk]
	add		\rkp, \rk, #16
	mov		\i, \rounds
1111:	eor		\in\().16b, \in\().16b, v15.16b		/* ^round key */
	movi		v15.16b, #0x40
	tbl		\in\().16b, {\in\().16b}, v13.16b	/* ShiftRows */
	sub_bytes	\in
	subs		\i, \i, #1
	ld1		{v15.4s}, [\rkp], #16
	beq		2222f
	mix_columns	\in, \enc
	b		1111b
2222:	eor		\in\().16b, \in\().16b, v15.16b		/* ^round key */
	.endm

	.macro		encrypt_block, in, rounds, rk, rkp, i
	do_block	1, \in, \rounds, \rk, \rkp, \i
	.endm

	.macro		decrypt_block, in, rounds, rk, rkp, i
	do_block	0, \in, \rounds, \rk, \rkp, \i
	.endm

	/*
	 * Interleaved versions: functionally equivalent to the
	 * ones above, but applied to AES states in parallel.
	 */

	.macro		sub_bytes_4x, in0, in1, in2, in3
	sub		v8.16b, \in0\().16b, v15.16b
	tbl		\in0\().16b, {v16.16b-v19.16b}, \in0\().16b
	sub		v9.16b, \in1\().16b, v15.16b
	tbl		\in1\().16b, {v16.16b-v19.16b}, \in1\().16b
	sub		v10.16b, \in2\().16b, v15.16b
	tbl		\in2\().16b, {v16.16b-v19.16b}, \in2\().16b
	sub		v11.16b, \in3\().16b, v15.16b
	tbl		\in3\().16b, {v16.16b-v19.16b}, \in3\().16b
	tbx		\in0\().16b, {v20.16b-v23.16b}, v8.16b
	tbx		\in1\().16b, {v20.16b-v23.16b}, v9.16b
	sub		v8.16b, v8.16b, v15.16b
	tbx		\in2\().16b, {v20.16b-v23.16b}, v10.16b
	sub		v9.16b, v9.16b, v15.16b
	tbx		\in3\().16b, {v20.16b-v23.16b}, v11.16b
	sub		v10.16b, v10.16b, v15.16b
	tbx		\in0\().16b, {v24.16b-v27.16b}, v8.16b
	sub		v11.16b, v11.16b, v15.16b
	tbx		\in1\().16b, {v24.16b-v27.16b}, v9.16b
	sub		v8.16b, v8.16b, v15.16b
	tbx		\in2\().16b, {v24.16b-v27.16b}, v10.16b
	sub		v9.16b, v9.16b, v15.16b
	tbx		\in3\().16b, {v24.16b-v27.16b}, v11.16b
	sub		v10.16b, v10.16b, v15.16b
	tbx		\in0\().16b, {v28.16b-v31.16b}, v8.16b
	sub		v11.16b, v11.16b, v15.16b
	tbx		\in1\().16b, {v28.16b-v31.16b}, v9.16b
	tbx		\in2\().16b, {v28.16b-v31.16b}, v10.16b
	tbx		\in3\().16b, {v28.16b-v31.16b}, v11.16b
	.endm

	.macro		mul_by_x_2x, out0, out1, in0, in1, tmp0, tmp1, const
	sshr		\tmp0\().16b, \in0\().16b, #7
	shl		\out0\().16b, \in0\().16b, #1
	sshr		\tmp1\().16b, \in1\().16b, #7
	and		\tmp0\().16b, \tmp0\().16b, \const\().16b
	shl		\out1\().16b, \in1\().16b, #1
	and		\tmp1\().16b, \tmp1\().16b, \const\().16b
	eor		\out0\().16b, \out0\().16b, \tmp0\().16b
	eor		\out1\().16b, \out1\().16b, \tmp1\().16b
	.endm

	.macro		mul_by_x2_2x, out0, out1, in0, in1, tmp0, tmp1, const
	ushr		\tmp0\().16b, \in0\().16b, #6
	shl		\out0\().16b, \in0\().16b, #2
	ushr		\tmp1\().16b, \in1\().16b, #6
	pmul		\tmp0\().16b, \tmp0\().16b, \const\().16b
	shl		\out1\().16b, \in1\().16b, #2
	pmul		\tmp1\().16b, \tmp1\().16b, \const\().16b
	eor		\out0\().16b, \out0\().16b, \tmp0\().16b
	eor		\out1\().16b, \out1\().16b, \tmp1\().16b
	.endm

	.macro		mix_columns_2x, in0, in1, enc
	.if		\enc == 0
	/* Inverse MixColumns: pre-multiply by { 5, 0, 4, 0 } */
	mul_by_x2_2x	v8, v9, \in0, \in1, v10, v11, v12
	eor		\in0\().16b, \in0\().16b, v8.16b
	rev32		v8.8h, v8.8h
	eor		\in1\().16b, \in1\().16b, v9.16b
	rev32		v9.8h, v9.8h
	eor		\in0\().16b, \in0\().16b, v8.16b
	eor		\in1\().16b, \in1\().16b, v9.16b
	.endif

	mul_by_x_2x	v8, v9, \in0, \in1, v10, v11, v12
	rev32		v10.8h, \in0\().8h
	rev32		v11.8h, \in1\().8h
	eor		v10.16b, v10.16b, v8.16b
	eor		v11.16b, v11.16b, v9.16b
	eor		\in0\().16b, \in0\().16b, v10.16b
	eor		\in1\().16b, \in1\().16b, v11.16b
	tbl		\in0\().16b, {\in0\().16b}, v14.16b
	tbl		\in1\().16b, {\in1\().16b}, v14.16b
	eor		\in0\().16b, \in0\().16b, v10.16b
	eor		\in1\().16b, \in1\().16b, v11.16b
	.endm

	.macro		do_block_4x, enc, in0, in1, in2, in3, rounds, rk, rkp, i
	ld1		{v15.4s}, [\rk]
	add		\rkp, \rk, #16
	mov		\i, \rounds
1111:	eor		\in0\().16b, \in0\().16b, v15.16b	/* ^round key */
	eor		\in1\().16b, \in1\().16b, v15.16b	/* ^round key */
	eor		\in2\().16b, \in2\().16b, v15.16b	/* ^round key */
	eor		\in3\().16b, \in3\().16b, v15.16b	/* ^round key */
	movi		v15.16b, #0x40
	tbl		\in0\().16b, {\in0\().16b}, v13.16b	/* ShiftRows */
	tbl		\in1\().16b, {\in1\().16b}, v13.16b	/* ShiftRows */
	tbl		\in2\().16b, {\in2\().16b}, v13.16b	/* ShiftRows */
	tbl		\in3\().16b, {\in3\().16b}, v13.16b	/* ShiftRows */
	sub_bytes_4x	\in0, \in1, \in2, \in3
	subs		\i, \i, #1
	ld1		{v15.4s}, [\rkp], #16
	beq		2222f
	mix_columns_2x	\in0, \in1, \enc
	mix_columns_2x	\in2, \in3, \enc
	b		1111b
2222:	eor		\in0\().16b, \in0\().16b, v15.16b	/* ^round key */
	eor		\in1\().16b, \in1\().16b, v15.16b	/* ^round key */
	eor		\in2\().16b, \in2\().16b, v15.16b	/* ^round key */
	eor		\in3\().16b, \in3\().16b, v15.16b	/* ^round key */
	.endm

	.macro		encrypt_block4x, in0, in1, in2, in3, rounds, rk, rkp, i
	do_block_4x	1, \in0, \in1, \in2, \in3, \rounds, \rk, \rkp, \i
	.endm

	.macro		decrypt_block4x, in0, in1, in2, in3, rounds, rk, rkp, i
	do_block_4x	0, \in0, \in1, \in2, \in3, \rounds, \rk, \rkp, \i
	.endm

#include "aes-modes.S"

	.section	".rodata", "a"
	.align		4
.LForward_ShiftRows:
	.octa		0x0b06010c07020d08030e09040f0a0500

.LReverse_ShiftRows:
	.octa		0x0306090c0f0205080b0e0104070a0d00

.Lror32by8:
	.octa		0x0c0f0e0d080b0a090407060500030201
