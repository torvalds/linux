/*
 * Accelerated CRC32(C) using ARM CRC, NEON and Crypto Extensions instructions
 *
 * Copyright (C) 2016 Linaro Ltd <ard.biesheuvel@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/* GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see http://www.gnu.org/licenses
 *
 * Please  visit http://www.xyratex.com/contact if you need additional
 * information or have any questions.
 *
 * GPL HEADER END
 */

/*
 * Copyright 2012 Xyratex Technology Limited
 *
 * Using hardware provided PCLMULQDQ instruction to accelerate the CRC32
 * calculation.
 * CRC32 polynomial:0x04c11db7(BE)/0xEDB88320(LE)
 * PCLMULQDQ is a new instruction in Intel SSE4.2, the reference can be found
 * at:
 * http://www.intel.com/products/processor/manuals/
 * Intel(R) 64 and IA-32 Architectures Software Developer's Manual
 * Volume 2B: Instruction Set Reference, N-Z
 *
 * Authors:   Gregory Prestas <Gregory_Prestas@us.xyratex.com>
 *	      Alexander Boyko <Alexander_Boyko@xyratex.com>
 */

#include <linux/linkage.h>
#include <asm/assembler.h>

	.text
	.align		6
	.arch		armv8-a
	.arch_extension	crc
	.fpu		crypto-neon-fp-armv8

.Lcrc32_constants:
	/*
	 * [x4*128+32 mod P(x) << 32)]'  << 1   = 0x154442bd4
	 * #define CONSTANT_R1  0x154442bd4LL
	 *
	 * [(x4*128-32 mod P(x) << 32)]' << 1   = 0x1c6e41596
	 * #define CONSTANT_R2  0x1c6e41596LL
	 */
	.quad		0x0000000154442bd4
	.quad		0x00000001c6e41596

	/*
	 * [(x128+32 mod P(x) << 32)]'   << 1   = 0x1751997d0
	 * #define CONSTANT_R3  0x1751997d0LL
	 *
	 * [(x128-32 mod P(x) << 32)]'   << 1   = 0x0ccaa009e
	 * #define CONSTANT_R4  0x0ccaa009eLL
	 */
	.quad		0x00000001751997d0
	.quad		0x00000000ccaa009e

	/*
	 * [(x64 mod P(x) << 32)]'       << 1   = 0x163cd6124
	 * #define CONSTANT_R5  0x163cd6124LL
	 */
	.quad		0x0000000163cd6124
	.quad		0x00000000FFFFFFFF

	/*
	 * #define CRCPOLY_TRUE_LE_FULL 0x1DB710641LL
	 *
	 * Barrett Reduction constant (u64`) = u` = (x**64 / P(x))`
	 *                                                      = 0x1F7011641LL
	 * #define CONSTANT_RU  0x1F7011641LL
	 */
	.quad		0x00000001DB710641
	.quad		0x00000001F7011641

.Lcrc32c_constants:
	.quad		0x00000000740eef02
	.quad		0x000000009e4addf8
	.quad		0x00000000f20c0dfe
	.quad		0x000000014cd00bd6
	.quad		0x00000000dd45aab8
	.quad		0x00000000FFFFFFFF
	.quad		0x0000000105ec76f0
	.quad		0x00000000dea713f1

	dCONSTANTl	.req	d0
	dCONSTANTh	.req	d1
	qCONSTANT	.req	q0

	BUF		.req	r0
	LEN		.req	r1
	CRC		.req	r2

	qzr		.req	q9

	/**
	 * Calculate crc32
	 * BUF - buffer
	 * LEN - sizeof buffer (multiple of 16 bytes), LEN should be > 63
	 * CRC - initial crc32
	 * return %eax crc32
	 * uint crc32_pmull_le(unsigned char const *buffer,
	 *                     size_t len, uint crc32)
	 */
ENTRY(crc32_pmull_le)
	adr		r3, .Lcrc32_constants
	b		0f

ENTRY(crc32c_pmull_le)
	adr		r3, .Lcrc32c_constants

0:	bic		LEN, LEN, #15
	vld1.8		{q1-q2}, [BUF, :128]!
	vld1.8		{q3-q4}, [BUF, :128]!
	vmov.i8		qzr, #0
	vmov.i8		qCONSTANT, #0
	vmov		dCONSTANTl[0], CRC
	veor.8		d2, d2, dCONSTANTl
	sub		LEN, LEN, #0x40
	cmp		LEN, #0x40
	blt		less_64

	vld1.64		{qCONSTANT}, [r3]

loop_64:		/* 64 bytes Full cache line folding */
	sub		LEN, LEN, #0x40

	vmull.p64	q5, d3, dCONSTANTh
	vmull.p64	q6, d5, dCONSTANTh
	vmull.p64	q7, d7, dCONSTANTh
	vmull.p64	q8, d9, dCONSTANTh

	vmull.p64	q1, d2, dCONSTANTl
	vmull.p64	q2, d4, dCONSTANTl
	vmull.p64	q3, d6, dCONSTANTl
	vmull.p64	q4, d8, dCONSTANTl

	veor.8		q1, q1, q5
	vld1.8		{q5}, [BUF, :128]!
	veor.8		q2, q2, q6
	vld1.8		{q6}, [BUF, :128]!
	veor.8		q3, q3, q7
	vld1.8		{q7}, [BUF, :128]!
	veor.8		q4, q4, q8
	vld1.8		{q8}, [BUF, :128]!

	veor.8		q1, q1, q5
	veor.8		q2, q2, q6
	veor.8		q3, q3, q7
	veor.8		q4, q4, q8

	cmp		LEN, #0x40
	bge		loop_64

less_64:		/* Folding cache line into 128bit */
	vldr		dCONSTANTl, [r3, #16]
	vldr		dCONSTANTh, [r3, #24]

	vmull.p64	q5, d3, dCONSTANTh
	vmull.p64	q1, d2, dCONSTANTl
	veor.8		q1, q1, q5
	veor.8		q1, q1, q2

	vmull.p64	q5, d3, dCONSTANTh
	vmull.p64	q1, d2, dCONSTANTl
	veor.8		q1, q1, q5
	veor.8		q1, q1, q3

	vmull.p64	q5, d3, dCONSTANTh
	vmull.p64	q1, d2, dCONSTANTl
	veor.8		q1, q1, q5
	veor.8		q1, q1, q4

	teq		LEN, #0
	beq		fold_64

loop_16:		/* Folding rest buffer into 128bit */
	subs		LEN, LEN, #0x10

	vld1.8		{q2}, [BUF, :128]!
	vmull.p64	q5, d3, dCONSTANTh
	vmull.p64	q1, d2, dCONSTANTl
	veor.8		q1, q1, q5
	veor.8		q1, q1, q2

	bne		loop_16

fold_64:
	/* perform the last 64 bit fold, also adds 32 zeroes
	 * to the input stream */
	vmull.p64	q2, d2, dCONSTANTh
	vext.8		q1, q1, qzr, #8
	veor.8		q1, q1, q2

	/* final 32-bit fold */
	vldr		dCONSTANTl, [r3, #32]
	vldr		d6, [r3, #40]
	vmov.i8		d7, #0

	vext.8		q2, q1, qzr, #4
	vand.8		d2, d2, d6
	vmull.p64	q1, d2, dCONSTANTl
	veor.8		q1, q1, q2

	/* Finish up with the bit-reversed barrett reduction 64 ==> 32 bits */
	vldr		dCONSTANTl, [r3, #48]
	vldr		dCONSTANTh, [r3, #56]

	vand.8		q2, q1, q3
	vext.8		q2, qzr, q2, #8
	vmull.p64	q2, d5, dCONSTANTh
	vand.8		q2, q2, q3
	vmull.p64	q2, d4, dCONSTANTl
	veor.8		q1, q1, q2
	vmov		r0, s5

	bx		lr
ENDPROC(crc32_pmull_le)
ENDPROC(crc32c_pmull_le)

	.macro		__crc32, c
	subs		ip, r2, #8
	bmi		.Ltail\c

	tst		r1, #3
	bne		.Lunaligned\c

	teq		ip, #0
.Laligned8\c:
	ldrd		r2, r3, [r1], #8
ARM_BE8(rev		r2, r2		)
ARM_BE8(rev		r3, r3		)
	crc32\c\()w	r0, r0, r2
	crc32\c\()w	r0, r0, r3
	bxeq		lr
	subs		ip, ip, #8
	bpl		.Laligned8\c

.Ltail\c:
	tst		ip, #4
	beq		2f
	ldr		r3, [r1], #4
ARM_BE8(rev		r3, r3		)
	crc32\c\()w	r0, r0, r3

2:	tst		ip, #2
	beq		1f
	ldrh		r3, [r1], #2
ARM_BE8(rev16		r3, r3		)
	crc32\c\()h	r0, r0, r3

1:	tst		ip, #1
	bxeq		lr
	ldrb		r3, [r1]
	crc32\c\()b	r0, r0, r3
	bx		lr

.Lunaligned\c:
	tst		r1, #1
	beq		2f
	ldrb		r3, [r1], #1
	subs		r2, r2, #1
	crc32\c\()b	r0, r0, r3

	tst		r1, #2
	beq		0f
2:	ldrh		r3, [r1], #2
	subs		r2, r2, #2
ARM_BE8(rev16		r3, r3		)
	crc32\c\()h	r0, r0, r3

0:	subs		ip, r2, #8
	bpl		.Laligned8\c
	b		.Ltail\c
	.endm

	.align		5
ENTRY(crc32_armv8_le)
	__crc32
ENDPROC(crc32_armv8_le)

	.align		5
ENTRY(crc32c_armv8_le)
	__crc32		c
ENDPROC(crc32c_armv8_le)
