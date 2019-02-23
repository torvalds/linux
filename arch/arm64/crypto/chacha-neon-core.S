/*
 * ChaCha/XChaCha NEON helper functions
 *
 * Copyright (C) 2016-2018 Linaro, Ltd. <ard.biesheuvel@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Originally based on:
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
#include <asm/assembler.h>
#include <asm/cache.h>

	.text
	.align		6

/*
 * chacha_permute - permute one block
 *
 * Permute one 64-byte block where the state matrix is stored in the four NEON
 * registers v0-v3.  It performs matrix operations on four words in parallel,
 * but requires shuffling to rearrange the words after each round.
 *
 * The round count is given in w3.
 *
 * Clobbers: w3, x10, v4, v12
 */
chacha_permute:

	adr_l		x10, ROT8
	ld1		{v12.4s}, [x10]

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

	subs		w3, w3, #2
	b.ne		.Ldoubleround

	ret
ENDPROC(chacha_permute)

ENTRY(chacha_block_xor_neon)
	// x0: Input state matrix, s
	// x1: 1 data block output, o
	// x2: 1 data block input, i
	// w3: nrounds

	stp		x29, x30, [sp, #-16]!
	mov		x29, sp

	// x0..3 = s0..3
	ld1		{v0.4s-v3.4s}, [x0]
	ld1		{v8.4s-v11.4s}, [x0]

	bl		chacha_permute

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

	ldp		x29, x30, [sp], #16
	ret
ENDPROC(chacha_block_xor_neon)

ENTRY(hchacha_block_neon)
	// x0: Input state matrix, s
	// x1: output (8 32-bit words)
	// w2: nrounds

	stp		x29, x30, [sp, #-16]!
	mov		x29, sp

	ld1		{v0.4s-v3.4s}, [x0]

	mov		w3, w2
	bl		chacha_permute

	st1		{v0.16b}, [x1], #16
	st1		{v3.16b}, [x1]

	ldp		x29, x30, [sp], #16
	ret
ENDPROC(hchacha_block_neon)

	a0		.req	w12
	a1		.req	w13
	a2		.req	w14
	a3		.req	w15
	a4		.req	w16
	a5		.req	w17
	a6		.req	w19
	a7		.req	w20
	a8		.req	w21
	a9		.req	w22
	a10		.req	w23
	a11		.req	w24
	a12		.req	w25
	a13		.req	w26
	a14		.req	w27
	a15		.req	w28

	.align		6
ENTRY(chacha_4block_xor_neon)
	frame_push	10

	// x0: Input state matrix, s
	// x1: 4 data blocks output, o
	// x2: 4 data blocks input, i
	// w3: nrounds
	// x4: byte count

	adr_l		x10, .Lpermute
	and		x5, x4, #63
	add		x10, x10, x5
	add		x11, x10, #64

	//
	// This function encrypts four consecutive ChaCha blocks by loading
	// the state matrix in NEON registers four times. The algorithm performs
	// each operation on the corresponding word of each state matrix, hence
	// requires no word shuffling. For final XORing step we transpose the
	// matrix by interleaving 32- and then 64-bit words, which allows us to
	// do XOR in NEON registers.
	//
	// At the same time, a fifth block is encrypted in parallel using
	// scalar registers
	//
	adr_l		x9, CTRINC		// ... and ROT8
	ld1		{v30.4s-v31.4s}, [x9]

	// x0..15[0-3] = s0..3[0..3]
	add		x8, x0, #16
	ld4r		{ v0.4s- v3.4s}, [x0]
	ld4r		{ v4.4s- v7.4s}, [x8], #16
	ld4r		{ v8.4s-v11.4s}, [x8], #16
	ld4r		{v12.4s-v15.4s}, [x8]

	mov		a0, v0.s[0]
	mov		a1, v1.s[0]
	mov		a2, v2.s[0]
	mov		a3, v3.s[0]
	mov		a4, v4.s[0]
	mov		a5, v5.s[0]
	mov		a6, v6.s[0]
	mov		a7, v7.s[0]
	mov		a8, v8.s[0]
	mov		a9, v9.s[0]
	mov		a10, v10.s[0]
	mov		a11, v11.s[0]
	mov		a12, v12.s[0]
	mov		a13, v13.s[0]
	mov		a14, v14.s[0]
	mov		a15, v15.s[0]

	// x12 += counter values 1-4
	add		v12.4s, v12.4s, v30.4s

.Ldoubleround4:
	// x0 += x4, x12 = rotl32(x12 ^ x0, 16)
	// x1 += x5, x13 = rotl32(x13 ^ x1, 16)
	// x2 += x6, x14 = rotl32(x14 ^ x2, 16)
	// x3 += x7, x15 = rotl32(x15 ^ x3, 16)
	add		v0.4s, v0.4s, v4.4s
	  add		a0, a0, a4
	add		v1.4s, v1.4s, v5.4s
	  add		a1, a1, a5
	add		v2.4s, v2.4s, v6.4s
	  add		a2, a2, a6
	add		v3.4s, v3.4s, v7.4s
	  add		a3, a3, a7

	eor		v12.16b, v12.16b, v0.16b
	  eor		a12, a12, a0
	eor		v13.16b, v13.16b, v1.16b
	  eor		a13, a13, a1
	eor		v14.16b, v14.16b, v2.16b
	  eor		a14, a14, a2
	eor		v15.16b, v15.16b, v3.16b
	  eor		a15, a15, a3

	rev32		v12.8h, v12.8h
	  ror		a12, a12, #16
	rev32		v13.8h, v13.8h
	  ror		a13, a13, #16
	rev32		v14.8h, v14.8h
	  ror		a14, a14, #16
	rev32		v15.8h, v15.8h
	  ror		a15, a15, #16

	// x8 += x12, x4 = rotl32(x4 ^ x8, 12)
	// x9 += x13, x5 = rotl32(x5 ^ x9, 12)
	// x10 += x14, x6 = rotl32(x6 ^ x10, 12)
	// x11 += x15, x7 = rotl32(x7 ^ x11, 12)
	add		v8.4s, v8.4s, v12.4s
	  add		a8, a8, a12
	add		v9.4s, v9.4s, v13.4s
	  add		a9, a9, a13
	add		v10.4s, v10.4s, v14.4s
	  add		a10, a10, a14
	add		v11.4s, v11.4s, v15.4s
	  add		a11, a11, a15

	eor		v16.16b, v4.16b, v8.16b
	  eor		a4, a4, a8
	eor		v17.16b, v5.16b, v9.16b
	  eor		a5, a5, a9
	eor		v18.16b, v6.16b, v10.16b
	  eor		a6, a6, a10
	eor		v19.16b, v7.16b, v11.16b
	  eor		a7, a7, a11

	shl		v4.4s, v16.4s, #12
	shl		v5.4s, v17.4s, #12
	shl		v6.4s, v18.4s, #12
	shl		v7.4s, v19.4s, #12

	sri		v4.4s, v16.4s, #20
	  ror		a4, a4, #20
	sri		v5.4s, v17.4s, #20
	  ror		a5, a5, #20
	sri		v6.4s, v18.4s, #20
	  ror		a6, a6, #20
	sri		v7.4s, v19.4s, #20
	  ror		a7, a7, #20

	// x0 += x4, x12 = rotl32(x12 ^ x0, 8)
	// x1 += x5, x13 = rotl32(x13 ^ x1, 8)
	// x2 += x6, x14 = rotl32(x14 ^ x2, 8)
	// x3 += x7, x15 = rotl32(x15 ^ x3, 8)
	add		v0.4s, v0.4s, v4.4s
	  add		a0, a0, a4
	add		v1.4s, v1.4s, v5.4s
	  add		a1, a1, a5
	add		v2.4s, v2.4s, v6.4s
	  add		a2, a2, a6
	add		v3.4s, v3.4s, v7.4s
	  add		a3, a3, a7

	eor		v12.16b, v12.16b, v0.16b
	  eor		a12, a12, a0
	eor		v13.16b, v13.16b, v1.16b
	  eor		a13, a13, a1
	eor		v14.16b, v14.16b, v2.16b
	  eor		a14, a14, a2
	eor		v15.16b, v15.16b, v3.16b
	  eor		a15, a15, a3

	tbl		v12.16b, {v12.16b}, v31.16b
	  ror		a12, a12, #24
	tbl		v13.16b, {v13.16b}, v31.16b
	  ror		a13, a13, #24
	tbl		v14.16b, {v14.16b}, v31.16b
	  ror		a14, a14, #24
	tbl		v15.16b, {v15.16b}, v31.16b
	  ror		a15, a15, #24

	// x8 += x12, x4 = rotl32(x4 ^ x8, 7)
	// x9 += x13, x5 = rotl32(x5 ^ x9, 7)
	// x10 += x14, x6 = rotl32(x6 ^ x10, 7)
	// x11 += x15, x7 = rotl32(x7 ^ x11, 7)
	add		v8.4s, v8.4s, v12.4s
	  add		a8, a8, a12
	add		v9.4s, v9.4s, v13.4s
	  add		a9, a9, a13
	add		v10.4s, v10.4s, v14.4s
	  add		a10, a10, a14
	add		v11.4s, v11.4s, v15.4s
	  add		a11, a11, a15

	eor		v16.16b, v4.16b, v8.16b
	  eor		a4, a4, a8
	eor		v17.16b, v5.16b, v9.16b
	  eor		a5, a5, a9
	eor		v18.16b, v6.16b, v10.16b
	  eor		a6, a6, a10
	eor		v19.16b, v7.16b, v11.16b
	  eor		a7, a7, a11

	shl		v4.4s, v16.4s, #7
	shl		v5.4s, v17.4s, #7
	shl		v6.4s, v18.4s, #7
	shl		v7.4s, v19.4s, #7

	sri		v4.4s, v16.4s, #25
	  ror		a4, a4, #25
	sri		v5.4s, v17.4s, #25
	  ror		a5, a5, #25
	sri		v6.4s, v18.4s, #25
	 ror		a6, a6, #25
	sri		v7.4s, v19.4s, #25
	  ror		a7, a7, #25

	// x0 += x5, x15 = rotl32(x15 ^ x0, 16)
	// x1 += x6, x12 = rotl32(x12 ^ x1, 16)
	// x2 += x7, x13 = rotl32(x13 ^ x2, 16)
	// x3 += x4, x14 = rotl32(x14 ^ x3, 16)
	add		v0.4s, v0.4s, v5.4s
	  add		a0, a0, a5
	add		v1.4s, v1.4s, v6.4s
	  add		a1, a1, a6
	add		v2.4s, v2.4s, v7.4s
	  add		a2, a2, a7
	add		v3.4s, v3.4s, v4.4s
	  add		a3, a3, a4

	eor		v15.16b, v15.16b, v0.16b
	  eor		a15, a15, a0
	eor		v12.16b, v12.16b, v1.16b
	  eor		a12, a12, a1
	eor		v13.16b, v13.16b, v2.16b
	  eor		a13, a13, a2
	eor		v14.16b, v14.16b, v3.16b
	  eor		a14, a14, a3

	rev32		v15.8h, v15.8h
	  ror		a15, a15, #16
	rev32		v12.8h, v12.8h
	  ror		a12, a12, #16
	rev32		v13.8h, v13.8h
	  ror		a13, a13, #16
	rev32		v14.8h, v14.8h
	  ror		a14, a14, #16

	// x10 += x15, x5 = rotl32(x5 ^ x10, 12)
	// x11 += x12, x6 = rotl32(x6 ^ x11, 12)
	// x8 += x13, x7 = rotl32(x7 ^ x8, 12)
	// x9 += x14, x4 = rotl32(x4 ^ x9, 12)
	add		v10.4s, v10.4s, v15.4s
	  add		a10, a10, a15
	add		v11.4s, v11.4s, v12.4s
	  add		a11, a11, a12
	add		v8.4s, v8.4s, v13.4s
	  add		a8, a8, a13
	add		v9.4s, v9.4s, v14.4s
	  add		a9, a9, a14

	eor		v16.16b, v5.16b, v10.16b
	  eor		a5, a5, a10
	eor		v17.16b, v6.16b, v11.16b
	  eor		a6, a6, a11
	eor		v18.16b, v7.16b, v8.16b
	  eor		a7, a7, a8
	eor		v19.16b, v4.16b, v9.16b
	  eor		a4, a4, a9

	shl		v5.4s, v16.4s, #12
	shl		v6.4s, v17.4s, #12
	shl		v7.4s, v18.4s, #12
	shl		v4.4s, v19.4s, #12

	sri		v5.4s, v16.4s, #20
	  ror		a5, a5, #20
	sri		v6.4s, v17.4s, #20
	  ror		a6, a6, #20
	sri		v7.4s, v18.4s, #20
	  ror		a7, a7, #20
	sri		v4.4s, v19.4s, #20
	  ror		a4, a4, #20

	// x0 += x5, x15 = rotl32(x15 ^ x0, 8)
	// x1 += x6, x12 = rotl32(x12 ^ x1, 8)
	// x2 += x7, x13 = rotl32(x13 ^ x2, 8)
	// x3 += x4, x14 = rotl32(x14 ^ x3, 8)
	add		v0.4s, v0.4s, v5.4s
	  add		a0, a0, a5
	add		v1.4s, v1.4s, v6.4s
	  add		a1, a1, a6
	add		v2.4s, v2.4s, v7.4s
	  add		a2, a2, a7
	add		v3.4s, v3.4s, v4.4s
	  add		a3, a3, a4

	eor		v15.16b, v15.16b, v0.16b
	  eor		a15, a15, a0
	eor		v12.16b, v12.16b, v1.16b
	  eor		a12, a12, a1
	eor		v13.16b, v13.16b, v2.16b
	  eor		a13, a13, a2
	eor		v14.16b, v14.16b, v3.16b
	  eor		a14, a14, a3

	tbl		v15.16b, {v15.16b}, v31.16b
	  ror		a15, a15, #24
	tbl		v12.16b, {v12.16b}, v31.16b
	  ror		a12, a12, #24
	tbl		v13.16b, {v13.16b}, v31.16b
	  ror		a13, a13, #24
	tbl		v14.16b, {v14.16b}, v31.16b
	  ror		a14, a14, #24

	// x10 += x15, x5 = rotl32(x5 ^ x10, 7)
	// x11 += x12, x6 = rotl32(x6 ^ x11, 7)
	// x8 += x13, x7 = rotl32(x7 ^ x8, 7)
	// x9 += x14, x4 = rotl32(x4 ^ x9, 7)
	add		v10.4s, v10.4s, v15.4s
	  add		a10, a10, a15
	add		v11.4s, v11.4s, v12.4s
	  add		a11, a11, a12
	add		v8.4s, v8.4s, v13.4s
	  add		a8, a8, a13
	add		v9.4s, v9.4s, v14.4s
	  add		a9, a9, a14

	eor		v16.16b, v5.16b, v10.16b
	  eor		a5, a5, a10
	eor		v17.16b, v6.16b, v11.16b
	  eor		a6, a6, a11
	eor		v18.16b, v7.16b, v8.16b
	  eor		a7, a7, a8
	eor		v19.16b, v4.16b, v9.16b
	  eor		a4, a4, a9

	shl		v5.4s, v16.4s, #7
	shl		v6.4s, v17.4s, #7
	shl		v7.4s, v18.4s, #7
	shl		v4.4s, v19.4s, #7

	sri		v5.4s, v16.4s, #25
	  ror		a5, a5, #25
	sri		v6.4s, v17.4s, #25
	  ror		a6, a6, #25
	sri		v7.4s, v18.4s, #25
	  ror		a7, a7, #25
	sri		v4.4s, v19.4s, #25
	  ror		a4, a4, #25

	subs		w3, w3, #2
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
	  mov		w6, v16.s[0]
	  mov		w7, v17.s[0]
	add		v1.4s, v1.4s, v17.4s
	  mov		w8, v18.s[0]
	  mov		w9, v19.s[0]
	add		v2.4s, v2.4s, v18.4s
	  add		a0, a0, w6
	  add		a1, a1, w7
	add		v3.4s, v3.4s, v19.4s
	  add		a2, a2, w8
	  add		a3, a3, w9
CPU_BE(	  rev		a0, a0		)
CPU_BE(	  rev		a1, a1		)
CPU_BE(	  rev		a2, a2		)
CPU_BE(	  rev		a3, a3		)

	ld4r		{v24.4s-v27.4s}, [x0], #16
	ld4r		{v28.4s-v31.4s}, [x0]

	// x4[0-3] += s1[0]
	// x5[0-3] += s1[1]
	// x6[0-3] += s1[2]
	// x7[0-3] += s1[3]
	add		v4.4s, v4.4s, v20.4s
	  mov		w6, v20.s[0]
	  mov		w7, v21.s[0]
	add		v5.4s, v5.4s, v21.4s
	  mov		w8, v22.s[0]
	  mov		w9, v23.s[0]
	add		v6.4s, v6.4s, v22.4s
	  add		a4, a4, w6
	  add		a5, a5, w7
	add		v7.4s, v7.4s, v23.4s
	  add		a6, a6, w8
	  add		a7, a7, w9
CPU_BE(	  rev		a4, a4		)
CPU_BE(	  rev		a5, a5		)
CPU_BE(	  rev		a6, a6		)
CPU_BE(	  rev		a7, a7		)

	// x8[0-3] += s2[0]
	// x9[0-3] += s2[1]
	// x10[0-3] += s2[2]
	// x11[0-3] += s2[3]
	add		v8.4s, v8.4s, v24.4s
	  mov		w6, v24.s[0]
	  mov		w7, v25.s[0]
	add		v9.4s, v9.4s, v25.4s
	  mov		w8, v26.s[0]
	  mov		w9, v27.s[0]
	add		v10.4s, v10.4s, v26.4s
	  add		a8, a8, w6
	  add		a9, a9, w7
	add		v11.4s, v11.4s, v27.4s
	  add		a10, a10, w8
	  add		a11, a11, w9
CPU_BE(	  rev		a8, a8		)
CPU_BE(	  rev		a9, a9		)
CPU_BE(	  rev		a10, a10	)
CPU_BE(	  rev		a11, a11	)

	// x12[0-3] += s3[0]
	// x13[0-3] += s3[1]
	// x14[0-3] += s3[2]
	// x15[0-3] += s3[3]
	add		v12.4s, v12.4s, v28.4s
	  mov		w6, v28.s[0]
	  mov		w7, v29.s[0]
	add		v13.4s, v13.4s, v29.4s
	  mov		w8, v30.s[0]
	  mov		w9, v31.s[0]
	add		v14.4s, v14.4s, v30.4s
	  add		a12, a12, w6
	  add		a13, a13, w7
	add		v15.4s, v15.4s, v31.4s
	  add		a14, a14, w8
	  add		a15, a15, w9
CPU_BE(	  rev		a12, a12	)
CPU_BE(	  rev		a13, a13	)
CPU_BE(	  rev		a14, a14	)
CPU_BE(	  rev		a15, a15	)

	// interleave 32-bit words in state n, n+1
	  ldp		w6, w7, [x2], #64
	zip1		v16.4s, v0.4s, v1.4s
	  ldp		w8, w9, [x2, #-56]
	  eor		a0, a0, w6
	zip2		v17.4s, v0.4s, v1.4s
	  eor		a1, a1, w7
	zip1		v18.4s, v2.4s, v3.4s
	  eor		a2, a2, w8
	zip2		v19.4s, v2.4s, v3.4s
	  eor		a3, a3, w9
	  ldp		w6, w7, [x2, #-48]
	zip1		v20.4s, v4.4s, v5.4s
	  ldp		w8, w9, [x2, #-40]
	  eor		a4, a4, w6
	zip2		v21.4s, v4.4s, v5.4s
	  eor		a5, a5, w7
	zip1		v22.4s, v6.4s, v7.4s
	  eor		a6, a6, w8
	zip2		v23.4s, v6.4s, v7.4s
	  eor		a7, a7, w9
	  ldp		w6, w7, [x2, #-32]
	zip1		v24.4s, v8.4s, v9.4s
	  ldp		w8, w9, [x2, #-24]
	  eor		a8, a8, w6
	zip2		v25.4s, v8.4s, v9.4s
	  eor		a9, a9, w7
	zip1		v26.4s, v10.4s, v11.4s
	  eor		a10, a10, w8
	zip2		v27.4s, v10.4s, v11.4s
	  eor		a11, a11, w9
	  ldp		w6, w7, [x2, #-16]
	zip1		v28.4s, v12.4s, v13.4s
	  ldp		w8, w9, [x2, #-8]
	  eor		a12, a12, w6
	zip2		v29.4s, v12.4s, v13.4s
	  eor		a13, a13, w7
	zip1		v30.4s, v14.4s, v15.4s
	  eor		a14, a14, w8
	zip2		v31.4s, v14.4s, v15.4s
	  eor		a15, a15, w9

	mov		x3, #64
	subs		x5, x4, #128
	add		x6, x5, x2
	csel		x3, x3, xzr, ge
	csel		x2, x2, x6, ge

	// interleave 64-bit words in state n, n+2
	zip1		v0.2d, v16.2d, v18.2d
	zip2		v4.2d, v16.2d, v18.2d
	  stp		a0, a1, [x1], #64
	zip1		v8.2d, v17.2d, v19.2d
	zip2		v12.2d, v17.2d, v19.2d
	  stp		a2, a3, [x1, #-56]
	ld1		{v16.16b-v19.16b}, [x2], x3

	subs		x6, x4, #192
	ccmp		x3, xzr, #4, lt
	add		x7, x6, x2
	csel		x3, x3, xzr, eq
	csel		x2, x2, x7, eq

	zip1		v1.2d, v20.2d, v22.2d
	zip2		v5.2d, v20.2d, v22.2d
	  stp		a4, a5, [x1, #-48]
	zip1		v9.2d, v21.2d, v23.2d
	zip2		v13.2d, v21.2d, v23.2d
	  stp		a6, a7, [x1, #-40]
	ld1		{v20.16b-v23.16b}, [x2], x3

	subs		x7, x4, #256
	ccmp		x3, xzr, #4, lt
	add		x8, x7, x2
	csel		x3, x3, xzr, eq
	csel		x2, x2, x8, eq

	zip1		v2.2d, v24.2d, v26.2d
	zip2		v6.2d, v24.2d, v26.2d
	  stp		a8, a9, [x1, #-32]
	zip1		v10.2d, v25.2d, v27.2d
	zip2		v14.2d, v25.2d, v27.2d
	  stp		a10, a11, [x1, #-24]
	ld1		{v24.16b-v27.16b}, [x2], x3

	subs		x8, x4, #320
	ccmp		x3, xzr, #4, lt
	add		x9, x8, x2
	csel		x2, x2, x9, eq

	zip1		v3.2d, v28.2d, v30.2d
	zip2		v7.2d, v28.2d, v30.2d
	  stp		a12, a13, [x1, #-16]
	zip1		v11.2d, v29.2d, v31.2d
	zip2		v15.2d, v29.2d, v31.2d
	  stp		a14, a15, [x1, #-8]
	ld1		{v28.16b-v31.16b}, [x2]

	// xor with corresponding input, write to output
	tbnz		x5, #63, 0f
	eor		v16.16b, v16.16b, v0.16b
	eor		v17.16b, v17.16b, v1.16b
	eor		v18.16b, v18.16b, v2.16b
	eor		v19.16b, v19.16b, v3.16b
	st1		{v16.16b-v19.16b}, [x1], #64
	cbz		x5, .Lout

	tbnz		x6, #63, 1f
	eor		v20.16b, v20.16b, v4.16b
	eor		v21.16b, v21.16b, v5.16b
	eor		v22.16b, v22.16b, v6.16b
	eor		v23.16b, v23.16b, v7.16b
	st1		{v20.16b-v23.16b}, [x1], #64
	cbz		x6, .Lout

	tbnz		x7, #63, 2f
	eor		v24.16b, v24.16b, v8.16b
	eor		v25.16b, v25.16b, v9.16b
	eor		v26.16b, v26.16b, v10.16b
	eor		v27.16b, v27.16b, v11.16b
	st1		{v24.16b-v27.16b}, [x1], #64
	cbz		x7, .Lout

	tbnz		x8, #63, 3f
	eor		v28.16b, v28.16b, v12.16b
	eor		v29.16b, v29.16b, v13.16b
	eor		v30.16b, v30.16b, v14.16b
	eor		v31.16b, v31.16b, v15.16b
	st1		{v28.16b-v31.16b}, [x1]

.Lout:	frame_pop
	ret

	// fewer than 128 bytes of in/output
0:	ld1		{v8.16b}, [x10]
	ld1		{v9.16b}, [x11]
	movi		v10.16b, #16
	sub		x2, x1, #64
	add		x1, x1, x5
	ld1		{v16.16b-v19.16b}, [x2]
	tbl		v4.16b, {v0.16b-v3.16b}, v8.16b
	tbx		v20.16b, {v16.16b-v19.16b}, v9.16b
	add		v8.16b, v8.16b, v10.16b
	add		v9.16b, v9.16b, v10.16b
	tbl		v5.16b, {v0.16b-v3.16b}, v8.16b
	tbx		v21.16b, {v16.16b-v19.16b}, v9.16b
	add		v8.16b, v8.16b, v10.16b
	add		v9.16b, v9.16b, v10.16b
	tbl		v6.16b, {v0.16b-v3.16b}, v8.16b
	tbx		v22.16b, {v16.16b-v19.16b}, v9.16b
	add		v8.16b, v8.16b, v10.16b
	add		v9.16b, v9.16b, v10.16b
	tbl		v7.16b, {v0.16b-v3.16b}, v8.16b
	tbx		v23.16b, {v16.16b-v19.16b}, v9.16b

	eor		v20.16b, v20.16b, v4.16b
	eor		v21.16b, v21.16b, v5.16b
	eor		v22.16b, v22.16b, v6.16b
	eor		v23.16b, v23.16b, v7.16b
	st1		{v20.16b-v23.16b}, [x1]
	b		.Lout

	// fewer than 192 bytes of in/output
1:	ld1		{v8.16b}, [x10]
	ld1		{v9.16b}, [x11]
	movi		v10.16b, #16
	add		x1, x1, x6
	tbl		v0.16b, {v4.16b-v7.16b}, v8.16b
	tbx		v20.16b, {v16.16b-v19.16b}, v9.16b
	add		v8.16b, v8.16b, v10.16b
	add		v9.16b, v9.16b, v10.16b
	tbl		v1.16b, {v4.16b-v7.16b}, v8.16b
	tbx		v21.16b, {v16.16b-v19.16b}, v9.16b
	add		v8.16b, v8.16b, v10.16b
	add		v9.16b, v9.16b, v10.16b
	tbl		v2.16b, {v4.16b-v7.16b}, v8.16b
	tbx		v22.16b, {v16.16b-v19.16b}, v9.16b
	add		v8.16b, v8.16b, v10.16b
	add		v9.16b, v9.16b, v10.16b
	tbl		v3.16b, {v4.16b-v7.16b}, v8.16b
	tbx		v23.16b, {v16.16b-v19.16b}, v9.16b

	eor		v20.16b, v20.16b, v0.16b
	eor		v21.16b, v21.16b, v1.16b
	eor		v22.16b, v22.16b, v2.16b
	eor		v23.16b, v23.16b, v3.16b
	st1		{v20.16b-v23.16b}, [x1]
	b		.Lout

	// fewer than 256 bytes of in/output
2:	ld1		{v4.16b}, [x10]
	ld1		{v5.16b}, [x11]
	movi		v6.16b, #16
	add		x1, x1, x7
	tbl		v0.16b, {v8.16b-v11.16b}, v4.16b
	tbx		v24.16b, {v20.16b-v23.16b}, v5.16b
	add		v4.16b, v4.16b, v6.16b
	add		v5.16b, v5.16b, v6.16b
	tbl		v1.16b, {v8.16b-v11.16b}, v4.16b
	tbx		v25.16b, {v20.16b-v23.16b}, v5.16b
	add		v4.16b, v4.16b, v6.16b
	add		v5.16b, v5.16b, v6.16b
	tbl		v2.16b, {v8.16b-v11.16b}, v4.16b
	tbx		v26.16b, {v20.16b-v23.16b}, v5.16b
	add		v4.16b, v4.16b, v6.16b
	add		v5.16b, v5.16b, v6.16b
	tbl		v3.16b, {v8.16b-v11.16b}, v4.16b
	tbx		v27.16b, {v20.16b-v23.16b}, v5.16b

	eor		v24.16b, v24.16b, v0.16b
	eor		v25.16b, v25.16b, v1.16b
	eor		v26.16b, v26.16b, v2.16b
	eor		v27.16b, v27.16b, v3.16b
	st1		{v24.16b-v27.16b}, [x1]
	b		.Lout

	// fewer than 320 bytes of in/output
3:	ld1		{v4.16b}, [x10]
	ld1		{v5.16b}, [x11]
	movi		v6.16b, #16
	add		x1, x1, x8
	tbl		v0.16b, {v12.16b-v15.16b}, v4.16b
	tbx		v28.16b, {v24.16b-v27.16b}, v5.16b
	add		v4.16b, v4.16b, v6.16b
	add		v5.16b, v5.16b, v6.16b
	tbl		v1.16b, {v12.16b-v15.16b}, v4.16b
	tbx		v29.16b, {v24.16b-v27.16b}, v5.16b
	add		v4.16b, v4.16b, v6.16b
	add		v5.16b, v5.16b, v6.16b
	tbl		v2.16b, {v12.16b-v15.16b}, v4.16b
	tbx		v30.16b, {v24.16b-v27.16b}, v5.16b
	add		v4.16b, v4.16b, v6.16b
	add		v5.16b, v5.16b, v6.16b
	tbl		v3.16b, {v12.16b-v15.16b}, v4.16b
	tbx		v31.16b, {v24.16b-v27.16b}, v5.16b

	eor		v28.16b, v28.16b, v0.16b
	eor		v29.16b, v29.16b, v1.16b
	eor		v30.16b, v30.16b, v2.16b
	eor		v31.16b, v31.16b, v3.16b
	st1		{v28.16b-v31.16b}, [x1]
	b		.Lout
ENDPROC(chacha_4block_xor_neon)

	.section	".rodata", "a", %progbits
	.align		L1_CACHE_SHIFT
.Lpermute:
	.set		.Li, 0
	.rept		192
	.byte		(.Li - 64)
	.set		.Li, .Li + 1
	.endr

CTRINC:	.word		1, 2, 3, 4
ROT8:	.word		0x02010003, 0x06050407, 0x0a09080b, 0x0e0d0c0f
