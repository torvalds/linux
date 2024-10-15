/* SPDX-License-Identifier: GPL-2.0 */
/*
 * NH - ε-almost-universal hash function, x86_64 AVX2 accelerated
 *
 * Copyright 2018 Google LLC
 *
 * Author: Eric Biggers <ebiggers@google.com>
 */

#include <linux/linkage.h>

#define		PASS0_SUMS	%ymm0
#define		PASS1_SUMS	%ymm1
#define		PASS2_SUMS	%ymm2
#define		PASS3_SUMS	%ymm3
#define		K0		%ymm4
#define		K0_XMM		%xmm4
#define		K1		%ymm5
#define		K1_XMM		%xmm5
#define		K2		%ymm6
#define		K2_XMM		%xmm6
#define		K3		%ymm7
#define		K3_XMM		%xmm7
#define		T0		%ymm8
#define		T1		%ymm9
#define		T2		%ymm10
#define		T2_XMM		%xmm10
#define		T3		%ymm11
#define		T3_XMM		%xmm11
#define		T4		%ymm12
#define		T5		%ymm13
#define		T6		%ymm14
#define		T7		%ymm15
#define		KEY		%rdi
#define		MESSAGE		%rsi
#define		MESSAGE_LEN	%rdx
#define		HASH		%rcx

.macro _nh_2xstride	k0, k1, k2, k3

	// Add message words to key words
	vpaddd		\k0, T3, T0
	vpaddd		\k1, T3, T1
	vpaddd		\k2, T3, T2
	vpaddd		\k3, T3, T3

	// Multiply 32x32 => 64 and accumulate
	vpshufd		$0x10, T0, T4
	vpshufd		$0x32, T0, T0
	vpshufd		$0x10, T1, T5
	vpshufd		$0x32, T1, T1
	vpshufd		$0x10, T2, T6
	vpshufd		$0x32, T2, T2
	vpshufd		$0x10, T3, T7
	vpshufd		$0x32, T3, T3
	vpmuludq	T4, T0, T0
	vpmuludq	T5, T1, T1
	vpmuludq	T6, T2, T2
	vpmuludq	T7, T3, T3
	vpaddq		T0, PASS0_SUMS, PASS0_SUMS
	vpaddq		T1, PASS1_SUMS, PASS1_SUMS
	vpaddq		T2, PASS2_SUMS, PASS2_SUMS
	vpaddq		T3, PASS3_SUMS, PASS3_SUMS
.endm

/*
 * void nh_avx2(const u32 *key, const u8 *message, size_t message_len,
 *		u8 hash[NH_HASH_BYTES])
 *
 * It's guaranteed that message_len % 16 == 0.
 */
SYM_FUNC_START(nh_avx2)

	vmovdqu		0x00(KEY), K0
	vmovdqu		0x10(KEY), K1
	add		$0x20, KEY
	vpxor		PASS0_SUMS, PASS0_SUMS, PASS0_SUMS
	vpxor		PASS1_SUMS, PASS1_SUMS, PASS1_SUMS
	vpxor		PASS2_SUMS, PASS2_SUMS, PASS2_SUMS
	vpxor		PASS3_SUMS, PASS3_SUMS, PASS3_SUMS

	sub		$0x40, MESSAGE_LEN
	jl		.Lloop4_done
.Lloop4:
	vmovdqu		(MESSAGE), T3
	vmovdqu		0x00(KEY), K2
	vmovdqu		0x10(KEY), K3
	_nh_2xstride	K0, K1, K2, K3

	vmovdqu		0x20(MESSAGE), T3
	vmovdqu		0x20(KEY), K0
	vmovdqu		0x30(KEY), K1
	_nh_2xstride	K2, K3, K0, K1

	add		$0x40, MESSAGE
	add		$0x40, KEY
	sub		$0x40, MESSAGE_LEN
	jge		.Lloop4

.Lloop4_done:
	and		$0x3f, MESSAGE_LEN
	jz		.Ldone

	cmp		$0x20, MESSAGE_LEN
	jl		.Llast

	// 2 or 3 strides remain; do 2 more.
	vmovdqu		(MESSAGE), T3
	vmovdqu		0x00(KEY), K2
	vmovdqu		0x10(KEY), K3
	_nh_2xstride	K0, K1, K2, K3
	add		$0x20, MESSAGE
	add		$0x20, KEY
	sub		$0x20, MESSAGE_LEN
	jz		.Ldone
	vmovdqa		K2, K0
	vmovdqa		K3, K1
.Llast:
	// Last stride.  Zero the high 128 bits of the message and keys so they
	// don't affect the result when processing them like 2 strides.
	vmovdqu		(MESSAGE), T3_XMM
	vmovdqa		K0_XMM, K0_XMM
	vmovdqa		K1_XMM, K1_XMM
	vmovdqu		0x00(KEY), K2_XMM
	vmovdqu		0x10(KEY), K3_XMM
	_nh_2xstride	K0, K1, K2, K3

.Ldone:
	// Sum the accumulators for each pass, then store the sums to 'hash'

	// PASS0_SUMS is (0A 0B 0C 0D)
	// PASS1_SUMS is (1A 1B 1C 1D)
	// PASS2_SUMS is (2A 2B 2C 2D)
	// PASS3_SUMS is (3A 3B 3C 3D)
	// We need the horizontal sums:
	//     (0A + 0B + 0C + 0D,
	//	1A + 1B + 1C + 1D,
	//	2A + 2B + 2C + 2D,
	//	3A + 3B + 3C + 3D)
	//

	vpunpcklqdq	PASS1_SUMS, PASS0_SUMS, T0	// T0 = (0A 1A 0C 1C)
	vpunpckhqdq	PASS1_SUMS, PASS0_SUMS, T1	// T1 = (0B 1B 0D 1D)
	vpunpcklqdq	PASS3_SUMS, PASS2_SUMS, T2	// T2 = (2A 3A 2C 3C)
	vpunpckhqdq	PASS3_SUMS, PASS2_SUMS, T3	// T3 = (2B 3B 2D 3D)

	vinserti128	$0x1, T2_XMM, T0, T4		// T4 = (0A 1A 2A 3A)
	vinserti128	$0x1, T3_XMM, T1, T5		// T5 = (0B 1B 2B 3B)
	vperm2i128	$0x31, T2, T0, T0		// T0 = (0C 1C 2C 3C)
	vperm2i128	$0x31, T3, T1, T1		// T1 = (0D 1D 2D 3D)

	vpaddq		T5, T4, T4
	vpaddq		T1, T0, T0
	vpaddq		T4, T0, T0
	vmovdqu		T0, (HASH)
	vzeroupper
	RET
SYM_FUNC_END(nh_avx2)
