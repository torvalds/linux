/*
 * Accelerated GHASH implementation with ARMv8 PMULL instructions.
 *
 * Copyright (C) 2014 Linaro Ltd. <ard.biesheuvel@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include <linux/linkage.h>
#include <asm/assembler.h>

	SHASH	.req	v0
	SHASH2	.req	v1
	T1	.req	v2
	T2	.req	v3
	MASK	.req	v4
	XL	.req	v5
	XM	.req	v6
	XH	.req	v7
	IN1	.req	v7

	.text
	.arch		armv8-a+crypto

	/*
	 * void pmull_ghash_update(int blocks, u64 dg[], const char *src,
	 *			   struct ghash_key const *k, const char *head)
	 */
ENTRY(pmull_ghash_update)
	ld1		{SHASH.16b}, [x3]
	ld1		{XL.16b}, [x1]
	movi		MASK.16b, #0xe1
	ext		SHASH2.16b, SHASH.16b, SHASH.16b, #8
	shl		MASK.2d, MASK.2d, #57
	eor		SHASH2.16b, SHASH2.16b, SHASH.16b

	/* do the head block first, if supplied */
	cbz		x4, 0f
	ld1		{T1.2d}, [x4]
	b		1f

0:	ld1		{T1.2d}, [x2], #16
	sub		w0, w0, #1

1:	/* multiply XL by SHASH in GF(2^128) */
CPU_LE(	rev64		T1.16b, T1.16b	)

	ext		T2.16b, XL.16b, XL.16b, #8
	ext		IN1.16b, T1.16b, T1.16b, #8
	eor		T1.16b, T1.16b, T2.16b
	eor		XL.16b, XL.16b, IN1.16b

	pmull2		XH.1q, SHASH.2d, XL.2d		// a1 * b1
	eor		T1.16b, T1.16b, XL.16b
	pmull		XL.1q, SHASH.1d, XL.1d		// a0 * b0
	pmull		XM.1q, SHASH2.1d, T1.1d		// (a1 + a0)(b1 + b0)

	ext		T1.16b, XL.16b, XH.16b, #8
	eor		T2.16b, XL.16b, XH.16b
	eor		XM.16b, XM.16b, T1.16b
	eor		XM.16b, XM.16b, T2.16b
	pmull		T2.1q, XL.1d, MASK.1d

	mov		XH.d[0], XM.d[1]
	mov		XM.d[1], XL.d[0]

	eor		XL.16b, XM.16b, T2.16b
	ext		T2.16b, XL.16b, XL.16b, #8
	pmull		XL.1q, XL.1d, MASK.1d
	eor		T2.16b, T2.16b, XH.16b
	eor		XL.16b, XL.16b, T2.16b

	cbnz		w0, 0b

	st1		{XL.16b}, [x1]
	ret
ENDPROC(pmull_ghash_update)
