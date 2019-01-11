/*
 * Accelerated CRC32(C) using arm64 CRC, NEON and Crypto Extensions instructions
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

	.section	".rodata", "a"
	.align		6
	.cpu		generic+crypto+crc

.Lcrc32_constants:
	/*
	 * [x4*128+32 mod P(x) << 32)]'  << 1   = 0x154442bd4
	 * #define CONSTANT_R1  0x154442bd4LL
	 *
	 * [(x4*128-32 mod P(x) << 32)]' << 1   = 0x1c6e41596
	 * #define CONSTANT_R2  0x1c6e41596LL
	 */
	.octa		0x00000001c6e415960000000154442bd4

	/*
	 * [(x128+32 mod P(x) << 32)]'   << 1   = 0x1751997d0
	 * #define CONSTANT_R3  0x1751997d0LL
	 *
	 * [(x128-32 mod P(x) << 32)]'   << 1   = 0x0ccaa009e
	 * #define CONSTANT_R4  0x0ccaa009eLL
	 */
	.octa		0x00000000ccaa009e00000001751997d0

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
	.octa		0x00000001F701164100000001DB710641

.Lcrc32c_constants:
	.octa		0x000000009e4addf800000000740eef02
	.octa		0x000000014cd00bd600000000f20c0dfe
	.quad		0x00000000dd45aab8
	.quad		0x00000000FFFFFFFF
	.octa		0x00000000dea713f10000000105ec76f0

	vCONSTANT	.req	v0
	dCONSTANT	.req	d0
	qCONSTANT	.req	q0

	BUF		.req	x19
	LEN		.req	x20
	CRC		.req	x21
	CONST		.req	x22

	vzr		.req	v9

	/**
	 * Calculate crc32
	 * BUF - buffer
	 * LEN - sizeof buffer (multiple of 16 bytes), LEN should be > 63
	 * CRC - initial crc32
	 * return %eax crc32
	 * uint crc32_pmull_le(unsigned char const *buffer,
	 *                     size_t len, uint crc32)
	 */
	.text
ENTRY(crc32_pmull_le)
	adr_l		x3, .Lcrc32_constants
	b		0f

ENTRY(crc32c_pmull_le)
	adr_l		x3, .Lcrc32c_constants

0:	frame_push	4, 64

	mov		BUF, x0
	mov		LEN, x1
	mov		CRC, x2
	mov		CONST, x3

	bic		LEN, LEN, #15
	ld1		{v1.16b-v4.16b}, [BUF], #0x40
	movi		vzr.16b, #0
	fmov		dCONSTANT, CRC
	eor		v1.16b, v1.16b, vCONSTANT.16b
	sub		LEN, LEN, #0x40
	cmp		LEN, #0x40
	b.lt		less_64

	ldr		qCONSTANT, [CONST]

loop_64:		/* 64 bytes Full cache line folding */
	sub		LEN, LEN, #0x40

	pmull2		v5.1q, v1.2d, vCONSTANT.2d
	pmull2		v6.1q, v2.2d, vCONSTANT.2d
	pmull2		v7.1q, v3.2d, vCONSTANT.2d
	pmull2		v8.1q, v4.2d, vCONSTANT.2d

	pmull		v1.1q, v1.1d, vCONSTANT.1d
	pmull		v2.1q, v2.1d, vCONSTANT.1d
	pmull		v3.1q, v3.1d, vCONSTANT.1d
	pmull		v4.1q, v4.1d, vCONSTANT.1d

	eor		v1.16b, v1.16b, v5.16b
	ld1		{v5.16b}, [BUF], #0x10
	eor		v2.16b, v2.16b, v6.16b
	ld1		{v6.16b}, [BUF], #0x10
	eor		v3.16b, v3.16b, v7.16b
	ld1		{v7.16b}, [BUF], #0x10
	eor		v4.16b, v4.16b, v8.16b
	ld1		{v8.16b}, [BUF], #0x10

	eor		v1.16b, v1.16b, v5.16b
	eor		v2.16b, v2.16b, v6.16b
	eor		v3.16b, v3.16b, v7.16b
	eor		v4.16b, v4.16b, v8.16b

	cmp		LEN, #0x40
	b.lt		less_64

	if_will_cond_yield_neon
	stp		q1, q2, [sp, #.Lframe_local_offset]
	stp		q3, q4, [sp, #.Lframe_local_offset + 32]
	do_cond_yield_neon
	ldp		q1, q2, [sp, #.Lframe_local_offset]
	ldp		q3, q4, [sp, #.Lframe_local_offset + 32]
	ldr		qCONSTANT, [CONST]
	movi		vzr.16b, #0
	endif_yield_neon
	b		loop_64

less_64:		/* Folding cache line into 128bit */
	ldr		qCONSTANT, [CONST, #16]

	pmull2		v5.1q, v1.2d, vCONSTANT.2d
	pmull		v1.1q, v1.1d, vCONSTANT.1d
	eor		v1.16b, v1.16b, v5.16b
	eor		v1.16b, v1.16b, v2.16b

	pmull2		v5.1q, v1.2d, vCONSTANT.2d
	pmull		v1.1q, v1.1d, vCONSTANT.1d
	eor		v1.16b, v1.16b, v5.16b
	eor		v1.16b, v1.16b, v3.16b

	pmull2		v5.1q, v1.2d, vCONSTANT.2d
	pmull		v1.1q, v1.1d, vCONSTANT.1d
	eor		v1.16b, v1.16b, v5.16b
	eor		v1.16b, v1.16b, v4.16b

	cbz		LEN, fold_64

loop_16:		/* Folding rest buffer into 128bit */
	subs		LEN, LEN, #0x10

	ld1		{v2.16b}, [BUF], #0x10
	pmull2		v5.1q, v1.2d, vCONSTANT.2d
	pmull		v1.1q, v1.1d, vCONSTANT.1d
	eor		v1.16b, v1.16b, v5.16b
	eor		v1.16b, v1.16b, v2.16b

	b.ne		loop_16

fold_64:
	/* perform the last 64 bit fold, also adds 32 zeroes
	 * to the input stream */
	ext		v2.16b, v1.16b, v1.16b, #8
	pmull2		v2.1q, v2.2d, vCONSTANT.2d
	ext		v1.16b, v1.16b, vzr.16b, #8
	eor		v1.16b, v1.16b, v2.16b

	/* final 32-bit fold */
	ldr		dCONSTANT, [CONST, #32]
	ldr		d3, [CONST, #40]

	ext		v2.16b, v1.16b, vzr.16b, #4
	and		v1.16b, v1.16b, v3.16b
	pmull		v1.1q, v1.1d, vCONSTANT.1d
	eor		v1.16b, v1.16b, v2.16b

	/* Finish up with the bit-reversed barrett reduction 64 ==> 32 bits */
	ldr		qCONSTANT, [CONST, #48]

	and		v2.16b, v1.16b, v3.16b
	ext		v2.16b, vzr.16b, v2.16b, #8
	pmull2		v2.1q, v2.2d, vCONSTANT.2d
	and		v2.16b, v2.16b, v3.16b
	pmull		v2.1q, v2.1d, vCONSTANT.1d
	eor		v1.16b, v1.16b, v2.16b
	mov		w0, v1.s[1]

	frame_pop
	ret
ENDPROC(crc32_pmull_le)
ENDPROC(crc32c_pmull_le)

	.macro		__crc32, c
0:	subs		x2, x2, #16
	b.mi		8f
	ldp		x3, x4, [x1], #16
CPU_BE(	rev		x3, x3		)
CPU_BE(	rev		x4, x4		)
	crc32\c\()x	w0, w0, x3
	crc32\c\()x	w0, w0, x4
	b.ne		0b
	ret

8:	tbz		x2, #3, 4f
	ldr		x3, [x1], #8
CPU_BE(	rev		x3, x3		)
	crc32\c\()x	w0, w0, x3
4:	tbz		x2, #2, 2f
	ldr		w3, [x1], #4
CPU_BE(	rev		w3, w3		)
	crc32\c\()w	w0, w0, w3
2:	tbz		x2, #1, 1f
	ldrh		w3, [x1], #2
CPU_BE(	rev16		w3, w3		)
	crc32\c\()h	w0, w0, w3
1:	tbz		x2, #0, 0f
	ldrb		w3, [x1]
	crc32\c\()b	w0, w0, w3
0:	ret
	.endm

	.align		5
ENTRY(crc32_armv8_le)
	__crc32
ENDPROC(crc32_armv8_le)

	.align		5
ENTRY(crc32c_armv8_le)
	__crc32		c
ENDPROC(crc32c_armv8_le)
