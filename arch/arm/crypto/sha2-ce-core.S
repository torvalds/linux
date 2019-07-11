/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * sha2-ce-core.S - SHA-224/256 secure hash using ARMv8 Crypto Extensions
 *
 * Copyright (C) 2015 Linaro Ltd.
 * Author: Ard Biesheuvel <ard.biesheuvel@linaro.org>
 */

#include <linux/linkage.h>
#include <asm/assembler.h>

	.text
	.fpu		crypto-neon-fp-armv8

	k0		.req	q7
	k1		.req	q8
	rk		.req	r3

	ta0		.req	q9
	ta1		.req	q10
	tb0		.req	q10
	tb1		.req	q9

	dga		.req	q11
	dgb		.req	q12

	dg0		.req	q13
	dg1		.req	q14
	dg2		.req	q15

	.macro		add_only, ev, s0
	vmov		dg2, dg0
	.ifnb		\s0
	vld1.32		{k\ev}, [rk, :128]!
	.endif
	sha256h.32	dg0, dg1, tb\ev
	sha256h2.32	dg1, dg2, tb\ev
	.ifnb		\s0
	vadd.u32	ta\ev, q\s0, k\ev
	.endif
	.endm

	.macro		add_update, ev, s0, s1, s2, s3
	sha256su0.32	q\s0, q\s1
	add_only	\ev, \s1
	sha256su1.32	q\s0, q\s2, q\s3
	.endm

	.align		6
.Lsha256_rcon:
	.word		0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5
	.word		0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5
	.word		0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3
	.word		0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174
	.word		0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc
	.word		0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da
	.word		0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7
	.word		0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967
	.word		0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13
	.word		0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85
	.word		0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3
	.word		0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070
	.word		0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5
	.word		0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3
	.word		0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208
	.word		0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2

	/*
	 * void sha2_ce_transform(struct sha256_state *sst, u8 const *src,
				  int blocks);
	 */
ENTRY(sha2_ce_transform)
	/* load state */
	vld1.32		{dga-dgb}, [r0]

	/* load input */
0:	vld1.32		{q0-q1}, [r1]!
	vld1.32		{q2-q3}, [r1]!
	subs		r2, r2, #1

#ifndef CONFIG_CPU_BIG_ENDIAN
	vrev32.8	q0, q0
	vrev32.8	q1, q1
	vrev32.8	q2, q2
	vrev32.8	q3, q3
#endif

	/* load first round constant */
	adr		rk, .Lsha256_rcon
	vld1.32		{k0}, [rk, :128]!

	vadd.u32	ta0, q0, k0
	vmov		dg0, dga
	vmov		dg1, dgb

	add_update	1, 0, 1, 2, 3
	add_update	0, 1, 2, 3, 0
	add_update	1, 2, 3, 0, 1
	add_update	0, 3, 0, 1, 2
	add_update	1, 0, 1, 2, 3
	add_update	0, 1, 2, 3, 0
	add_update	1, 2, 3, 0, 1
	add_update	0, 3, 0, 1, 2
	add_update	1, 0, 1, 2, 3
	add_update	0, 1, 2, 3, 0
	add_update	1, 2, 3, 0, 1
	add_update	0, 3, 0, 1, 2

	add_only	1, 1
	add_only	0, 2
	add_only	1, 3
	add_only	0

	/* update state */
	vadd.u32	dga, dga, dg0
	vadd.u32	dgb, dgb, dg1
	bne		0b

	/* store new state */
	vst1.32		{dga-dgb}, [r0]
	bx		lr
ENDPROC(sha2_ce_transform)
