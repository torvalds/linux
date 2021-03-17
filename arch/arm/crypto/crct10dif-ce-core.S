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

	// Fold reg1, reg2 into the next 32 data bytes, storing the result back
	// into reg1, reg2.
	.macro		fold_32_bytes, reg1, reg2
	vld1.64		{q11-q12}, [buf]!

	vmull.p64	q8, \reg1\()h, FOLD_CONST_H
	vmull.p64	\reg1, \reg1\()l, FOLD_CONST_L
	vmull.p64	q9, \reg2\()h, FOLD_CONST_H
	vmull.p64	\reg2, \reg2\()l, FOLD_CONST_L

CPU_LE(	vrev64.8	q11, q11	)
CPU_LE(	vrev64.8	q12, q12	)
	vswp		q11l, q11h
	vswp		q12l, q12h

	veor.8		\reg1, \reg1, q8
	veor.8		\reg2, \reg2, q9
	veor.8		\reg1, \reg1, q11
	veor.8		\reg2, \reg2, q12
	.endm

	// Fold src_reg into dst_reg, optionally loading the next fold constants
	.macro		fold_16_bytes, src_reg, dst_reg, load_next_consts
	vmull.p64	q8, \src_reg\()l, FOLD_CONST_L
	vmull.p64	\src_reg, \src_reg\()h, FOLD_CONST_H
	.ifnb		\load_next_consts
	vld1.64		{FOLD_CONSTS}, [fold_consts_ptr, :128]!
	.endif
	veor.8		\dst_reg, \dst_reg, q8
	veor.8		\dst_reg, \dst_reg, \src_reg
	.endm

	.macro		__adrl, out, sym
	movw		\out, #:lower16:\sym
	movt		\out, #:upper16:\sym
	.endm

//
// u16 crc_t10dif_pmull(u16 init_crc, const u8 *buf, size_t len);
//
// Assumes len >= 16.
//
ENTRY(crc_t10dif_pmull)

	// For sizes less than 256 bytes, we can't fold 128 bytes at a time.
	cmp		len, #256
	blt		.Lless_than_256_bytes

	__adrl		fold_consts_ptr, .Lfold_across_128_bytes_consts

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
.Lfold_128_bytes_loop:
	fold_32_bytes	q0, q1
	fold_32_bytes	q2, q3
	fold_32_bytes	q4, q5
	fold_32_bytes	q6, q7
	subs		len, len, #128
	bge		.Lfold_128_bytes_loop

	// Now fold the 112 bytes in q0-q6 into the 16 bytes in q7.

	// Fold across 64 bytes.
	vld1.64		{FOLD_CONSTS}, [fold_consts_ptr, :128]!
	fold_16_bytes	q0, q4
	fold_16_bytes	q1, q5
	fold_16_bytes	q2, q6
	fold_16_bytes	q3, q7, 1
	// Fold across 32 bytes.
	fold_16_bytes	q4, q6
	fold_16_bytes	q5, q7, 1
	// Fold across 16 bytes.
	fold_16_bytes	q6, q7

	// Add 128 to get the correct number of data bytes remaining in 0...127
	// (not counting q7), following the previous extra subtraction by 128.
	// Then subtract 16 to simplify the termination condition of the
	// following loop.
	adds		len, len, #(128-16)

	// While >= 16 data bytes remain (not counting q7), fold the 16 bytes q7
	// into them, storing the result back into q7.
	blt		.Lfold_16_bytes_loop_done
.Lfold_16_bytes_loop:
	vmull.p64	q8, q7l, FOLD_CONST_L
	vmull.p64	q7, q7h, FOLD_CONST_H
	veor.8		q7, q7, q8
	vld1.64		{q0}, [buf]!
CPU_LE(	vrev64.8	q0, q0	)
	vswp		q0l, q0h
	veor.8		q7, q7, q0
	subs		len, len, #16
	bge		.Lfold_16_bytes_loop

.Lfold_16_bytes_loop_done:
	// Add 16 to get the correct number of data bytes remaining in 0...15
	// (not counting q7), following the previous extra subtraction by 16.
	adds		len, len, #16
	beq		.Lreduce_final_16_bytes

.Lhandle_partial_segment:
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
	__adrl		r3, .Lbyteshift_table + 16
	sub		r3, r3, len
	vld1.8		{q2}, [r3]
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
	vmull.p64	q0, q3l, FOLD_CONST_L
	vmull.p64	q7, q3h, FOLD_CONST_H
	veor.8		q7, q7, q0
	veor.8		q7, q7, q2

.Lreduce_final_16_bytes:
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

.Lless_than_256_bytes:
	// Checksumming a buffer of length 16...255 bytes

	__adrl		fold_consts_ptr, .Lfold_across_16_bytes_consts

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
	beq		.Lreduce_final_16_bytes		// len == 16
	subs		len, len, #32
	addlt		len, len, #16
	blt		.Lhandle_partial_segment	// 17 <= len <= 31
	b		.Lfold_16_bytes_loop		// 32 <= len <= 255
ENDPROC(crc_t10dif_pmull)

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
