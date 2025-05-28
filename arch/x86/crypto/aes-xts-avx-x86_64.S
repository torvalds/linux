/* SPDX-License-Identifier: Apache-2.0 OR BSD-2-Clause */
//
// AES-XTS for modern x86_64 CPUs
//
// Copyright 2024 Google LLC
//
// Author: Eric Biggers <ebiggers@google.com>
//
//------------------------------------------------------------------------------
//
// This file is dual-licensed, meaning that you can use it under your choice of
// either of the following two licenses:
//
// Licensed under the Apache License 2.0 (the "License").  You may obtain a copy
// of the License at
//
//	http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// or
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

/*
 * This file implements AES-XTS for modern x86_64 CPUs.  To handle the
 * complexities of coding for x86 SIMD, e.g. where every vector length needs
 * different code, it uses a macro to generate several implementations that
 * share similar source code but are targeted at different CPUs, listed below:
 *
 * AES-NI + AVX
 *    - 128-bit vectors (1 AES block per vector)
 *    - VEX-coded instructions
 *    - xmm0-xmm15
 *    - This is for older CPUs that lack VAES but do have AVX.
 *
 * VAES + VPCLMULQDQ + AVX2
 *    - 256-bit vectors (2 AES blocks per vector)
 *    - VEX-coded instructions
 *    - ymm0-ymm15
 *    - This is for CPUs that have VAES but lack AVX512 or AVX10,
 *      e.g. Intel's Alder Lake and AMD's Zen 3.
 *
 * VAES + VPCLMULQDQ + AVX10/256 + BMI2
 *    - 256-bit vectors (2 AES blocks per vector)
 *    - EVEX-coded instructions
 *    - ymm0-ymm31
 *    - This is for CPUs that have AVX512 but where using zmm registers causes
 *      downclocking, and for CPUs that have AVX10/256 but not AVX10/512.
 *    - By "AVX10/256" we really mean (AVX512BW + AVX512VL) || AVX10/256.
 *      To avoid confusion with 512-bit, we just write AVX10/256.
 *
 * VAES + VPCLMULQDQ + AVX10/512 + BMI2
 *    - Same as the previous one, but upgrades to 512-bit vectors
 *      (4 AES blocks per vector) in zmm0-zmm31.
 *    - This is for CPUs that have good AVX512 or AVX10/512 support.
 *
 * This file doesn't have an implementation for AES-NI alone (without AVX), as
 * the lack of VEX would make all the assembly code different.
 *
 * When we use VAES, we also use VPCLMULQDQ to parallelize the computation of
 * the XTS tweaks.  This avoids a bottleneck.  Currently there don't seem to be
 * any CPUs that support VAES but not VPCLMULQDQ.  If that changes, we might
 * need to start also providing an implementation using VAES alone.
 *
 * The AES-XTS implementations in this file support everything required by the
 * crypto API, including support for arbitrary input lengths and multi-part
 * processing.  However, they are most heavily optimized for the common case of
 * power-of-2 length inputs that are processed in a single part (disk sectors).
 */

#include <linux/linkage.h>
#include <linux/cfi_types.h>

.section .rodata
.p2align 4
.Lgf_poly:
	// The low 64 bits of this value represent the polynomial x^7 + x^2 + x
	// + 1.  It is the value that must be XOR'd into the low 64 bits of the
	// tweak each time a 1 is carried out of the high 64 bits.
	//
	// The high 64 bits of this value is just the internal carry bit that
	// exists when there's a carry out of the low 64 bits of the tweak.
	.quad	0x87, 1

	// This table contains constants for vpshufb and vpblendvb, used to
	// handle variable byte shifts and blending during ciphertext stealing
	// on CPUs that don't support AVX10-style masking.
.Lcts_permute_table:
	.byte	0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80
	.byte	0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80
	.byte	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07
	.byte	0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
	.byte	0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80
	.byte	0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80
.text

.macro	_define_Vi	i
.if VL == 16
	.set	V\i,		%xmm\i
.elseif VL == 32
	.set	V\i,		%ymm\i
.elseif VL == 64
	.set	V\i,		%zmm\i
.else
	.error "Unsupported Vector Length (VL)"
.endif
.endm

.macro _define_aliases
	// Define register aliases V0-V15, or V0-V31 if all 32 SIMD registers
	// are available, that map to the xmm, ymm, or zmm registers according
	// to the selected Vector Length (VL).
.irp i, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
	_define_Vi	\i
.endr
.if USE_AVX10
.irp i, 16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
	_define_Vi	\i
.endr
.endif

	// Function parameters
	.set	KEY,		%rdi	// Initially points to crypto_aes_ctx, then is
					// advanced to point to 7th-from-last round key
	.set	SRC,		%rsi	// Pointer to next source data
	.set	DST,		%rdx	// Pointer to next destination data
	.set	LEN,		%ecx	// Remaining length in bytes
	.set	LEN8,		%cl
	.set	LEN64,		%rcx
	.set	TWEAK,		%r8	// Pointer to next tweak

	// %rax holds the AES key length in bytes.
	.set	KEYLEN,		%eax
	.set	KEYLEN64,	%rax

	// %r9-r11 are available as temporaries.

	// V0-V3 hold the data blocks during the main loop, or temporary values
	// otherwise.  V4-V5 hold temporary values.

	// V6-V9 hold XTS tweaks.  Each 128-bit lane holds one tweak.
	.set	TWEAK0_XMM,	%xmm6
	.set	TWEAK0,		V6
	.set	TWEAK1_XMM,	%xmm7
	.set	TWEAK1,		V7
	.set	TWEAK2,		V8
	.set	TWEAK3,		V9

	// V10-V13 are used for computing the next values of TWEAK[0-3].
	.set	NEXT_TWEAK0,	V10
	.set	NEXT_TWEAK1,	V11
	.set	NEXT_TWEAK2,	V12
	.set	NEXT_TWEAK3,	V13

	// V14 holds the constant from .Lgf_poly, copied to all 128-bit lanes.
	.set	GF_POLY_XMM,	%xmm14
	.set	GF_POLY,	V14

	// V15 holds the key for AES "round 0", copied to all 128-bit lanes.
	.set	KEY0_XMM,	%xmm15
	.set	KEY0,		V15

	// If 32 SIMD registers are available, then V16-V29 hold the remaining
	// AES round keys, copied to all 128-bit lanes.
	//
	// AES-128, AES-192, and AES-256 use different numbers of round keys.
	// To allow handling all three variants efficiently, we align the round
	// keys to the *end* of this register range.  I.e., AES-128 uses
	// KEY5-KEY14, AES-192 uses KEY3-KEY14, and AES-256 uses KEY1-KEY14.
	// (All also use KEY0 for the XOR-only "round" at the beginning.)
.if USE_AVX10
	.set	KEY1_XMM,	%xmm16
	.set	KEY1,		V16
	.set	KEY2_XMM,	%xmm17
	.set	KEY2,		V17
	.set	KEY3_XMM,	%xmm18
	.set	KEY3,		V18
	.set	KEY4_XMM,	%xmm19
	.set	KEY4,		V19
	.set	KEY5_XMM,	%xmm20
	.set	KEY5,		V20
	.set	KEY6_XMM,	%xmm21
	.set	KEY6,		V21
	.set	KEY7_XMM,	%xmm22
	.set	KEY7,		V22
	.set	KEY8_XMM,	%xmm23
	.set	KEY8,		V23
	.set	KEY9_XMM,	%xmm24
	.set	KEY9,		V24
	.set	KEY10_XMM,	%xmm25
	.set	KEY10,		V25
	.set	KEY11_XMM,	%xmm26
	.set	KEY11,		V26
	.set	KEY12_XMM,	%xmm27
	.set	KEY12,		V27
	.set	KEY13_XMM,	%xmm28
	.set	KEY13,		V28
	.set	KEY14_XMM,	%xmm29
	.set	KEY14,		V29
.endif
	// V30-V31 are currently unused.
.endm

// Move a vector between memory and a register.
// The register operand must be in the first 16 vector registers.
.macro	_vmovdqu	src, dst
.if VL < 64
	vmovdqu		\src, \dst
.else
	vmovdqu8	\src, \dst
.endif
.endm

// Broadcast a 128-bit value into a vector.
.macro	_vbroadcast128	src, dst
.if VL == 16 && !USE_AVX10
	vmovdqu		\src, \dst
.elseif VL == 32 && !USE_AVX10
	vbroadcasti128	\src, \dst
.else
	vbroadcasti32x4	\src, \dst
.endif
.endm

// XOR two vectors together.
// Any register operands must be in the first 16 vector registers.
.macro	_vpxor	src1, src2, dst
.if VL < 64
	vpxor		\src1, \src2, \dst
.else
	vpxord		\src1, \src2, \dst
.endif
.endm

// XOR three vectors together.
.macro	_xor3	src1, src2, src3_and_dst
.if USE_AVX10
	// vpternlogd with immediate 0x96 is a three-argument XOR.
	vpternlogd	$0x96, \src1, \src2, \src3_and_dst
.else
	vpxor		\src1, \src3_and_dst, \src3_and_dst
	vpxor		\src2, \src3_and_dst, \src3_and_dst
.endif
.endm

// Given a 128-bit XTS tweak in the xmm register \src, compute the next tweak
// (by multiplying by the polynomial 'x') and write it to \dst.
.macro	_next_tweak	src, tmp, dst
	vpshufd		$0x13, \src, \tmp
	vpaddq		\src, \src, \dst
	vpsrad		$31, \tmp, \tmp
.if USE_AVX10
	vpternlogd	$0x78, GF_POLY_XMM, \tmp, \dst
.else
	vpand		GF_POLY_XMM, \tmp, \tmp
	vpxor		\tmp, \dst, \dst
.endif
.endm

// Given the XTS tweak(s) in the vector \src, compute the next vector of
// tweak(s) (by multiplying by the polynomial 'x^(VL/16)') and write it to \dst.
//
// If VL > 16, then there are multiple tweaks, and we use vpclmulqdq to compute
// all tweaks in the vector in parallel.  If VL=16, we just do the regular
// computation without vpclmulqdq, as it's the faster method for a single tweak.
.macro	_next_tweakvec	src, tmp1, tmp2, dst
.if VL == 16
	_next_tweak	\src, \tmp1, \dst
.else
	vpsrlq		$64 - VL/16, \src, \tmp1
	vpclmulqdq	$0x01, GF_POLY, \tmp1, \tmp2
	vpslldq		$8, \tmp1, \tmp1
	vpsllq		$VL/16, \src, \dst
	_xor3		\tmp1, \tmp2, \dst
.endif
.endm

// Given the first XTS tweak at (TWEAK), compute the first set of tweaks and
// store them in the vector registers TWEAK0-TWEAK3.  Clobbers V0-V5.
.macro	_compute_first_set_of_tweaks
	vmovdqu		(TWEAK), TWEAK0_XMM
	_vbroadcast128	.Lgf_poly(%rip), GF_POLY
.if VL == 16
	// With VL=16, multiplying by x serially is fastest.
	_next_tweak	TWEAK0, %xmm0, TWEAK1
	_next_tweak	TWEAK1, %xmm0, TWEAK2
	_next_tweak	TWEAK2, %xmm0, TWEAK3
.else
.if VL == 32
	// Compute the second block of TWEAK0.
	_next_tweak	TWEAK0_XMM, %xmm0, %xmm1
	vinserti128	$1, %xmm1, TWEAK0, TWEAK0
.elseif VL == 64
	// Compute the remaining blocks of TWEAK0.
	_next_tweak	TWEAK0_XMM, %xmm0, %xmm1
	_next_tweak	%xmm1, %xmm0, %xmm2
	_next_tweak	%xmm2, %xmm0, %xmm3
	vinserti32x4	$1, %xmm1, TWEAK0, TWEAK0
	vinserti32x4	$2, %xmm2, TWEAK0, TWEAK0
	vinserti32x4	$3, %xmm3, TWEAK0, TWEAK0
.endif
	// Compute TWEAK[1-3] from TWEAK0.
	vpsrlq		$64 - 1*VL/16, TWEAK0, V0
	vpsrlq		$64 - 2*VL/16, TWEAK0, V2
	vpsrlq		$64 - 3*VL/16, TWEAK0, V4
	vpclmulqdq	$0x01, GF_POLY, V0, V1
	vpclmulqdq	$0x01, GF_POLY, V2, V3
	vpclmulqdq	$0x01, GF_POLY, V4, V5
	vpslldq		$8, V0, V0
	vpslldq		$8, V2, V2
	vpslldq		$8, V4, V4
	vpsllq		$1*VL/16, TWEAK0, TWEAK1
	vpsllq		$2*VL/16, TWEAK0, TWEAK2
	vpsllq		$3*VL/16, TWEAK0, TWEAK3
.if USE_AVX10
	vpternlogd	$0x96, V0, V1, TWEAK1
	vpternlogd	$0x96, V2, V3, TWEAK2
	vpternlogd	$0x96, V4, V5, TWEAK3
.else
	vpxor		V0, TWEAK1, TWEAK1
	vpxor		V2, TWEAK2, TWEAK2
	vpxor		V4, TWEAK3, TWEAK3
	vpxor		V1, TWEAK1, TWEAK1
	vpxor		V3, TWEAK2, TWEAK2
	vpxor		V5, TWEAK3, TWEAK3
.endif
.endif
.endm

// Do one step in computing the next set of tweaks using the method of just
// multiplying by x repeatedly (the same method _next_tweak uses).
.macro	_tweak_step_mulx	i
.if \i == 0
	.set PREV_TWEAK, TWEAK3
	.set NEXT_TWEAK, NEXT_TWEAK0
.elseif \i == 5
	.set PREV_TWEAK, NEXT_TWEAK0
	.set NEXT_TWEAK, NEXT_TWEAK1
.elseif \i == 10
	.set PREV_TWEAK, NEXT_TWEAK1
	.set NEXT_TWEAK, NEXT_TWEAK2
.elseif \i == 15
	.set PREV_TWEAK, NEXT_TWEAK2
	.set NEXT_TWEAK, NEXT_TWEAK3
.endif
.if \i >= 0 && \i < 20 && \i % 5 == 0
	vpshufd		$0x13, PREV_TWEAK, V5
.elseif \i >= 0 && \i < 20 && \i % 5 == 1
	vpaddq		PREV_TWEAK, PREV_TWEAK, NEXT_TWEAK
.elseif \i >= 0 && \i < 20 && \i % 5 == 2
	vpsrad		$31, V5, V5
.elseif \i >= 0 && \i < 20 && \i % 5 == 3
	vpand		GF_POLY, V5, V5
.elseif \i >= 0 && \i < 20 && \i % 5 == 4
	vpxor		V5, NEXT_TWEAK, NEXT_TWEAK
.elseif \i == 1000
	vmovdqa		NEXT_TWEAK0, TWEAK0
	vmovdqa		NEXT_TWEAK1, TWEAK1
	vmovdqa		NEXT_TWEAK2, TWEAK2
	vmovdqa		NEXT_TWEAK3, TWEAK3
.endif
.endm

// Do one step in computing the next set of tweaks using the VPCLMULQDQ method
// (the same method _next_tweakvec uses for VL > 16).  This means multiplying
// each tweak by x^(4*VL/16) independently.
//
// Since 4*VL/16 is a multiple of 8 when VL > 16 (which it is here), the needed
// shift amounts are byte-aligned, which allows the use of vpsrldq and vpslldq
// to do 128-bit wide shifts.  The 128-bit left shift (vpslldq) saves
// instructions directly.  The 128-bit right shift (vpsrldq) performs better
// than a 64-bit right shift on Intel CPUs in the context where it is used here,
// because it runs on a different execution port from the AES instructions.
.macro	_tweak_step_pclmul	i
.if \i == 0
	vpsrldq		$(128 - 4*VL/16) / 8, TWEAK0, NEXT_TWEAK0
.elseif \i == 2
	vpsrldq		$(128 - 4*VL/16) / 8, TWEAK1, NEXT_TWEAK1
.elseif \i == 4
	vpsrldq		$(128 - 4*VL/16) / 8, TWEAK2, NEXT_TWEAK2
.elseif \i == 6
	vpsrldq		$(128 - 4*VL/16) / 8, TWEAK3, NEXT_TWEAK3
.elseif \i == 8
	vpclmulqdq	$0x00, GF_POLY, NEXT_TWEAK0, NEXT_TWEAK0
.elseif \i == 10
	vpclmulqdq	$0x00, GF_POLY, NEXT_TWEAK1, NEXT_TWEAK1
.elseif \i == 12
	vpclmulqdq	$0x00, GF_POLY, NEXT_TWEAK2, NEXT_TWEAK2
.elseif \i == 14
	vpclmulqdq	$0x00, GF_POLY, NEXT_TWEAK3, NEXT_TWEAK3
.elseif \i == 1000
	vpslldq		$(4*VL/16) / 8, TWEAK0, TWEAK0
	vpslldq		$(4*VL/16) / 8, TWEAK1, TWEAK1
	vpslldq		$(4*VL/16) / 8, TWEAK2, TWEAK2
	vpslldq		$(4*VL/16) / 8, TWEAK3, TWEAK3
	_vpxor		NEXT_TWEAK0, TWEAK0, TWEAK0
	_vpxor		NEXT_TWEAK1, TWEAK1, TWEAK1
	_vpxor		NEXT_TWEAK2, TWEAK2, TWEAK2
	_vpxor		NEXT_TWEAK3, TWEAK3, TWEAK3
.endif
.endm

// _tweak_step does one step of the computation of the next set of tweaks from
// TWEAK[0-3].  To complete all steps, this is invoked with increasing values of
// \i that include at least 0 through 19, then 1000 which signals the last step.
//
// This is used to interleave the computation of the next set of tweaks with the
// AES en/decryptions, which increases performance in some cases.  Clobbers V5.
.macro	_tweak_step	i
.if VL == 16
	_tweak_step_mulx	\i
.else
	_tweak_step_pclmul	\i
.endif
.endm

.macro	_setup_round_keys	enc

	// Select either the encryption round keys or the decryption round keys.
.if \enc
	.set	OFFS, 0
.else
	.set	OFFS, 240
.endif

	// Load the round key for "round 0".
	_vbroadcast128	OFFS(KEY), KEY0

	// Increment KEY to make it so that 7*16(KEY) is the last round key.
	// For AES-128, increment by 3*16, resulting in the 10 round keys (not
	// counting the zero-th round key which was just loaded into KEY0) being
	// -2*16(KEY) through 7*16(KEY).  For AES-192, increment by 5*16 and use
	// 12 round keys -4*16(KEY) through 7*16(KEY).  For AES-256, increment
	// by 7*16 and use 14 round keys -6*16(KEY) through 7*16(KEY).
	//
	// This rebasing provides two benefits.  First, it makes the offset to
	// any round key be in the range [-96, 112], fitting in a signed byte.
	// This shortens VEX-encoded instructions that access the later round
	// keys which otherwise would need 4-byte offsets.  Second, it makes it
	// easy to do AES-128 and AES-192 by skipping irrelevant rounds at the
	// beginning.  Skipping rounds at the end doesn't work as well because
	// the last round needs different instructions.
	//
	// An alternative approach would be to roll up all the round loops.  We
	// don't do that because (a) it isn't compatible with caching the round
	// keys in registers which we do when possible (see below), (b) we
	// interleave the AES rounds with the XTS tweak computation, and (c) it
	// seems unwise to rely *too* heavily on the CPU's branch predictor.
	lea		OFFS-16(KEY, KEYLEN64, 4), KEY

	// If all 32 SIMD registers are available, cache all the round keys.
.if USE_AVX10
	cmp		$24, KEYLEN
	jl		.Laes128\@
	je		.Laes192\@
	_vbroadcast128	-6*16(KEY), KEY1
	_vbroadcast128	-5*16(KEY), KEY2
.Laes192\@:
	_vbroadcast128	-4*16(KEY), KEY3
	_vbroadcast128	-3*16(KEY), KEY4
.Laes128\@:
	_vbroadcast128	-2*16(KEY), KEY5
	_vbroadcast128	-1*16(KEY), KEY6
	_vbroadcast128	0*16(KEY), KEY7
	_vbroadcast128	1*16(KEY), KEY8
	_vbroadcast128	2*16(KEY), KEY9
	_vbroadcast128	3*16(KEY), KEY10
	_vbroadcast128	4*16(KEY), KEY11
	_vbroadcast128	5*16(KEY), KEY12
	_vbroadcast128	6*16(KEY), KEY13
	_vbroadcast128	7*16(KEY), KEY14
.endif
.endm

// Do a single non-last round of AES encryption (if \enc==1) or decryption (if
// \enc==0) on the block(s) in \data using the round key(s) in \key.  The
// register length determines the number of AES blocks en/decrypted.
.macro	_vaes	enc, key, data
.if \enc
	vaesenc		\key, \data, \data
.else
	vaesdec		\key, \data, \data
.endif
.endm

// Same as _vaes, but does the last round.
.macro	_vaeslast	enc, key, data
.if \enc
	vaesenclast	\key, \data, \data
.else
	vaesdeclast	\key, \data, \data
.endif
.endm

// Do a single non-last round of AES en/decryption on the block(s) in \data,
// using the same key for all block(s).  The round key is loaded from the
// appropriate register or memory location for round \i.  May clobber \tmp.
.macro _vaes_1x		enc, i, xmm_suffix, data, tmp
.if USE_AVX10
	_vaes		\enc, KEY\i\xmm_suffix, \data
.else
.ifnb \xmm_suffix
	_vaes		\enc, (\i-7)*16(KEY), \data
.else
	_vbroadcast128	(\i-7)*16(KEY), \tmp
	_vaes		\enc, \tmp, \data
.endif
.endif
.endm

// Do a single non-last round of AES en/decryption on the blocks in registers
// V0-V3, using the same key for all blocks.  The round key is loaded from the
// appropriate register or memory location for round \i.  In addition, does two
// steps of the computation of the next set of tweaks.  May clobber V4 and V5.
.macro	_vaes_4x	enc, i
.if USE_AVX10
	_tweak_step	(2*(\i-5))
	_vaes		\enc, KEY\i, V0
	_vaes		\enc, KEY\i, V1
	_tweak_step	(2*(\i-5) + 1)
	_vaes		\enc, KEY\i, V2
	_vaes		\enc, KEY\i, V3
.else
	_vbroadcast128	(\i-7)*16(KEY), V4
	_tweak_step	(2*(\i-5))
	_vaes		\enc, V4, V0
	_vaes		\enc, V4, V1
	_tweak_step	(2*(\i-5) + 1)
	_vaes		\enc, V4, V2
	_vaes		\enc, V4, V3
.endif
.endm

// Do tweaked AES en/decryption (i.e., XOR with \tweak, then AES en/decrypt,
// then XOR with \tweak again) of the block(s) in \data.  To process a single
// block, use xmm registers and set \xmm_suffix=_XMM.  To process a vector of
// length VL, use V* registers and leave \xmm_suffix empty.  Clobbers \tmp.
.macro	_aes_crypt	enc, xmm_suffix, tweak, data, tmp
	_xor3		KEY0\xmm_suffix, \tweak, \data
	cmp		$24, KEYLEN
	jl		.Laes128\@
	je		.Laes192\@
	_vaes_1x	\enc, 1, \xmm_suffix, \data, tmp=\tmp
	_vaes_1x	\enc, 2, \xmm_suffix, \data, tmp=\tmp
.Laes192\@:
	_vaes_1x	\enc, 3, \xmm_suffix, \data, tmp=\tmp
	_vaes_1x	\enc, 4, \xmm_suffix, \data, tmp=\tmp
.Laes128\@:
.irp i, 5,6,7,8,9,10,11,12,13
	_vaes_1x	\enc, \i, \xmm_suffix, \data, tmp=\tmp
.endr
.if USE_AVX10
	vpxord		KEY14\xmm_suffix, \tweak, \tmp
.else
.ifnb \xmm_suffix
	vpxor		7*16(KEY), \tweak, \tmp
.else
	_vbroadcast128	7*16(KEY), \tmp
	vpxor		\tweak, \tmp, \tmp
.endif
.endif
	_vaeslast	\enc, \tmp, \data
.endm

.macro	_aes_xts_crypt	enc
	_define_aliases

.if !\enc
	// When decrypting a message whose length isn't a multiple of the AES
	// block length, exclude the last full block from the main loop by
	// subtracting 16 from LEN.  This is needed because ciphertext stealing
	// decryption uses the last two tweaks in reverse order.  We'll handle
	// the last full block and the partial block specially at the end.
	lea		-16(LEN), %eax
	test		$15, LEN8
	cmovnz		%eax, LEN
.endif

	// Load the AES key length: 16 (AES-128), 24 (AES-192), or 32 (AES-256).
	movl		480(KEY), KEYLEN

	// Setup the pointer to the round keys and cache as many as possible.
	_setup_round_keys	\enc

	// Compute the first set of tweaks TWEAK[0-3].
	_compute_first_set_of_tweaks

	add		$-4*VL, LEN  // shorter than 'sub 4*VL' when VL=32
	jl		.Lhandle_remainder\@

.Lmain_loop\@:
	// This is the main loop, en/decrypting 4*VL bytes per iteration.

	// XOR each source block with its tweak and the zero-th round key.
.if USE_AVX10
	_vmovdqu	0*VL(SRC), V0
	_vmovdqu	1*VL(SRC), V1
	_vmovdqu	2*VL(SRC), V2
	_vmovdqu	3*VL(SRC), V3
	vpternlogd	$0x96, TWEAK0, KEY0, V0
	vpternlogd	$0x96, TWEAK1, KEY0, V1
	vpternlogd	$0x96, TWEAK2, KEY0, V2
	vpternlogd	$0x96, TWEAK3, KEY0, V3
.else
	vpxor		0*VL(SRC), KEY0, V0
	vpxor		1*VL(SRC), KEY0, V1
	vpxor		2*VL(SRC), KEY0, V2
	vpxor		3*VL(SRC), KEY0, V3
	vpxor		TWEAK0, V0, V0
	vpxor		TWEAK1, V1, V1
	vpxor		TWEAK2, V2, V2
	vpxor		TWEAK3, V3, V3
.endif
	cmp		$24, KEYLEN
	jl		.Laes128\@
	je		.Laes192\@
	// Do all the AES rounds on the data blocks, interleaved with
	// the computation of the next set of tweaks.
	_vaes_4x	\enc, 1
	_vaes_4x	\enc, 2
.Laes192\@:
	_vaes_4x	\enc, 3
	_vaes_4x	\enc, 4
.Laes128\@:
.irp i, 5,6,7,8,9,10,11,12,13
	_vaes_4x	\enc, \i
.endr
	// Do the last AES round, then XOR the results with the tweaks again.
	// Reduce latency by doing the XOR before the vaesenclast, utilizing the
	// property vaesenclast(key, a) ^ b == vaesenclast(key ^ b, a)
	// (and likewise for vaesdeclast).
.if USE_AVX10
	_tweak_step	18
	_tweak_step	19
	vpxord		TWEAK0, KEY14, V4
	vpxord		TWEAK1, KEY14, V5
	_vaeslast	\enc, V4, V0
	_vaeslast	\enc, V5, V1
	vpxord		TWEAK2, KEY14, V4
	vpxord		TWEAK3, KEY14, V5
	_vaeslast	\enc, V4, V2
	_vaeslast	\enc, V5, V3
.else
	_vbroadcast128	7*16(KEY), V4
	_tweak_step	18 // uses V5
	_tweak_step	19 // uses V5
	vpxor		TWEAK0, V4, V5
	_vaeslast	\enc, V5, V0
	vpxor		TWEAK1, V4, V5
	_vaeslast	\enc, V5, V1
	vpxor		TWEAK2, V4, V5
	vpxor		TWEAK3, V4, V4
	_vaeslast	\enc, V5, V2
	_vaeslast	\enc, V4, V3
.endif

	// Store the destination blocks.
	_vmovdqu	V0, 0*VL(DST)
	_vmovdqu	V1, 1*VL(DST)
	_vmovdqu	V2, 2*VL(DST)
	_vmovdqu	V3, 3*VL(DST)

	// Finish computing the next set of tweaks.
	_tweak_step	1000

	sub		$-4*VL, SRC  // shorter than 'add 4*VL' when VL=32
	sub		$-4*VL, DST
	add		$-4*VL, LEN
	jge		.Lmain_loop\@

	// Check for the uncommon case where the data length isn't a multiple of
	// 4*VL.  Handle it out-of-line in order to optimize for the common
	// case.  In the common case, just fall through to the ret.
	test		$4*VL-1, LEN8
	jnz		.Lhandle_remainder\@
.Ldone\@:
	// Store the next tweak back to *TWEAK to support continuation calls.
	vmovdqu		TWEAK0_XMM, (TWEAK)
.if VL > 16
	vzeroupper
.endif
	RET

.Lhandle_remainder\@:

	// En/decrypt any remaining full blocks, one vector at a time.
.if VL > 16
	add		$3*VL, LEN	// Undo extra sub of 4*VL, then sub VL.
	jl		.Lvec_at_a_time_done\@
.Lvec_at_a_time\@:
	_vmovdqu	(SRC), V0
	_aes_crypt	\enc, , TWEAK0, V0, tmp=V1
	_vmovdqu	V0, (DST)
	_next_tweakvec	TWEAK0, V0, V1, TWEAK0
	add		$VL, SRC
	add		$VL, DST
	sub		$VL, LEN
	jge		.Lvec_at_a_time\@
.Lvec_at_a_time_done\@:
	add		$VL-16, LEN	// Undo extra sub of VL, then sub 16.
.else
	add		$4*VL-16, LEN	// Undo extra sub of 4*VL, then sub 16.
.endif

	// En/decrypt any remaining full blocks, one at a time.
	jl		.Lblock_at_a_time_done\@
.Lblock_at_a_time\@:
	vmovdqu		(SRC), %xmm0
	_aes_crypt	\enc, _XMM, TWEAK0_XMM, %xmm0, tmp=%xmm1
	vmovdqu		%xmm0, (DST)
	_next_tweak	TWEAK0_XMM, %xmm0, TWEAK0_XMM
	add		$16, SRC
	add		$16, DST
	sub		$16, LEN
	jge		.Lblock_at_a_time\@
.Lblock_at_a_time_done\@:
	add		$16, LEN	// Undo the extra sub of 16.
	// Now 0 <= LEN <= 15.  If LEN is zero, we're done.
	jz		.Ldone\@

	// Otherwise 1 <= LEN <= 15, but the real remaining length is 16 + LEN.
	// Do ciphertext stealing to process the last 16 + LEN bytes.

.if \enc
	// If encrypting, the main loop already encrypted the last full block to
	// create the CTS intermediate ciphertext.  Prepare for the rest of CTS
	// by rewinding the pointers and loading the intermediate ciphertext.
	sub		$16, SRC
	sub		$16, DST
	vmovdqu		(DST), %xmm0
.else
	// If decrypting, the main loop didn't decrypt the last full block
	// because CTS decryption uses the last two tweaks in reverse order.
	// Do it now by advancing the tweak and decrypting the last full block.
	_next_tweak	TWEAK0_XMM, %xmm0, TWEAK1_XMM
	vmovdqu		(SRC), %xmm0
	_aes_crypt	\enc, _XMM, TWEAK1_XMM, %xmm0, tmp=%xmm1
.endif

.if USE_AVX10
	// Create a mask that has the first LEN bits set.
	mov		$-1, %r9d
	bzhi		LEN, %r9d, %r9d
	kmovd		%r9d, %k1

	// Swap the first LEN bytes of the en/decryption of the last full block
	// with the partial block.  Note that to support in-place en/decryption,
	// the load from the src partial block must happen before the store to
	// the dst partial block.
	vmovdqa		%xmm0, %xmm1
	vmovdqu8	16(SRC), %xmm0{%k1}
	vmovdqu8	%xmm1, 16(DST){%k1}
.else
	lea		.Lcts_permute_table(%rip), %r9

	// Load the src partial block, left-aligned.  Note that to support
	// in-place en/decryption, this must happen before the store to the dst
	// partial block.
	vmovdqu		(SRC, LEN64, 1), %xmm1

	// Shift the first LEN bytes of the en/decryption of the last full block
	// to the end of a register, then store it to DST+LEN.  This stores the
	// dst partial block.  It also writes to the second part of the dst last
	// full block, but that part is overwritten later.
	vpshufb		(%r9, LEN64, 1), %xmm0, %xmm2
	vmovdqu		%xmm2, (DST, LEN64, 1)

	// Make xmm3 contain [16-LEN,16-LEN+1,...,14,15,0x80,0x80,...].
	sub		LEN64, %r9
	vmovdqu		32(%r9), %xmm3

	// Shift the src partial block to the beginning of its register.
	vpshufb		%xmm3, %xmm1, %xmm1

	// Do a blend to generate the src partial block followed by the second
	// part of the en/decryption of the last full block.
	vpblendvb	%xmm3, %xmm0, %xmm1, %xmm0
.endif
	// En/decrypt again and store the last full block.
	_aes_crypt	\enc, _XMM, TWEAK0_XMM, %xmm0, tmp=%xmm1
	vmovdqu		%xmm0, (DST)
	jmp		.Ldone\@
.endm

// void aes_xts_encrypt_iv(const struct crypto_aes_ctx *tweak_key,
//			   u8 iv[AES_BLOCK_SIZE]);
//
// Encrypt |iv| using the AES key |tweak_key| to get the first tweak.  Assumes
// that the CPU supports AES-NI and AVX, but not necessarily VAES or AVX10.
SYM_TYPED_FUNC_START(aes_xts_encrypt_iv)
	.set	TWEAK_KEY,	%rdi
	.set	IV,		%rsi
	.set	KEYLEN,		%eax
	.set	KEYLEN64,	%rax

	vmovdqu		(IV), %xmm0
	vpxor		(TWEAK_KEY), %xmm0, %xmm0
	movl		480(TWEAK_KEY), KEYLEN
	lea		-16(TWEAK_KEY, KEYLEN64, 4), TWEAK_KEY
	cmp		$24, KEYLEN
	jl		.Lencrypt_iv_aes128
	je		.Lencrypt_iv_aes192
	vaesenc		-6*16(TWEAK_KEY), %xmm0, %xmm0
	vaesenc		-5*16(TWEAK_KEY), %xmm0, %xmm0
.Lencrypt_iv_aes192:
	vaesenc		-4*16(TWEAK_KEY), %xmm0, %xmm0
	vaesenc		-3*16(TWEAK_KEY), %xmm0, %xmm0
.Lencrypt_iv_aes128:
.irp i, -2,-1,0,1,2,3,4,5,6
	vaesenc		\i*16(TWEAK_KEY), %xmm0, %xmm0
.endr
	vaesenclast	7*16(TWEAK_KEY), %xmm0, %xmm0
	vmovdqu		%xmm0, (IV)
	RET
SYM_FUNC_END(aes_xts_encrypt_iv)

// Below are the actual AES-XTS encryption and decryption functions,
// instantiated from the above macro.  They all have the following prototype:
//
// void (*xts_crypt_func)(const struct crypto_aes_ctx *key,
//			  const u8 *src, u8 *dst, int len,
//			  u8 tweak[AES_BLOCK_SIZE]);
//
// |key| is the data key.  |tweak| contains the next tweak; the encryption of
// the original IV with the tweak key was already done.  This function supports
// incremental computation, but |len| must always be >= 16 (AES_BLOCK_SIZE), and
// |len| must be a multiple of 16 except on the last call.  If |len| is a
// multiple of 16, then this function updates |tweak| to contain the next tweak.

.set	VL, 16
.set	USE_AVX10, 0
SYM_TYPED_FUNC_START(aes_xts_encrypt_aesni_avx)
	_aes_xts_crypt	1
SYM_FUNC_END(aes_xts_encrypt_aesni_avx)
SYM_TYPED_FUNC_START(aes_xts_decrypt_aesni_avx)
	_aes_xts_crypt	0
SYM_FUNC_END(aes_xts_decrypt_aesni_avx)

#if defined(CONFIG_AS_VAES) && defined(CONFIG_AS_VPCLMULQDQ)
.set	VL, 32
.set	USE_AVX10, 0
SYM_TYPED_FUNC_START(aes_xts_encrypt_vaes_avx2)
	_aes_xts_crypt	1
SYM_FUNC_END(aes_xts_encrypt_vaes_avx2)
SYM_TYPED_FUNC_START(aes_xts_decrypt_vaes_avx2)
	_aes_xts_crypt	0
SYM_FUNC_END(aes_xts_decrypt_vaes_avx2)

.set	VL, 32
.set	USE_AVX10, 1
SYM_TYPED_FUNC_START(aes_xts_encrypt_vaes_avx10_256)
	_aes_xts_crypt	1
SYM_FUNC_END(aes_xts_encrypt_vaes_avx10_256)
SYM_TYPED_FUNC_START(aes_xts_decrypt_vaes_avx10_256)
	_aes_xts_crypt	0
SYM_FUNC_END(aes_xts_decrypt_vaes_avx10_256)

.set	VL, 64
.set	USE_AVX10, 1
SYM_TYPED_FUNC_START(aes_xts_encrypt_vaes_avx10_512)
	_aes_xts_crypt	1
SYM_FUNC_END(aes_xts_encrypt_vaes_avx10_512)
SYM_TYPED_FUNC_START(aes_xts_decrypt_vaes_avx10_512)
	_aes_xts_crypt	0
SYM_FUNC_END(aes_xts_decrypt_vaes_avx10_512)
#endif /* CONFIG_AS_VAES && CONFIG_AS_VPCLMULQDQ */
