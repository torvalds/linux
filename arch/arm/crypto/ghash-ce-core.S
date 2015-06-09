/*
 * Accelerated GHASH implementation with ARMv8 vmull.p64 instructions.
 *
 * Copyright (C) 2015 Linaro Ltd. <ard.biesheuvel@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include <linux/linkage.h>
#include <asm/assembler.h>

	SHASH		.req	q0
	SHASH2		.req	q1
	T1		.req	q2
	T2		.req	q3
	MASK		.req	q4
	XL		.req	q5
	XM		.req	q6
	XH		.req	q7
	IN1		.req	q7

	SHASH_L		.req	d0
	SHASH_H		.req	d1
	SHASH2_L	.req	d2
	T1_L		.req	d4
	MASK_L		.req	d8
	XL_L		.req	d10
	XL_H		.req	d11
	XM_L		.req	d12
	XM_H		.req	d13
	XH_L		.req	d14

	.text
	.fpu		crypto-neon-fp-armv8

	/*
	 * void pmull_ghash_update(int blocks, u64 dg[], const char *src,
	 *			   struct ghash_key const *k, const char *head)
	 */
ENTRY(pmull_ghash_update)
	vld1.64		{SHASH}, [r3]
	vld1.64		{XL}, [r1]
	vmov.i8		MASK, #0xe1
	vext.8		SHASH2, SHASH, SHASH, #8
	vshl.u64	MASK, MASK, #57
	veor		SHASH2, SHASH2, SHASH

	/* do the head block first, if supplied */
	ldr		ip, [sp]
	teq		ip, #0
	beq		0f
	vld1.64		{T1}, [ip]
	teq		r0, #0
	b		1f

0:	vld1.64		{T1}, [r2]!
	subs		r0, r0, #1

1:	/* multiply XL by SHASH in GF(2^128) */
#ifndef CONFIG_CPU_BIG_ENDIAN
	vrev64.8	T1, T1
#endif
	vext.8		T2, XL, XL, #8
	vext.8		IN1, T1, T1, #8
	veor		T1, T1, T2
	veor		XL, XL, IN1

	vmull.p64	XH, SHASH_H, XL_H		@ a1 * b1
	veor		T1, T1, XL
	vmull.p64	XL, SHASH_L, XL_L		@ a0 * b0
	vmull.p64	XM, SHASH2_L, T1_L		@ (a1 + a0)(b1 + b0)

	vext.8		T1, XL, XH, #8
	veor		T2, XL, XH
	veor		XM, XM, T1
	veor		XM, XM, T2
	vmull.p64	T2, XL_L, MASK_L

	vmov		XH_L, XM_H
	vmov		XM_H, XL_L

	veor		XL, XM, T2
	vext.8		T2, XL, XL, #8
	vmull.p64	XL, XL_L, MASK_L
	veor		T2, T2, XH
	veor		XL, XL, T2

	bne		0b

	vst1.64		{XL}, [r1]
	bx		lr
ENDPROC(pmull_ghash_update)
