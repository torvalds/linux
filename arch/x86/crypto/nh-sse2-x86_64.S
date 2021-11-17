/* SPDX-License-Identifier: GPL-2.0 */
/*
 * NH - ε-almost-universal hash function, x86_64 SSE2 accelerated
 *
 * Copyright 2018 Google LLC
 *
 * Author: Eric Biggers <ebiggers@google.com>
 */

#include <linux/linkage.h>

#define		PASS0_SUMS	%xmm0
#define		PASS1_SUMS	%xmm1
#define		PASS2_SUMS	%xmm2
#define		PASS3_SUMS	%xmm3
#define		K0		%xmm4
#define		K1		%xmm5
#define		K2		%xmm6
#define		K3		%xmm7
#define		T0		%xmm8
#define		T1		%xmm9
#define		T2		%xmm10
#define		T3		%xmm11
#define		T4		%xmm12
#define		T5		%xmm13
#define		T6		%xmm14
#define		T7		%xmm15
#define		KEY		%rdi
#define		MESSAGE		%rsi
#define		MESSAGE_LEN	%rdx
#define		HASH		%rcx

.macro _nh_stride	k0, k1, k2, k3, offset

	// Load next message stride
	movdqu		\offset(MESSAGE), T1

	// Load next key stride
	movdqu		\offset(KEY), \k3

	// Add message words to key words
	movdqa		T1, T2
	movdqa		T1, T3
	paddd		T1, \k0    // reuse k0 to avoid a move
	paddd		\k1, T1
	paddd		\k2, T2
	paddd		\k3, T3

	// Multiply 32x32 => 64 and accumulate
	pshufd		$0x10, \k0, T4
	pshufd		$0x32, \k0, \k0
	pshufd		$0x10, T1, T5
	pshufd		$0x32, T1, T1
	pshufd		$0x10, T2, T6
	pshufd		$0x32, T2, T2
	pshufd		$0x10, T3, T7
	pshufd		$0x32, T3, T3
	pmuludq		T4, \k0
	pmuludq		T5, T1
	pmuludq		T6, T2
	pmuludq		T7, T3
	paddq		\k0, PASS0_SUMS
	paddq		T1, PASS1_SUMS
	paddq		T2, PASS2_SUMS
	paddq		T3, PASS3_SUMS
.endm

/*
 * void nh_sse2(const u32 *key, const u8 *message, size_t message_len,
 *		u8 hash[NH_HASH_BYTES])
 *
 * It's guaranteed that message_len % 16 == 0.
 */
SYM_FUNC_START(nh_sse2)

	movdqu		0x00(KEY), K0
	movdqu		0x10(KEY), K1
	movdqu		0x20(KEY), K2
	add		$0x30, KEY
	pxor		PASS0_SUMS, PASS0_SUMS
	pxor		PASS1_SUMS, PASS1_SUMS
	pxor		PASS2_SUMS, PASS2_SUMS
	pxor		PASS3_SUMS, PASS3_SUMS

	sub		$0x40, MESSAGE_LEN
	jl		.Lloop4_done
.Lloop4:
	_nh_stride	K0, K1, K2, K3, 0x00
	_nh_stride	K1, K2, K3, K0, 0x10
	_nh_stride	K2, K3, K0, K1, 0x20
	_nh_stride	K3, K0, K1, K2, 0x30
	add		$0x40, KEY
	add		$0x40, MESSAGE
	sub		$0x40, MESSAGE_LEN
	jge		.Lloop4

.Lloop4_done:
	and		$0x3f, MESSAGE_LEN
	jz		.Ldone
	_nh_stride	K0, K1, K2, K3, 0x00

	sub		$0x10, MESSAGE_LEN
	jz		.Ldone
	_nh_stride	K1, K2, K3, K0, 0x10

	sub		$0x10, MESSAGE_LEN
	jz		.Ldone
	_nh_stride	K2, K3, K0, K1, 0x20

.Ldone:
	// Sum the accumulators for each pass, then store the sums to 'hash'
	movdqa		PASS0_SUMS, T0
	movdqa		PASS2_SUMS, T1
	punpcklqdq	PASS1_SUMS, T0		// => (PASS0_SUM_A PASS1_SUM_A)
	punpcklqdq	PASS3_SUMS, T1		// => (PASS2_SUM_A PASS3_SUM_A)
	punpckhqdq	PASS1_SUMS, PASS0_SUMS	// => (PASS0_SUM_B PASS1_SUM_B)
	punpckhqdq	PASS3_SUMS, PASS2_SUMS	// => (PASS2_SUM_B PASS3_SUM_B)
	paddq		PASS0_SUMS, T0
	paddq		PASS2_SUMS, T1
	movdqu		T0, 0x00(HASH)
	movdqu		T1, 0x10(HASH)
	ret
SYM_FUNC_END(nh_sse2)
