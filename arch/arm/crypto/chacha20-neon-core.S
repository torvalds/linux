/*
 * ChaCha20 256-bit cipher algorithm, RFC7539, ARM NEON functions
 *
 * Copyright (C) 2016 Linaro, Ltd. <ard.biesheuvel@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Based on:
 * ChaCha20 256-bit cipher algorithm, RFC7539, x64 SSE3 functions
 *
 * Copyright (C) 2015 Martin Willi
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

 /*
  * NEON doesn't have a rotate instruction.  The alternatives are, more or less:
  *
  * (a)  vshl.u32 + vsri.u32		(needs temporary register)
  * (b)  vshl.u32 + vshr.u32 + vorr	(needs temporary register)
  * (c)  vrev32.16			(16-bit rotations only)
  * (d)  vtbl.8 + vtbl.8		(multiple of 8 bits rotations only,
  *					 needs index vector)
  *
  * ChaCha20 has 16, 12, 8, and 7-bit rotations.  For the 12 and 7-bit
  * rotations, the only choices are (a) and (b).  We use (a) since it takes
  * two-thirds the cycles of (b) on both Cortex-A7 and Cortex-A53.
  *
  * For the 16-bit rotation, we use vrev32.16 since it's consistently fastest
  * and doesn't need a temporary register.
  *
  * For the 8-bit rotation, we use vtbl.8 + vtbl.8.  On Cortex-A7, this sequence
  * is twice as fast as (a), even when doing (a) on multiple registers
  * simultaneously to eliminate the stall between vshl and vsri.  Also, it
  * parallelizes better when temporary registers are scarce.
  *
  * A disadvantage is that on Cortex-A53, the vtbl sequence is the same speed as
  * (a), so the need to load the rotation table actually makes the vtbl method
  * slightly slower overall on that CPU (~1.3% slower ChaCha20).  Still, it
  * seems to be a good compromise to get a more significant speed boost on some
  * CPUs, e.g. ~4.8% faster ChaCha20 on Cortex-A7.
  */

#include <linux/linkage.h>

	.text
	.fpu		neon
	.align		5

ENTRY(chacha20_block_xor_neon)
	// r0: Input state matrix, s
	// r1: 1 data block output, o
	// r2: 1 data block input, i

	//
	// This function encrypts one ChaCha20 block by loading the state matrix
	// in four NEON registers. It performs matrix operation on four words in
	// parallel, but requireds shuffling to rearrange the words after each
	// round.
	//

	// x0..3 = s0..3
	add		ip, r0, #0x20
	vld1.32		{q0-q1}, [r0]
	vld1.32		{q2-q3}, [ip]

	vmov		q8, q0
	vmov		q9, q1
	vmov		q10, q2
	vmov		q11, q3

	adr		ip, .Lrol8_table
	mov		r3, #10
	vld1.8		{d10}, [ip, :64]

.Ldoubleround:
	// x0 += x1, x3 = rotl32(x3 ^ x0, 16)
	vadd.i32	q0, q0, q1
	veor		q3, q3, q0
	vrev32.16	q3, q3

	// x2 += x3, x1 = rotl32(x1 ^ x2, 12)
	vadd.i32	q2, q2, q3
	veor		q4, q1, q2
	vshl.u32	q1, q4, #12
	vsri.u32	q1, q4, #20

	// x0 += x1, x3 = rotl32(x3 ^ x0, 8)
	vadd.i32	q0, q0, q1
	veor		q3, q3, q0
	vtbl.8		d6, {d6}, d10
	vtbl.8		d7, {d7}, d10

	// x2 += x3, x1 = rotl32(x1 ^ x2, 7)
	vadd.i32	q2, q2, q3
	veor		q4, q1, q2
	vshl.u32	q1, q4, #7
	vsri.u32	q1, q4, #25

	// x1 = shuffle32(x1, MASK(0, 3, 2, 1))
	vext.8		q1, q1, q1, #4
	// x2 = shuffle32(x2, MASK(1, 0, 3, 2))
	vext.8		q2, q2, q2, #8
	// x3 = shuffle32(x3, MASK(2, 1, 0, 3))
	vext.8		q3, q3, q3, #12

	// x0 += x1, x3 = rotl32(x3 ^ x0, 16)
	vadd.i32	q0, q0, q1
	veor		q3, q3, q0
	vrev32.16	q3, q3

	// x2 += x3, x1 = rotl32(x1 ^ x2, 12)
	vadd.i32	q2, q2, q3
	veor		q4, q1, q2
	vshl.u32	q1, q4, #12
	vsri.u32	q1, q4, #20

	// x0 += x1, x3 = rotl32(x3 ^ x0, 8)
	vadd.i32	q0, q0, q1
	veor		q3, q3, q0
	vtbl.8		d6, {d6}, d10
	vtbl.8		d7, {d7}, d10

	// x2 += x3, x1 = rotl32(x1 ^ x2, 7)
	vadd.i32	q2, q2, q3
	veor		q4, q1, q2
	vshl.u32	q1, q4, #7
	vsri.u32	q1, q4, #25

	// x1 = shuffle32(x1, MASK(2, 1, 0, 3))
	vext.8		q1, q1, q1, #12
	// x2 = shuffle32(x2, MASK(1, 0, 3, 2))
	vext.8		q2, q2, q2, #8
	// x3 = shuffle32(x3, MASK(0, 3, 2, 1))
	vext.8		q3, q3, q3, #4

	subs		r3, r3, #1
	bne		.Ldoubleround

	add		ip, r2, #0x20
	vld1.8		{q4-q5}, [r2]
	vld1.8		{q6-q7}, [ip]

	// o0 = i0 ^ (x0 + s0)
	vadd.i32	q0, q0, q8
	veor		q0, q0, q4

	// o1 = i1 ^ (x1 + s1)
	vadd.i32	q1, q1, q9
	veor		q1, q1, q5

	// o2 = i2 ^ (x2 + s2)
	vadd.i32	q2, q2, q10
	veor		q2, q2, q6

	// o3 = i3 ^ (x3 + s3)
	vadd.i32	q3, q3, q11
	veor		q3, q3, q7

	add		ip, r1, #0x20
	vst1.8		{q0-q1}, [r1]
	vst1.8		{q2-q3}, [ip]

	bx		lr
ENDPROC(chacha20_block_xor_neon)

	.align		4
.Lctrinc:	.word	0, 1, 2, 3
.Lrol8_table:	.byte	3, 0, 1, 2, 7, 4, 5, 6

	.align		5
ENTRY(chacha20_4block_xor_neon)
	push		{r4-r5}
	mov		r4, sp			// preserve the stack pointer
	sub		ip, sp, #0x20		// allocate a 32 byte buffer
	bic		ip, ip, #0x1f		// aligned to 32 bytes
	mov		sp, ip

	// r0: Input state matrix, s
	// r1: 4 data blocks output, o
	// r2: 4 data blocks input, i

	//
	// This function encrypts four consecutive ChaCha20 blocks by loading
	// the state matrix in NEON registers four times. The algorithm performs
	// each operation on the corresponding word of each state matrix, hence
	// requires no word shuffling. The words are re-interleaved before the
	// final addition of the original state and the XORing step.
	//

	// x0..15[0-3] = s0..15[0-3]
	add		ip, r0, #0x20
	vld1.32		{q0-q1}, [r0]
	vld1.32		{q2-q3}, [ip]

	adr		r5, .Lctrinc
	vdup.32		q15, d7[1]
	vdup.32		q14, d7[0]
	vld1.32		{q4}, [r5, :128]
	vdup.32		q13, d6[1]
	vdup.32		q12, d6[0]
	vdup.32		q11, d5[1]
	vdup.32		q10, d5[0]
	vadd.u32	q12, q12, q4		// x12 += counter values 0-3
	vdup.32		q9, d4[1]
	vdup.32		q8, d4[0]
	vdup.32		q7, d3[1]
	vdup.32		q6, d3[0]
	vdup.32		q5, d2[1]
	vdup.32		q4, d2[0]
	vdup.32		q3, d1[1]
	vdup.32		q2, d1[0]
	vdup.32		q1, d0[1]
	vdup.32		q0, d0[0]

	adr		ip, .Lrol8_table
	mov		r3, #10
	b		1f

.Ldoubleround4:
	vld1.32		{q8-q9}, [sp, :256]
1:
	// x0 += x4, x12 = rotl32(x12 ^ x0, 16)
	// x1 += x5, x13 = rotl32(x13 ^ x1, 16)
	// x2 += x6, x14 = rotl32(x14 ^ x2, 16)
	// x3 += x7, x15 = rotl32(x15 ^ x3, 16)
	vadd.i32	q0, q0, q4
	vadd.i32	q1, q1, q5
	vadd.i32	q2, q2, q6
	vadd.i32	q3, q3, q7

	veor		q12, q12, q0
	veor		q13, q13, q1
	veor		q14, q14, q2
	veor		q15, q15, q3

	vrev32.16	q12, q12
	vrev32.16	q13, q13
	vrev32.16	q14, q14
	vrev32.16	q15, q15

	// x8 += x12, x4 = rotl32(x4 ^ x8, 12)
	// x9 += x13, x5 = rotl32(x5 ^ x9, 12)
	// x10 += x14, x6 = rotl32(x6 ^ x10, 12)
	// x11 += x15, x7 = rotl32(x7 ^ x11, 12)
	vadd.i32	q8, q8, q12
	vadd.i32	q9, q9, q13
	vadd.i32	q10, q10, q14
	vadd.i32	q11, q11, q15

	vst1.32		{q8-q9}, [sp, :256]

	veor		q8, q4, q8
	veor		q9, q5, q9
	vshl.u32	q4, q8, #12
	vshl.u32	q5, q9, #12
	vsri.u32	q4, q8, #20
	vsri.u32	q5, q9, #20

	veor		q8, q6, q10
	veor		q9, q7, q11
	vshl.u32	q6, q8, #12
	vshl.u32	q7, q9, #12
	vsri.u32	q6, q8, #20
	vsri.u32	q7, q9, #20

	// x0 += x4, x12 = rotl32(x12 ^ x0, 8)
	// x1 += x5, x13 = rotl32(x13 ^ x1, 8)
	// x2 += x6, x14 = rotl32(x14 ^ x2, 8)
	// x3 += x7, x15 = rotl32(x15 ^ x3, 8)
	vld1.8		{d16}, [ip, :64]
	vadd.i32	q0, q0, q4
	vadd.i32	q1, q1, q5
	vadd.i32	q2, q2, q6
	vadd.i32	q3, q3, q7

	veor		q12, q12, q0
	veor		q13, q13, q1
	veor		q14, q14, q2
	veor		q15, q15, q3

	vtbl.8		d24, {d24}, d16
	vtbl.8		d25, {d25}, d16
	vtbl.8		d26, {d26}, d16
	vtbl.8		d27, {d27}, d16
	vtbl.8		d28, {d28}, d16
	vtbl.8		d29, {d29}, d16
	vtbl.8		d30, {d30}, d16
	vtbl.8		d31, {d31}, d16

	vld1.32		{q8-q9}, [sp, :256]

	// x8 += x12, x4 = rotl32(x4 ^ x8, 7)
	// x9 += x13, x5 = rotl32(x5 ^ x9, 7)
	// x10 += x14, x6 = rotl32(x6 ^ x10, 7)
	// x11 += x15, x7 = rotl32(x7 ^ x11, 7)
	vadd.i32	q8, q8, q12
	vadd.i32	q9, q9, q13
	vadd.i32	q10, q10, q14
	vadd.i32	q11, q11, q15

	vst1.32		{q8-q9}, [sp, :256]

	veor		q8, q4, q8
	veor		q9, q5, q9
	vshl.u32	q4, q8, #7
	vshl.u32	q5, q9, #7
	vsri.u32	q4, q8, #25
	vsri.u32	q5, q9, #25

	veor		q8, q6, q10
	veor		q9, q7, q11
	vshl.u32	q6, q8, #7
	vshl.u32	q7, q9, #7
	vsri.u32	q6, q8, #25
	vsri.u32	q7, q9, #25

	vld1.32		{q8-q9}, [sp, :256]

	// x0 += x5, x15 = rotl32(x15 ^ x0, 16)
	// x1 += x6, x12 = rotl32(x12 ^ x1, 16)
	// x2 += x7, x13 = rotl32(x13 ^ x2, 16)
	// x3 += x4, x14 = rotl32(x14 ^ x3, 16)
	vadd.i32	q0, q0, q5
	vadd.i32	q1, q1, q6
	vadd.i32	q2, q2, q7
	vadd.i32	q3, q3, q4

	veor		q15, q15, q0
	veor		q12, q12, q1
	veor		q13, q13, q2
	veor		q14, q14, q3

	vrev32.16	q15, q15
	vrev32.16	q12, q12
	vrev32.16	q13, q13
	vrev32.16	q14, q14

	// x10 += x15, x5 = rotl32(x5 ^ x10, 12)
	// x11 += x12, x6 = rotl32(x6 ^ x11, 12)
	// x8 += x13, x7 = rotl32(x7 ^ x8, 12)
	// x9 += x14, x4 = rotl32(x4 ^ x9, 12)
	vadd.i32	q10, q10, q15
	vadd.i32	q11, q11, q12
	vadd.i32	q8, q8, q13
	vadd.i32	q9, q9, q14

	vst1.32		{q8-q9}, [sp, :256]

	veor		q8, q7, q8
	veor		q9, q4, q9
	vshl.u32	q7, q8, #12
	vshl.u32	q4, q9, #12
	vsri.u32	q7, q8, #20
	vsri.u32	q4, q9, #20

	veor		q8, q5, q10
	veor		q9, q6, q11
	vshl.u32	q5, q8, #12
	vshl.u32	q6, q9, #12
	vsri.u32	q5, q8, #20
	vsri.u32	q6, q9, #20

	// x0 += x5, x15 = rotl32(x15 ^ x0, 8)
	// x1 += x6, x12 = rotl32(x12 ^ x1, 8)
	// x2 += x7, x13 = rotl32(x13 ^ x2, 8)
	// x3 += x4, x14 = rotl32(x14 ^ x3, 8)
	vld1.8		{d16}, [ip, :64]
	vadd.i32	q0, q0, q5
	vadd.i32	q1, q1, q6
	vadd.i32	q2, q2, q7
	vadd.i32	q3, q3, q4

	veor		q15, q15, q0
	veor		q12, q12, q1
	veor		q13, q13, q2
	veor		q14, q14, q3

	vtbl.8		d30, {d30}, d16
	vtbl.8		d31, {d31}, d16
	vtbl.8		d24, {d24}, d16
	vtbl.8		d25, {d25}, d16
	vtbl.8		d26, {d26}, d16
	vtbl.8		d27, {d27}, d16
	vtbl.8		d28, {d28}, d16
	vtbl.8		d29, {d29}, d16

	vld1.32		{q8-q9}, [sp, :256]

	// x10 += x15, x5 = rotl32(x5 ^ x10, 7)
	// x11 += x12, x6 = rotl32(x6 ^ x11, 7)
	// x8 += x13, x7 = rotl32(x7 ^ x8, 7)
	// x9 += x14, x4 = rotl32(x4 ^ x9, 7)
	vadd.i32	q10, q10, q15
	vadd.i32	q11, q11, q12
	vadd.i32	q8, q8, q13
	vadd.i32	q9, q9, q14

	vst1.32		{q8-q9}, [sp, :256]

	veor		q8, q7, q8
	veor		q9, q4, q9
	vshl.u32	q7, q8, #7
	vshl.u32	q4, q9, #7
	vsri.u32	q7, q8, #25
	vsri.u32	q4, q9, #25

	veor		q8, q5, q10
	veor		q9, q6, q11
	vshl.u32	q5, q8, #7
	vshl.u32	q6, q9, #7
	vsri.u32	q5, q8, #25
	vsri.u32	q6, q9, #25

	subs		r3, r3, #1
	bne		.Ldoubleround4

	// x0..7[0-3] are in q0-q7, x10..15[0-3] are in q10-q15.
	// x8..9[0-3] are on the stack.

	// Re-interleave the words in the first two rows of each block (x0..7).
	// Also add the counter values 0-3 to x12[0-3].
	  vld1.32	{q8}, [r5, :128]	// load counter values 0-3
	vzip.32		q0, q1			// => (0 1 0 1) (0 1 0 1)
	vzip.32		q2, q3			// => (2 3 2 3) (2 3 2 3)
	vzip.32		q4, q5			// => (4 5 4 5) (4 5 4 5)
	vzip.32		q6, q7			// => (6 7 6 7) (6 7 6 7)
	  vadd.u32	q12, q8			// x12 += counter values 0-3
	vswp		d1, d4
	vswp		d3, d6
	  vld1.32	{q8-q9}, [r0]!		// load s0..7
	vswp		d9, d12
	vswp		d11, d14

	// Swap q1 and q4 so that we'll free up consecutive registers (q0-q1)
	// after XORing the first 32 bytes.
	vswp		q1, q4

	// First two rows of each block are (q0 q1) (q2 q6) (q4 q5) (q3 q7)

	// x0..3[0-3] += s0..3[0-3]	(add orig state to 1st row of each block)
	vadd.u32	q0, q0, q8
	vadd.u32	q2, q2, q8
	vadd.u32	q4, q4, q8
	vadd.u32	q3, q3, q8

	// x4..7[0-3] += s4..7[0-3]	(add orig state to 2nd row of each block)
	vadd.u32	q1, q1, q9
	vadd.u32	q6, q6, q9
	vadd.u32	q5, q5, q9
	vadd.u32	q7, q7, q9

	// XOR first 32 bytes using keystream from first two rows of first block
	vld1.8		{q8-q9}, [r2]!
	veor		q8, q8, q0
	veor		q9, q9, q1
	vst1.8		{q8-q9}, [r1]!

	// Re-interleave the words in the last two rows of each block (x8..15).
	vld1.32		{q8-q9}, [sp, :256]
	vzip.32		q12, q13	// => (12 13 12 13) (12 13 12 13)
	vzip.32		q14, q15	// => (14 15 14 15) (14 15 14 15)
	vzip.32		q8, q9		// => (8 9 8 9) (8 9 8 9)
	vzip.32		q10, q11	// => (10 11 10 11) (10 11 10 11)
	  vld1.32	{q0-q1}, [r0]	// load s8..15
	vswp		d25, d28
	vswp		d27, d30
	vswp		d17, d20
	vswp		d19, d22

	// Last two rows of each block are (q8 q12) (q10 q14) (q9 q13) (q11 q15)

	// x8..11[0-3] += s8..11[0-3]	(add orig state to 3rd row of each block)
	vadd.u32	q8,  q8,  q0
	vadd.u32	q10, q10, q0
	vadd.u32	q9,  q9,  q0
	vadd.u32	q11, q11, q0

	// x12..15[0-3] += s12..15[0-3] (add orig state to 4th row of each block)
	vadd.u32	q12, q12, q1
	vadd.u32	q14, q14, q1
	vadd.u32	q13, q13, q1
	vadd.u32	q15, q15, q1

	// XOR the rest of the data with the keystream

	vld1.8		{q0-q1}, [r2]!
	veor		q0, q0, q8
	veor		q1, q1, q12
	vst1.8		{q0-q1}, [r1]!

	vld1.8		{q0-q1}, [r2]!
	veor		q0, q0, q2
	veor		q1, q1, q6
	vst1.8		{q0-q1}, [r1]!

	vld1.8		{q0-q1}, [r2]!
	veor		q0, q0, q10
	veor		q1, q1, q14
	vst1.8		{q0-q1}, [r1]!

	vld1.8		{q0-q1}, [r2]!
	veor		q0, q0, q4
	veor		q1, q1, q5
	vst1.8		{q0-q1}, [r1]!

	vld1.8		{q0-q1}, [r2]!
	veor		q0, q0, q9
	veor		q1, q1, q13
	vst1.8		{q0-q1}, [r1]!

	vld1.8		{q0-q1}, [r2]!
	veor		q0, q0, q3
	veor		q1, q1, q7
	vst1.8		{q0-q1}, [r1]!

	vld1.8		{q0-q1}, [r2]
	  mov		sp, r4		// restore original stack pointer
	veor		q0, q0, q11
	veor		q1, q1, q15
	vst1.8		{q0-q1}, [r1]

	pop		{r4-r5}
	bx		lr
ENDPROC(chacha20_4block_xor_neon)
