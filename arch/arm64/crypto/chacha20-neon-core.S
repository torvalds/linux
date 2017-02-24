/*
 * ChaCha20 256-bit cipher algorithm, RFC7539, arm64 NEON functions
 *
 * Copyright (C) 2016 Linaro, Ltd. <ard.biesheuvel@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Based on:
 * ChaCha20 256-bit cipher algorithm, RFC7539, x64 SSSE3 functions
 *
 * Copyright (C) 2015 Martin Willi
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/linkage.h>

	.text
	.align		6

ENTRY(chacha20_block_xor_neon)
	// x0: Input state matrix, s
	// x1: 1 data block output, o
	// x2: 1 data block input, i

	//
	// This function encrypts one ChaCha20 block by loading the state matrix
	// in four NEON registers. It performs matrix operation on four words in
	// parallel, but requires shuffling to rearrange the words after each
	// round.
	//

	// x0..3 = s0..3
	adr		x3, ROT8
	ld1		{v0.4s-v3.4s}, [x0]
	ld1		{v8.4s-v11.4s}, [x0]
	ld1		{v12.4s}, [x3]

	mov		x3, #10

.Ldoubleround:
	// x0 += x1, x3 = rotl32(x3 ^ x0, 16)
	add		v0.4s, v0.4s, v1.4s
	eor		v3.16b, v3.16b, v0.16b
	rev32		v3.8h, v3.8h

	// x2 += x3, x1 = rotl32(x1 ^ x2, 12)
	add		v2.4s, v2.4s, v3.4s
	eor		v4.16b, v1.16b, v2.16b
	shl		v1.4s, v4.4s, #12
	sri		v1.4s, v4.4s, #20

	// x0 += x1, x3 = rotl32(x3 ^ x0, 8)
	add		v0.4s, v0.4s, v1.4s
	eor		v3.16b, v3.16b, v0.16b
	tbl		v3.16b, {v3.16b}, v12.16b

	// x2 += x3, x1 = rotl32(x1 ^ x2, 7)
	add		v2.4s, v2.4s, v3.4s
	eor		v4.16b, v1.16b, v2.16b
	shl		v1.4s, v4.4s, #7
	sri		v1.4s, v4.4s, #25

	// x1 = shuffle32(x1, MASK(0, 3, 2, 1))
	ext		v1.16b, v1.16b, v1.16b, #4
	// x2 = shuffle32(x2, MASK(1, 0, 3, 2))
	ext		v2.16b, v2.16b, v2.16b, #8
	// x3 = shuffle32(x3, MASK(2, 1, 0, 3))
	ext		v3.16b, v3.16b, v3.16b, #12

	// x0 += x1, x3 = rotl32(x3 ^ x0, 16)
	add		v0.4s, v0.4s, v1.4s
	eor		v3.16b, v3.16b, v0.16b
	rev32		v3.8h, v3.8h

	// x2 += x3, x1 = rotl32(x1 ^ x2, 12)
	add		v2.4s, v2.4s, v3.4s
	eor		v4.16b, v1.16b, v2.16b
	shl		v1.4s, v4.4s, #12
	sri		v1.4s, v4.4s, #20

	// x0 += x1, x3 = rotl32(x3 ^ x0, 8)
	add		v0.4s, v0.4s, v1.4s
	eor		v3.16b, v3.16b, v0.16b
	tbl		v3.16b, {v3.16b}, v12.16b

	// x2 += x3, x1 = rotl32(x1 ^ x2, 7)
	add		v2.4s, v2.4s, v3.4s
	eor		v4.16b, v1.16b, v2.16b
	shl		v1.4s, v4.4s, #7
	sri		v1.4s, v4.4s, #25

	// x1 = shuffle32(x1, MASK(2, 1, 0, 3))
	ext		v1.16b, v1.16b, v1.16b, #12
	// x2 = shuffle32(x2, MASK(1, 0, 3, 2))
	ext		v2.16b, v2.16b, v2.16b, #8
	// x3 = shuffle32(x3, MASK(0, 3, 2, 1))
	ext		v3.16b, v3.16b, v3.16b, #4

	subs		x3, x3, #1
	b.ne		.Ldoubleround

	ld1		{v4.16b-v7.16b}, [x2]

	// o0 = i0 ^ (x0 + s0)
	add		v0.4s, v0.4s, v8.4s
	eor		v0.16b, v0.16b, v4.16b

	// o1 = i1 ^ (x1 + s1)
	add		v1.4s, v1.4s, v9.4s
	eor		v1.16b, v1.16b, v5.16b

	// o2 = i2 ^ (x2 + s2)
	add		v2.4s, v2.4s, v10.4s
	eor		v2.16b, v2.16b, v6.16b

	// o3 = i3 ^ (x3 + s3)
	add		v3.4s, v3.4s, v11.4s
	eor		v3.16b, v3.16b, v7.16b

	st1		{v0.16b-v3.16b}, [x1]

	ret
ENDPROC(chacha20_block_xor_neon)

	.align		6
ENTRY(chacha20_4block_xor_neon)
	// x0: Input state matrix, s
	// x1: 4 data blocks output, o
	// x2: 4 data blocks input, i

	//
	// This function encrypts four consecutive ChaCha20 blocks by loading
	// the state matrix in NEON registers four times. The algorithm performs
	// each operation on the corresponding word of each state matrix, hence
	// requires no word shuffling. For final XORing step we transpose the
	// matrix by interleaving 32- and then 64-bit words, which allows us to
	// do XOR in NEON registers.
	//
	adr		x3, CTRINC		// ... and ROT8
	ld1		{v30.4s-v31.4s}, [x3]

	// x0..15[0-3] = s0..3[0..3]
	mov		x4, x0
	ld4r		{ v0.4s- v3.4s}, [x4], #16
	ld4r		{ v4.4s- v7.4s}, [x4], #16
	ld4r		{ v8.4s-v11.4s}, [x4], #16
	ld4r		{v12.4s-v15.4s}, [x4]

	// x12 += counter values 0-3
	add		v12.4s, v12.4s, v30.4s

	mov		x3, #10

.Ldoubleround4:
	// x0 += x4, x12 = rotl32(x12 ^ x0, 16)
	// x1 += x5, x13 = rotl32(x13 ^ x1, 16)
	// x2 += x6, x14 = rotl32(x14 ^ x2, 16)
	// x3 += x7, x15 = rotl32(x15 ^ x3, 16)
	add		v0.4s, v0.4s, v4.4s
	add		v1.4s, v1.4s, v5.4s
	add		v2.4s, v2.4s, v6.4s
	add		v3.4s, v3.4s, v7.4s

	eor		v12.16b, v12.16b, v0.16b
	eor		v13.16b, v13.16b, v1.16b
	eor		v14.16b, v14.16b, v2.16b
	eor		v15.16b, v15.16b, v3.16b

	rev32		v12.8h, v12.8h
	rev32		v13.8h, v13.8h
	rev32		v14.8h, v14.8h
	rev32		v15.8h, v15.8h

	// x8 += x12, x4 = rotl32(x4 ^ x8, 12)
	// x9 += x13, x5 = rotl32(x5 ^ x9, 12)
	// x10 += x14, x6 = rotl32(x6 ^ x10, 12)
	// x11 += x15, x7 = rotl32(x7 ^ x11, 12)
	add		v8.4s, v8.4s, v12.4s
	add		v9.4s, v9.4s, v13.4s
	add		v10.4s, v10.4s, v14.4s
	add		v11.4s, v11.4s, v15.4s

	eor		v16.16b, v4.16b, v8.16b
	eor		v17.16b, v5.16b, v9.16b
	eor		v18.16b, v6.16b, v10.16b
	eor		v19.16b, v7.16b, v11.16b

	shl		v4.4s, v16.4s, #12
	shl		v5.4s, v17.4s, #12
	shl		v6.4s, v18.4s, #12
	shl		v7.4s, v19.4s, #12

	sri		v4.4s, v16.4s, #20
	sri		v5.4s, v17.4s, #20
	sri		v6.4s, v18.4s, #20
	sri		v7.4s, v19.4s, #20

	// x0 += x4, x12 = rotl32(x12 ^ x0, 8)
	// x1 += x5, x13 = rotl32(x13 ^ x1, 8)
	// x2 += x6, x14 = rotl32(x14 ^ x2, 8)
	// x3 += x7, x15 = rotl32(x15 ^ x3, 8)
	add		v0.4s, v0.4s, v4.4s
	add		v1.4s, v1.4s, v5.4s
	add		v2.4s, v2.4s, v6.4s
	add		v3.4s, v3.4s, v7.4s

	eor		v12.16b, v12.16b, v0.16b
	eor		v13.16b, v13.16b, v1.16b
	eor		v14.16b, v14.16b, v2.16b
	eor		v15.16b, v15.16b, v3.16b

	tbl		v12.16b, {v12.16b}, v31.16b
	tbl		v13.16b, {v13.16b}, v31.16b
	tbl		v14.16b, {v14.16b}, v31.16b
	tbl		v15.16b, {v15.16b}, v31.16b

	// x8 += x12, x4 = rotl32(x4 ^ x8, 7)
	// x9 += x13, x5 = rotl32(x5 ^ x9, 7)
	// x10 += x14, x6 = rotl32(x6 ^ x10, 7)
	// x11 += x15, x7 = rotl32(x7 ^ x11, 7)
	add		v8.4s, v8.4s, v12.4s
	add		v9.4s, v9.4s, v13.4s
	add		v10.4s, v10.4s, v14.4s
	add		v11.4s, v11.4s, v15.4s

	eor		v16.16b, v4.16b, v8.16b
	eor		v17.16b, v5.16b, v9.16b
	eor		v18.16b, v6.16b, v10.16b
	eor		v19.16b, v7.16b, v11.16b

	shl		v4.4s, v16.4s, #7
	shl		v5.4s, v17.4s, #7
	shl		v6.4s, v18.4s, #7
	shl		v7.4s, v19.4s, #7

	sri		v4.4s, v16.4s, #25
	sri		v5.4s, v17.4s, #25
	sri		v6.4s, v18.4s, #25
	sri		v7.4s, v19.4s, #25

	// x0 += x5, x15 = rotl32(x15 ^ x0, 16)
	// x1 += x6, x12 = rotl32(x12 ^ x1, 16)
	// x2 += x7, x13 = rotl32(x13 ^ x2, 16)
	// x3 += x4, x14 = rotl32(x14 ^ x3, 16)
	add		v0.4s, v0.4s, v5.4s
	add		v1.4s, v1.4s, v6.4s
	add		v2.4s, v2.4s, v7.4s
	add		v3.4s, v3.4s, v4.4s

	eor		v15.16b, v15.16b, v0.16b
	eor		v12.16b, v12.16b, v1.16b
	eor		v13.16b, v13.16b, v2.16b
	eor		v14.16b, v14.16b, v3.16b

	rev32		v15.8h, v15.8h
	rev32		v12.8h, v12.8h
	rev32		v13.8h, v13.8h
	rev32		v14.8h, v14.8h

	// x10 += x15, x5 = rotl32(x5 ^ x10, 12)
	// x11 += x12, x6 = rotl32(x6 ^ x11, 12)
	// x8 += x13, x7 = rotl32(x7 ^ x8, 12)
	// x9 += x14, x4 = rotl32(x4 ^ x9, 12)
	add		v10.4s, v10.4s, v15.4s
	add		v11.4s, v11.4s, v12.4s
	add		v8.4s, v8.4s, v13.4s
	add		v9.4s, v9.4s, v14.4s

	eor		v16.16b, v5.16b, v10.16b
	eor		v17.16b, v6.16b, v11.16b
	eor		v18.16b, v7.16b, v8.16b
	eor		v19.16b, v4.16b, v9.16b

	shl		v5.4s, v16.4s, #12
	shl		v6.4s, v17.4s, #12
	shl		v7.4s, v18.4s, #12
	shl		v4.4s, v19.4s, #12

	sri		v5.4s, v16.4s, #20
	sri		v6.4s, v17.4s, #20
	sri		v7.4s, v18.4s, #20
	sri		v4.4s, v19.4s, #20

	// x0 += x5, x15 = rotl32(x15 ^ x0, 8)
	// x1 += x6, x12 = rotl32(x12 ^ x1, 8)
	// x2 += x7, x13 = rotl32(x13 ^ x2, 8)
	// x3 += x4, x14 = rotl32(x14 ^ x3, 8)
	add		v0.4s, v0.4s, v5.4s
	add		v1.4s, v1.4s, v6.4s
	add		v2.4s, v2.4s, v7.4s
	add		v3.4s, v3.4s, v4.4s

	eor		v15.16b, v15.16b, v0.16b
	eor		v12.16b, v12.16b, v1.16b
	eor		v13.16b, v13.16b, v2.16b
	eor		v14.16b, v14.16b, v3.16b

	tbl		v15.16b, {v15.16b}, v31.16b
	tbl		v12.16b, {v12.16b}, v31.16b
	tbl		v13.16b, {v13.16b}, v31.16b
	tbl		v14.16b, {v14.16b}, v31.16b

	// x10 += x15, x5 = rotl32(x5 ^ x10, 7)
	// x11 += x12, x6 = rotl32(x6 ^ x11, 7)
	// x8 += x13, x7 = rotl32(x7 ^ x8, 7)
	// x9 += x14, x4 = rotl32(x4 ^ x9, 7)
	add		v10.4s, v10.4s, v15.4s
	add		v11.4s, v11.4s, v12.4s
	add		v8.4s, v8.4s, v13.4s
	add		v9.4s, v9.4s, v14.4s

	eor		v16.16b, v5.16b, v10.16b
	eor		v17.16b, v6.16b, v11.16b
	eor		v18.16b, v7.16b, v8.16b
	eor		v19.16b, v4.16b, v9.16b

	shl		v5.4s, v16.4s, #7
	shl		v6.4s, v17.4s, #7
	shl		v7.4s, v18.4s, #7
	shl		v4.4s, v19.4s, #7

	sri		v5.4s, v16.4s, #25
	sri		v6.4s, v17.4s, #25
	sri		v7.4s, v18.4s, #25
	sri		v4.4s, v19.4s, #25

	subs		x3, x3, #1
	b.ne		.Ldoubleround4

	ld4r		{v16.4s-v19.4s}, [x0], #16
	ld4r		{v20.4s-v23.4s}, [x0], #16

	// x12 += counter values 0-3
	add		v12.4s, v12.4s, v30.4s

	// x0[0-3] += s0[0]
	// x1[0-3] += s0[1]
	// x2[0-3] += s0[2]
	// x3[0-3] += s0[3]
	add		v0.4s, v0.4s, v16.4s
	add		v1.4s, v1.4s, v17.4s
	add		v2.4s, v2.4s, v18.4s
	add		v3.4s, v3.4s, v19.4s

	ld4r		{v24.4s-v27.4s}, [x0], #16
	ld4r		{v28.4s-v31.4s}, [x0]

	// x4[0-3] += s1[0]
	// x5[0-3] += s1[1]
	// x6[0-3] += s1[2]
	// x7[0-3] += s1[3]
	add		v4.4s, v4.4s, v20.4s
	add		v5.4s, v5.4s, v21.4s
	add		v6.4s, v6.4s, v22.4s
	add		v7.4s, v7.4s, v23.4s

	// x8[0-3] += s2[0]
	// x9[0-3] += s2[1]
	// x10[0-3] += s2[2]
	// x11[0-3] += s2[3]
	add		v8.4s, v8.4s, v24.4s
	add		v9.4s, v9.4s, v25.4s
	add		v10.4s, v10.4s, v26.4s
	add		v11.4s, v11.4s, v27.4s

	// x12[0-3] += s3[0]
	// x13[0-3] += s3[1]
	// x14[0-3] += s3[2]
	// x15[0-3] += s3[3]
	add		v12.4s, v12.4s, v28.4s
	add		v13.4s, v13.4s, v29.4s
	add		v14.4s, v14.4s, v30.4s
	add		v15.4s, v15.4s, v31.4s

	// interleave 32-bit words in state n, n+1
	zip1		v16.4s, v0.4s, v1.4s
	zip2		v17.4s, v0.4s, v1.4s
	zip1		v18.4s, v2.4s, v3.4s
	zip2		v19.4s, v2.4s, v3.4s
	zip1		v20.4s, v4.4s, v5.4s
	zip2		v21.4s, v4.4s, v5.4s
	zip1		v22.4s, v6.4s, v7.4s
	zip2		v23.4s, v6.4s, v7.4s
	zip1		v24.4s, v8.4s, v9.4s
	zip2		v25.4s, v8.4s, v9.4s
	zip1		v26.4s, v10.4s, v11.4s
	zip2		v27.4s, v10.4s, v11.4s
	zip1		v28.4s, v12.4s, v13.4s
	zip2		v29.4s, v12.4s, v13.4s
	zip1		v30.4s, v14.4s, v15.4s
	zip2		v31.4s, v14.4s, v15.4s

	// interleave 64-bit words in state n, n+2
	zip1		v0.2d, v16.2d, v18.2d
	zip2		v4.2d, v16.2d, v18.2d
	zip1		v8.2d, v17.2d, v19.2d
	zip2		v12.2d, v17.2d, v19.2d
	ld1		{v16.16b-v19.16b}, [x2], #64

	zip1		v1.2d, v20.2d, v22.2d
	zip2		v5.2d, v20.2d, v22.2d
	zip1		v9.2d, v21.2d, v23.2d
	zip2		v13.2d, v21.2d, v23.2d
	ld1		{v20.16b-v23.16b}, [x2], #64

	zip1		v2.2d, v24.2d, v26.2d
	zip2		v6.2d, v24.2d, v26.2d
	zip1		v10.2d, v25.2d, v27.2d
	zip2		v14.2d, v25.2d, v27.2d
	ld1		{v24.16b-v27.16b}, [x2], #64

	zip1		v3.2d, v28.2d, v30.2d
	zip2		v7.2d, v28.2d, v30.2d
	zip1		v11.2d, v29.2d, v31.2d
	zip2		v15.2d, v29.2d, v31.2d
	ld1		{v28.16b-v31.16b}, [x2]

	// xor with corresponding input, write to output
	eor		v16.16b, v16.16b, v0.16b
	eor		v17.16b, v17.16b, v1.16b
	eor		v18.16b, v18.16b, v2.16b
	eor		v19.16b, v19.16b, v3.16b
	eor		v20.16b, v20.16b, v4.16b
	eor		v21.16b, v21.16b, v5.16b
	st1		{v16.16b-v19.16b}, [x1], #64
	eor		v22.16b, v22.16b, v6.16b
	eor		v23.16b, v23.16b, v7.16b
	eor		v24.16b, v24.16b, v8.16b
	eor		v25.16b, v25.16b, v9.16b
	st1		{v20.16b-v23.16b}, [x1], #64
	eor		v26.16b, v26.16b, v10.16b
	eor		v27.16b, v27.16b, v11.16b
	eor		v28.16b, v28.16b, v12.16b
	st1		{v24.16b-v27.16b}, [x1], #64
	eor		v29.16b, v29.16b, v13.16b
	eor		v30.16b, v30.16b, v14.16b
	eor		v31.16b, v31.16b, v15.16b
	st1		{v28.16b-v31.16b}, [x1]

	ret
ENDPROC(chacha20_4block_xor_neon)

CTRINC:	.word		0, 1, 2, 3
ROT8:	.word		0x02010003, 0x06050407, 0x0a09080b, 0x0e0d0c0f
