/* SPDX-License-Identifier: GPL-2.0 */
/*
 * NH - ε-almost-universal hash function, NEON accelerated version
 *
 * Copyright 2018 Google LLC
 *
 * Author: Eric Biggers <ebiggers@google.com>
 */

#include <linux/linkage.h>

	.text
	.fpu		neon

	KEY		.req	r0
	MESSAGE		.req	r1
	MESSAGE_LEN	.req	r2
	HASH		.req	r3

	PASS0_SUMS	.req	q0
	PASS0_SUM_A	.req	d0
	PASS0_SUM_B	.req	d1
	PASS1_SUMS	.req	q1
	PASS1_SUM_A	.req	d2
	PASS1_SUM_B	.req	d3
	PASS2_SUMS	.req	q2
	PASS2_SUM_A	.req	d4
	PASS2_SUM_B	.req	d5
	PASS3_SUMS	.req	q3
	PASS3_SUM_A	.req	d6
	PASS3_SUM_B	.req	d7
	K0		.req	q4
	K1		.req	q5
	K2		.req	q6
	K3		.req	q7
	T0		.req	q8
	T0_L		.req	d16
	T0_H		.req	d17
	T1		.req	q9
	T1_L		.req	d18
	T1_H		.req	d19
	T2		.req	q10
	T2_L		.req	d20
	T2_H		.req	d21
	T3		.req	q11
	T3_L		.req	d22
	T3_H		.req	d23

.macro _nh_stride	k0, k1, k2, k3

	// Load next message stride
	vld1.8		{T3}, [MESSAGE]!

	// Load next key stride
	vld1.32		{\k3}, [KEY]!

	// Add message words to key words
	vadd.u32	T0, T3, \k0
	vadd.u32	T1, T3, \k1
	vadd.u32	T2, T3, \k2
	vadd.u32	T3, T3, \k3

	// Multiply 32x32 => 64 and accumulate
	vmlal.u32	PASS0_SUMS, T0_L, T0_H
	vmlal.u32	PASS1_SUMS, T1_L, T1_H
	vmlal.u32	PASS2_SUMS, T2_L, T2_H
	vmlal.u32	PASS3_SUMS, T3_L, T3_H
.endm

/*
 * void nh_neon(const u32 *key, const u8 *message, size_t message_len,
 *		u8 hash[NH_HASH_BYTES])
 *
 * It's guaranteed that message_len % 16 == 0.
 */
ENTRY(nh_neon)

	vld1.32		{K0,K1}, [KEY]!
	  vmov.u64	PASS0_SUMS, #0
	  vmov.u64	PASS1_SUMS, #0
	vld1.32		{K2}, [KEY]!
	  vmov.u64	PASS2_SUMS, #0
	  vmov.u64	PASS3_SUMS, #0

	subs		MESSAGE_LEN, MESSAGE_LEN, #64
	blt		.Lloop4_done
.Lloop4:
	_nh_stride	K0, K1, K2, K3
	_nh_stride	K1, K2, K3, K0
	_nh_stride	K2, K3, K0, K1
	_nh_stride	K3, K0, K1, K2
	subs		MESSAGE_LEN, MESSAGE_LEN, #64
	bge		.Lloop4

.Lloop4_done:
	ands		MESSAGE_LEN, MESSAGE_LEN, #63
	beq		.Ldone
	_nh_stride	K0, K1, K2, K3

	subs		MESSAGE_LEN, MESSAGE_LEN, #16
	beq		.Ldone
	_nh_stride	K1, K2, K3, K0

	subs		MESSAGE_LEN, MESSAGE_LEN, #16
	beq		.Ldone
	_nh_stride	K2, K3, K0, K1

.Ldone:
	// Sum the accumulators for each pass, then store the sums to 'hash'
	vadd.u64	T0_L, PASS0_SUM_A, PASS0_SUM_B
	vadd.u64	T0_H, PASS1_SUM_A, PASS1_SUM_B
	vadd.u64	T1_L, PASS2_SUM_A, PASS2_SUM_B
	vadd.u64	T1_H, PASS3_SUM_A, PASS3_SUM_B
	vst1.8		{T0-T1}, [HASH]
	bx		lr
ENDPROC(nh_neon)
