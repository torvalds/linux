//
// Accelerated CRC-T10DIF using ARM NEON and Crypto Extensions instructions
//
// Copyright (C) 2016 Linaro Ltd <ard.biesheuvel@linaro.org>
// Copyright (C) 2019 Google LLC <ebiggers@google.com>
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

#ifdef CONFIG_CPU_ENDIAN_BE8
#define CPU_LE(code...)
#else
#define CPU_LE(code...)		code
#endif

	.text
	.arch		armv8-a
	.fpu		crypto-neon-fp-armv8

	init_crc	.req	r0
	buf		.req	r1
	len		.req	r2

	fold_consts_ptr	.req	ip

	q0l		.req	d0
	q0h		.req	d1
	q1l		.req	d2
	q1h		.req	d3
	q2l		.req	d4
	q2h		.req	d5
	q3l		.req	d6
	q3h		.req	d7
	q4l		.req	d8
	q4h		.req	d9
	q5l		.req	d10
	q5h		.req	d11
	q6l		.req	d12
	q6h		.req	d13
	q7l		.req	d14
	q7h		.req	d15
	q8l		.req	d16
	q8h		.req	d17
	q9l		.req	d18
	q9h		.req	d19
	q10l		.req	d20
	q10h		.req	d21
	q11l		.req	d22
	q11h		.req	d23
	q12l		.req	d24
	q12h		.req	d25

	FOLD_CONSTS	.req	q10
	FOLD_CONST_L	.req	q10l
	FOLD_CONST_H	.req	q10h

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
	 * significant. The resulting 80-bit vectors are XOR'ed together.
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
	 * matching ranks. Then, the final XOR can be pulled forward, and
	 * applied between the halves of each of the remaining three vectors,
	 * which are then shifted into place, and XORed together to produce the
	 * final 80-bit result.
	 */
        .macro		pmull16x64_p8, v16, v64
	vext.8		q11, \v64, \v64, #1
	vld1.64		{q12}, [r4, :128]
	vuzp.8		q11, \v64
	vtbl.8		d24, {\v16\()_L-\v16\()_H}, d24
	vtbl.8		d25, {\v16\()_L-\v16\()_H}, d25
	bl		__pmull16x64_p8
	veor		\v64, q12, q14
        .endm

__pmull16x64_p8:
	vmull.p8	q13, d23, d24
	vmull.p8	q14, d23, d25
	vmull.p8	q15, d22, d24
	vmull.p8	q12, d22, d25

	veor		q14, q14, q15
	veor		d24, d24, d25
	veor		d26, d26, d27
	veor		d28, d28, d29
	vmov.i32	d25, #0
	vmov.i32	d29, #0
	vext.8		q12, q12, q12, #14
	vext.8		q14, q14, q14, #15
	veor		d24, d24, d26
	bx		lr
ENDPROC(__pmull16x64_p8)

        .macro		pmull16x64_p64, v16, v64
	vmull.p64	q11, \v64\()l, \v16\()_L
	vmull.p64	\v64, \v64\()h, \v16\()_H
	veor		\v64, \v64, q11
	.endm

	// Fold reg1, reg2 into the next 32 data bytes, storing the result back
	// into reg1, reg2.
	.macro		fold_32_bytes, reg1, reg2, p
	vld1.64		{q8-q9}, [buf]!

	pmull16x64_\p	FOLD_CONST, \reg1
	pmull16x64_\p	FOLD_CONST, \reg2

CPU_LE(	vrev64.8	q8, q8	)
CPU_LE(	vrev64.8	q9, q9	)
	vswp		q8l, q8h
	vswp		q9l, q9h

	veor.8		\reg1, \reg1, q8
	veor.8		\reg2, \reg2, q9
	.endm

	// Fold src_reg into dst_reg, optionally loading the next fold constants
	.macro		fold_16_bytes, src_reg, dst_reg, p, load_next_consts
	pmull16x64_\p	FOLD_CONST, \src_reg
	.ifnb		\load_next_consts
	vld1.64		{FOLD_CONSTS}, [fold_consts_ptr, :128]!
	.endif
	veor.8		\dst_reg, \dst_reg, \src_reg
	.endm

	.macro		crct10dif, p
	// For sizes less than 256 bytes, we can't fold 128 bytes at a time.
	cmp		len, #256
	blt		.Lless_than_256_bytes\@

	mov_l		fold_consts_ptr, .Lfold_across_128_bytes_consts

	// Load the first 128 data bytes.  Byte swapping is necessary to make
	// the bit order match the polynomial coefficient order.
	vld1.64		{q0-q1}, [buf]!
	vld1.64		{q2-q3}, [buf]!
	vld1.64		{q4-q5}, [buf]!
	vld1.64		{q6-q7}, [buf]!
CPU_LE(	vrev64.8	q0, q0	)
CPU_LE(	vrev64.8	q1, q1	)
CPU_LE(	vrev64.8	q2, q2	)
CPU_LE(	vrev64.8	q3, q3	)
CPU_LE(	vrev64.8	q4, q4	)
CPU_LE(	vrev64.8	q5, q5	)
CPU_LE(	vrev64.8	q6, q6	)
CPU_LE(	vrev64.8	q7, q7	)
	vswp		q0l, q0h
	vswp		q1l, q1h
	vswp		q2l, q2h
	vswp		q3l, q3h
	vswp		q4l, q4h
	vswp		q5l, q5h
	vswp		q6l, q6h
	vswp		q7l, q7h

	// XOR the first 16 data *bits* with the initial CRC value.
	vmov.i8		q8h, #0
	vmov.u16	q8h[3], init_crc
	veor		q0h, q0h, q8h

	// Load the constants for folding across 128 bytes.
	vld1.64		{FOLD_CONSTS}, [fold_consts_ptr, :128]!

	// Subtract 128 for the 128 data bytes just consumed.  Subtract another
	// 128 to simplify the termination condition of the following loop.
	sub		len, len, #256

	// While >= 128 data bytes remain (not counting q0-q7), fold the 128
	// bytes q0-q7 into them, storing the result back into q0-q7.
.Lfold_128_bytes_loop\@:
	fold_32_bytes	q0, q1, \p
	fold_32_bytes	q2, q3, \p
	fold_32_bytes	q4, q5, \p
	fold_32_bytes	q6, q7, \p
	subs		len, len, #128
	bge		.Lfold_128_bytes_loop\@

	// Now fold the 112 bytes in q0-q6 into the 16 bytes in q7.

	// Fold across 64 bytes.
	vld1.64		{FOLD_CONSTS}, [fold_consts_ptr, :128]!
	fold_16_bytes	q0, q4, \p
	fold_16_bytes	q1, q5, \p
	fold_16_bytes	q2, q6, \p
	fold_16_bytes	q3, q7, \p, 1
	// Fold across 32 bytes.
	fold_16_bytes	q4, q6, \p
	fold_16_bytes	q5, q7, \p, 1
	// Fold across 16 bytes.
	fold_16_bytes	q6, q7, \p

	// Add 128 to get the correct number of data bytes remaining in 0...127
	// (not counting q7), following the previous extra subtraction by 128.
	// Then subtract 16 to simplify the termination condition of the
	// following loop.
	adds		len, len, #(128-16)

	// While >= 16 data bytes remain (not counting q7), fold the 16 bytes q7
	// into them, storing the result back into q7.
	blt		.Lfold_16_bytes_loop_done\@
.Lfold_16_bytes_loop\@:
	pmull16x64_\p	FOLD_CONST, q7
	vld1.64		{q0}, [buf]!
CPU_LE(	vrev64.8	q0, q0	)
	vswp		q0l, q0h
	veor.8		q7, q7, q0
	subs		len, len, #16
	bge		.Lfold_16_bytes_loop\@

.Lfold_16_bytes_loop_done\@:
	// Add 16 to get the correct number of data bytes remaining in 0...15
	// (not counting q7), following the previous extra subtraction by 16.
	adds		len, len, #16
	beq		.Lreduce_final_16_bytes\@

.Lhandle_partial_segment\@:
	// Reduce the last '16 + len' bytes where 1 <= len <= 15 and the first
	// 16 bytes are in q7 and the rest are the remaining data in 'buf'.  To
	// do this without needing a fold constant for each possible 'len',
	// redivide the bytes into a first chunk of 'len' bytes and a second
	// chunk of 16 bytes, then fold the first chunk into the second.

	// q0 = last 16 original data bytes
	add		buf, buf, len
	sub		buf, buf, #16
	vld1.64		{q0}, [buf]
CPU_LE(	vrev64.8	q0, q0	)
	vswp		q0l, q0h

	// q1 = high order part of second chunk: q7 left-shifted by 'len' bytes.
	mov_l		r1, .Lbyteshift_table + 16
	sub		r1, r1, len
	vld1.8		{q2}, [r1]
	vtbl.8		q1l, {q7l-q7h}, q2l
	vtbl.8		q1h, {q7l-q7h}, q2h

	// q3 = first chunk: q7 right-shifted by '16-len' bytes.
	vmov.i8		q3, #0x80
	veor.8		q2, q2, q3
	vtbl.8		q3l, {q7l-q7h}, q2l
	vtbl.8		q3h, {q7l-q7h}, q2h

	// Convert to 8-bit masks: 'len' 0x00 bytes, then '16-len' 0xff bytes.
	vshr.s8		q2, q2, #7

	// q2 = second chunk: 'len' bytes from q0 (low-order bytes),
	// then '16-len' bytes from q1 (high-order bytes).
	vbsl.8		q2, q1, q0

	// Fold the first chunk into the second chunk, storing the result in q7.
	pmull16x64_\p	FOLD_CONST, q3
	veor.8		q7, q3, q2
	b		.Lreduce_final_16_bytes\@

.Lless_than_256_bytes\@:
	// Checksumming a buffer of length 16...255 bytes

	mov_l		fold_consts_ptr, .Lfold_across_16_bytes_consts

	// Load the first 16 data bytes.
	vld1.64		{q7}, [buf]!
CPU_LE(	vrev64.8	q7, q7	)
	vswp		q7l, q7h

	// XOR the first 16 data *bits* with the initial CRC value.
	vmov.i8		q0h, #0
	vmov.u16	q0h[3], init_crc
	veor.8		q7h, q7h, q0h

	// Load the fold-across-16-bytes constants.
	vld1.64		{FOLD_CONSTS}, [fold_consts_ptr, :128]!

	cmp		len, #16
	beq		.Lreduce_final_16_bytes\@	// len == 16
	subs		len, len, #32
	addlt		len, len, #16
	blt		.Lhandle_partial_segment\@	// 17 <= len <= 31
	b		.Lfold_16_bytes_loop\@		// 32 <= len <= 255

.Lreduce_final_16_bytes\@:
	.endm

//
// u16 crc_t10dif_pmull(u16 init_crc, const u8 *buf, size_t len);
//
// Assumes len >= 16.
//
ENTRY(crc_t10dif_pmull64)
	crct10dif	p64

	// Reduce the 128-bit value M(x), stored in q7, to the final 16-bit CRC.

	// Load 'x^48 * (x^48 mod G(x))' and 'x^48 * (x^80 mod G(x))'.
	vld1.64		{FOLD_CONSTS}, [fold_consts_ptr, :128]!

	// Fold the high 64 bits into the low 64 bits, while also multiplying by
	// x^64.  This produces a 128-bit value congruent to x^64 * M(x) and
	// whose low 48 bits are 0.
	vmull.p64	q0, q7h, FOLD_CONST_H	// high bits * x^48 * (x^80 mod G(x))
	veor.8		q0h, q0h, q7l		// + low bits * x^64

	// Fold the high 32 bits into the low 96 bits.  This produces a 96-bit
	// value congruent to x^64 * M(x) and whose low 48 bits are 0.
	vmov.i8		q1, #0
	vmov		s4, s3			// extract high 32 bits
	vmov		s3, s5			// zero high 32 bits
	vmull.p64	q1, q1l, FOLD_CONST_L	// high 32 bits * x^48 * (x^48 mod G(x))
	veor.8		q0, q0, q1		// + low bits

	// Load G(x) and floor(x^48 / G(x)).
	vld1.64		{FOLD_CONSTS}, [fold_consts_ptr, :128]

	// Use Barrett reduction to compute the final CRC value.
	vmull.p64	q1, q0h, FOLD_CONST_H	// high 32 bits * floor(x^48 / G(x))
	vshr.u64	q1l, q1l, #32		// /= x^32
	vmull.p64	q1, q1l, FOLD_CONST_L	// *= G(x)
	vshr.u64	q0l, q0l, #48
	veor.8		q0l, q0l, q1l		// + low 16 nonzero bits
	// Final CRC value (x^16 * M(x)) mod G(x) is in low 16 bits of q0.

	vmov.u16	r0, q0l[0]
	bx		lr
ENDPROC(crc_t10dif_pmull64)

ENTRY(crc_t10dif_pmull8)
	push		{r4, lr}
	mov_l		r4, .L16x64perm

	crct10dif	p8

CPU_LE(	vrev64.8	q7, q7	)
	vswp		q7l, q7h
	vst1.64		{q7}, [r3, :128]
	pop		{r4, pc}
ENDPROC(crc_t10dif_pmull8)

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

.L16x64perm:
	.quad		0x808080800000000, 0x909090901010101
