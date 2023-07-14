/* SPDX-License-Identifier: GPL-2.0 */
/*
 * NH - ε-almost-universal hash function, ARM64 NEON accelerated version
 *
 * Copyright 2018 Google LLC
 *
 * Author: Eric Biggers <ebiggers@google.com>
 */

#include <linux/linkage.h>
#include <linux/cfi_types.h>

	KEY		.req	x0
	MESSAGE		.req	x1
	MESSAGE_LEN	.req	x2
	HASH		.req	x3

	PASS0_SUMS	.req	v0
	PASS1_SUMS	.req	v1
	PASS2_SUMS	.req	v2
	PASS3_SUMS	.req	v3
	K0		.req	v4
	K1		.req	v5
	K2		.req	v6
	K3		.req	v7
	T0		.req	v8
	T1		.req	v9
	T2		.req	v10
	T3		.req	v11
	T4		.req	v12
	T5		.req	v13
	T6		.req	v14
	T7		.req	v15

.macro _nh_stride	k0, k1, k2, k3

	// Load next message stride
	ld1		{T3.16b}, [MESSAGE], #16

	// Load next key stride
	ld1		{\k3\().4s}, [KEY], #16

	// Add message words to key words
	add		T0.4s, T3.4s, \k0\().4s
	add		T1.4s, T3.4s, \k1\().4s
	add		T2.4s, T3.4s, \k2\().4s
	add		T3.4s, T3.4s, \k3\().4s

	// Multiply 32x32 => 64 and accumulate
	mov		T4.d[0], T0.d[1]
	mov		T5.d[0], T1.d[1]
	mov		T6.d[0], T2.d[1]
	mov		T7.d[0], T3.d[1]
	umlal		PASS0_SUMS.2d, T0.2s, T4.2s
	umlal		PASS1_SUMS.2d, T1.2s, T5.2s
	umlal		PASS2_SUMS.2d, T2.2s, T6.2s
	umlal		PASS3_SUMS.2d, T3.2s, T7.2s
.endm

/*
 * void nh_neon(const u32 *key, const u8 *message, size_t message_len,
 *		__le64 hash[NH_NUM_PASSES])
 *
 * It's guaranteed that message_len % 16 == 0.
 */
SYM_TYPED_FUNC_START(nh_neon)

	ld1		{K0.4s,K1.4s}, [KEY], #32
	  movi		PASS0_SUMS.2d, #0
	  movi		PASS1_SUMS.2d, #0
	ld1		{K2.4s}, [KEY], #16
	  movi		PASS2_SUMS.2d, #0
	  movi		PASS3_SUMS.2d, #0

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
	addp		T0.2d, PASS0_SUMS.2d, PASS1_SUMS.2d
	addp		T1.2d, PASS2_SUMS.2d, PASS3_SUMS.2d
	st1		{T0.16b,T1.16b}, [HASH]
	ret
SYM_FUNC_END(nh_neon)
