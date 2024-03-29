/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * AES-XTS for modern x86_64 CPUs
 *
 * Copyright 2024 Google LLC
 *
 * Author: Eric Biggers <ebiggers@google.com>
 */

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

// Function parameters
.set	KEY,		%rdi	// Initially points to crypto_aes_ctx, then is
				// advanced to point directly to the round keys
.set	SRC,		%rsi	// Pointer to next source data
.set	DST,		%rdx	// Pointer to next destination data
.set	LEN,		%rcx	// Remaining length in bytes
.set	TWEAK,		%r8	// Pointer to next tweak

// %r9d holds the AES key length in bytes.
.set	KEYLEN,		%r9d

// %rax and %r10-r11 are available as temporaries.

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
	_define_Vi	0
	_define_Vi	1
	_define_Vi	2
	_define_Vi	3
	_define_Vi	4
	_define_Vi	5
	_define_Vi	6
	_define_Vi	7
	_define_Vi	8
	_define_Vi	9
	_define_Vi	10
	_define_Vi	11
	_define_Vi	12
	_define_Vi	13
	_define_Vi	14
	_define_Vi	15
.if USE_AVX10
	_define_Vi	16
	_define_Vi	17
	_define_Vi	18
	_define_Vi	19
	_define_Vi	20
	_define_Vi	21
	_define_Vi	22
	_define_Vi	23
	_define_Vi	24
	_define_Vi	25
	_define_Vi	26
	_define_Vi	27
	_define_Vi	28
	_define_Vi	29
	_define_Vi	30
	_define_Vi	31
.endif

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

	// V15 holds the first AES round key, copied to all 128-bit lanes.
	.set	KEY0_XMM,	%xmm15
	.set	KEY0,		V15

	// If 32 SIMD registers are available, then V16-V29 hold the remaining
	// AES round keys, copied to all 128-bit lanes.
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
.macro	_vpxor	src1, src2, dst
.if USE_AVX10
	vpxord		\src1, \src2, \dst
.else
	vpxor		\src1, \src2, \dst
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
	vpand		GF_POLY_XMM, \tmp, \tmp
	vpxor		\tmp, \dst, \dst
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
.if \i < 20 && \i % 5 == 0
	vpshufd		$0x13, PREV_TWEAK, V5
.elseif \i < 20 && \i % 5 == 1
	vpaddq		PREV_TWEAK, PREV_TWEAK, NEXT_TWEAK
.elseif \i < 20 && \i % 5 == 2
	vpsrad		$31, V5, V5
.elseif \i < 20 && \i % 5 == 3
	vpand		GF_POLY, V5, V5
.elseif \i < 20 && \i % 5 == 4
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
// each tweak by x^(4*VL/16) independently.  Since 4*VL/16 is a multiple of 8
// when VL > 16 (which it is here), the needed shift amounts are byte-aligned,
// which allows the use of vpsrldq and vpslldq to do 128-bit wide shifts.
.macro	_tweak_step_pclmul	i
.if \i == 2
	vpsrldq		$(128 - 4*VL/16) / 8, TWEAK0, NEXT_TWEAK0
.elseif \i == 4
	vpsrldq		$(128 - 4*VL/16) / 8, TWEAK1, NEXT_TWEAK1
.elseif \i == 6
	vpsrldq		$(128 - 4*VL/16) / 8, TWEAK2, NEXT_TWEAK2
.elseif \i == 8
	vpsrldq		$(128 - 4*VL/16) / 8, TWEAK3, NEXT_TWEAK3
.elseif \i == 10
	vpclmulqdq	$0x00, GF_POLY, NEXT_TWEAK0, NEXT_TWEAK0
.elseif \i == 12
	vpclmulqdq	$0x00, GF_POLY, NEXT_TWEAK1, NEXT_TWEAK1
.elseif \i == 14
	vpclmulqdq	$0x00, GF_POLY, NEXT_TWEAK2, NEXT_TWEAK2
.elseif \i == 16
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
// TWEAK[0-3].  To complete all steps, this must be invoked with \i values 0
// through at least 19, then 1000 which signals the last step.
//
// This is used to interleave the computation of the next set of tweaks with the
// AES en/decryptions, which increases performance in some cases.
.macro	_tweak_step	i
.if VL == 16
	_tweak_step_mulx	\i
.else
	_tweak_step_pclmul	\i
.endif
.endm

// Load the round keys: just the first one if !USE_AVX10, otherwise all of them.
.macro	_load_round_keys
	_vbroadcast128	0*16(KEY), KEY0
.if USE_AVX10
	_vbroadcast128	1*16(KEY), KEY1
	_vbroadcast128	2*16(KEY), KEY2
	_vbroadcast128	3*16(KEY), KEY3
	_vbroadcast128	4*16(KEY), KEY4
	_vbroadcast128	5*16(KEY), KEY5
	_vbroadcast128	6*16(KEY), KEY6
	_vbroadcast128	7*16(KEY), KEY7
	_vbroadcast128	8*16(KEY), KEY8
	_vbroadcast128	9*16(KEY), KEY9
	_vbroadcast128	10*16(KEY), KEY10
	// Note: if it's AES-128 or AES-192, the last several round keys won't
	// be used.  We do the loads anyway to save a conditional jump.
	_vbroadcast128	11*16(KEY), KEY11
	_vbroadcast128	12*16(KEY), KEY12
	_vbroadcast128	13*16(KEY), KEY13
	_vbroadcast128	14*16(KEY), KEY14
.endif
.endm

// Do a single round of AES encryption (if \enc==1) or decryption (if \enc==0)
// on the block(s) in \data using the round key(s) in \key.  The register length
// determines the number of AES blocks en/decrypted.
.macro	_vaes	enc, last, key, data
.if \enc
.if \last
	vaesenclast	\key, \data, \data
.else
	vaesenc		\key, \data, \data
.endif
.else
.if \last
	vaesdeclast	\key, \data, \data
.else
	vaesdec		\key, \data, \data
.endif
.endif
.endm

// Do a single round of AES en/decryption on the block(s) in \data, using the
// same key for all block(s).  The round key is loaded from the appropriate
// register or memory location for round \i.  May clobber V4.
.macro _vaes_1x		enc, last, i, xmm_suffix, data
.if USE_AVX10
	_vaes		\enc, \last, KEY\i\xmm_suffix, \data
.else
.ifnb \xmm_suffix
	_vaes		\enc, \last, \i*16(KEY), \data
.else
	_vbroadcast128	\i*16(KEY), V4
	_vaes		\enc, \last, V4, \data
.endif
.endif
.endm

// Do a single round of AES en/decryption on the blocks in registers V0-V3,
// using the same key for all blocks.  The round key is loaded from the
// appropriate register or memory location for round \i.  In addition, does step
// \i of the computation of the next set of tweaks.  May clobber V4.
.macro	_vaes_4x	enc, last, i
.if USE_AVX10
	_tweak_step	(2*(\i-1))
	_vaes		\enc, \last, KEY\i, V0
	_vaes		\enc, \last, KEY\i, V1
	_tweak_step	(2*(\i-1) + 1)
	_vaes		\enc, \last, KEY\i, V2
	_vaes		\enc, \last, KEY\i, V3
.else
	_vbroadcast128	\i*16(KEY), V4
	_tweak_step	(2*(\i-1))
	_vaes		\enc, \last, V4, V0
	_vaes		\enc, \last, V4, V1
	_tweak_step	(2*(\i-1) + 1)
	_vaes		\enc, \last, V4, V2
	_vaes		\enc, \last, V4, V3
.endif
.endm

// Do tweaked AES en/decryption (i.e., XOR with \tweak, then AES en/decrypt,
// then XOR with \tweak again) of the block(s) in \data.  To process a single
// block, use xmm registers and set \xmm_suffix=_XMM.  To process a vector of
// length VL, use V* registers and leave \xmm_suffix empty.  May clobber V4.
.macro	_aes_crypt	enc, xmm_suffix, tweak, data
	_xor3		KEY0\xmm_suffix, \tweak, \data
	_vaes_1x	\enc, 0, 1, \xmm_suffix, \data
	_vaes_1x	\enc, 0, 2, \xmm_suffix, \data
	_vaes_1x	\enc, 0, 3, \xmm_suffix, \data
	_vaes_1x	\enc, 0, 4, \xmm_suffix, \data
	_vaes_1x	\enc, 0, 5, \xmm_suffix, \data
	_vaes_1x	\enc, 0, 6, \xmm_suffix, \data
	_vaes_1x	\enc, 0, 7, \xmm_suffix, \data
	_vaes_1x	\enc, 0, 8, \xmm_suffix, \data
	_vaes_1x	\enc, 0, 9, \xmm_suffix, \data
	cmp		$24, KEYLEN
	jle		.Laes_128_or_192\@
	_vaes_1x	\enc, 0, 10, \xmm_suffix, \data
	_vaes_1x	\enc, 0, 11, \xmm_suffix, \data
	_vaes_1x	\enc, 0, 12, \xmm_suffix, \data
	_vaes_1x	\enc, 0, 13, \xmm_suffix, \data
	_vaes_1x	\enc, 1, 14, \xmm_suffix, \data
	jmp		.Laes_done\@
.Laes_128_or_192\@:
	je		.Laes_192\@
	_vaes_1x	\enc, 1, 10, \xmm_suffix, \data
	jmp		.Laes_done\@
.Laes_192\@:
	_vaes_1x	\enc, 0, 10, \xmm_suffix, \data
	_vaes_1x	\enc, 0, 11, \xmm_suffix, \data
	_vaes_1x	\enc, 1, 12, \xmm_suffix, \data
.Laes_done\@:
	_vpxor		\tweak, \data, \data
.endm

.macro	_aes_xts_crypt	enc
	_define_aliases

	// Load the AES key length: 16 (AES-128), 24 (AES-192), or 32 (AES-256).
	movl		480(KEY), KEYLEN

	// If decrypting, advance KEY to the decryption round keys.
.if !\enc
	add		$240, KEY
.endif

	// Check whether the data length is a multiple of the AES block length.
	test		$15, LEN
	jnz		.Lneed_cts\@
.Lxts_init\@:

	// Cache as many round keys as possible.
	_load_round_keys

	// Compute the first set of tweaks TWEAK[0-3].
	_compute_first_set_of_tweaks

	sub		$4*VL, LEN
	jl		.Lhandle_remainder\@

.Lmain_loop\@:
	// This is the main loop, en/decrypting 4*VL bytes per iteration.

	// XOR each source block with its tweak and the first round key.
.if USE_AVX10
	vmovdqu8	0*VL(SRC), V0
	vmovdqu8	1*VL(SRC), V1
	vmovdqu8	2*VL(SRC), V2
	vmovdqu8	3*VL(SRC), V3
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
	// Do all the AES rounds on the data blocks, interleaved with
	// the computation of the next set of tweaks.
	_vaes_4x	\enc, 0, 1
	_vaes_4x	\enc, 0, 2
	_vaes_4x	\enc, 0, 3
	_vaes_4x	\enc, 0, 4
	_vaes_4x	\enc, 0, 5
	_vaes_4x	\enc, 0, 6
	_vaes_4x	\enc, 0, 7
	_vaes_4x	\enc, 0, 8
	_vaes_4x	\enc, 0, 9
	// Try to optimize for AES-256 by keeping the code for AES-128 and
	// AES-192 out-of-line.
	cmp		$24, KEYLEN
	jle		.Lencrypt_4x_aes_128_or_192\@
	_vaes_4x	\enc, 0, 10
	_vaes_4x	\enc, 0, 11
	_vaes_4x	\enc, 0, 12
	_vaes_4x	\enc, 0, 13
	_vaes_4x	\enc, 1, 14
.Lencrypt_4x_done\@:

	// XOR in the tweaks again.
	_vpxor		TWEAK0, V0, V0
	_vpxor		TWEAK1, V1, V1
	_vpxor		TWEAK2, V2, V2
	_vpxor		TWEAK3, V3, V3

	// Store the destination blocks.
	_vmovdqu	V0, 0*VL(DST)
	_vmovdqu	V1, 1*VL(DST)
	_vmovdqu	V2, 2*VL(DST)
	_vmovdqu	V3, 3*VL(DST)

	// Finish computing the next set of tweaks.
	_tweak_step	1000

	add		$4*VL, SRC
	add		$4*VL, DST
	sub		$4*VL, LEN
	jge		.Lmain_loop\@

	// Check for the uncommon case where the data length isn't a multiple of
	// 4*VL.  Handle it out-of-line in order to optimize for the common
	// case.  In the common case, just fall through to the ret.
	test		$4*VL-1, LEN
	jnz		.Lhandle_remainder\@
.Ldone\@:
	// Store the next tweak back to *TWEAK to support continuation calls.
	vmovdqu		TWEAK0_XMM, (TWEAK)
.if VL > 16
	vzeroupper
.endif
	RET

.Lhandle_remainder\@:
	add		$4*VL, LEN	// Undo the extra sub from earlier.

	// En/decrypt any remaining full blocks, one vector at a time.
.if VL > 16
	sub		$VL, LEN
	jl		.Lvec_at_a_time_done\@
.Lvec_at_a_time\@:
	_vmovdqu	(SRC), V0
	_aes_crypt	\enc, , TWEAK0, V0
	_vmovdqu	V0, (DST)
	_next_tweakvec	TWEAK0, V0, V1, TWEAK0
	add		$VL, SRC
	add		$VL, DST
	sub		$VL, LEN
	jge		.Lvec_at_a_time\@
.Lvec_at_a_time_done\@:
	add		$VL-16, LEN	// Undo the extra sub from earlier.
.else
	sub		$16, LEN
.endif

	// En/decrypt any remaining full blocks, one at a time.
	jl		.Lblock_at_a_time_done\@
.Lblock_at_a_time\@:
	vmovdqu		(SRC), %xmm0
	_aes_crypt	\enc, _XMM, TWEAK0_XMM, %xmm0
	vmovdqu		%xmm0, (DST)
	_next_tweak	TWEAK0_XMM, %xmm0, TWEAK0_XMM
	add		$16, SRC
	add		$16, DST
	sub		$16, LEN
	jge		.Lblock_at_a_time\@
.Lblock_at_a_time_done\@:
	add		$16, LEN	// Undo the extra sub from earlier.

.Lfull_blocks_done\@:
	// Now 0 <= LEN <= 15.  If LEN is nonzero, do ciphertext stealing to
	// process the last 16 + LEN bytes.  If LEN is zero, we're done.
	test		LEN, LEN
	jnz		.Lcts\@
	jmp		.Ldone\@

	// Out-of-line handling of AES-128 and AES-192
.Lencrypt_4x_aes_128_or_192\@:
	jz		.Lencrypt_4x_aes_192\@
	_vaes_4x	\enc, 1, 10
	jmp		.Lencrypt_4x_done\@
.Lencrypt_4x_aes_192\@:
	_vaes_4x	\enc, 0, 10
	_vaes_4x	\enc, 0, 11
	_vaes_4x	\enc, 1, 12
	jmp		.Lencrypt_4x_done\@

.Lneed_cts\@:
	// The data length isn't a multiple of the AES block length, so
	// ciphertext stealing (CTS) will be needed.  Subtract one block from
	// LEN so that the main loop doesn't process the last full block.  The
	// CTS step will process it specially along with the partial block.
	sub		$16, LEN
	jmp		.Lxts_init\@

.Lcts\@:
	// Do ciphertext stealing (CTS) to en/decrypt the last full block and
	// the partial block.  CTS needs two tweaks.  TWEAK0_XMM contains the
	// next tweak; compute the one after that.  Decryption uses these two
	// tweaks in reverse order, so also define aliases to handle that.
	_next_tweak	TWEAK0_XMM, %xmm0, TWEAK1_XMM
.if \enc
	.set		CTS_TWEAK0,	TWEAK0_XMM
	.set		CTS_TWEAK1,	TWEAK1_XMM
.else
	.set		CTS_TWEAK0,	TWEAK1_XMM
	.set		CTS_TWEAK1,	TWEAK0_XMM
.endif

	// En/decrypt the last full block.
	vmovdqu		(SRC), %xmm0
	_aes_crypt	\enc, _XMM, CTS_TWEAK0, %xmm0

.if USE_AVX10
	// Create a mask that has the first LEN bits set.
	mov		$-1, %rax
	bzhi		LEN, %rax, %rax
	kmovq		%rax, %k1

	// Swap the first LEN bytes of the above result with the partial block.
	// Note that to support in-place en/decryption, the load from the src
	// partial block must happen before the store to the dst partial block.
	vmovdqa		%xmm0, %xmm1
	vmovdqu8	16(SRC), %xmm0{%k1}
	vmovdqu8	%xmm1, 16(DST){%k1}
.else
	lea		.Lcts_permute_table(%rip), %rax

	// Load the src partial block, left-aligned.  Note that to support
	// in-place en/decryption, this must happen before the store to the dst
	// partial block.
	vmovdqu		(SRC, LEN, 1), %xmm1

	// Shift the first LEN bytes of the en/decryption of the last full block
	// to the end of a register, then store it to DST+LEN.  This stores the
	// dst partial block.  It also writes to the second part of the dst last
	// full block, but that part is overwritten later.
	vpshufb		(%rax, LEN, 1), %xmm0, %xmm2
	vmovdqu		%xmm2, (DST, LEN, 1)

	// Make xmm3 contain [16-LEN,16-LEN+1,...,14,15,0x80,0x80,...].
	sub		LEN, %rax
	vmovdqu		32(%rax), %xmm3

	// Shift the src partial block to the beginning of its register.
	vpshufb		%xmm3, %xmm1, %xmm1

	// Do a blend to generate the src partial block followed by the second
	// part of the en/decryption of the last full block.
	vpblendvb	%xmm3, %xmm0, %xmm1, %xmm0
.endif
	// En/decrypt again and store the last full block.
	_aes_crypt	\enc, _XMM, CTS_TWEAK1, %xmm0
	vmovdqu		%xmm0, (DST)
	jmp		.Ldone\@
.endm

// void aes_xts_encrypt_iv(const struct crypto_aes_ctx *tweak_key,
//			   u8 iv[AES_BLOCK_SIZE]);
SYM_FUNC_START(aes_xts_encrypt_iv)
	vmovdqu		(%rsi), %xmm0
	vpxor		0*16(%rdi), %xmm0, %xmm0
	vaesenc		1*16(%rdi), %xmm0, %xmm0
	vaesenc		2*16(%rdi), %xmm0, %xmm0
	vaesenc		3*16(%rdi), %xmm0, %xmm0
	vaesenc		4*16(%rdi), %xmm0, %xmm0
	vaesenc		5*16(%rdi), %xmm0, %xmm0
	vaesenc		6*16(%rdi), %xmm0, %xmm0
	vaesenc		7*16(%rdi), %xmm0, %xmm0
	vaesenc		8*16(%rdi), %xmm0, %xmm0
	vaesenc		9*16(%rdi), %xmm0, %xmm0
	cmpl		$24, 480(%rdi)
	jle		.Lencrypt_iv_aes_128_or_192
	vaesenc		10*16(%rdi), %xmm0, %xmm0
	vaesenc		11*16(%rdi), %xmm0, %xmm0
	vaesenc		12*16(%rdi), %xmm0, %xmm0
	vaesenc		13*16(%rdi), %xmm0, %xmm0
	vaesenclast	14*16(%rdi), %xmm0, %xmm0
.Lencrypt_iv_done:
	vmovdqu		%xmm0, (%rsi)
	RET

	// Out-of-line handling of AES-128 and AES-192
.Lencrypt_iv_aes_128_or_192:
	jz		.Lencrypt_iv_aes_192
	vaesenclast	10*16(%rdi), %xmm0, %xmm0
	jmp		.Lencrypt_iv_done
.Lencrypt_iv_aes_192:
	vaesenc		10*16(%rdi), %xmm0, %xmm0
	vaesenc		11*16(%rdi), %xmm0, %xmm0
	vaesenclast	12*16(%rdi), %xmm0, %xmm0
	jmp		.Lencrypt_iv_done
SYM_FUNC_END(aes_xts_encrypt_iv)

// Below are the actual AES-XTS encryption and decryption functions,
// instantiated from the above macro.  They all have the following prototype:
//
// void (*xts_asm_func)(const struct crypto_aes_ctx *key,
//			const u8 *src, u8 *dst, size_t len,
//			u8 tweak[AES_BLOCK_SIZE]);
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
