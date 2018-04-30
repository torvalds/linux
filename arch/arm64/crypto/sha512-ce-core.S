/* SPDX-License-Identifier: GPL-2.0 */
/*
 * sha512-ce-core.S - core SHA-384/SHA-512 transform using v8 Crypto Extensions
 *
 * Copyright (C) 2018 Linaro Ltd <ard.biesheuvel@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/linkage.h>
#include <asm/assembler.h>

	.irp		b,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19
	.set		.Lq\b, \b
	.set		.Lv\b\().2d, \b
	.endr

	.macro		sha512h, rd, rn, rm
	.inst		0xce608000 | .L\rd | (.L\rn << 5) | (.L\rm << 16)
	.endm

	.macro		sha512h2, rd, rn, rm
	.inst		0xce608400 | .L\rd | (.L\rn << 5) | (.L\rm << 16)
	.endm

	.macro		sha512su0, rd, rn
	.inst		0xcec08000 | .L\rd | (.L\rn << 5)
	.endm

	.macro		sha512su1, rd, rn, rm
	.inst		0xce608800 | .L\rd | (.L\rn << 5) | (.L\rm << 16)
	.endm

	/*
	 * The SHA-512 round constants
	 */
	.section	".rodata", "a"
	.align		4
.Lsha512_rcon:
	.quad		0x428a2f98d728ae22, 0x7137449123ef65cd
	.quad		0xb5c0fbcfec4d3b2f, 0xe9b5dba58189dbbc
	.quad		0x3956c25bf348b538, 0x59f111f1b605d019
	.quad		0x923f82a4af194f9b, 0xab1c5ed5da6d8118
	.quad		0xd807aa98a3030242, 0x12835b0145706fbe
	.quad		0x243185be4ee4b28c, 0x550c7dc3d5ffb4e2
	.quad		0x72be5d74f27b896f, 0x80deb1fe3b1696b1
	.quad		0x9bdc06a725c71235, 0xc19bf174cf692694
	.quad		0xe49b69c19ef14ad2, 0xefbe4786384f25e3
	.quad		0x0fc19dc68b8cd5b5, 0x240ca1cc77ac9c65
	.quad		0x2de92c6f592b0275, 0x4a7484aa6ea6e483
	.quad		0x5cb0a9dcbd41fbd4, 0x76f988da831153b5
	.quad		0x983e5152ee66dfab, 0xa831c66d2db43210
	.quad		0xb00327c898fb213f, 0xbf597fc7beef0ee4
	.quad		0xc6e00bf33da88fc2, 0xd5a79147930aa725
	.quad		0x06ca6351e003826f, 0x142929670a0e6e70
	.quad		0x27b70a8546d22ffc, 0x2e1b21385c26c926
	.quad		0x4d2c6dfc5ac42aed, 0x53380d139d95b3df
	.quad		0x650a73548baf63de, 0x766a0abb3c77b2a8
	.quad		0x81c2c92e47edaee6, 0x92722c851482353b
	.quad		0xa2bfe8a14cf10364, 0xa81a664bbc423001
	.quad		0xc24b8b70d0f89791, 0xc76c51a30654be30
	.quad		0xd192e819d6ef5218, 0xd69906245565a910
	.quad		0xf40e35855771202a, 0x106aa07032bbd1b8
	.quad		0x19a4c116b8d2d0c8, 0x1e376c085141ab53
	.quad		0x2748774cdf8eeb99, 0x34b0bcb5e19b48a8
	.quad		0x391c0cb3c5c95a63, 0x4ed8aa4ae3418acb
	.quad		0x5b9cca4f7763e373, 0x682e6ff3d6b2b8a3
	.quad		0x748f82ee5defb2fc, 0x78a5636f43172f60
	.quad		0x84c87814a1f0ab72, 0x8cc702081a6439ec
	.quad		0x90befffa23631e28, 0xa4506cebde82bde9
	.quad		0xbef9a3f7b2c67915, 0xc67178f2e372532b
	.quad		0xca273eceea26619c, 0xd186b8c721c0c207
	.quad		0xeada7dd6cde0eb1e, 0xf57d4f7fee6ed178
	.quad		0x06f067aa72176fba, 0x0a637dc5a2c898a6
	.quad		0x113f9804bef90dae, 0x1b710b35131c471b
	.quad		0x28db77f523047d84, 0x32caab7b40c72493
	.quad		0x3c9ebe0a15c9bebc, 0x431d67c49c100d4c
	.quad		0x4cc5d4becb3e42b6, 0x597f299cfc657e2a
	.quad		0x5fcb6fab3ad6faec, 0x6c44198c4a475817

	.macro		dround, i0, i1, i2, i3, i4, rc0, rc1, in0, in1, in2, in3, in4
	.ifnb		\rc1
	ld1		{v\rc1\().2d}, [x4], #16
	.endif
	add		v5.2d, v\rc0\().2d, v\in0\().2d
	ext		v6.16b, v\i2\().16b, v\i3\().16b, #8
	ext		v5.16b, v5.16b, v5.16b, #8
	ext		v7.16b, v\i1\().16b, v\i2\().16b, #8
	add		v\i3\().2d, v\i3\().2d, v5.2d
	.ifnb		\in1
	ext		v5.16b, v\in3\().16b, v\in4\().16b, #8
	sha512su0	v\in0\().2d, v\in1\().2d
	.endif
	sha512h		q\i3, q6, v7.2d
	.ifnb		\in1
	sha512su1	v\in0\().2d, v\in2\().2d, v5.2d
	.endif
	add		v\i4\().2d, v\i1\().2d, v\i3\().2d
	sha512h2	q\i3, q\i1, v\i0\().2d
	.endm

	/*
	 * void sha512_ce_transform(struct sha512_state *sst, u8 const *src,
	 *			  int blocks)
	 */
	.text
ENTRY(sha512_ce_transform)
	frame_push	3

	mov		x19, x0
	mov		x20, x1
	mov		x21, x2

	/* load state */
0:	ld1		{v8.2d-v11.2d}, [x19]

	/* load first 4 round constants */
	adr_l		x3, .Lsha512_rcon
	ld1		{v20.2d-v23.2d}, [x3], #64

	/* load input */
1:	ld1		{v12.2d-v15.2d}, [x20], #64
	ld1		{v16.2d-v19.2d}, [x20], #64
	sub		w21, w21, #1

CPU_LE(	rev64		v12.16b, v12.16b	)
CPU_LE(	rev64		v13.16b, v13.16b	)
CPU_LE(	rev64		v14.16b, v14.16b	)
CPU_LE(	rev64		v15.16b, v15.16b	)
CPU_LE(	rev64		v16.16b, v16.16b	)
CPU_LE(	rev64		v17.16b, v17.16b	)
CPU_LE(	rev64		v18.16b, v18.16b	)
CPU_LE(	rev64		v19.16b, v19.16b	)

	mov		x4, x3				// rc pointer

	mov		v0.16b, v8.16b
	mov		v1.16b, v9.16b
	mov		v2.16b, v10.16b
	mov		v3.16b, v11.16b

	// v0  ab  cd  --  ef  gh  ab
	// v1  cd  --  ef  gh  ab  cd
	// v2  ef  gh  ab  cd  --  ef
	// v3  gh  ab  cd  --  ef  gh
	// v4  --  ef  gh  ab  cd  --

	dround		0, 1, 2, 3, 4, 20, 24, 12, 13, 19, 16, 17
	dround		3, 0, 4, 2, 1, 21, 25, 13, 14, 12, 17, 18
	dround		2, 3, 1, 4, 0, 22, 26, 14, 15, 13, 18, 19
	dround		4, 2, 0, 1, 3, 23, 27, 15, 16, 14, 19, 12
	dround		1, 4, 3, 0, 2, 24, 28, 16, 17, 15, 12, 13

	dround		0, 1, 2, 3, 4, 25, 29, 17, 18, 16, 13, 14
	dround		3, 0, 4, 2, 1, 26, 30, 18, 19, 17, 14, 15
	dround		2, 3, 1, 4, 0, 27, 31, 19, 12, 18, 15, 16
	dround		4, 2, 0, 1, 3, 28, 24, 12, 13, 19, 16, 17
	dround		1, 4, 3, 0, 2, 29, 25, 13, 14, 12, 17, 18

	dround		0, 1, 2, 3, 4, 30, 26, 14, 15, 13, 18, 19
	dround		3, 0, 4, 2, 1, 31, 27, 15, 16, 14, 19, 12
	dround		2, 3, 1, 4, 0, 24, 28, 16, 17, 15, 12, 13
	dround		4, 2, 0, 1, 3, 25, 29, 17, 18, 16, 13, 14
	dround		1, 4, 3, 0, 2, 26, 30, 18, 19, 17, 14, 15

	dround		0, 1, 2, 3, 4, 27, 31, 19, 12, 18, 15, 16
	dround		3, 0, 4, 2, 1, 28, 24, 12, 13, 19, 16, 17
	dround		2, 3, 1, 4, 0, 29, 25, 13, 14, 12, 17, 18
	dround		4, 2, 0, 1, 3, 30, 26, 14, 15, 13, 18, 19
	dround		1, 4, 3, 0, 2, 31, 27, 15, 16, 14, 19, 12

	dround		0, 1, 2, 3, 4, 24, 28, 16, 17, 15, 12, 13
	dround		3, 0, 4, 2, 1, 25, 29, 17, 18, 16, 13, 14
	dround		2, 3, 1, 4, 0, 26, 30, 18, 19, 17, 14, 15
	dround		4, 2, 0, 1, 3, 27, 31, 19, 12, 18, 15, 16
	dround		1, 4, 3, 0, 2, 28, 24, 12, 13, 19, 16, 17

	dround		0, 1, 2, 3, 4, 29, 25, 13, 14, 12, 17, 18
	dround		3, 0, 4, 2, 1, 30, 26, 14, 15, 13, 18, 19
	dround		2, 3, 1, 4, 0, 31, 27, 15, 16, 14, 19, 12
	dround		4, 2, 0, 1, 3, 24, 28, 16, 17, 15, 12, 13
	dround		1, 4, 3, 0, 2, 25, 29, 17, 18, 16, 13, 14

	dround		0, 1, 2, 3, 4, 26, 30, 18, 19, 17, 14, 15
	dround		3, 0, 4, 2, 1, 27, 31, 19, 12, 18, 15, 16
	dround		2, 3, 1, 4, 0, 28, 24, 12
	dround		4, 2, 0, 1, 3, 29, 25, 13
	dround		1, 4, 3, 0, 2, 30, 26, 14

	dround		0, 1, 2, 3, 4, 31, 27, 15
	dround		3, 0, 4, 2, 1, 24,   , 16
	dround		2, 3, 1, 4, 0, 25,   , 17
	dround		4, 2, 0, 1, 3, 26,   , 18
	dround		1, 4, 3, 0, 2, 27,   , 19

	/* update state */
	add		v8.2d, v8.2d, v0.2d
	add		v9.2d, v9.2d, v1.2d
	add		v10.2d, v10.2d, v2.2d
	add		v11.2d, v11.2d, v3.2d

	/* handled all input blocks? */
	cbz		w21, 3f

	if_will_cond_yield_neon
	st1		{v8.2d-v11.2d}, [x19]
	do_cond_yield_neon
	b		0b
	endif_yield_neon

	b		1b

	/* store new state */
3:	st1		{v8.2d-v11.2d}, [x19]
	frame_pop
	ret
ENDPROC(sha512_ce_transform)
