/*
 * Accelerated GHASH implementation with ARMv8 PMULL instructions.
 *
 * Copyright (C) 2014 Linaro Ltd. <ard.biesheuvel@linaro.org>
 *
 * Based on arch/x86/crypto/ghash-pmullni-intel_asm.S
 *
 * Copyright (c) 2009 Intel Corp.
 *   Author: Huang Ying <ying.huang@intel.com>
 *           Vinodh Gopal
 *           Erdinc Ozturk
 *           Deniz Karakoyunlu
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include <linux/linkage.h>
#include <asm/assembler.h>

	DATA	.req	v0
	SHASH	.req	v1
	IN1	.req	v2
	T1	.req	v2
	T2	.req	v3
	T3	.req	v4
	VZR	.req	v5

	.text
	.arch		armv8-a+crypto

	/*
	 * void pmull_ghash_update(int blocks, u64 dg[], const char *src,
	 *			   struct ghash_key const *k, const char *head)
	 */
ENTRY(pmull_ghash_update)
	ld1		{DATA.16b}, [x1]
	ld1		{SHASH.16b}, [x3]
	eor		VZR.16b, VZR.16b, VZR.16b

	/* do the head block first, if supplied */
	cbz		x4, 0f
	ld1		{IN1.2d}, [x4]
	b		1f

0:	ld1		{IN1.2d}, [x2], #16
	sub		w0, w0, #1
1:	ext		IN1.16b, IN1.16b, IN1.16b, #8
CPU_LE(	rev64		IN1.16b, IN1.16b	)
	eor		DATA.16b, DATA.16b, IN1.16b

	/* multiply DATA by SHASH in GF(2^128) */
	ext		T2.16b, DATA.16b, DATA.16b, #8
	ext		T3.16b, SHASH.16b, SHASH.16b, #8
	eor		T2.16b, T2.16b, DATA.16b
	eor		T3.16b, T3.16b, SHASH.16b

	pmull2		T1.1q, SHASH.2d, DATA.2d	// a1 * b1
	pmull		DATA.1q, SHASH.1d, DATA.1d	// a0 * b0
	pmull		T2.1q, T2.1d, T3.1d		// (a1 + a0)(b1 + b0)
	eor		T2.16b, T2.16b, T1.16b		// (a0 * b1) + (a1 * b0)
	eor		T2.16b, T2.16b, DATA.16b

	ext		T3.16b, VZR.16b, T2.16b, #8
	ext		T2.16b, T2.16b, VZR.16b, #8
	eor		DATA.16b, DATA.16b, T3.16b
	eor		T1.16b, T1.16b, T2.16b	// <T1:DATA> is result of
						// carry-less multiplication

	/* first phase of the reduction */
	shl		T3.2d, DATA.2d, #1
	eor		T3.16b, T3.16b, DATA.16b
	shl		T3.2d, T3.2d, #5
	eor		T3.16b, T3.16b, DATA.16b
	shl		T3.2d, T3.2d, #57
	ext		T2.16b, VZR.16b, T3.16b, #8
	ext		T3.16b, T3.16b, VZR.16b, #8
	eor		DATA.16b, DATA.16b, T2.16b
	eor		T1.16b, T1.16b, T3.16b

	/* second phase of the reduction */
	ushr		T2.2d, DATA.2d, #5
	eor		T2.16b, T2.16b, DATA.16b
	ushr		T2.2d, T2.2d, #1
	eor		T2.16b, T2.16b, DATA.16b
	ushr		T2.2d, T2.2d, #1
	eor		T1.16b, T1.16b, T2.16b
	eor		DATA.16b, DATA.16b, T1.16b

	cbnz		w0, 0b

	st1		{DATA.16b}, [x1]
	ret
ENDPROC(pmull_ghash_update)
