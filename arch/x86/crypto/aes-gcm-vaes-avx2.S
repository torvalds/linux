/* SPDX-License-Identifier: Apache-2.0 OR BSD-2-Clause */
//
// AES-GCM implementation for x86_64 CPUs that support the following CPU
// features: VAES && VPCLMULQDQ && AVX2
//
// Copyright 2025 Google LLC
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
//
// -----------------------------------------------------------------------------
//
// This is similar to aes-gcm-vaes-avx512.S, but it uses AVX2 instead of AVX512.
// This means it can only use 16 vector registers instead of 32, the maximum
// vector length is 32 bytes, and some instructions such as vpternlogd and
// masked loads/stores are unavailable.  However, it is able to run on CPUs that
// have VAES without AVX512, namely AMD Zen 3 (including "Milan" server CPUs),
// various Intel client CPUs such as Alder Lake, and Intel Sierra Forest.
//
// This implementation also uses Karatsuba multiplication instead of schoolbook
// multiplication for GHASH in its main loop.  This does not help much on Intel,
// but it improves performance by ~5% on AMD Zen 3.  Other factors weighing
// slightly in favor of Karatsuba multiplication in this implementation are the
// lower maximum vector length (which means there are fewer key powers, so we
// can cache the halves of each key power XOR'd together and still use less
// memory than the AVX512 implementation), and the unavailability of the
// vpternlogd instruction (which helped schoolbook a bit more than Karatsuba).

#include <linux/linkage.h>

.section .rodata
.p2align 4

	// The below three 16-byte values must be in the order that they are, as
	// they are really two 32-byte tables and a 16-byte value that overlap:
	//
	// - The first 32-byte table begins at .Lselect_high_bytes_table.
	//   For 0 <= len <= 16, the 16-byte value at
	//   '.Lselect_high_bytes_table + len' selects the high 'len' bytes of
	//   another 16-byte value when AND'ed with it.
	//
	// - The second 32-byte table begins at .Lrshift_and_bswap_table.
	//   For 0 <= len <= 16, the 16-byte value at
	//   '.Lrshift_and_bswap_table + len' is a vpshufb mask that does the
	//   following operation: right-shift by '16 - len' bytes (shifting in
	//   zeroes), then reflect all 16 bytes.
	//
	// - The 16-byte value at .Lbswap_mask is a vpshufb mask that reflects
	//   all 16 bytes.
.Lselect_high_bytes_table:
	.octa	0
.Lrshift_and_bswap_table:
	.octa	0xffffffffffffffffffffffffffffffff
.Lbswap_mask:
	.octa	0x000102030405060708090a0b0c0d0e0f

	// Sixteen 0x0f bytes.  By XOR'ing an entry of .Lrshift_and_bswap_table
	// with this, we get a mask that left-shifts by '16 - len' bytes.
.Lfifteens:
	.octa	0x0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f

	// This is the GHASH reducing polynomial without its constant term, i.e.
	// x^128 + x^7 + x^2 + x, represented using the backwards mapping
	// between bits and polynomial coefficients.
	//
	// Alternatively, it can be interpreted as the naturally-ordered
	// representation of the polynomial x^127 + x^126 + x^121 + 1, i.e. the
	// "reversed" GHASH reducing polynomial without its x^128 term.
.Lgfpoly:
	.octa	0xc2000000000000000000000000000001

	// Same as above, but with the (1 << 64) bit set.
.Lgfpoly_and_internal_carrybit:
	.octa	0xc2000000000000010000000000000001

	// Values needed to prepare the initial vector of counter blocks.
.Lctr_pattern:
	.octa	0
	.octa	1

	// The number of AES blocks per vector, as a 128-bit value.
.Linc_2blocks:
	.octa	2

// Offsets in struct aes_gcm_key_vaes_avx2
#define OFFSETOF_AESKEYLEN	0
#define OFFSETOF_AESROUNDKEYS	16
#define OFFSETOF_H_POWERS	288
#define NUM_H_POWERS		8
#define OFFSETOFEND_H_POWERS    (OFFSETOF_H_POWERS + (NUM_H_POWERS * 16))
#define OFFSETOF_H_POWERS_XORED	OFFSETOFEND_H_POWERS

.text

// Do one step of GHASH-multiplying the 128-bit lanes of \a by the 128-bit lanes
// of \b and storing the reduced products in \dst.  Uses schoolbook
// multiplication.
.macro	_ghash_mul_step	i, a, b, dst, gfpoly, t0, t1, t2
.if \i == 0
	vpclmulqdq	$0x00, \a, \b, \t0	  // LO = a_L * b_L
	vpclmulqdq	$0x01, \a, \b, \t1	  // MI_0 = a_L * b_H
.elseif \i == 1
	vpclmulqdq	$0x10, \a, \b, \t2	  // MI_1 = a_H * b_L
.elseif \i == 2
	vpxor		\t2, \t1, \t1		  // MI = MI_0 + MI_1
.elseif \i == 3
	vpclmulqdq	$0x01, \t0, \gfpoly, \t2  // LO_L*(x^63 + x^62 + x^57)
.elseif \i == 4
	vpshufd		$0x4e, \t0, \t0		  // Swap halves of LO
.elseif \i == 5
	vpxor		\t0, \t1, \t1		  // Fold LO into MI (part 1)
	vpxor		\t2, \t1, \t1		  // Fold LO into MI (part 2)
.elseif \i == 6
	vpclmulqdq	$0x11, \a, \b, \dst	  // HI = a_H * b_H
.elseif \i == 7
	vpclmulqdq	$0x01, \t1, \gfpoly, \t0  // MI_L*(x^63 + x^62 + x^57)
.elseif \i == 8
	vpshufd		$0x4e, \t1, \t1		  // Swap halves of MI
.elseif \i == 9
	vpxor		\t1, \dst, \dst		  // Fold MI into HI (part 1)
	vpxor		\t0, \dst, \dst		  // Fold MI into HI (part 2)
.endif
.endm

// GHASH-multiply the 128-bit lanes of \a by the 128-bit lanes of \b and store
// the reduced products in \dst.  See _ghash_mul_step for full explanation.
.macro	_ghash_mul	a, b, dst, gfpoly, t0, t1, t2
.irp i, 0,1,2,3,4,5,6,7,8,9
	_ghash_mul_step	\i, \a, \b, \dst, \gfpoly, \t0, \t1, \t2
.endr
.endm

// GHASH-multiply the 128-bit lanes of \a by the 128-bit lanes of \b and add the
// *unreduced* products to \lo, \mi, and \hi.
.macro	_ghash_mul_noreduce	a, b, lo, mi, hi, t0
	vpclmulqdq	$0x00, \a, \b, \t0	// a_L * b_L
	vpxor		\t0, \lo, \lo
	vpclmulqdq	$0x01, \a, \b, \t0	// a_L * b_H
	vpxor		\t0, \mi, \mi
	vpclmulqdq	$0x10, \a, \b, \t0	// a_H * b_L
	vpxor		\t0, \mi, \mi
	vpclmulqdq	$0x11, \a, \b, \t0	// a_H * b_H
	vpxor		\t0, \hi, \hi
.endm

// Reduce the unreduced products from \lo, \mi, and \hi and store the 128-bit
// reduced products in \hi.  See _ghash_mul_step for explanation of reduction.
.macro	_ghash_reduce	lo, mi, hi, gfpoly, t0
	vpclmulqdq	$0x01, \lo, \gfpoly, \t0
	vpshufd		$0x4e, \lo, \lo
	vpxor		\lo, \mi, \mi
	vpxor		\t0, \mi, \mi
	vpclmulqdq	$0x01, \mi, \gfpoly, \t0
	vpshufd		$0x4e, \mi, \mi
	vpxor		\mi, \hi, \hi
	vpxor		\t0, \hi, \hi
.endm

// This is a specialized version of _ghash_mul that computes \a * \a, i.e. it
// squares \a.  It skips computing MI = (a_L * a_H) + (a_H * a_L) = 0.
.macro	_ghash_square	a, dst, gfpoly, t0, t1
	vpclmulqdq	$0x00, \a, \a, \t0	  // LO = a_L * a_L
	vpclmulqdq	$0x11, \a, \a, \dst	  // HI = a_H * a_H
	vpclmulqdq	$0x01, \t0, \gfpoly, \t1  // LO_L*(x^63 + x^62 + x^57)
	vpshufd		$0x4e, \t0, \t0		  // Swap halves of LO
	vpxor		\t0, \t1, \t1		  // Fold LO into MI
	vpclmulqdq	$0x01, \t1, \gfpoly, \t0  // MI_L*(x^63 + x^62 + x^57)
	vpshufd		$0x4e, \t1, \t1		  // Swap halves of MI
	vpxor		\t1, \dst, \dst		  // Fold MI into HI (part 1)
	vpxor		\t0, \dst, \dst		  // Fold MI into HI (part 2)
.endm

// void aes_gcm_precompute_vaes_avx2(struct aes_gcm_key_vaes_avx2 *key);
//
// Given the expanded AES key |key->base.aes_key|, derive the GHASH subkey and
// initialize |key->h_powers| and |key->h_powers_xored|.
//
// We use h_powers[0..7] to store H^8 through H^1, and h_powers_xored[0..7] to
// store the 64-bit halves of the key powers XOR'd together (for Karatsuba
// multiplication) in the order 8,6,7,5,4,2,3,1.
SYM_FUNC_START(aes_gcm_precompute_vaes_avx2)

	// Function arguments
	.set	KEY,		%rdi

	// Additional local variables
	.set	POWERS_PTR,	%rsi
	.set	RNDKEYLAST_PTR,	%rdx
	.set	TMP0,		%ymm0
	.set	TMP0_XMM,	%xmm0
	.set	TMP1,		%ymm1
	.set	TMP1_XMM,	%xmm1
	.set	TMP2,		%ymm2
	.set	TMP2_XMM,	%xmm2
	.set	H_CUR,		%ymm3
	.set	H_CUR_XMM,	%xmm3
	.set	H_CUR2,		%ymm4
	.set	H_INC,		%ymm5
	.set	H_INC_XMM,	%xmm5
	.set	GFPOLY,		%ymm6
	.set	GFPOLY_XMM,	%xmm6

	// Encrypt an all-zeroes block to get the raw hash subkey.
	movl		OFFSETOF_AESKEYLEN(KEY), %eax
	lea		OFFSETOF_AESROUNDKEYS+6*16(KEY,%rax,4), RNDKEYLAST_PTR
	vmovdqu		OFFSETOF_AESROUNDKEYS(KEY), H_CUR_XMM
	lea		OFFSETOF_AESROUNDKEYS+16(KEY), %rax
1:
	vaesenc		(%rax), H_CUR_XMM, H_CUR_XMM
	add		$16, %rax
	cmp		%rax, RNDKEYLAST_PTR
	jne		1b
	vaesenclast	(RNDKEYLAST_PTR), H_CUR_XMM, H_CUR_XMM

	// Reflect the bytes of the raw hash subkey.
	vpshufb		.Lbswap_mask(%rip), H_CUR_XMM, H_CUR_XMM

	// Finish preprocessing the byte-reflected hash subkey by multiplying it
	// by x^-1 ("standard" interpretation of polynomial coefficients) or
	// equivalently x^1 (natural interpretation).  This gets the key into a
	// format that avoids having to bit-reflect the data blocks later.
	vpshufd		$0xd3, H_CUR_XMM, TMP0_XMM
	vpsrad		$31, TMP0_XMM, TMP0_XMM
	vpaddq		H_CUR_XMM, H_CUR_XMM, H_CUR_XMM
	vpand		.Lgfpoly_and_internal_carrybit(%rip), TMP0_XMM, TMP0_XMM
	vpxor		TMP0_XMM, H_CUR_XMM, H_CUR_XMM

	// Load the gfpoly constant.
	vbroadcasti128	.Lgfpoly(%rip), GFPOLY

	// Square H^1 to get H^2.
	_ghash_square	H_CUR_XMM, H_INC_XMM, GFPOLY_XMM, TMP0_XMM, TMP1_XMM

	// Create H_CUR = [H^2, H^1] and H_INC = [H^2, H^2].
	vinserti128	$1, H_CUR_XMM, H_INC, H_CUR
	vinserti128	$1, H_INC_XMM, H_INC, H_INC

	// Compute H_CUR2 = [H^4, H^3].
	_ghash_mul	H_INC, H_CUR, H_CUR2, GFPOLY, TMP0, TMP1, TMP2

	// Store [H^2, H^1] and [H^4, H^3].
	vmovdqu		H_CUR, OFFSETOF_H_POWERS+3*32(KEY)
	vmovdqu		H_CUR2, OFFSETOF_H_POWERS+2*32(KEY)

	// For Karatsuba multiplication: compute and store the two 64-bit halves
	// of each key power XOR'd together.  Order is 4,2,3,1.
	vpunpcklqdq	H_CUR, H_CUR2, TMP0
	vpunpckhqdq	H_CUR, H_CUR2, TMP1
	vpxor		TMP1, TMP0, TMP0
	vmovdqu		TMP0, OFFSETOF_H_POWERS_XORED+32(KEY)

	// Compute and store H_CUR = [H^6, H^5] and H_CUR2 = [H^8, H^7].
	_ghash_mul	H_INC, H_CUR2, H_CUR, GFPOLY, TMP0, TMP1, TMP2
	_ghash_mul	H_INC, H_CUR, H_CUR2, GFPOLY, TMP0, TMP1, TMP2
	vmovdqu		H_CUR, OFFSETOF_H_POWERS+1*32(KEY)
	vmovdqu		H_CUR2, OFFSETOF_H_POWERS+0*32(KEY)

	// Again, compute and store the two 64-bit halves of each key power
	// XOR'd together.  Order is 8,6,7,5.
	vpunpcklqdq	H_CUR, H_CUR2, TMP0
	vpunpckhqdq	H_CUR, H_CUR2, TMP1
	vpxor		TMP1, TMP0, TMP0
	vmovdqu		TMP0, OFFSETOF_H_POWERS_XORED(KEY)

	vzeroupper
	RET
SYM_FUNC_END(aes_gcm_precompute_vaes_avx2)

// Do one step of the GHASH update of four vectors of data blocks.
//   \i: the step to do, 0 through 9
//   \ghashdata_ptr: pointer to the data blocks (ciphertext or AAD)
//   KEY: pointer to struct aes_gcm_key_vaes_avx2
//   BSWAP_MASK: mask for reflecting the bytes of blocks
//   H_POW[2-1]_XORED: cached values from KEY->h_powers_xored
//   TMP[0-2]: temporary registers.  TMP[1-2] must be preserved across steps.
//   LO, MI: working state for this macro that must be preserved across steps
//   GHASH_ACC: the GHASH accumulator (input/output)
.macro	_ghash_step_4x	i, ghashdata_ptr
	.set		HI, GHASH_ACC # alias
	.set		HI_XMM, GHASH_ACC_XMM
.if \i == 0
	// First vector
	vmovdqu		0*32(\ghashdata_ptr), TMP1
	vpshufb		BSWAP_MASK, TMP1, TMP1
	vmovdqu		OFFSETOF_H_POWERS+0*32(KEY), TMP2
	vpxor		GHASH_ACC, TMP1, TMP1
	vpclmulqdq	$0x00, TMP2, TMP1, LO
	vpclmulqdq	$0x11, TMP2, TMP1, HI
	vpunpckhqdq	TMP1, TMP1, TMP0
	vpxor		TMP1, TMP0, TMP0
	vpclmulqdq	$0x00, H_POW2_XORED, TMP0, MI
.elseif \i == 1
.elseif \i == 2
	// Second vector
	vmovdqu		1*32(\ghashdata_ptr), TMP1
	vpshufb		BSWAP_MASK, TMP1, TMP1
	vmovdqu		OFFSETOF_H_POWERS+1*32(KEY), TMP2
	vpclmulqdq	$0x00, TMP2, TMP1, TMP0
	vpxor		TMP0, LO, LO
	vpclmulqdq	$0x11, TMP2, TMP1, TMP0
	vpxor		TMP0, HI, HI
	vpunpckhqdq	TMP1, TMP1, TMP0
	vpxor		TMP1, TMP0, TMP0
	vpclmulqdq	$0x10, H_POW2_XORED, TMP0, TMP0
	vpxor		TMP0, MI, MI
.elseif \i == 3
	// Third vector
	vmovdqu		2*32(\ghashdata_ptr), TMP1
	vpshufb		BSWAP_MASK, TMP1, TMP1
	vmovdqu		OFFSETOF_H_POWERS+2*32(KEY), TMP2
.elseif \i == 4
	vpclmulqdq	$0x00, TMP2, TMP1, TMP0
	vpxor		TMP0, LO, LO
	vpclmulqdq	$0x11, TMP2, TMP1, TMP0
	vpxor		TMP0, HI, HI
.elseif \i == 5
	vpunpckhqdq	TMP1, TMP1, TMP0
	vpxor		TMP1, TMP0, TMP0
	vpclmulqdq	$0x00, H_POW1_XORED, TMP0, TMP0
	vpxor		TMP0, MI, MI

	// Fourth vector
	vmovdqu		3*32(\ghashdata_ptr), TMP1
	vpshufb		BSWAP_MASK, TMP1, TMP1
.elseif \i == 6
	vmovdqu		OFFSETOF_H_POWERS+3*32(KEY), TMP2
	vpclmulqdq	$0x00, TMP2, TMP1, TMP0
	vpxor		TMP0, LO, LO
	vpclmulqdq	$0x11, TMP2, TMP1, TMP0
	vpxor		TMP0, HI, HI
	vpunpckhqdq	TMP1, TMP1, TMP0
	vpxor		TMP1, TMP0, TMP0
	vpclmulqdq	$0x10, H_POW1_XORED, TMP0, TMP0
	vpxor		TMP0, MI, MI
.elseif \i == 7
	// Finalize 'mi' following Karatsuba multiplication.
	vpxor		LO, MI, MI
	vpxor		HI, MI, MI

	// Fold lo into mi.
	vbroadcasti128	.Lgfpoly(%rip), TMP2
	vpclmulqdq	$0x01, LO, TMP2, TMP0
	vpshufd		$0x4e, LO, LO
	vpxor		LO, MI, MI
	vpxor		TMP0, MI, MI
.elseif \i == 8
	// Fold mi into hi.
	vpclmulqdq	$0x01, MI, TMP2, TMP0
	vpshufd		$0x4e, MI, MI
	vpxor		MI, HI, HI
	vpxor		TMP0, HI, HI
.elseif \i == 9
	vextracti128	$1, HI, TMP0_XMM
	vpxor		TMP0_XMM, HI_XMM, GHASH_ACC_XMM
.endif
.endm

// Update GHASH with four vectors of data blocks.  See _ghash_step_4x for full
// explanation.
.macro	_ghash_4x	ghashdata_ptr
.irp i, 0,1,2,3,4,5,6,7,8,9
	_ghash_step_4x	\i, \ghashdata_ptr
.endr
.endm

// Load 1 <= %ecx <= 16 bytes from the pointer \src into the xmm register \dst
// and zeroize any remaining bytes.  Clobbers %rax, %rcx, and \tmp{64,32}.
.macro	_load_partial_block	src, dst, tmp64, tmp32
	sub		$8, %ecx		// LEN - 8
	jle		.Lle8\@

	// Load 9 <= LEN <= 16 bytes.
	vmovq		(\src), \dst		// Load first 8 bytes
	mov		(\src, %rcx), %rax	// Load last 8 bytes
	neg		%ecx
	shl		$3, %ecx
	shr		%cl, %rax		// Discard overlapping bytes
	vpinsrq		$1, %rax, \dst, \dst
	jmp		.Ldone\@

.Lle8\@:
	add		$4, %ecx		// LEN - 4
	jl		.Llt4\@

	// Load 4 <= LEN <= 8 bytes.
	mov		(\src), %eax		// Load first 4 bytes
	mov		(\src, %rcx), \tmp32	// Load last 4 bytes
	jmp		.Lcombine\@

.Llt4\@:
	// Load 1 <= LEN <= 3 bytes.
	add		$2, %ecx		// LEN - 2
	movzbl		(\src), %eax		// Load first byte
	jl		.Lmovq\@
	movzwl		(\src, %rcx), \tmp32	// Load last 2 bytes
.Lcombine\@:
	shl		$3, %ecx
	shl		%cl, \tmp64
	or		\tmp64, %rax		// Combine the two parts
.Lmovq\@:
	vmovq		%rax, \dst
.Ldone\@:
.endm

// Store 1 <= %ecx <= 16 bytes from the xmm register \src to the pointer \dst.
// Clobbers %rax, %rcx, and \tmp{64,32}.
.macro	_store_partial_block	src, dst, tmp64, tmp32
	sub		$8, %ecx		// LEN - 8
	jl		.Llt8\@

	// Store 8 <= LEN <= 16 bytes.
	vpextrq		$1, \src, %rax
	mov		%ecx, \tmp32
	shl		$3, %ecx
	ror		%cl, %rax
	mov		%rax, (\dst, \tmp64)	// Store last LEN - 8 bytes
	vmovq		\src, (\dst)		// Store first 8 bytes
	jmp		.Ldone\@

.Llt8\@:
	add		$4, %ecx		// LEN - 4
	jl		.Llt4\@

	// Store 4 <= LEN <= 7 bytes.
	vpextrd		$1, \src, %eax
	mov		%ecx, \tmp32
	shl		$3, %ecx
	ror		%cl, %eax
	mov		%eax, (\dst, \tmp64)	// Store last LEN - 4 bytes
	vmovd		\src, (\dst)		// Store first 4 bytes
	jmp		.Ldone\@

.Llt4\@:
	// Store 1 <= LEN <= 3 bytes.
	vpextrb		$0, \src, 0(\dst)
	cmp		$-2, %ecx		// LEN - 4 == -2, i.e. LEN == 2?
	jl		.Ldone\@
	vpextrb		$1, \src, 1(\dst)
	je		.Ldone\@
	vpextrb		$2, \src, 2(\dst)
.Ldone\@:
.endm

// void aes_gcm_aad_update_vaes_avx2(const struct aes_gcm_key_vaes_avx2 *key,
//				     u8 ghash_acc[16],
//				     const u8 *aad, int aadlen);
//
// This function processes the AAD (Additional Authenticated Data) in GCM.
// Using the key |key|, it updates the GHASH accumulator |ghash_acc| with the
// data given by |aad| and |aadlen|.  On the first call, |ghash_acc| must be all
// zeroes.  |aadlen| must be a multiple of 16, except on the last call where it
// can be any length.  The caller must do any buffering needed to ensure this.
//
// This handles large amounts of AAD efficiently, while also keeping overhead
// low for small amounts which is the common case.  TLS and IPsec use less than
// one block of AAD, but (uncommonly) other use cases may use much more.
SYM_FUNC_START(aes_gcm_aad_update_vaes_avx2)

	// Function arguments
	.set	KEY,		%rdi
	.set	GHASH_ACC_PTR,	%rsi
	.set	AAD,		%rdx
	.set	AADLEN,		%ecx	// Must be %ecx for _load_partial_block
	.set	AADLEN64,	%rcx	// Zero-extend AADLEN before using!

	// Additional local variables.
	// %rax and %r8 are used as temporary registers.
	.set	TMP0,		%ymm0
	.set	TMP0_XMM,	%xmm0
	.set	TMP1,		%ymm1
	.set	TMP1_XMM,	%xmm1
	.set	TMP2,		%ymm2
	.set	TMP2_XMM,	%xmm2
	.set	LO,		%ymm3
	.set	LO_XMM,		%xmm3
	.set	MI,		%ymm4
	.set	MI_XMM,		%xmm4
	.set	GHASH_ACC,	%ymm5
	.set	GHASH_ACC_XMM,	%xmm5
	.set	BSWAP_MASK,	%ymm6
	.set	BSWAP_MASK_XMM,	%xmm6
	.set	GFPOLY,		%ymm7
	.set	GFPOLY_XMM,	%xmm7
	.set	H_POW2_XORED,	%ymm8
	.set	H_POW1_XORED,	%ymm9

	// Load the bswap_mask and gfpoly constants.  Since AADLEN is usually
	// small, usually only 128-bit vectors will be used.  So as an
	// optimization, don't broadcast these constants to both 128-bit lanes
	// quite yet.
	vmovdqu		.Lbswap_mask(%rip), BSWAP_MASK_XMM
	vmovdqu		.Lgfpoly(%rip), GFPOLY_XMM

	// Load the GHASH accumulator.
	vmovdqu		(GHASH_ACC_PTR), GHASH_ACC_XMM

	// Check for the common case of AADLEN <= 16, as well as AADLEN == 0.
	test		AADLEN, AADLEN
	jz		.Laad_done
	cmp		$16, AADLEN
	jle		.Laad_lastblock

	// AADLEN > 16, so we'll operate on full vectors.  Broadcast bswap_mask
	// and gfpoly to both 128-bit lanes.
	vinserti128	$1, BSWAP_MASK_XMM, BSWAP_MASK, BSWAP_MASK
	vinserti128	$1, GFPOLY_XMM, GFPOLY, GFPOLY

	// If AADLEN >= 128, update GHASH with 128 bytes of AAD at a time.
	add		$-128, AADLEN	// 128 is 4 bytes, -128 is 1 byte
	jl		.Laad_loop_4x_done
	vmovdqu		OFFSETOF_H_POWERS_XORED(KEY), H_POW2_XORED
	vmovdqu		OFFSETOF_H_POWERS_XORED+32(KEY), H_POW1_XORED
.Laad_loop_4x:
	_ghash_4x	AAD
	sub		$-128, AAD
	add		$-128, AADLEN
	jge		.Laad_loop_4x
.Laad_loop_4x_done:

	// If AADLEN >= 32, update GHASH with 32 bytes of AAD at a time.
	add		$96, AADLEN
	jl		.Laad_loop_1x_done
.Laad_loop_1x:
	vmovdqu		(AAD), TMP0
	vpshufb		BSWAP_MASK, TMP0, TMP0
	vpxor		TMP0, GHASH_ACC, GHASH_ACC
	vmovdqu		OFFSETOFEND_H_POWERS-32(KEY), TMP0
	_ghash_mul	TMP0, GHASH_ACC, GHASH_ACC, GFPOLY, TMP1, TMP2, LO
	vextracti128	$1, GHASH_ACC, TMP0_XMM
	vpxor		TMP0_XMM, GHASH_ACC_XMM, GHASH_ACC_XMM
	add		$32, AAD
	sub		$32, AADLEN
	jge		.Laad_loop_1x
.Laad_loop_1x_done:
	add		$32, AADLEN
	// Now 0 <= AADLEN < 32.

	jz		.Laad_done
	cmp		$16, AADLEN
	jle		.Laad_lastblock

	// Update GHASH with the remaining 17 <= AADLEN <= 31 bytes of AAD.
	mov		AADLEN, AADLEN	// Zero-extend AADLEN to AADLEN64.
	vmovdqu		(AAD), TMP0_XMM
	vmovdqu		-16(AAD, AADLEN64), TMP1_XMM
	vpshufb		BSWAP_MASK_XMM, TMP0_XMM, TMP0_XMM
	vpxor		TMP0_XMM, GHASH_ACC_XMM, GHASH_ACC_XMM
	lea		.Lrshift_and_bswap_table(%rip), %rax
	vpshufb		-16(%rax, AADLEN64), TMP1_XMM, TMP1_XMM
	vinserti128	$1, TMP1_XMM, GHASH_ACC, GHASH_ACC
	vmovdqu		OFFSETOFEND_H_POWERS-32(KEY), TMP0
	_ghash_mul	TMP0, GHASH_ACC, GHASH_ACC, GFPOLY, TMP1, TMP2, LO
	vextracti128	$1, GHASH_ACC, TMP0_XMM
	vpxor		TMP0_XMM, GHASH_ACC_XMM, GHASH_ACC_XMM
	jmp		.Laad_done

.Laad_lastblock:
	// Update GHASH with the remaining 1 <= AADLEN <= 16 bytes of AAD.
	_load_partial_block	AAD, TMP0_XMM, %r8, %r8d
	vpshufb		BSWAP_MASK_XMM, TMP0_XMM, TMP0_XMM
	vpxor		TMP0_XMM, GHASH_ACC_XMM, GHASH_ACC_XMM
	vmovdqu		OFFSETOFEND_H_POWERS-16(KEY), TMP0_XMM
	_ghash_mul	TMP0_XMM, GHASH_ACC_XMM, GHASH_ACC_XMM, GFPOLY_XMM, \
			TMP1_XMM, TMP2_XMM, LO_XMM

.Laad_done:
	// Store the updated GHASH accumulator back to memory.
	vmovdqu		GHASH_ACC_XMM, (GHASH_ACC_PTR)

	vzeroupper
	RET
SYM_FUNC_END(aes_gcm_aad_update_vaes_avx2)

// Do one non-last round of AES encryption on the blocks in the given AESDATA
// vectors using the round key that has been broadcast to all 128-bit lanes of
// \round_key.
.macro	_vaesenc	round_key, vecs:vararg
.irp i, \vecs
	vaesenc		\round_key, AESDATA\i, AESDATA\i
.endr
.endm

// Generate counter blocks in the given AESDATA vectors, then do the zero-th AES
// round on them.  Clobbers TMP0.
.macro	_ctr_begin	vecs:vararg
	vbroadcasti128	.Linc_2blocks(%rip), TMP0
.irp i, \vecs
	vpshufb		BSWAP_MASK, LE_CTR, AESDATA\i
	vpaddd		TMP0, LE_CTR, LE_CTR
.endr
.irp i, \vecs
	vpxor		RNDKEY0, AESDATA\i, AESDATA\i
.endr
.endm

// Generate and encrypt counter blocks in the given AESDATA vectors, excluding
// the last AES round.  Clobbers %rax and TMP0.
.macro	_aesenc_loop	vecs:vararg
	_ctr_begin	\vecs
	lea		OFFSETOF_AESROUNDKEYS+16(KEY), %rax
.Laesenc_loop\@:
	vbroadcasti128	(%rax), TMP0
	_vaesenc	TMP0, \vecs
	add		$16, %rax
	cmp		%rax, RNDKEYLAST_PTR
	jne		.Laesenc_loop\@
.endm

// Finalize the keystream blocks in the given AESDATA vectors by doing the last
// AES round, then XOR those keystream blocks with the corresponding data.
// Reduce latency by doing the XOR before the vaesenclast, utilizing the
// property vaesenclast(key, a) ^ b == vaesenclast(key ^ b, a).  Clobbers TMP0.
.macro	_aesenclast_and_xor	vecs:vararg
.irp i, \vecs
	vpxor		\i*32(SRC), RNDKEYLAST, TMP0
	vaesenclast	TMP0, AESDATA\i, AESDATA\i
.endr
.irp i, \vecs
	vmovdqu		AESDATA\i, \i*32(DST)
.endr
.endm

// void aes_gcm_{enc,dec}_update_vaes_avx2(const struct aes_gcm_key_vaes_avx2 *key,
//					   const u32 le_ctr[4], u8 ghash_acc[16],
//					   const u8 *src, u8 *dst, int datalen);
//
// This macro generates a GCM encryption or decryption update function with the
// above prototype (with \enc selecting which one).  The function computes the
// next portion of the CTR keystream, XOR's it with |datalen| bytes from |src|,
// and writes the resulting encrypted or decrypted data to |dst|.  It also
// updates the GHASH accumulator |ghash_acc| using the next |datalen| ciphertext
// bytes.
//
// |datalen| must be a multiple of 16, except on the last call where it can be
// any length.  The caller must do any buffering needed to ensure this.  Both
// in-place and out-of-place en/decryption are supported.
//
// |le_ctr| must give the current counter in little-endian format.  This
// function loads the counter from |le_ctr| and increments the loaded counter as
// needed, but it does *not* store the updated counter back to |le_ctr|.  The
// caller must update |le_ctr| if any more data segments follow.  Internally,
// only the low 32-bit word of the counter is incremented, following the GCM
// standard.
.macro	_aes_gcm_update	enc

	// Function arguments
	.set	KEY,		%rdi
	.set	LE_CTR_PTR,	%rsi
	.set	LE_CTR_PTR32,	%esi
	.set	GHASH_ACC_PTR,	%rdx
	.set	SRC,		%rcx	// Assumed to be %rcx.
					// See .Ltail_xor_and_ghash_1to16bytes
	.set	DST,		%r8
	.set	DATALEN,	%r9d
	.set	DATALEN64,	%r9	// Zero-extend DATALEN before using!

	// Additional local variables

	// %rax is used as a temporary register.  LE_CTR_PTR is also available
	// as a temporary register after the counter is loaded.

	// AES key length in bytes
	.set	AESKEYLEN,	%r10d
	.set	AESKEYLEN64,	%r10

	// Pointer to the last AES round key for the chosen AES variant
	.set	RNDKEYLAST_PTR,	%r11

	// BSWAP_MASK is the shuffle mask for byte-reflecting 128-bit values
	// using vpshufb, copied to all 128-bit lanes.
	.set	BSWAP_MASK,	%ymm0
	.set	BSWAP_MASK_XMM,	%xmm0

	// GHASH_ACC is the accumulator variable for GHASH.  When fully reduced,
	// only the lowest 128-bit lane can be nonzero.  When not fully reduced,
	// more than one lane may be used, and they need to be XOR'd together.
	.set	GHASH_ACC,	%ymm1
	.set	GHASH_ACC_XMM,	%xmm1

	// TMP[0-2] are temporary registers.
	.set	TMP0,		%ymm2
	.set	TMP0_XMM,	%xmm2
	.set	TMP1,		%ymm3
	.set	TMP1_XMM,	%xmm3
	.set	TMP2,		%ymm4
	.set	TMP2_XMM,	%xmm4

	// LO and MI are used to accumulate unreduced GHASH products.
	.set	LO,		%ymm5
	.set	LO_XMM,		%xmm5
	.set	MI,		%ymm6
	.set	MI_XMM,		%xmm6

	// H_POW[2-1]_XORED contain cached values from KEY->h_powers_xored.  The
	// descending numbering reflects the order of the key powers.
	.set	H_POW2_XORED,	%ymm7
	.set	H_POW2_XORED_XMM, %xmm7
	.set	H_POW1_XORED,	%ymm8

	// RNDKEY0 caches the zero-th round key, and RNDKEYLAST the last one.
	.set	RNDKEY0,	%ymm9
	.set	RNDKEYLAST,	%ymm10

	// LE_CTR contains the next set of little-endian counter blocks.
	.set	LE_CTR,		%ymm11

	// AESDATA[0-3] hold the counter blocks that are being encrypted by AES.
	.set	AESDATA0,	%ymm12
	.set	AESDATA0_XMM,	%xmm12
	.set	AESDATA1,	%ymm13
	.set	AESDATA1_XMM,	%xmm13
	.set	AESDATA2,	%ymm14
	.set	AESDATA3,	%ymm15

.if \enc
	.set	GHASHDATA_PTR,	DST
.else
	.set	GHASHDATA_PTR,	SRC
.endif

	vbroadcasti128	.Lbswap_mask(%rip), BSWAP_MASK

	// Load the GHASH accumulator and the starting counter.
	vmovdqu		(GHASH_ACC_PTR), GHASH_ACC_XMM
	vbroadcasti128	(LE_CTR_PTR), LE_CTR

	// Load the AES key length in bytes.
	movl		OFFSETOF_AESKEYLEN(KEY), AESKEYLEN

	// Make RNDKEYLAST_PTR point to the last AES round key.  This is the
	// round key with index 10, 12, or 14 for AES-128, AES-192, or AES-256
	// respectively.  Then load the zero-th and last round keys.
	lea		OFFSETOF_AESROUNDKEYS+6*16(KEY,AESKEYLEN64,4), RNDKEYLAST_PTR
	vbroadcasti128	OFFSETOF_AESROUNDKEYS(KEY), RNDKEY0
	vbroadcasti128	(RNDKEYLAST_PTR), RNDKEYLAST

	// Finish initializing LE_CTR by adding 1 to the second block.
	vpaddd		.Lctr_pattern(%rip), LE_CTR, LE_CTR

	// If there are at least 128 bytes of data, then continue into the loop
	// that processes 128 bytes of data at a time.  Otherwise skip it.
	add		$-128, DATALEN	// 128 is 4 bytes, -128 is 1 byte
	jl		.Lcrypt_loop_4x_done\@

	vmovdqu		OFFSETOF_H_POWERS_XORED(KEY), H_POW2_XORED
	vmovdqu		OFFSETOF_H_POWERS_XORED+32(KEY), H_POW1_XORED

	// Main loop: en/decrypt and hash 4 vectors (128 bytes) at a time.

.if \enc
	// Encrypt the first 4 vectors of plaintext blocks.
	_aesenc_loop	0,1,2,3
	_aesenclast_and_xor	0,1,2,3
	sub		$-128, SRC	// 128 is 4 bytes, -128 is 1 byte
	add		$-128, DATALEN
	jl		.Lghash_last_ciphertext_4x\@
.endif

.align 16
.Lcrypt_loop_4x\@:

	// Start the AES encryption of the counter blocks.
	_ctr_begin	0,1,2,3
	cmp		$24, AESKEYLEN
	jl		128f	// AES-128?
	je		192f	// AES-192?
	// AES-256
	vbroadcasti128	-13*16(RNDKEYLAST_PTR), TMP0
	_vaesenc	TMP0, 0,1,2,3
	vbroadcasti128	-12*16(RNDKEYLAST_PTR), TMP0
	_vaesenc	TMP0, 0,1,2,3
192:
	vbroadcasti128	-11*16(RNDKEYLAST_PTR), TMP0
	_vaesenc	TMP0, 0,1,2,3
	vbroadcasti128	-10*16(RNDKEYLAST_PTR), TMP0
	_vaesenc	TMP0, 0,1,2,3
128:

	// Finish the AES encryption of the counter blocks in AESDATA[0-3],
	// interleaved with the GHASH update of the ciphertext blocks.
.irp i, 9,8,7,6,5,4,3,2,1
	_ghash_step_4x  (9 - \i), GHASHDATA_PTR
	vbroadcasti128	-\i*16(RNDKEYLAST_PTR), TMP0
	_vaesenc	TMP0, 0,1,2,3
.endr
	_ghash_step_4x	9, GHASHDATA_PTR
.if \enc
	sub		$-128, DST	// 128 is 4 bytes, -128 is 1 byte
.endif
	_aesenclast_and_xor	0,1,2,3
	sub		$-128, SRC
.if !\enc
	sub		$-128, DST
.endif
	add		$-128, DATALEN
	jge		.Lcrypt_loop_4x\@

.if \enc
.Lghash_last_ciphertext_4x\@:
	// Update GHASH with the last set of ciphertext blocks.
	_ghash_4x	DST
	sub		$-128, DST
.endif

.Lcrypt_loop_4x_done\@:

	// Undo the extra subtraction by 128 and check whether data remains.
	sub		$-128, DATALEN	// 128 is 4 bytes, -128 is 1 byte
	jz		.Ldone\@

	// The data length isn't a multiple of 128 bytes.  Process the remaining
	// data of length 1 <= DATALEN < 128.
	//
	// Since there are enough key powers available for all remaining data,
	// there is no need to do a GHASH reduction after each iteration.
	// Instead, multiply each remaining block by its own key power, and only
	// do a GHASH reduction at the very end.

	// Make POWERS_PTR point to the key powers [H^N, H^(N-1), ...] where N
	// is the number of blocks that remain.
	.set		POWERS_PTR, LE_CTR_PTR	// LE_CTR_PTR is free to be reused.
	.set		POWERS_PTR32, LE_CTR_PTR32
	mov		DATALEN, %eax
	neg		%rax
	and		$~15, %rax  // -round_up(DATALEN, 16)
	lea		OFFSETOFEND_H_POWERS(KEY,%rax), POWERS_PTR

	// Start collecting the unreduced GHASH intermediate value LO, MI, HI.
	.set		HI, H_POW2_XORED	// H_POW2_XORED is free to be reused.
	.set		HI_XMM, H_POW2_XORED_XMM
	vpxor		LO_XMM, LO_XMM, LO_XMM
	vpxor		MI_XMM, MI_XMM, MI_XMM
	vpxor		HI_XMM, HI_XMM, HI_XMM

	// 1 <= DATALEN < 128.  Generate 2 or 4 more vectors of keystream blocks
	// excluding the last AES round, depending on the remaining DATALEN.
	cmp		$64, DATALEN
	jg		.Ltail_gen_4_keystream_vecs\@
	_aesenc_loop	0,1
	cmp		$32, DATALEN
	jge		.Ltail_xor_and_ghash_full_vec_loop\@
	jmp		.Ltail_xor_and_ghash_partial_vec\@
.Ltail_gen_4_keystream_vecs\@:
	_aesenc_loop	0,1,2,3

	// XOR the remaining data and accumulate the unreduced GHASH products
	// for DATALEN >= 32, starting with one full 32-byte vector at a time.
.Ltail_xor_and_ghash_full_vec_loop\@:
.if \enc
	_aesenclast_and_xor	0
	vpshufb		BSWAP_MASK, AESDATA0, AESDATA0
.else
	vmovdqu		(SRC), TMP1
	vpxor		TMP1, RNDKEYLAST, TMP0
	vaesenclast	TMP0, AESDATA0, AESDATA0
	vmovdqu		AESDATA0, (DST)
	vpshufb		BSWAP_MASK, TMP1, AESDATA0
.endif
	// The ciphertext blocks (i.e. GHASH input data) are now in AESDATA0.
	vpxor		GHASH_ACC, AESDATA0, AESDATA0
	vmovdqu		(POWERS_PTR), TMP2
	_ghash_mul_noreduce	TMP2, AESDATA0, LO, MI, HI, TMP0
	vmovdqa		AESDATA1, AESDATA0
	vmovdqa		AESDATA2, AESDATA1
	vmovdqa		AESDATA3, AESDATA2
	vpxor		GHASH_ACC_XMM, GHASH_ACC_XMM, GHASH_ACC_XMM
	add		$32, SRC
	add		$32, DST
	add		$32, POWERS_PTR
	sub		$32, DATALEN
	cmp		$32, DATALEN
	jge		.Ltail_xor_and_ghash_full_vec_loop\@
	test		DATALEN, DATALEN
	jz		.Ltail_ghash_reduce\@

.Ltail_xor_and_ghash_partial_vec\@:
	// XOR the remaining data and accumulate the unreduced GHASH products,
	// for 1 <= DATALEN < 32.
	vaesenclast	RNDKEYLAST, AESDATA0, AESDATA0
	cmp		$16, DATALEN
	jle		.Ltail_xor_and_ghash_1to16bytes\@

	// Handle 17 <= DATALEN < 32.

	// Load a vpshufb mask that will right-shift by '32 - DATALEN' bytes
	// (shifting in zeroes), then reflect all 16 bytes.
	lea		.Lrshift_and_bswap_table(%rip), %rax
	vmovdqu		-16(%rax, DATALEN64), TMP2_XMM

	// Move the second keystream block to its own register and left-align it
	vextracti128	$1, AESDATA0, AESDATA1_XMM
	vpxor		.Lfifteens(%rip), TMP2_XMM, TMP0_XMM
	vpshufb		TMP0_XMM, AESDATA1_XMM, AESDATA1_XMM

	// Using overlapping loads and stores, XOR the source data with the
	// keystream and write the destination data.  Then prepare the GHASH
	// input data: the full ciphertext block and the zero-padded partial
	// ciphertext block, both byte-reflected, in AESDATA0.
.if \enc
	vpxor		-16(SRC, DATALEN64), AESDATA1_XMM, AESDATA1_XMM
	vpxor		(SRC), AESDATA0_XMM, AESDATA0_XMM
	vmovdqu		AESDATA1_XMM, -16(DST, DATALEN64)
	vmovdqu		AESDATA0_XMM, (DST)
	vpshufb		TMP2_XMM, AESDATA1_XMM, AESDATA1_XMM
	vpshufb		BSWAP_MASK_XMM, AESDATA0_XMM, AESDATA0_XMM
.else
	vmovdqu		-16(SRC, DATALEN64), TMP1_XMM
	vmovdqu		(SRC), TMP0_XMM
	vpxor		TMP1_XMM, AESDATA1_XMM, AESDATA1_XMM
	vpxor		TMP0_XMM, AESDATA0_XMM, AESDATA0_XMM
	vmovdqu		AESDATA1_XMM, -16(DST, DATALEN64)
	vmovdqu		AESDATA0_XMM, (DST)
	vpshufb		TMP2_XMM, TMP1_XMM, AESDATA1_XMM
	vpshufb		BSWAP_MASK_XMM, TMP0_XMM, AESDATA0_XMM
.endif
	vpxor		GHASH_ACC_XMM, AESDATA0_XMM, AESDATA0_XMM
	vinserti128	$1, AESDATA1_XMM, AESDATA0, AESDATA0
	vmovdqu		(POWERS_PTR), TMP2
	jmp		.Ltail_ghash_last_vec\@

.Ltail_xor_and_ghash_1to16bytes\@:
	// Handle 1 <= DATALEN <= 16.  Carefully load and store the
	// possibly-partial block, which we mustn't access out of bounds.
	vmovdqu		(POWERS_PTR), TMP2_XMM
	mov		SRC, KEY	// Free up %rcx, assuming SRC == %rcx
	mov		DATALEN, %ecx
	_load_partial_block	KEY, TMP0_XMM, POWERS_PTR, POWERS_PTR32
	vpxor		TMP0_XMM, AESDATA0_XMM, AESDATA0_XMM
	mov		DATALEN, %ecx
	_store_partial_block	AESDATA0_XMM, DST, POWERS_PTR, POWERS_PTR32
.if \enc
	lea		.Lselect_high_bytes_table(%rip), %rax
	vpshufb		BSWAP_MASK_XMM, AESDATA0_XMM, AESDATA0_XMM
	vpand		(%rax, DATALEN64), AESDATA0_XMM, AESDATA0_XMM
.else
	vpshufb		BSWAP_MASK_XMM, TMP0_XMM, AESDATA0_XMM
.endif
	vpxor		GHASH_ACC_XMM, AESDATA0_XMM, AESDATA0_XMM

.Ltail_ghash_last_vec\@:
	// Accumulate the unreduced GHASH products for the last 1-2 blocks.  The
	// GHASH input data is in AESDATA0.  If only one block remains, then the
	// second block in AESDATA0 is zero and does not affect the result.
	_ghash_mul_noreduce	TMP2, AESDATA0, LO, MI, HI, TMP0

.Ltail_ghash_reduce\@:
	// Finally, do the GHASH reduction.
	vbroadcasti128	.Lgfpoly(%rip), TMP0
	_ghash_reduce	LO, MI, HI, TMP0, TMP1
	vextracti128	$1, HI, GHASH_ACC_XMM
	vpxor		HI_XMM, GHASH_ACC_XMM, GHASH_ACC_XMM

.Ldone\@:
	// Store the updated GHASH accumulator back to memory.
	vmovdqu		GHASH_ACC_XMM, (GHASH_ACC_PTR)

	vzeroupper
	RET
.endm

// void aes_gcm_enc_final_vaes_avx2(const struct aes_gcm_key_vaes_avx2 *key,
//				    const u32 le_ctr[4], u8 ghash_acc[16],
//				    u64 total_aadlen, u64 total_datalen);
// bool aes_gcm_dec_final_vaes_avx2(const struct aes_gcm_key_vaes_avx2 *key,
//				    const u32 le_ctr[4], const u8 ghash_acc[16],
//				    u64 total_aadlen, u64 total_datalen,
//				    const u8 tag[16], int taglen);
//
// This macro generates one of the above two functions (with \enc selecting
// which one).  Both functions finish computing the GCM authentication tag by
// updating GHASH with the lengths block and encrypting the GHASH accumulator.
// |total_aadlen| and |total_datalen| must be the total length of the additional
// authenticated data and the en/decrypted data in bytes, respectively.
//
// The encryption function then stores the full-length (16-byte) computed
// authentication tag to |ghash_acc|.  The decryption function instead loads the
// expected authentication tag (the one that was transmitted) from the 16-byte
// buffer |tag|, compares the first 4 <= |taglen| <= 16 bytes of it to the
// computed tag in constant time, and returns true if and only if they match.
.macro	_aes_gcm_final	enc

	// Function arguments
	.set	KEY,		%rdi
	.set	LE_CTR_PTR,	%rsi
	.set	GHASH_ACC_PTR,	%rdx
	.set	TOTAL_AADLEN,	%rcx
	.set	TOTAL_DATALEN,	%r8
	.set	TAG,		%r9
	.set	TAGLEN,		%r10d	// Originally at 8(%rsp)
	.set	TAGLEN64,	%r10

	// Additional local variables.
	// %rax and %xmm0-%xmm3 are used as temporary registers.
	.set	AESKEYLEN,	%r11d
	.set	AESKEYLEN64,	%r11
	.set	GFPOLY,		%xmm4
	.set	BSWAP_MASK,	%xmm5
	.set	LE_CTR,		%xmm6
	.set	GHASH_ACC,	%xmm7
	.set	H_POW1,		%xmm8

	// Load some constants.
	vmovdqa		.Lgfpoly(%rip), GFPOLY
	vmovdqa		.Lbswap_mask(%rip), BSWAP_MASK

	// Load the AES key length in bytes.
	movl		OFFSETOF_AESKEYLEN(KEY), AESKEYLEN

	// Set up a counter block with 1 in the low 32-bit word.  This is the
	// counter that produces the ciphertext needed to encrypt the auth tag.
	// GFPOLY has 1 in the low word, so grab the 1 from there using a blend.
	vpblendd	$0xe, (LE_CTR_PTR), GFPOLY, LE_CTR

	// Build the lengths block and XOR it with the GHASH accumulator.
	// Although the lengths block is defined as the AAD length followed by
	// the en/decrypted data length, both in big-endian byte order, a byte
	// reflection of the full block is needed because of the way we compute
	// GHASH (see _ghash_mul_step).  By using little-endian values in the
	// opposite order, we avoid having to reflect any bytes here.
	vmovq		TOTAL_DATALEN, %xmm0
	vpinsrq		$1, TOTAL_AADLEN, %xmm0, %xmm0
	vpsllq		$3, %xmm0, %xmm0	// Bytes to bits
	vpxor		(GHASH_ACC_PTR), %xmm0, GHASH_ACC

	// Load the first hash key power (H^1), which is stored last.
	vmovdqu		OFFSETOFEND_H_POWERS-16(KEY), H_POW1

	// Load TAGLEN if decrypting.
.if !\enc
	movl		8(%rsp), TAGLEN
.endif

	// Make %rax point to the last AES round key for the chosen AES variant.
	lea		OFFSETOF_AESROUNDKEYS+6*16(KEY,AESKEYLEN64,4), %rax

	// Start the AES encryption of the counter block by swapping the counter
	// block to big-endian and XOR-ing it with the zero-th AES round key.
	vpshufb		BSWAP_MASK, LE_CTR, %xmm0
	vpxor		OFFSETOF_AESROUNDKEYS(KEY), %xmm0, %xmm0

	// Complete the AES encryption and multiply GHASH_ACC by H^1.
	// Interleave the AES and GHASH instructions to improve performance.
	cmp		$24, AESKEYLEN
	jl		128f	// AES-128?
	je		192f	// AES-192?
	// AES-256
	vaesenc		-13*16(%rax), %xmm0, %xmm0
	vaesenc		-12*16(%rax), %xmm0, %xmm0
192:
	vaesenc		-11*16(%rax), %xmm0, %xmm0
	vaesenc		-10*16(%rax), %xmm0, %xmm0
128:
.irp i, 0,1,2,3,4,5,6,7,8
	_ghash_mul_step	\i, H_POW1, GHASH_ACC, GHASH_ACC, GFPOLY, \
			%xmm1, %xmm2, %xmm3
	vaesenc		(\i-9)*16(%rax), %xmm0, %xmm0
.endr
	_ghash_mul_step	9, H_POW1, GHASH_ACC, GHASH_ACC, GFPOLY, \
			%xmm1, %xmm2, %xmm3

	// Undo the byte reflection of the GHASH accumulator.
	vpshufb		BSWAP_MASK, GHASH_ACC, GHASH_ACC

	// Do the last AES round and XOR the resulting keystream block with the
	// GHASH accumulator to produce the full computed authentication tag.
	//
	// Reduce latency by taking advantage of the property vaesenclast(key,
	// a) ^ b == vaesenclast(key ^ b, a).  I.e., XOR GHASH_ACC into the last
	// round key, instead of XOR'ing the final AES output with GHASH_ACC.
	//
	// enc_final then returns the computed auth tag, while dec_final
	// compares it with the transmitted one and returns a bool.  To compare
	// the tags, dec_final XORs them together and uses vptest to check
	// whether the result is all-zeroes.  This should be constant-time.
	// dec_final applies the vaesenclast optimization to this additional
	// value XOR'd too.
.if \enc
	vpxor		(%rax), GHASH_ACC, %xmm1
	vaesenclast	%xmm1, %xmm0, GHASH_ACC
	vmovdqu		GHASH_ACC, (GHASH_ACC_PTR)
.else
	vpxor		(TAG), GHASH_ACC, GHASH_ACC
	vpxor		(%rax), GHASH_ACC, GHASH_ACC
	vaesenclast	GHASH_ACC, %xmm0, %xmm0
	lea		.Lselect_high_bytes_table(%rip), %rax
	vmovdqu		(%rax, TAGLEN64), %xmm1
	vpshufb		BSWAP_MASK, %xmm1, %xmm1 // select low bytes, not high
	xor		%eax, %eax
	vptest		%xmm1, %xmm0
	sete		%al
.endif
	// No need for vzeroupper here, since only used xmm registers were used.
	RET
.endm

SYM_FUNC_START(aes_gcm_enc_update_vaes_avx2)
	_aes_gcm_update	1
SYM_FUNC_END(aes_gcm_enc_update_vaes_avx2)
SYM_FUNC_START(aes_gcm_dec_update_vaes_avx2)
	_aes_gcm_update	0
SYM_FUNC_END(aes_gcm_dec_update_vaes_avx2)

SYM_FUNC_START(aes_gcm_enc_final_vaes_avx2)
	_aes_gcm_final	1
SYM_FUNC_END(aes_gcm_enc_final_vaes_avx2)
SYM_FUNC_START(aes_gcm_dec_final_vaes_avx2)
	_aes_gcm_final	0
SYM_FUNC_END(aes_gcm_dec_final_vaes_avx2)
