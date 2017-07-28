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

	mov		r3, #10

.Ldoubleround:
	// x0 += x1, x3 = rotl32(x3 ^ x0, 16)
	vadd.i32	q0, q0, q1
	veor		q4, q3, q0
	vshl.u32	q3, q4, #16
	vsri.u32	q3, q4, #16

	// x2 += x3, x1 = rotl32(x1 ^ x2, 12)
	vadd.i32	q2, q2, q3
	veor		q4, q1, q2
	vshl.u32	q1, q4, #12
	vsri.u32	q1, q4, #20

	// x0 += x1, x3 = rotl32(x3 ^ x0, 8)
	vadd.i32	q0, q0, q1
	veor		q4, q3, q0
	vshl.u32	q3, q4, #8
	vsri.u32	q3, q4, #24

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
	veor		q4, q3, q0
	vshl.u32	q3, q4, #16
	vsri.u32	q3, q4, #16

	// x2 += x3, x1 = rotl32(x1 ^ x2, 12)
	vadd.i32	q2, q2, q3
	veor		q4, q1, q2
	vshl.u32	q1, q4, #12
	vsri.u32	q1, q4, #20

	// x0 += x1, x3 = rotl32(x3 ^ x0, 8)
	vadd.i32	q0, q0, q1
	veor		q4, q3, q0
	vshl.u32	q3, q4, #8
	vsri.u32	q3, q4, #24

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

	.align		5
ENTRY(chacha20_4block_xor_neon)
	push		{r4-r6, lr}
	mov		ip, sp			// preserve the stack pointer
	sub		r3, sp, #0x20		// allocate a 32 byte buffer
	bic		r3, r3, #0x1f		// aligned to 32 bytes
	mov		sp, r3

	// r0: Input state matrix, s
	// r1: 4 data blocks output, o
	// r2: 4 data blocks input, i

	//
	// This function encrypts four consecutive ChaCha20 blocks by loading
	// the state matrix in NEON registers four times. The algorithm performs
	// each operation on the corresponding word of each state matrix, hence
	// requires no word shuffling. For final XORing step we transpose the
	// matrix by interleaving 32- and then 64-bit words, which allows us to
	// do XOR in NEON registers.
	//

	// x0..15[0-3] = s0..3[0..3]
	add		r3, r0, #0x20
	vld1.32		{q0-q1}, [r0]
	vld1.32		{q2-q3}, [r3]

	adr		r3, CTRINC
	vdup.32		q15, d7[1]
	vdup.32		q14, d7[0]
	vld1.32		{q11}, [r3, :128]
	vdup.32		q13, d6[1]
	vdup.32		q12, d6[0]
	vadd.i32	q12, q12, q11		// x12 += counter values 0-3
	vdup.32		q11, d5[1]
	vdup.32		q10, d5[0]
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

	mov		r3, #10

.Ldoubleround4:
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
	vadd.i32	q0, q0, q4
	vadd.i32	q1, q1, q5
	vadd.i32	q2, q2, q6
	vadd.i32	q3, q3, q7

	veor		q8, q12, q0
	veor		q9, q13, q1
	vshl.u32	q12, q8, #8
	vshl.u32	q13, q9, #8
	vsri.u32	q12, q8, #24
	vsri.u32	q13, q9, #24

	veor		q8, q14, q2
	veor		q9, q15, q3
	vshl.u32	q14, q8, #8
	vshl.u32	q15, q9, #8
	vsri.u32	q14, q8, #24
	vsri.u32	q15, q9, #24

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
	vadd.i32	q0, q0, q5
	vadd.i32	q1, q1, q6
	vadd.i32	q2, q2, q7
	vadd.i32	q3, q3, q4

	veor		q8, q15, q0
	veor		q9, q12, q1
	vshl.u32	q15, q8, #8
	vshl.u32	q12, q9, #8
	vsri.u32	q15, q8, #24
	vsri.u32	q12, q9, #24

	veor		q8, q13, q2
	veor		q9, q14, q3
	vshl.u32	q13, q8, #8
	vshl.u32	q14, q9, #8
	vsri.u32	q13, q8, #24
	vsri.u32	q14, q9, #24

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
	beq		0f

	vld1.32		{q8-q9}, [sp, :256]
	b		.Ldoubleround4

	// x0[0-3] += s0[0]
	// x1[0-3] += s0[1]
	// x2[0-3] += s0[2]
	// x3[0-3] += s0[3]
0:	ldmia		r0!, {r3-r6}
	vdup.32		q8, r3
	vdup.32		q9, r4
	vadd.i32	q0, q0, q8
	vadd.i32	q1, q1, q9
	vdup.32		q8, r5
	vdup.32		q9, r6
	vadd.i32	q2, q2, q8
	vadd.i32	q3, q3, q9

	// x4[0-3] += s1[0]
	// x5[0-3] += s1[1]
	// x6[0-3] += s1[2]
	// x7[0-3] += s1[3]
	ldmia		r0!, {r3-r6}
	vdup.32		q8, r3
	vdup.32		q9, r4
	vadd.i32	q4, q4, q8
	vadd.i32	q5, q5, q9
	vdup.32		q8, r5
	vdup.32		q9, r6
	vadd.i32	q6, q6, q8
	vadd.i32	q7, q7, q9

	// interleave 32-bit words in state n, n+1
	vzip.32		q0, q1
	vzip.32		q2, q3
	vzip.32		q4, q5
	vzip.32		q6, q7

	// interleave 64-bit words in state n, n+2
	vswp		d1, d4
	vswp		d3, d6
	vswp		d9, d12
	vswp		d11, d14

	// xor with corresponding input, write to output
	vld1.8		{q8-q9}, [r2]!
	veor		q8, q8, q0
	veor		q9, q9, q4
	vst1.8		{q8-q9}, [r1]!

	vld1.32		{q8-q9}, [sp, :256]

	// x8[0-3] += s2[0]
	// x9[0-3] += s2[1]
	// x10[0-3] += s2[2]
	// x11[0-3] += s2[3]
	ldmia		r0!, {r3-r6}
	vdup.32		q0, r3
	vdup.32		q4, r4
	vadd.i32	q8, q8, q0
	vadd.i32	q9, q9, q4
	vdup.32		q0, r5
	vdup.32		q4, r6
	vadd.i32	q10, q10, q0
	vadd.i32	q11, q11, q4

	// x12[0-3] += s3[0]
	// x13[0-3] += s3[1]
	// x14[0-3] += s3[2]
	// x15[0-3] += s3[3]
	ldmia		r0!, {r3-r6}
	vdup.32		q0, r3
	vdup.32		q4, r4
	adr		r3, CTRINC
	vadd.i32	q12, q12, q0
	vld1.32		{q0}, [r3, :128]
	vadd.i32	q13, q13, q4
	vadd.i32	q12, q12, q0		// x12 += counter values 0-3

	vdup.32		q0, r5
	vdup.32		q4, r6
	vadd.i32	q14, q14, q0
	vadd.i32	q15, q15, q4

	// interleave 32-bit words in state n, n+1
	vzip.32		q8, q9
	vzip.32		q10, q11
	vzip.32		q12, q13
	vzip.32		q14, q15

	// interleave 64-bit words in state n, n+2
	vswp		d17, d20
	vswp		d19, d22
	vswp		d25, d28
	vswp		d27, d30

	vmov		q4, q1

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
	veor		q0, q0, q11
	veor		q1, q1, q15
	vst1.8		{q0-q1}, [r1]

	mov		sp, ip
	pop		{r4-r6, pc}
ENDPROC(chacha20_4block_xor_neon)

	.align		4
CTRINC:	.word		0, 1, 2, 3
