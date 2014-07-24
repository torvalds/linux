/*
 * linux/arch/arm64/crypto/aes-neon.S - AES cipher for ARMv8 NEON
 *
 * Copyright (C) 2013 Linaro Ltd <ard.biesheuvel@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/linkage.h>

#define AES_ENTRY(func)		ENTRY(neon_ ## func)
#define AES_ENDPROC(func)	ENDPROC(neon_ ## func)

	/* multiply by polynomial 'x' in GF(2^8) */
	.macro		mul_by_x, out, in, temp, const
	sshr		\temp, \in, #7
	add		\out, \in, \in
	and		\temp, \temp, \const
	eor		\out, \out, \temp
	.endm

	/* preload the entire Sbox */
	.macro		prepare, sbox, shiftrows, temp
	adr		\temp, \sbox
	movi		v12.16b, #0x40
	ldr		q13, \shiftrows
	movi		v14.16b, #0x1b
	ld1		{v16.16b-v19.16b}, [\temp], #64
	ld1		{v20.16b-v23.16b}, [\temp], #64
	ld1		{v24.16b-v27.16b}, [\temp], #64
	ld1		{v28.16b-v31.16b}, [\temp]
	.endm

	/* do preload for encryption */
	.macro		enc_prepare, ignore0, ignore1, temp
	prepare		.LForward_Sbox, .LForward_ShiftRows, \temp
	.endm

	.macro		enc_switch_key, ignore0, ignore1, temp
	/* do nothing */
	.endm

	/* do preload for decryption */
	.macro		dec_prepare, ignore0, ignore1, temp
	prepare		.LReverse_Sbox, .LReverse_ShiftRows, \temp
	.endm

	/* apply SubBytes transformation using the the preloaded Sbox */
	.macro		sub_bytes, in
	sub		v9.16b, \in\().16b, v12.16b
	tbl		\in\().16b, {v16.16b-v19.16b}, \in\().16b
	sub		v10.16b, v9.16b, v12.16b
	tbx		\in\().16b, {v20.16b-v23.16b}, v9.16b
	sub		v11.16b, v10.16b, v12.16b
	tbx		\in\().16b, {v24.16b-v27.16b}, v10.16b
	tbx		\in\().16b, {v28.16b-v31.16b}, v11.16b
	.endm

	/* apply MixColumns transformation */
	.macro		mix_columns, in
	mul_by_x	v10.16b, \in\().16b, v9.16b, v14.16b
	rev32		v8.8h, \in\().8h
	eor		\in\().16b, v10.16b, \in\().16b
	shl		v9.4s, v8.4s, #24
	shl		v11.4s, \in\().4s, #24
	sri		v9.4s, v8.4s, #8
	sri		v11.4s, \in\().4s, #8
	eor		v9.16b, v9.16b, v8.16b
	eor		v10.16b, v10.16b, v9.16b
	eor		\in\().16b, v10.16b, v11.16b
	.endm

	/* Inverse MixColumns: pre-multiply by { 5, 0, 4, 0 } */
	.macro		inv_mix_columns, in
	mul_by_x	v11.16b, \in\().16b, v10.16b, v14.16b
	mul_by_x	v11.16b, v11.16b, v10.16b, v14.16b
	eor		\in\().16b, \in\().16b, v11.16b
	rev32		v11.8h, v11.8h
	eor		\in\().16b, \in\().16b, v11.16b
	mix_columns	\in
	.endm

	.macro		do_block, enc, in, rounds, rk, rkp, i
	ld1		{v15.16b}, [\rk]
	add		\rkp, \rk, #16
	mov		\i, \rounds
1111:	eor		\in\().16b, \in\().16b, v15.16b		/* ^round key */
	tbl		\in\().16b, {\in\().16b}, v13.16b	/* ShiftRows */
	sub_bytes	\in
	ld1		{v15.16b}, [\rkp], #16
	subs		\i, \i, #1
	beq		2222f
	.if		\enc == 1
	mix_columns	\in
	.else
	inv_mix_columns	\in
	.endif
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
	 * ones above, but applied to 2 or 4 AES states in parallel.
	 */

	.macro		sub_bytes_2x, in0, in1
	sub		v8.16b, \in0\().16b, v12.16b
	sub		v9.16b, \in1\().16b, v12.16b
	tbl		\in0\().16b, {v16.16b-v19.16b}, \in0\().16b
	tbl		\in1\().16b, {v16.16b-v19.16b}, \in1\().16b
	sub		v10.16b, v8.16b, v12.16b
	sub		v11.16b, v9.16b, v12.16b
	tbx		\in0\().16b, {v20.16b-v23.16b}, v8.16b
	tbx		\in1\().16b, {v20.16b-v23.16b}, v9.16b
	sub		v8.16b, v10.16b, v12.16b
	sub		v9.16b, v11.16b, v12.16b
	tbx		\in0\().16b, {v24.16b-v27.16b}, v10.16b
	tbx		\in1\().16b, {v24.16b-v27.16b}, v11.16b
	tbx		\in0\().16b, {v28.16b-v31.16b}, v8.16b
	tbx		\in1\().16b, {v28.16b-v31.16b}, v9.16b
	.endm

	.macro		sub_bytes_4x, in0, in1, in2, in3
	sub		v8.16b, \in0\().16b, v12.16b
	tbl		\in0\().16b, {v16.16b-v19.16b}, \in0\().16b
	sub		v9.16b, \in1\().16b, v12.16b
	tbl		\in1\().16b, {v16.16b-v19.16b}, \in1\().16b
	sub		v10.16b, \in2\().16b, v12.16b
	tbl		\in2\().16b, {v16.16b-v19.16b}, \in2\().16b
	sub		v11.16b, \in3\().16b, v12.16b
	tbl		\in3\().16b, {v16.16b-v19.16b}, \in3\().16b
	tbx		\in0\().16b, {v20.16b-v23.16b}, v8.16b
	tbx		\in1\().16b, {v20.16b-v23.16b}, v9.16b
	sub		v8.16b, v8.16b, v12.16b
	tbx		\in2\().16b, {v20.16b-v23.16b}, v10.16b
	sub		v9.16b, v9.16b, v12.16b
	tbx		\in3\().16b, {v20.16b-v23.16b}, v11.16b
	sub		v10.16b, v10.16b, v12.16b
	tbx		\in0\().16b, {v24.16b-v27.16b}, v8.16b
	sub		v11.16b, v11.16b, v12.16b
	tbx		\in1\().16b, {v24.16b-v27.16b}, v9.16b
	sub		v8.16b, v8.16b, v12.16b
	tbx		\in2\().16b, {v24.16b-v27.16b}, v10.16b
	sub		v9.16b, v9.16b, v12.16b
	tbx		\in3\().16b, {v24.16b-v27.16b}, v11.16b
	sub		v10.16b, v10.16b, v12.16b
	tbx		\in0\().16b, {v28.16b-v31.16b}, v8.16b
	sub		v11.16b, v11.16b, v12.16b
	tbx		\in1\().16b, {v28.16b-v31.16b}, v9.16b
	tbx		\in2\().16b, {v28.16b-v31.16b}, v10.16b
	tbx		\in3\().16b, {v28.16b-v31.16b}, v11.16b
	.endm

	.macro		mul_by_x_2x, out0, out1, in0, in1, tmp0, tmp1, const
	sshr		\tmp0\().16b, \in0\().16b,  #7
	add		\out0\().16b, \in0\().16b,  \in0\().16b
	sshr		\tmp1\().16b, \in1\().16b,  #7
	and		\tmp0\().16b, \tmp0\().16b, \const\().16b
	add		\out1\().16b, \in1\().16b,  \in1\().16b
	and		\tmp1\().16b, \tmp1\().16b, \const\().16b
	eor		\out0\().16b, \out0\().16b, \tmp0\().16b
	eor		\out1\().16b, \out1\().16b, \tmp1\().16b
	.endm

	.macro		mix_columns_2x, in0, in1
	mul_by_x_2x	v8, v9, \in0, \in1, v10, v11, v14
	rev32		v10.8h, \in0\().8h
	rev32		v11.8h, \in1\().8h
	eor		\in0\().16b, v8.16b, \in0\().16b
	eor		\in1\().16b, v9.16b, \in1\().16b
	shl		v12.4s, v10.4s, #24
	shl		v13.4s, v11.4s, #24
	eor		v8.16b, v8.16b, v10.16b
	sri		v12.4s, v10.4s, #8
	shl		v10.4s, \in0\().4s, #24
	eor		v9.16b, v9.16b, v11.16b
	sri		v13.4s, v11.4s, #8
	shl		v11.4s, \in1\().4s, #24
	sri		v10.4s, \in0\().4s, #8
	eor		\in0\().16b, v8.16b, v12.16b
	sri		v11.4s, \in1\().4s, #8
	eor		\in1\().16b, v9.16b, v13.16b
	eor		\in0\().16b, v10.16b, \in0\().16b
	eor		\in1\().16b, v11.16b, \in1\().16b
	.endm

	.macro		inv_mix_cols_2x, in0, in1
	mul_by_x_2x	v8, v9, \in0, \in1, v10, v11, v14
	mul_by_x_2x	v8, v9, v8, v9, v10, v11, v14
	eor		\in0\().16b, \in0\().16b, v8.16b
	eor		\in1\().16b, \in1\().16b, v9.16b
	rev32		v8.8h, v8.8h
	rev32		v9.8h, v9.8h
	eor		\in0\().16b, \in0\().16b, v8.16b
	eor		\in1\().16b, \in1\().16b, v9.16b
	mix_columns_2x	\in0, \in1
	.endm

	.macro		inv_mix_cols_4x, in0, in1, in2, in3
	mul_by_x_2x	v8, v9, \in0, \in1, v10, v11, v14
	mul_by_x_2x	v10, v11, \in2, \in3, v12, v13, v14
	mul_by_x_2x	v8, v9, v8, v9, v12, v13, v14
	mul_by_x_2x	v10, v11, v10, v11, v12, v13, v14
	eor		\in0\().16b, \in0\().16b, v8.16b
	eor		\in1\().16b, \in1\().16b, v9.16b
	eor		\in2\().16b, \in2\().16b, v10.16b
	eor		\in3\().16b, \in3\().16b, v11.16b
	rev32		v8.8h, v8.8h
	rev32		v9.8h, v9.8h
	rev32		v10.8h, v10.8h
	rev32		v11.8h, v11.8h
	eor		\in0\().16b, \in0\().16b, v8.16b
	eor		\in1\().16b, \in1\().16b, v9.16b
	eor		\in2\().16b, \in2\().16b, v10.16b
	eor		\in3\().16b, \in3\().16b, v11.16b
	mix_columns_2x	\in0, \in1
	mix_columns_2x	\in2, \in3
	.endm

	.macro		do_block_2x, enc, in0, in1 rounds, rk, rkp, i
	ld1		{v15.16b}, [\rk]
	add		\rkp, \rk, #16
	mov		\i, \rounds
1111:	eor		\in0\().16b, \in0\().16b, v15.16b	/* ^round key */
	eor		\in1\().16b, \in1\().16b, v15.16b	/* ^round key */
	sub_bytes_2x	\in0, \in1
	tbl		\in0\().16b, {\in0\().16b}, v13.16b	/* ShiftRows */
	tbl		\in1\().16b, {\in1\().16b}, v13.16b	/* ShiftRows */
	ld1		{v15.16b}, [\rkp], #16
	subs		\i, \i, #1
	beq		2222f
	.if		\enc == 1
	mix_columns_2x	\in0, \in1
	ldr		q13, .LForward_ShiftRows
	.else
	inv_mix_cols_2x	\in0, \in1
	ldr		q13, .LReverse_ShiftRows
	.endif
	movi		v12.16b, #0x40
	b		1111b
2222:	eor		\in0\().16b, \in0\().16b, v15.16b	/* ^round key */
	eor		\in1\().16b, \in1\().16b, v15.16b	/* ^round key */
	.endm

	.macro		do_block_4x, enc, in0, in1, in2, in3, rounds, rk, rkp, i
	ld1		{v15.16b}, [\rk]
	add		\rkp, \rk, #16
	mov		\i, \rounds
1111:	eor		\in0\().16b, \in0\().16b, v15.16b	/* ^round key */
	eor		\in1\().16b, \in1\().16b, v15.16b	/* ^round key */
	eor		\in2\().16b, \in2\().16b, v15.16b	/* ^round key */
	eor		\in3\().16b, \in3\().16b, v15.16b	/* ^round key */
	sub_bytes_4x	\in0, \in1, \in2, \in3
	tbl		\in0\().16b, {\in0\().16b}, v13.16b	/* ShiftRows */
	tbl		\in1\().16b, {\in1\().16b}, v13.16b	/* ShiftRows */
	tbl		\in2\().16b, {\in2\().16b}, v13.16b	/* ShiftRows */
	tbl		\in3\().16b, {\in3\().16b}, v13.16b	/* ShiftRows */
	ld1		{v15.16b}, [\rkp], #16
	subs		\i, \i, #1
	beq		2222f
	.if		\enc == 1
	mix_columns_2x	\in0, \in1
	mix_columns_2x	\in2, \in3
	ldr		q13, .LForward_ShiftRows
	.else
	inv_mix_cols_4x	\in0, \in1, \in2, \in3
	ldr		q13, .LReverse_ShiftRows
	.endif
	movi		v12.16b, #0x40
	b		1111b
2222:	eor		\in0\().16b, \in0\().16b, v15.16b	/* ^round key */
	eor		\in1\().16b, \in1\().16b, v15.16b	/* ^round key */
	eor		\in2\().16b, \in2\().16b, v15.16b	/* ^round key */
	eor		\in3\().16b, \in3\().16b, v15.16b	/* ^round key */
	.endm

	.macro		encrypt_block2x, in0, in1, rounds, rk, rkp, i
	do_block_2x	1, \in0, \in1, \rounds, \rk, \rkp, \i
	.endm

	.macro		decrypt_block2x, in0, in1, rounds, rk, rkp, i
	do_block_2x	0, \in0, \in1, \rounds, \rk, \rkp, \i
	.endm

	.macro		encrypt_block4x, in0, in1, in2, in3, rounds, rk, rkp, i
	do_block_4x	1, \in0, \in1, \in2, \in3, \rounds, \rk, \rkp, \i
	.endm

	.macro		decrypt_block4x, in0, in1, in2, in3, rounds, rk, rkp, i
	do_block_4x	0, \in0, \in1, \in2, \in3, \rounds, \rk, \rkp, \i
	.endm

#include "aes-modes.S"

	.text
	.align		4
.LForward_ShiftRows:
	.byte		0x0, 0x5, 0xa, 0xf, 0x4, 0x9, 0xe, 0x3
	.byte		0x8, 0xd, 0x2, 0x7, 0xc, 0x1, 0x6, 0xb

.LReverse_ShiftRows:
	.byte		0x0, 0xd, 0xa, 0x7, 0x4, 0x1, 0xe, 0xb
	.byte		0x8, 0x5, 0x2, 0xf, 0xc, 0x9, 0x6, 0x3

.LForward_Sbox:
	.byte		0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5
	.byte		0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76
	.byte		0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0
	.byte		0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0
	.byte		0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc
	.byte		0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15
	.byte		0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a
	.byte		0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75
	.byte		0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0
	.byte		0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84
	.byte		0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b
	.byte		0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf
	.byte		0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85
	.byte		0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8
	.byte		0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5
	.byte		0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2
	.byte		0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17
	.byte		0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73
	.byte		0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88
	.byte		0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb
	.byte		0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c
	.byte		0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79
	.byte		0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9
	.byte		0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08
	.byte		0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6
	.byte		0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a
	.byte		0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e
	.byte		0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e
	.byte		0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94
	.byte		0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf
	.byte		0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68
	.byte		0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16

.LReverse_Sbox:
	.byte		0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38
	.byte		0xbf, 0x40, 0xa3, 0x9e, 0x81, 0xf3, 0xd7, 0xfb
	.byte		0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87
	.byte		0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb
	.byte		0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2, 0x23, 0x3d
	.byte		0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e
	.byte		0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2
	.byte		0x76, 0x5b, 0xa2, 0x49, 0x6d, 0x8b, 0xd1, 0x25
	.byte		0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16
	.byte		0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92
	.byte		0x6c, 0x70, 0x48, 0x50, 0xfd, 0xed, 0xb9, 0xda
	.byte		0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84
	.byte		0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a
	.byte		0xf7, 0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06
	.byte		0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02
	.byte		0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b
	.byte		0x3a, 0x91, 0x11, 0x41, 0x4f, 0x67, 0xdc, 0xea
	.byte		0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73
	.byte		0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85
	.byte		0xe2, 0xf9, 0x37, 0xe8, 0x1c, 0x75, 0xdf, 0x6e
	.byte		0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89
	.byte		0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b
	.byte		0xfc, 0x56, 0x3e, 0x4b, 0xc6, 0xd2, 0x79, 0x20
	.byte		0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4
	.byte		0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07, 0xc7, 0x31
	.byte		0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f
	.byte		0x60, 0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d
	.byte		0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef
	.byte		0xa0, 0xe0, 0x3b, 0x4d, 0xae, 0x2a, 0xf5, 0xb0
	.byte		0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61
	.byte		0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26
	.byte		0xe1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0c, 0x7d
