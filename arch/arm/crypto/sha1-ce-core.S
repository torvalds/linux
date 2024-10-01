/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * sha1-ce-core.S - SHA-1 secure hash using ARMv8 Crypto Extensions
 *
 * Copyright (C) 2015 Linaro Ltd.
 * Author: Ard Biesheuvel <ard.biesheuvel@linaro.org>
 */

#include <linux/linkage.h>
#include <asm/assembler.h>

	.text
	.arch		armv8-a
	.fpu		crypto-neon-fp-armv8

	k0		.req	q0
	k1		.req	q1
	k2		.req	q2
	k3		.req	q3

	ta0		.req	q4
	ta1		.req	q5
	tb0		.req	q5
	tb1		.req	q4

	dga		.req	q6
	dgb		.req	q7
	dgbs		.req	s28

	dg0		.req	q12
	dg1a0		.req	q13
	dg1a1		.req	q14
	dg1b0		.req	q14
	dg1b1		.req	q13

	.macro		add_only, op, ev, rc, s0, dg1
	.ifnb		\s0
	vadd.u32	tb\ev, q\s0, \rc
	.endif
	sha1h.32	dg1b\ev, dg0
	.ifb		\dg1
	sha1\op\().32	dg0, dg1a\ev, ta\ev
	.else
	sha1\op\().32	dg0, \dg1, ta\ev
	.endif
	.endm

	.macro		add_update, op, ev, rc, s0, s1, s2, s3, dg1
	sha1su0.32	q\s0, q\s1, q\s2
	add_only	\op, \ev, \rc, \s1, \dg1
	sha1su1.32	q\s0, q\s3
	.endm

	.align		6
.Lsha1_rcon:
	.word		0x5a827999, 0x5a827999, 0x5a827999, 0x5a827999
	.word		0x6ed9eba1, 0x6ed9eba1, 0x6ed9eba1, 0x6ed9eba1
	.word		0x8f1bbcdc, 0x8f1bbcdc, 0x8f1bbcdc, 0x8f1bbcdc
	.word		0xca62c1d6, 0xca62c1d6, 0xca62c1d6, 0xca62c1d6

	/*
	 * void sha1_ce_transform(struct sha1_state *sst, u8 const *src,
	 *			  int blocks);
	 */
ENTRY(sha1_ce_transform)
	/* load round constants */
	adr		ip, .Lsha1_rcon
	vld1.32		{k0-k1}, [ip, :128]!
	vld1.32		{k2-k3}, [ip, :128]

	/* load state */
	vld1.32		{dga}, [r0]
	vldr		dgbs, [r0, #16]

	/* load input */
0:	vld1.32		{q8-q9}, [r1]!
	vld1.32		{q10-q11}, [r1]!
	subs		r2, r2, #1

#ifndef CONFIG_CPU_BIG_ENDIAN
	vrev32.8	q8, q8
	vrev32.8	q9, q9
	vrev32.8	q10, q10
	vrev32.8	q11, q11
#endif

	vadd.u32	ta0, q8, k0
	vmov		dg0, dga

	add_update	c, 0, k0,  8,  9, 10, 11, dgb
	add_update	c, 1, k0,  9, 10, 11,  8
	add_update	c, 0, k0, 10, 11,  8,  9
	add_update	c, 1, k0, 11,  8,  9, 10
	add_update	c, 0, k1,  8,  9, 10, 11

	add_update	p, 1, k1,  9, 10, 11,  8
	add_update	p, 0, k1, 10, 11,  8,  9
	add_update	p, 1, k1, 11,  8,  9, 10
	add_update	p, 0, k1,  8,  9, 10, 11
	add_update	p, 1, k2,  9, 10, 11,  8

	add_update	m, 0, k2, 10, 11,  8,  9
	add_update	m, 1, k2, 11,  8,  9, 10
	add_update	m, 0, k2,  8,  9, 10, 11
	add_update	m, 1, k2,  9, 10, 11,  8
	add_update	m, 0, k3, 10, 11,  8,  9

	add_update	p, 1, k3, 11,  8,  9, 10
	add_only	p, 0, k3,  9
	add_only	p, 1, k3, 10
	add_only	p, 0, k3, 11
	add_only	p, 1

	/* update state */
	vadd.u32	dga, dga, dg0
	vadd.u32	dgb, dgb, dg1a0
	bne		0b

	/* store new state */
	vst1.32		{dga}, [r0]
	vstr		dgbs, [r0, #16]
	bx		lr
ENDPROC(sha1_ce_transform)
