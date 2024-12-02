//
// Accelerated CRC-T10DIF using arm64 NEON and Crypto Extensions instructions
//
// Copyright (C) 2016 Linaro Ltd
// Copyright (C) 2019-2024 Google LLC
//
// Authors: Ard Biesheuvel <ardb@google.com>
//          Eric Biggers <ebiggers@google.com>
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License version 2 as
// published by the Free Software Foundation.
//

// Derived from the x86 version:
//
// Implement fast CRC-T10DIF computation with SSE and PCLMULQDQ instructions
//
// Copyright (c) 2013, Intel Corporation
//
// Authors:
//     Erdinc Ozturk <erdinc.ozturk@intel.com>
//     Vinodh Gopal <vinodh.gopal@intel.com>
//     James Guilford <james.guilford@intel.com>
//     Tim Chen <tim.c.chen@linux.intel.com>
//
// This software is available to you under a choice of one of two
// licenses.  You may choose to be licensed under the terms of the GNU
// General Public License (GPL) Version 2, available from the file
// COPYING in the main directory of this source tree, or the
// OpenIB.org BSD license below:
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// * Redistributions of source code must retain the above copyright
//   notice, this list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright
//   notice, this list of conditions and the following disclaimer in the
//   documentation and/or other materials provided with the
//   distribution.
//
// * Neither the name of the Intel Corporation nor the names of its
//   contributors may be used to endorse or promote products derived from
//   this software without specific prior written permission.
//
//
// THIS SOFTWARE IS PROVIDED BY INTEL CORPORATION ""AS IS"" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL INTEL CORPORATION OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
//       Reference paper titled "Fast CRC Computation for Generic
//	Polynomials Using PCLMULQDQ Instruction"
//       URL: http://www.intel.com/content/dam/www/public/us/en/documents
//  /white-papers/fast-crc-computation-generic-polynomials-pclmulqdq-paper.pdf
//

#include <linux/linkage.h>
#include <asm/assembler.h>

	.text
	.arch		armv8-a+crypto

	init_crc	.req	w0
	buf		.req	x1
	len		.req	x2
	fold_consts_ptr	.req	x5

	fold_consts	.req	v10

	t3		.req	v17
	t4		.req	v18
	t5		.req	v19
	t6		.req	v20
	t7		.req	v21
	t8		.req	v22

	perm		.req	v27

	.macro		pmull16x64_p64, a16, b64, c64
	pmull2		\c64\().1q, \a16\().2d, \b64\().2d
	pmull		\b64\().1q, \a16\().1d, \b64\().1d
	.endm

	/*
	 * Pairwise long polynomial multiplication of two 16-bit values
	 *
	 *   { w0, w1 }, { y0, y1 }
	 *
	 * by two 64-bit values
	 *
	 *   { x0, x1, x2, x3, x4, x5, x6, x7 }, { z0, z1, z2, z3, z4, z5, z6, z7 }
	 *
	 * where each vector element is a byte, ordered from least to most
	 * significant.
	 *
	 * This can be implemented using 8x8 long polynomial multiplication, by
	 * reorganizing the input so that each pairwise 8x8 multiplication
	 * produces one of the terms from the decomposition below, and
	 * combining the results of each rank and shifting them into place.
	 *
	 * Rank
	 *  0            w0*x0 ^              |        y0*z0 ^
	 *  1       (w0*x1 ^ w1*x0) <<  8 ^   |   (y0*z1 ^ y1*z0) <<  8 ^
	 *  2       (w0*x2 ^ w1*x1) << 16 ^   |   (y0*z2 ^ y1*z1) << 16 ^
	 *  3       (w0*x3 ^ w1*x2) << 24 ^   |   (y0*z3 ^ y1*z2) << 24 ^
	 *  4       (w0*x4 ^ w1*x3) << 32 ^   |   (y0*z4 ^ y1*z3) << 32 ^
	 *  5       (w0*x5 ^ w1*x4) << 40 ^   |   (y0*z5 ^ y1*z4) << 40 ^
	 *  6       (w0*x6 ^ w1*x5) << 48 ^   |   (y0*z6 ^ y1*z5) << 48 ^
	 *  7       (w0*x7 ^ w1*x6) << 56 ^   |   (y0*z7 ^ y1*z6) << 56 ^
	 *  8            w1*x7      << 64     |        y1*z7      << 64
	 *
	 * The inputs can be reorganized into
	 *
	 *   { w0, w0, w0, w0, y0, y0, y0, y0 }, { w1, w1, w1, w1, y1, y1, y1, y1 }
	 *   { x0, x2, x4, x6, z0, z2, z4, z6 }, { x1, x3, x5, x7, z1, z3, z5, z7 }
	 *
	 * and after performing 8x8->16 bit long polynomial multiplication of
	 * each of the halves of the first vector with those of the second one,
	 * we obtain the following four vectors of 16-bit elements:
	 *
	 *   a := { w0*x0, w0*x2, w0*x4, w0*x6 }, { y0*z0, y0*z2, y0*z4, y0*z6 }
	 *   b := { w0*x1, w0*x3, w0*x5, w0*x7 }, { y0*z1, y0*z3, y0*z5, y0*z7 }
	 *   c := { w1*x0, w1*x2, w1*x4, w1*x6 }, { y1*z0, y1*z2, y1*z4, y1*z6 }
	 *   d := { w1*x1, w1*x3, w1*x5, w1*x7 }, { y1*z1, y1*z3, y1*z5, y1*z7 }
	 *
	 * Results b and c can be XORed together, as the vector elements have
	 * matching ranks. Then, the final XOR (*) can be pulled forward, and
	 * applied between the halves of each of the remaining three vectors,
	 * which are then shifted into place, and combined to produce two
	 * 80-bit results.
	 *
	 * (*) NOTE: the 16x64 bit polynomial multiply below is not equivalent
	 * to the 64x64 bit one above, but XOR'ing the outputs together will
	 * produce the expected result, and this is sufficient in the context of
	 * this algorithm.
	 */
	.macro		pmull16x64_p8, a16, b64, c64
	ext		t7.16b, \b64\().16b, \b64\().16b, #1
	tbl		t5.16b, {\a16\().16b}, perm.16b
	uzp1		t7.16b, \b64\().16b, t7.16b
	bl		__pmull_p8_16x64
	ext		\b64\().16b, t4.16b, t4.16b, #15
	eor		\c64\().16b, t8.16b, t5.16b
	.endm

SYM_FUNC_START_LOCAL(__pmull_p8_16x64)
	ext		t6.16b, t5.16b, t5.16b, #8

	pmull		t3.8h, t7.8b, t5.8b
	pmull		t4.8h, t7.8b, t6.8b
	pmull2		t5.8h, t7.16b, t5.16b
	pmull2		t6.8h, t7.16b, t6.16b

	ext		t8.16b, t3.16b, t3.16b, #8
	eor		t4.16b, t4.16b, t6.16b
	ext		t7.16b, t5.16b, t5.16b, #8
	ext		t6.16b, t4.16b, t4.16b, #8
	eor		t8.8b, t8.8b, t3.8b
	eor		t5.8b, t5.8b, t7.8b
	eor		t4.8b, t4.8b, t6.8b
	ext		t5.16b, t5.16b, t5.16b, #14
	ret
SYM_FUNC_END(__pmull_p8_16x64)


	// Fold reg1, reg2 into the next 32 data bytes, storing the result back
	// into reg1, reg2.
	.macro		fold_32_bytes, p, reg1, reg2
	ldp		q11, q12, [buf], #0x20

	pmull16x64_\p	fold_consts, \reg1, v8

CPU_LE(	rev64		v11.16b, v11.16b		)
CPU_LE(	rev64		v12.16b, v12.16b		)

	pmull16x64_\p	fold_consts, \reg2, v9

CPU_LE(	ext		v11.16b, v11.16b, v11.16b, #8	)
CPU_LE(	ext		v12.16b, v12.16b, v12.16b, #8	)

	eor		\reg1\().16b, \reg1\().16b, v8.16b
	eor		\reg2\().16b, \reg2\().16b, v9.16b
	eor		\reg1\().16b, \reg1\().16b, v11.16b
	eor		\reg2\().16b, \reg2\().16b, v12.16b
	.endm

	// Fold src_reg into dst_reg, optionally loading the next fold constants
	.macro		fold_16_bytes, p, src_reg, dst_reg, load_next_consts
	pmull16x64_\p	fold_consts, \src_reg, v8
	.ifnb		\load_next_consts
	ld1		{fold_consts.2d}, [fold_consts_ptr], #16
	.endif
	eor		\dst_reg\().16b, \dst_reg\().16b, v8.16b
	eor		\dst_reg\().16b, \dst_reg\().16b, \src_reg\().16b
	.endm

	.macro		crc_t10dif_pmull, p

	// For sizes less than 256 bytes, we can't fold 128 bytes at a time.
	cmp		len, #256
	b.lt		.Lless_than_256_bytes_\@

	adr_l		fold_consts_ptr, .Lfold_across_128_bytes_consts

	// Load the first 128 data bytes.  Byte swapping is necessary to make
	// the bit order match the polynomial coefficient order.
	ldp		q0, q1, [buf]
	ldp		q2, q3, [buf, #0x20]
	ldp		q4, q5, [buf, #0x40]
	ldp		q6, q7, [buf, #0x60]
	add		buf, buf, #0x80
CPU_LE(	rev64		v0.16b, v0.16b			)
CPU_LE(	rev64		v1.16b, v1.16b			)
CPU_LE(	rev64		v2.16b, v2.16b			)
CPU_LE(	rev64		v3.16b, v3.16b			)
CPU_LE(	rev64		v4.16b, v4.16b			)
CPU_LE(	rev64		v5.16b, v5.16b			)
CPU_LE(	rev64		v6.16b, v6.16b			)
CPU_LE(	rev64		v7.16b, v7.16b			)
CPU_LE(	ext		v0.16b, v0.16b, v0.16b, #8	)
CPU_LE(	ext		v1.16b, v1.16b, v1.16b, #8	)
CPU_LE(	ext		v2.16b, v2.16b, v2.16b, #8	)
CPU_LE(	ext		v3.16b, v3.16b, v3.16b, #8	)
CPU_LE(	ext		v4.16b, v4.16b, v4.16b, #8	)
CPU_LE(	ext		v5.16b, v5.16b, v5.16b, #8	)
CPU_LE(	ext		v6.16b, v6.16b, v6.16b, #8	)
CPU_LE(	ext		v7.16b, v7.16b, v7.16b, #8	)

	// XOR the first 16 data *bits* with the initial CRC value.
	movi		v8.16b, #0
	mov		v8.h[7], init_crc
	eor		v0.16b, v0.16b, v8.16b

	// Load the constants for folding across 128 bytes.
	ld1		{fold_consts.2d}, [fold_consts_ptr]

	// Subtract 128 for the 128 data bytes just consumed.  Subtract another
	// 128 to simplify the termination condition of the following loop.
	sub		len, len, #256

	// While >= 128 data bytes remain (not counting v0-v7), fold the 128
	// bytes v0-v7 into them, storing the result back into v0-v7.
.Lfold_128_bytes_loop_\@:
	fold_32_bytes	\p, v0, v1
	fold_32_bytes	\p, v2, v3
	fold_32_bytes	\p, v4, v5
	fold_32_bytes	\p, v6, v7

	subs		len, len, #128
	b.ge		.Lfold_128_bytes_loop_\@

	// Now fold the 112 bytes in v0-v6 into the 16 bytes in v7.

	// Fold across 64 bytes.
	add		fold_consts_ptr, fold_consts_ptr, #16
	ld1		{fold_consts.2d}, [fold_consts_ptr], #16
	fold_16_bytes	\p, v0, v4
	fold_16_bytes	\p, v1, v5
	fold_16_bytes	\p, v2, v6
	fold_16_bytes	\p, v3, v7, 1
	// Fold across 32 bytes.
	fold_16_bytes	\p, v4, v6
	fold_16_bytes	\p, v5, v7, 1
	// Fold across 16 bytes.
	fold_16_bytes	\p, v6, v7

	// Add 128 to get the correct number of data bytes remaining in 0...127
	// (not counting v7), following the previous extra subtraction by 128.
	// Then subtract 16 to simplify the termination condition of the
	// following loop.
	adds		len, len, #(128-16)

	// While >= 16 data bytes remain (not counting v7), fold the 16 bytes v7
	// into them, storing the result back into v7.
	b.lt		.Lfold_16_bytes_loop_done_\@
.Lfold_16_bytes_loop_\@:
	pmull16x64_\p	fold_consts, v7, v8
	eor		v7.16b, v7.16b, v8.16b
	ldr		q0, [buf], #16
CPU_LE(	rev64		v0.16b, v0.16b			)
CPU_LE(	ext		v0.16b, v0.16b, v0.16b, #8	)
	eor		v7.16b, v7.16b, v0.16b
	subs		len, len, #16
	b.ge		.Lfold_16_bytes_loop_\@

.Lfold_16_bytes_loop_done_\@:
	// Add 16 to get the correct number of data bytes remaining in 0...15
	// (not counting v7), following the previous extra subtraction by 16.
	adds		len, len, #16
	b.eq		.Lreduce_final_16_bytes_\@

.Lhandle_partial_segment_\@:
	// Reduce the last '16 + len' bytes where 1 <= len <= 15 and the first
	// 16 bytes are in v7 and the rest are the remaining data in 'buf'.  To
	// do this without needing a fold constant for each possible 'len',
	// redivide the bytes into a first chunk of 'len' bytes and a second
	// chunk of 16 bytes, then fold the first chunk into the second.

	// v0 = last 16 original data bytes
	add		buf, buf, len
	ldr		q0, [buf, #-16]
CPU_LE(	rev64		v0.16b, v0.16b			)
CPU_LE(	ext		v0.16b, v0.16b, v0.16b, #8	)

	// v1 = high order part of second chunk: v7 left-shifted by 'len' bytes.
	adr_l		x4, .Lbyteshift_table + 16
	sub		x4, x4, len
	ld1		{v2.16b}, [x4]
	tbl		v1.16b, {v7.16b}, v2.16b

	// v3 = first chunk: v7 right-shifted by '16-len' bytes.
	movi		v3.16b, #0x80
	eor		v2.16b, v2.16b, v3.16b
	tbl		v3.16b, {v7.16b}, v2.16b

	// Convert to 8-bit masks: 'len' 0x00 bytes, then '16-len' 0xff bytes.
	sshr		v2.16b, v2.16b, #7

	// v2 = second chunk: 'len' bytes from v0 (low-order bytes),
	// then '16-len' bytes from v1 (high-order bytes).
	bsl		v2.16b, v1.16b, v0.16b

	// Fold the first chunk into the second chunk, storing the result in v7.
	pmull16x64_\p	fold_consts, v3, v0
	eor		v7.16b, v3.16b, v0.16b
	eor		v7.16b, v7.16b, v2.16b
	b		.Lreduce_final_16_bytes_\@

.Lless_than_256_bytes_\@:
	// Checksumming a buffer of length 16...255 bytes

	adr_l		fold_consts_ptr, .Lfold_across_16_bytes_consts

	// Load the first 16 data bytes.
	ldr		q7, [buf], #0x10
CPU_LE(	rev64		v7.16b, v7.16b			)
CPU_LE(	ext		v7.16b, v7.16b, v7.16b, #8	)

	// XOR the first 16 data *bits* with the initial CRC value.
	movi		v0.16b, #0
	mov		v0.h[7], init_crc
	eor		v7.16b, v7.16b, v0.16b

	// Load the fold-across-16-bytes constants.
	ld1		{fold_consts.2d}, [fold_consts_ptr], #16

	cmp		len, #16
	b.eq		.Lreduce_final_16_bytes_\@	// len == 16
	subs		len, len, #32
	b.ge		.Lfold_16_bytes_loop_\@		// 32 <= len <= 255
	add		len, len, #16
	b		.Lhandle_partial_segment_\@	// 17 <= len <= 31

.Lreduce_final_16_bytes_\@:
	.endm

//
// u16 crc_t10dif_pmull_p8(u16 init_crc, const u8 *buf, size_t len);
//
// Assumes len >= 16.
//
SYM_FUNC_START(crc_t10dif_pmull_p8)
	frame_push	1

	// Compose { 0,0,0,0, 8,8,8,8, 1,1,1,1, 9,9,9,9 }
	movi		perm.4h, #8, lsl #8
	orr		perm.2s, #1, lsl #16
	orr		perm.2s, #1, lsl #24
	zip1		perm.16b, perm.16b, perm.16b
	zip1		perm.16b, perm.16b, perm.16b

	crc_t10dif_pmull p8

CPU_LE(	rev64		v7.16b, v7.16b			)
CPU_LE(	ext		v7.16b, v7.16b, v7.16b, #8	)
	str		q7, [x3]

	frame_pop
	ret
SYM_FUNC_END(crc_t10dif_pmull_p8)

	.align		5
//
// u16 crc_t10dif_pmull_p64(u16 init_crc, const u8 *buf, size_t len);
//
// Assumes len >= 16.
//
SYM_FUNC_START(crc_t10dif_pmull_p64)
	crc_t10dif_pmull	p64

	// Reduce the 128-bit value M(x), stored in v7, to the final 16-bit CRC.

	movi		v2.16b, #0		// init zero register

	// Load 'x^48 * (x^48 mod G(x))' and 'x^48 * (x^80 mod G(x))'.
	ld1		{fold_consts.2d}, [fold_consts_ptr], #16

	// Fold the high 64 bits into the low 64 bits, while also multiplying by
	// x^64.  This produces a 128-bit value congruent to x^64 * M(x) and
	// whose low 48 bits are 0.
	ext		v0.16b, v2.16b, v7.16b, #8
	pmull2		v7.1q, v7.2d, fold_consts.2d	// high bits * x^48 * (x^80 mod G(x))
	eor		v0.16b, v0.16b, v7.16b		// + low bits * x^64

	// Fold the high 32 bits into the low 96 bits.  This produces a 96-bit
	// value congruent to x^64 * M(x) and whose low 48 bits are 0.
	ext		v1.16b, v0.16b, v2.16b, #12	// extract high 32 bits
	mov		v0.s[3], v2.s[0]		// zero high 32 bits
	pmull		v1.1q, v1.1d, fold_consts.1d	// high 32 bits * x^48 * (x^48 mod G(x))
	eor		v0.16b, v0.16b, v1.16b		// + low bits

	// Load G(x) and floor(x^48 / G(x)).
	ld1		{fold_consts.2d}, [fold_consts_ptr]

	// Use Barrett reduction to compute the final CRC value.
	pmull2		v1.1q, v0.2d, fold_consts.2d	// high 32 bits * floor(x^48 / G(x))
	ushr		v1.2d, v1.2d, #32		// /= x^32
	pmull		v1.1q, v1.1d, fold_consts.1d	// *= G(x)
	ushr		v0.2d, v0.2d, #48
	eor		v0.16b, v0.16b, v1.16b		// + low 16 nonzero bits
	// Final CRC value (x^16 * M(x)) mod G(x) is in low 16 bits of v0.

	umov		w0, v0.h[0]
	ret
SYM_FUNC_END(crc_t10dif_pmull_p64)

	.section	".rodata", "a"
	.align		4

// Fold constants precomputed from the polynomial 0x18bb7
// G(x) = x^16 + x^15 + x^11 + x^9 + x^8 + x^7 + x^5 + x^4 + x^2 + x^1 + x^0
.Lfold_across_128_bytes_consts:
	.quad		0x0000000000006123	// x^(8*128)	mod G(x)
	.quad		0x0000000000002295	// x^(8*128+64)	mod G(x)
// .Lfold_across_64_bytes_consts:
	.quad		0x0000000000001069	// x^(4*128)	mod G(x)
	.quad		0x000000000000dd31	// x^(4*128+64)	mod G(x)
// .Lfold_across_32_bytes_consts:
	.quad		0x000000000000857d	// x^(2*128)	mod G(x)
	.quad		0x0000000000007acc	// x^(2*128+64)	mod G(x)
.Lfold_across_16_bytes_consts:
	.quad		0x000000000000a010	// x^(1*128)	mod G(x)
	.quad		0x0000000000001faa	// x^(1*128+64)	mod G(x)
// .Lfinal_fold_consts:
	.quad		0x1368000000000000	// x^48 * (x^48 mod G(x))
	.quad		0x2d56000000000000	// x^48 * (x^80 mod G(x))
// .Lbarrett_reduction_consts:
	.quad		0x0000000000018bb7	// G(x)
	.quad		0x00000001f65a57f8	// floor(x^48 / G(x))

// For 1 <= len <= 15, the 16-byte vector beginning at &byteshift_table[16 -
// len] is the index vector to shift left by 'len' bytes, and is also {0x80,
// ..., 0x80} XOR the index vector to shift right by '16 - len' bytes.
.Lbyteshift_table:
	.byte		 0x0, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87
	.byte		0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f
	.byte		 0x0,  0x1,  0x2,  0x3,  0x4,  0x5,  0x6,  0x7
	.byte		 0x8,  0x9,  0xa,  0xb,  0xc,  0xd,  0xe , 0x0
