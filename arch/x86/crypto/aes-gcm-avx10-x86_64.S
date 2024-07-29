/* SPDX-License-Identifier: Apache-2.0 OR BSD-2-Clause */
//
// VAES and VPCLMULQDQ optimized AES-GCM for x86_64
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
//
//------------------------------------------------------------------------------
//
// This file implements AES-GCM (Galois/Counter Mode) for x86_64 CPUs that
// support VAES (vector AES), VPCLMULQDQ (vector carryless multiplication), and
// either AVX512 or AVX10.  Some of the functions, notably the encryption and
// decryption update functions which are the most performance-critical, are
// provided in two variants generated from a macro: one using 256-bit vectors
// (suffix: vaes_avx10_256) and one using 512-bit vectors (vaes_avx10_512).  The
// other, "shared" functions (vaes_avx10) use at most 256-bit vectors.
//
// The functions that use 512-bit vectors are intended for CPUs that support
// 512-bit vectors *and* where using them doesn't cause significant
// downclocking.  They require the following CPU features:
//
//	VAES && VPCLMULQDQ && BMI2 && ((AVX512BW && AVX512VL) || AVX10/512)
//
// The other functions require the following CPU features:
//
//	VAES && VPCLMULQDQ && BMI2 && ((AVX512BW && AVX512VL) || AVX10/256)
//
// All functions use the "System V" ABI.  The Windows ABI is not supported.
//
// Note that we use "avx10" in the names of the functions as a shorthand to
// really mean "AVX10 or a certain set of AVX512 features".  Due to Intel's
// introduction of AVX512 and then its replacement by AVX10, there doesn't seem
// to be a simple way to name things that makes sense on all CPUs.
//
// Note that the macros that support both 256-bit and 512-bit vectors could
// fairly easily be changed to support 128-bit too.  However, this would *not*
// be sufficient to allow the code to run on CPUs without AVX512 or AVX10,
// because the code heavily uses several features of these extensions other than
// the vector length: the increase in the number of SIMD registers from 16 to
// 32, masking support, and new instructions such as vpternlogd (which can do a
// three-argument XOR).  These features are very useful for AES-GCM.

#include <linux/linkage.h>

.section .rodata
.p2align 6

	// A shuffle mask that reflects the bytes of 16-byte blocks
.Lbswap_mask:
	.octa   0x000102030405060708090a0b0c0d0e0f

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

	// The below constants are used for incrementing the counter blocks.
	// ctr_pattern points to the four 128-bit values [0, 1, 2, 3].
	// inc_2blocks and inc_4blocks point to the single 128-bit values 2 and
	// 4.  Note that the same '2' is reused in ctr_pattern and inc_2blocks.
.Lctr_pattern:
	.octa	0
	.octa	1
.Linc_2blocks:
	.octa	2
	.octa	3
.Linc_4blocks:
	.octa	4

// Number of powers of the hash key stored in the key struct.  The powers are
// stored from highest (H^NUM_H_POWERS) to lowest (H^1).
#define NUM_H_POWERS		16

// Offset to AES key length (in bytes) in the key struct
#define OFFSETOF_AESKEYLEN	480

// Offset to start of hash key powers array in the key struct
#define OFFSETOF_H_POWERS	512

// Offset to end of hash key powers array in the key struct.
//
// This is immediately followed by three zeroized padding blocks, which are
// included so that partial vectors can be handled more easily.  E.g. if VL=64
// and two blocks remain, we load the 4 values [H^2, H^1, 0, 0].  The most
// padding blocks needed is 3, which occurs if [H^1, 0, 0, 0] is loaded.
#define OFFSETOFEND_H_POWERS	(OFFSETOF_H_POWERS + (NUM_H_POWERS * 16))

.text

// Set the vector length in bytes.  This sets the VL variable and defines
// register aliases V0-V31 that map to the ymm or zmm registers.
.macro	_set_veclen	vl
	.set	VL,	\vl
.irp i, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15, \
	16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
.if VL == 32
	.set	V\i,	%ymm\i
.elseif VL == 64
	.set	V\i,	%zmm\i
.else
	.error "Unsupported vector length"
.endif
.endr
.endm

// The _ghash_mul_step macro does one step of GHASH multiplication of the
// 128-bit lanes of \a by the corresponding 128-bit lanes of \b and storing the
// reduced products in \dst.  \t0, \t1, and \t2 are temporary registers of the
// same size as \a and \b.  To complete all steps, this must invoked with \i=0
// through \i=9.  The division into steps allows users of this macro to
// optionally interleave the computation with other instructions.  Users of this
// macro must preserve the parameter registers across steps.
//
// The multiplications are done in GHASH's representation of the finite field
// GF(2^128).  Elements of GF(2^128) are represented as binary polynomials
// (i.e. polynomials whose coefficients are bits) modulo a reducing polynomial
// G.  The GCM specification uses G = x^128 + x^7 + x^2 + x + 1.  Addition is
// just XOR, while multiplication is more complex and has two parts: (a) do
// carryless multiplication of two 128-bit input polynomials to get a 256-bit
// intermediate product polynomial, and (b) reduce the intermediate product to
// 128 bits by adding multiples of G that cancel out terms in it.  (Adding
// multiples of G doesn't change which field element the polynomial represents.)
//
// Unfortunately, the GCM specification maps bits to/from polynomial
// coefficients backwards from the natural order.  In each byte it specifies the
// highest bit to be the lowest order polynomial coefficient, *not* the highest!
// This makes it nontrivial to work with the GHASH polynomials.  We could
// reflect the bits, but x86 doesn't have an instruction that does that.
//
// Instead, we operate on the values without bit-reflecting them.  This *mostly*
// just works, since XOR and carryless multiplication are symmetric with respect
// to bit order, but it has some consequences.  First, due to GHASH's byte
// order, by skipping bit reflection, *byte* reflection becomes necessary to
// give the polynomial terms a consistent order.  E.g., considering an N-bit
// value interpreted using the G = x^128 + x^7 + x^2 + x + 1 convention, bits 0
// through N-1 of the byte-reflected value represent the coefficients of x^(N-1)
// through x^0, whereas bits 0 through N-1 of the non-byte-reflected value
// represent x^7...x^0, x^15...x^8, ..., x^(N-1)...x^(N-8) which can't be worked
// with.  Fortunately, x86's vpshufb instruction can do byte reflection.
//
// Second, forgoing the bit reflection causes an extra multiple of x (still
// using the G = x^128 + x^7 + x^2 + x + 1 convention) to be introduced by each
// multiplication.  This is because an M-bit by N-bit carryless multiplication
// really produces a (M+N-1)-bit product, but in practice it's zero-extended to
// M+N bits.  In the G = x^128 + x^7 + x^2 + x + 1 convention, which maps bits
// to polynomial coefficients backwards, this zero-extension actually changes
// the product by introducing an extra factor of x.  Therefore, users of this
// macro must ensure that one of the inputs has an extra factor of x^-1, i.e.
// the multiplicative inverse of x, to cancel out the extra x.
//
// Third, the backwards coefficients convention is just confusing to work with,
// since it makes "low" and "high" in the polynomial math mean the opposite of
// their normal meaning in computer programming.  This can be solved by using an
// alternative interpretation: the polynomial coefficients are understood to be
// in the natural order, and the multiplication is actually \a * \b * x^-128 mod
// x^128 + x^127 + x^126 + x^121 + 1.  This doesn't change the inputs, outputs,
// or the implementation at all; it just changes the mathematical interpretation
// of what each instruction is doing.  Starting from here, we'll use this
// alternative interpretation, as it's easier to understand the code that way.
//
// Moving onto the implementation, the vpclmulqdq instruction does 64 x 64 =>
// 128-bit carryless multiplication, so we break the 128 x 128 multiplication
// into parts as follows (the _L and _H suffixes denote low and high 64 bits):
//
//     LO = a_L * b_L
//     MI = (a_L * b_H) + (a_H * b_L)
//     HI = a_H * b_H
//
// The 256-bit product is x^128*HI + x^64*MI + LO.  LO, MI, and HI are 128-bit.
// Note that MI "overlaps" with LO and HI.  We don't consolidate MI into LO and
// HI right away, since the way the reduction works makes that unnecessary.
//
// For the reduction, we cancel out the low 128 bits by adding multiples of G =
// x^128 + x^127 + x^126 + x^121 + 1.  This is done by two iterations, each of
// which cancels out the next lowest 64 bits.  Consider a value x^64*A + B,
// where A and B are 128-bit.  Adding B_L*G to that value gives:
//
//       x^64*A + B + B_L*G
//     = x^64*A + x^64*B_H + B_L + B_L*(x^128 + x^127 + x^126 + x^121 + 1)
//     = x^64*A + x^64*B_H + B_L + x^128*B_L + x^64*B_L*(x^63 + x^62 + x^57) + B_L
//     = x^64*A + x^64*B_H + x^128*B_L + x^64*B_L*(x^63 + x^62 + x^57) + B_L + B_L
//     = x^64*(A + B_H + x^64*B_L + B_L*(x^63 + x^62 + x^57))
//
// So: if we sum A, B with its halves swapped, and the low half of B times x^63
// + x^62 + x^57, we get a 128-bit value C where x^64*C is congruent to the
// original value x^64*A + B.  I.e., the low 64 bits got canceled out.
//
// We just need to apply this twice: first to fold LO into MI, and second to
// fold the updated MI into HI.
//
// The needed three-argument XORs are done using the vpternlogd instruction with
// immediate 0x96, since this is faster than two vpxord instructions.
//
// A potential optimization, assuming that b is fixed per-key (if a is fixed
// per-key it would work the other way around), is to use one iteration of the
// reduction described above to precompute a value c such that x^64*c = b mod G,
// and then multiply a_L by c (and implicitly by x^64) instead of by b:
//
//     MI = (a_L * c_L) + (a_H * b_L)
//     HI = (a_L * c_H) + (a_H * b_H)
//
// This would eliminate the LO part of the intermediate product, which would
// eliminate the need to fold LO into MI.  This would save two instructions,
// including a vpclmulqdq.  However, we currently don't use this optimization
// because it would require twice as many per-key precomputed values.
//
// Using Karatsuba multiplication instead of "schoolbook" multiplication
// similarly would save a vpclmulqdq but does not seem to be worth it.
.macro	_ghash_mul_step	i, a, b, dst, gfpoly, t0, t1, t2
.if \i == 0
	vpclmulqdq	$0x00, \a, \b, \t0	  // LO = a_L * b_L
	vpclmulqdq	$0x01, \a, \b, \t1	  // MI_0 = a_L * b_H
.elseif \i == 1
	vpclmulqdq	$0x10, \a, \b, \t2	  // MI_1 = a_H * b_L
.elseif \i == 2
	vpxord		\t2, \t1, \t1		  // MI = MI_0 + MI_1
.elseif \i == 3
	vpclmulqdq	$0x01, \t0, \gfpoly, \t2  // LO_L*(x^63 + x^62 + x^57)
.elseif \i == 4
	vpshufd		$0x4e, \t0, \t0		  // Swap halves of LO
.elseif \i == 5
	vpternlogd	$0x96, \t2, \t0, \t1	  // Fold LO into MI
.elseif \i == 6
	vpclmulqdq	$0x11, \a, \b, \dst	  // HI = a_H * b_H
.elseif \i == 7
	vpclmulqdq	$0x01, \t1, \gfpoly, \t0  // MI_L*(x^63 + x^62 + x^57)
.elseif \i == 8
	vpshufd		$0x4e, \t1, \t1		  // Swap halves of MI
.elseif \i == 9
	vpternlogd	$0x96, \t0, \t1, \dst	  // Fold MI into HI
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
.macro	_ghash_mul_noreduce	a, b, lo, mi, hi, t0, t1, t2, t3
	vpclmulqdq	$0x00, \a, \b, \t0	// a_L * b_L
	vpclmulqdq	$0x01, \a, \b, \t1	// a_L * b_H
	vpclmulqdq	$0x10, \a, \b, \t2	// a_H * b_L
	vpclmulqdq	$0x11, \a, \b, \t3	// a_H * b_H
	vpxord		\t0, \lo, \lo
	vpternlogd	$0x96, \t2, \t1, \mi
	vpxord		\t3, \hi, \hi
.endm

// Reduce the unreduced products from \lo, \mi, and \hi and store the 128-bit
// reduced products in \hi.  See _ghash_mul_step for explanation of reduction.
.macro	_ghash_reduce	lo, mi, hi, gfpoly, t0
	vpclmulqdq	$0x01, \lo, \gfpoly, \t0
	vpshufd		$0x4e, \lo, \lo
	vpternlogd	$0x96, \t0, \lo, \mi
	vpclmulqdq	$0x01, \mi, \gfpoly, \t0
	vpshufd		$0x4e, \mi, \mi
	vpternlogd	$0x96, \t0, \mi, \hi
.endm

// void aes_gcm_precompute_##suffix(struct aes_gcm_key_avx10 *key);
//
// Given the expanded AES key |key->aes_key|, this function derives the GHASH
// subkey and initializes |key->ghash_key_powers| with powers of it.
//
// The number of key powers initialized is NUM_H_POWERS, and they are stored in
// the order H^NUM_H_POWERS to H^1.  The zeroized padding blocks after the key
// powers themselves are also initialized.
//
// This macro supports both VL=32 and VL=64.  _set_veclen must have been invoked
// with the desired length.  In the VL=32 case, the function computes twice as
// many key powers than are actually used by the VL=32 GCM update functions.
// This is done to keep the key format the same regardless of vector length.
.macro	_aes_gcm_precompute

	// Function arguments
	.set	KEY,		%rdi

	// Additional local variables.  V0-V2 and %rax are used as temporaries.
	.set	POWERS_PTR,	%rsi
	.set	RNDKEYLAST_PTR,	%rdx
	.set	H_CUR,		V3
	.set	H_CUR_YMM,	%ymm3
	.set	H_CUR_XMM,	%xmm3
	.set	H_INC,		V4
	.set	H_INC_YMM,	%ymm4
	.set	H_INC_XMM,	%xmm4
	.set	GFPOLY,		V5
	.set	GFPOLY_YMM,	%ymm5
	.set	GFPOLY_XMM,	%xmm5

	// Get pointer to lowest set of key powers (located at end of array).
	lea		OFFSETOFEND_H_POWERS-VL(KEY), POWERS_PTR

	// Encrypt an all-zeroes block to get the raw hash subkey.
	movl		OFFSETOF_AESKEYLEN(KEY), %eax
	lea		6*16(KEY,%rax,4), RNDKEYLAST_PTR
	vmovdqu		(KEY), %xmm0  // Zero-th round key XOR all-zeroes block
	add		$16, KEY
1:
	vaesenc		(KEY), %xmm0, %xmm0
	add		$16, KEY
	cmp		KEY, RNDKEYLAST_PTR
	jne		1b
	vaesenclast	(RNDKEYLAST_PTR), %xmm0, %xmm0

	// Reflect the bytes of the raw hash subkey.
	vpshufb		.Lbswap_mask(%rip), %xmm0, H_CUR_XMM

	// Zeroize the padding blocks.
	vpxor		%xmm0, %xmm0, %xmm0
	vmovdqu		%ymm0, VL(POWERS_PTR)
	vmovdqu		%xmm0, VL+2*16(POWERS_PTR)

	// Finish preprocessing the first key power, H^1.  Since this GHASH
	// implementation operates directly on values with the backwards bit
	// order specified by the GCM standard, it's necessary to preprocess the
	// raw key as follows.  First, reflect its bytes.  Second, multiply it
	// by x^-1 mod x^128 + x^7 + x^2 + x + 1 (if using the backwards
	// interpretation of polynomial coefficients), which can also be
	// interpreted as multiplication by x mod x^128 + x^127 + x^126 + x^121
	// + 1 using the alternative, natural interpretation of polynomial
	// coefficients.  For details, see the comment above _ghash_mul_step.
	//
	// Either way, for the multiplication the concrete operation performed
	// is a left shift of the 128-bit value by 1 bit, then an XOR with (0xc2
	// << 120) | 1 if a 1 bit was carried out.  However, there's no 128-bit
	// wide shift instruction, so instead double each of the two 64-bit
	// halves and incorporate the internal carry bit into the value XOR'd.
	vpshufd		$0xd3, H_CUR_XMM, %xmm0
	vpsrad		$31, %xmm0, %xmm0
	vpaddq		H_CUR_XMM, H_CUR_XMM, H_CUR_XMM
	vpand		.Lgfpoly_and_internal_carrybit(%rip), %xmm0, %xmm0
	vpxor		%xmm0, H_CUR_XMM, H_CUR_XMM

	// Load the gfpoly constant.
	vbroadcasti32x4	.Lgfpoly(%rip), GFPOLY

	// Square H^1 to get H^2.
	//
	// Note that as with H^1, all higher key powers also need an extra
	// factor of x^-1 (or x using the natural interpretation).  Nothing
	// special needs to be done to make this happen, though: H^1 * H^1 would
	// end up with two factors of x^-1, but the multiplication consumes one.
	// So the product H^2 ends up with the desired one factor of x^-1.
	_ghash_mul	H_CUR_XMM, H_CUR_XMM, H_INC_XMM, GFPOLY_XMM, \
			%xmm0, %xmm1, %xmm2

	// Create H_CUR_YMM = [H^2, H^1] and H_INC_YMM = [H^2, H^2].
	vinserti128	$1, H_CUR_XMM, H_INC_YMM, H_CUR_YMM
	vinserti128	$1, H_INC_XMM, H_INC_YMM, H_INC_YMM

.if VL == 64
	// Create H_CUR = [H^4, H^3, H^2, H^1] and H_INC = [H^4, H^4, H^4, H^4].
	_ghash_mul	H_INC_YMM, H_CUR_YMM, H_INC_YMM, GFPOLY_YMM, \
			%ymm0, %ymm1, %ymm2
	vinserti64x4	$1, H_CUR_YMM, H_INC, H_CUR
	vshufi64x2	$0, H_INC, H_INC, H_INC
.endif

	// Store the lowest set of key powers.
	vmovdqu8	H_CUR, (POWERS_PTR)

	// Compute and store the remaining key powers.  With VL=32, repeatedly
	// multiply [H^(i+1), H^i] by [H^2, H^2] to get [H^(i+3), H^(i+2)].
	// With VL=64, repeatedly multiply [H^(i+3), H^(i+2), H^(i+1), H^i] by
	// [H^4, H^4, H^4, H^4] to get [H^(i+7), H^(i+6), H^(i+5), H^(i+4)].
	mov		$(NUM_H_POWERS*16/VL) - 1, %eax
.Lprecompute_next\@:
	sub		$VL, POWERS_PTR
	_ghash_mul	H_INC, H_CUR, H_CUR, GFPOLY, V0, V1, V2
	vmovdqu8	H_CUR, (POWERS_PTR)
	dec		%eax
	jnz		.Lprecompute_next\@

	vzeroupper	// This is needed after using ymm or zmm registers.
	RET
.endm

// XOR together the 128-bit lanes of \src (whose low lane is \src_xmm) and store
// the result in \dst_xmm.  This implicitly zeroizes the other lanes of dst.
.macro	_horizontal_xor	src, src_xmm, dst_xmm, t0_xmm, t1_xmm, t2_xmm
	vextracti32x4	$1, \src, \t0_xmm
.if VL == 32
	vpxord		\t0_xmm, \src_xmm, \dst_xmm
.elseif VL == 64
	vextracti32x4	$2, \src, \t1_xmm
	vextracti32x4	$3, \src, \t2_xmm
	vpxord		\t0_xmm, \src_xmm, \dst_xmm
	vpternlogd	$0x96, \t1_xmm, \t2_xmm, \dst_xmm
.else
	.error "Unsupported vector length"
.endif
.endm

// Do one step of the GHASH update of the data blocks given in the vector
// registers GHASHDATA[0-3].  \i specifies the step to do, 0 through 9.  The
// division into steps allows users of this macro to optionally interleave the
// computation with other instructions.  This macro uses the vector register
// GHASH_ACC as input/output; GHASHDATA[0-3] as inputs that are clobbered;
// H_POW[4-1], GFPOLY, and BSWAP_MASK as inputs that aren't clobbered; and
// GHASHTMP[0-2] as temporaries.  This macro handles the byte-reflection of the
// data blocks.  The parameter registers must be preserved across steps.
//
// The GHASH update does: GHASH_ACC = H_POW4*(GHASHDATA0 + GHASH_ACC) +
// H_POW3*GHASHDATA1 + H_POW2*GHASHDATA2 + H_POW1*GHASHDATA3, where the
// operations are vectorized operations on vectors of 16-byte blocks.  E.g.,
// with VL=32 there are 2 blocks per vector and the vectorized terms correspond
// to the following non-vectorized terms:
//
//	H_POW4*(GHASHDATA0 + GHASH_ACC) => H^8*(blk0 + GHASH_ACC_XMM) and H^7*(blk1 + 0)
//	H_POW3*GHASHDATA1 => H^6*blk2 and H^5*blk3
//	H_POW2*GHASHDATA2 => H^4*blk4 and H^3*blk5
//	H_POW1*GHASHDATA3 => H^2*blk6 and H^1*blk7
//
// With VL=64, we use 4 blocks/vector, H^16 through H^1, and blk0 through blk15.
//
// More concretely, this code does:
//   - Do vectorized "schoolbook" multiplications to compute the intermediate
//     256-bit product of each block and its corresponding hash key power.
//     There are 4*VL/16 of these intermediate products.
//   - Sum (XOR) the intermediate 256-bit products across vectors.  This leaves
//     VL/16 256-bit intermediate values.
//   - Do a vectorized reduction of these 256-bit intermediate values to
//     128-bits each.  This leaves VL/16 128-bit intermediate values.
//   - Sum (XOR) these values and store the 128-bit result in GHASH_ACC_XMM.
//
// See _ghash_mul_step for the full explanation of the operations performed for
// each individual finite field multiplication and reduction.
.macro	_ghash_step_4x	i
.if \i == 0
	vpshufb		BSWAP_MASK, GHASHDATA0, GHASHDATA0
	vpxord		GHASH_ACC, GHASHDATA0, GHASHDATA0
	vpshufb		BSWAP_MASK, GHASHDATA1, GHASHDATA1
	vpshufb		BSWAP_MASK, GHASHDATA2, GHASHDATA2
.elseif \i == 1
	vpshufb		BSWAP_MASK, GHASHDATA3, GHASHDATA3
	vpclmulqdq	$0x00, H_POW4, GHASHDATA0, GHASH_ACC	// LO_0
	vpclmulqdq	$0x00, H_POW3, GHASHDATA1, GHASHTMP0	// LO_1
	vpclmulqdq	$0x00, H_POW2, GHASHDATA2, GHASHTMP1	// LO_2
.elseif \i == 2
	vpxord		GHASHTMP0, GHASH_ACC, GHASH_ACC		// sum(LO_{1,0})
	vpclmulqdq	$0x00, H_POW1, GHASHDATA3, GHASHTMP2	// LO_3
	vpternlogd	$0x96, GHASHTMP2, GHASHTMP1, GHASH_ACC	// LO = sum(LO_{3,2,1,0})
	vpclmulqdq	$0x01, H_POW4, GHASHDATA0, GHASHTMP0	// MI_0
.elseif \i == 3
	vpclmulqdq	$0x01, H_POW3, GHASHDATA1, GHASHTMP1	// MI_1
	vpclmulqdq	$0x01, H_POW2, GHASHDATA2, GHASHTMP2	// MI_2
	vpternlogd	$0x96, GHASHTMP2, GHASHTMP1, GHASHTMP0	// sum(MI_{2,1,0})
	vpclmulqdq	$0x01, H_POW1, GHASHDATA3, GHASHTMP1	// MI_3
.elseif \i == 4
	vpclmulqdq	$0x10, H_POW4, GHASHDATA0, GHASHTMP2	// MI_4
	vpternlogd	$0x96, GHASHTMP2, GHASHTMP1, GHASHTMP0	// sum(MI_{4,3,2,1,0})
	vpclmulqdq	$0x10, H_POW3, GHASHDATA1, GHASHTMP1	// MI_5
	vpclmulqdq	$0x10, H_POW2, GHASHDATA2, GHASHTMP2	// MI_6
.elseif \i == 5
	vpternlogd	$0x96, GHASHTMP2, GHASHTMP1, GHASHTMP0	// sum(MI_{6,5,4,3,2,1,0})
	vpclmulqdq	$0x01, GHASH_ACC, GFPOLY, GHASHTMP2	// LO_L*(x^63 + x^62 + x^57)
	vpclmulqdq	$0x10, H_POW1, GHASHDATA3, GHASHTMP1	// MI_7
	vpxord		GHASHTMP1, GHASHTMP0, GHASHTMP0		// MI = sum(MI_{7,6,5,4,3,2,1,0})
.elseif \i == 6
	vpshufd		$0x4e, GHASH_ACC, GHASH_ACC		// Swap halves of LO
	vpclmulqdq	$0x11, H_POW4, GHASHDATA0, GHASHDATA0	// HI_0
	vpclmulqdq	$0x11, H_POW3, GHASHDATA1, GHASHDATA1	// HI_1
	vpclmulqdq	$0x11, H_POW2, GHASHDATA2, GHASHDATA2	// HI_2
.elseif \i == 7
	vpternlogd	$0x96, GHASHTMP2, GHASH_ACC, GHASHTMP0	// Fold LO into MI
	vpclmulqdq	$0x11, H_POW1, GHASHDATA3, GHASHDATA3	// HI_3
	vpternlogd	$0x96, GHASHDATA2, GHASHDATA1, GHASHDATA0 // sum(HI_{2,1,0})
	vpclmulqdq	$0x01, GHASHTMP0, GFPOLY, GHASHTMP1	// MI_L*(x^63 + x^62 + x^57)
.elseif \i == 8
	vpxord		GHASHDATA3, GHASHDATA0, GHASH_ACC	// HI = sum(HI_{3,2,1,0})
	vpshufd		$0x4e, GHASHTMP0, GHASHTMP0		// Swap halves of MI
	vpternlogd	$0x96, GHASHTMP1, GHASHTMP0, GHASH_ACC	// Fold MI into HI
.elseif \i == 9
	_horizontal_xor	GHASH_ACC, GHASH_ACC_XMM, GHASH_ACC_XMM, \
			GHASHDATA0_XMM, GHASHDATA1_XMM, GHASHDATA2_XMM
.endif
.endm

// Do one non-last round of AES encryption on the counter blocks in V0-V3 using
// the round key that has been broadcast to all 128-bit lanes of \round_key.
.macro	_vaesenc_4x	round_key
	vaesenc		\round_key, V0, V0
	vaesenc		\round_key, V1, V1
	vaesenc		\round_key, V2, V2
	vaesenc		\round_key, V3, V3
.endm

// Start the AES encryption of four vectors of counter blocks.
.macro	_ctr_begin_4x

	// Increment LE_CTR four times to generate four vectors of little-endian
	// counter blocks, swap each to big-endian, and store them in V0-V3.
	vpshufb		BSWAP_MASK, LE_CTR, V0
	vpaddd		LE_CTR_INC, LE_CTR, LE_CTR
	vpshufb		BSWAP_MASK, LE_CTR, V1
	vpaddd		LE_CTR_INC, LE_CTR, LE_CTR
	vpshufb		BSWAP_MASK, LE_CTR, V2
	vpaddd		LE_CTR_INC, LE_CTR, LE_CTR
	vpshufb		BSWAP_MASK, LE_CTR, V3
	vpaddd		LE_CTR_INC, LE_CTR, LE_CTR

	// AES "round zero": XOR in the zero-th round key.
	vpxord		RNDKEY0, V0, V0
	vpxord		RNDKEY0, V1, V1
	vpxord		RNDKEY0, V2, V2
	vpxord		RNDKEY0, V3, V3
.endm

// void aes_gcm_{enc,dec}_update_##suffix(const struct aes_gcm_key_avx10 *key,
//					  const u32 le_ctr[4], u8 ghash_acc[16],
//					  const u8 *src, u8 *dst, int datalen);
//
// This macro generates a GCM encryption or decryption update function with the
// above prototype (with \enc selecting which one).  This macro supports both
// VL=32 and VL=64.  _set_veclen must have been invoked with the desired length.
//
// This function computes the next portion of the CTR keystream, XOR's it with
// |datalen| bytes from |src|, and writes the resulting encrypted or decrypted
// data to |dst|.  It also updates the GHASH accumulator |ghash_acc| using the
// next |datalen| ciphertext bytes.
//
// |datalen| must be a multiple of 16, except on the last call where it can be
// any length.  The caller must do any buffering needed to ensure this.  Both
// in-place and out-of-place en/decryption are supported.
//
// |le_ctr| must give the current counter in little-endian format.  For a new
// message, the low word of the counter must be 2.  This function loads the
// counter from |le_ctr| and increments the loaded counter as needed, but it
// does *not* store the updated counter back to |le_ctr|.  The caller must
// update |le_ctr| if any more data segments follow.  Internally, only the low
// 32-bit word of the counter is incremented, following the GCM standard.
.macro	_aes_gcm_update	enc

	// Function arguments
	.set	KEY,		%rdi
	.set	LE_CTR_PTR,	%rsi
	.set	GHASH_ACC_PTR,	%rdx
	.set	SRC,		%rcx
	.set	DST,		%r8
	.set	DATALEN,	%r9d
	.set	DATALEN64,	%r9	// Zero-extend DATALEN before using!

	// Additional local variables

	// %rax and %k1 are used as temporary registers.  LE_CTR_PTR is also
	// available as a temporary register after the counter is loaded.

	// AES key length in bytes
	.set	AESKEYLEN,	%r10d
	.set	AESKEYLEN64,	%r10

	// Pointer to the last AES round key for the chosen AES variant
	.set	RNDKEYLAST_PTR,	%r11

	// In the main loop, V0-V3 are used as AES input and output.  Elsewhere
	// they are used as temporary registers.

	// GHASHDATA[0-3] hold the ciphertext blocks and GHASH input data.
	.set	GHASHDATA0,	V4
	.set	GHASHDATA0_XMM,	%xmm4
	.set	GHASHDATA1,	V5
	.set	GHASHDATA1_XMM,	%xmm5
	.set	GHASHDATA2,	V6
	.set	GHASHDATA2_XMM,	%xmm6
	.set	GHASHDATA3,	V7

	// BSWAP_MASK is the shuffle mask for byte-reflecting 128-bit values
	// using vpshufb, copied to all 128-bit lanes.
	.set	BSWAP_MASK,	V8

	// RNDKEY temporarily holds the next AES round key.
	.set	RNDKEY,		V9

	// GHASH_ACC is the accumulator variable for GHASH.  When fully reduced,
	// only the lowest 128-bit lane can be nonzero.  When not fully reduced,
	// more than one lane may be used, and they need to be XOR'd together.
	.set	GHASH_ACC,	V10
	.set	GHASH_ACC_XMM,	%xmm10

	// LE_CTR_INC is the vector of 32-bit words that need to be added to a
	// vector of little-endian counter blocks to advance it forwards.
	.set	LE_CTR_INC,	V11

	// LE_CTR contains the next set of little-endian counter blocks.
	.set	LE_CTR,		V12

	// RNDKEY0, RNDKEYLAST, and RNDKEY_M[9-5] contain cached AES round keys,
	// copied to all 128-bit lanes.  RNDKEY0 is the zero-th round key,
	// RNDKEYLAST the last, and RNDKEY_M\i the one \i-th from the last.
	.set	RNDKEY0,	V13
	.set	RNDKEYLAST,	V14
	.set	RNDKEY_M9,	V15
	.set	RNDKEY_M8,	V16
	.set	RNDKEY_M7,	V17
	.set	RNDKEY_M6,	V18
	.set	RNDKEY_M5,	V19

	// RNDKEYLAST[0-3] temporarily store the last AES round key XOR'd with
	// the corresponding block of source data.  This is useful because
	// vaesenclast(key, a) ^ b == vaesenclast(key ^ b, a), and key ^ b can
	// be computed in parallel with the AES rounds.
	.set	RNDKEYLAST0,	V20
	.set	RNDKEYLAST1,	V21
	.set	RNDKEYLAST2,	V22
	.set	RNDKEYLAST3,	V23

	// GHASHTMP[0-2] are temporary variables used by _ghash_step_4x.  These
	// cannot coincide with anything used for AES encryption, since for
	// performance reasons GHASH and AES encryption are interleaved.
	.set	GHASHTMP0,	V24
	.set	GHASHTMP1,	V25
	.set	GHASHTMP2,	V26

	// H_POW[4-1] contain the powers of the hash key H^(4*VL/16)...H^1.  The
	// descending numbering reflects the order of the key powers.
	.set	H_POW4,		V27
	.set	H_POW3,		V28
	.set	H_POW2,		V29
	.set	H_POW1,		V30

	// GFPOLY contains the .Lgfpoly constant, copied to all 128-bit lanes.
	.set	GFPOLY,		V31

	// Load some constants.
	vbroadcasti32x4	.Lbswap_mask(%rip), BSWAP_MASK
	vbroadcasti32x4	.Lgfpoly(%rip), GFPOLY

	// Load the GHASH accumulator and the starting counter.
	vmovdqu		(GHASH_ACC_PTR), GHASH_ACC_XMM
	vbroadcasti32x4	(LE_CTR_PTR), LE_CTR

	// Load the AES key length in bytes.
	movl		OFFSETOF_AESKEYLEN(KEY), AESKEYLEN

	// Make RNDKEYLAST_PTR point to the last AES round key.  This is the
	// round key with index 10, 12, or 14 for AES-128, AES-192, or AES-256
	// respectively.  Then load the zero-th and last round keys.
	lea		6*16(KEY,AESKEYLEN64,4), RNDKEYLAST_PTR
	vbroadcasti32x4	(KEY), RNDKEY0
	vbroadcasti32x4	(RNDKEYLAST_PTR), RNDKEYLAST

	// Finish initializing LE_CTR by adding [0, 1, ...] to its low words.
	vpaddd		.Lctr_pattern(%rip), LE_CTR, LE_CTR

	// Initialize LE_CTR_INC to contain VL/16 in all 128-bit lanes.
.if VL == 32
	vbroadcasti32x4	.Linc_2blocks(%rip), LE_CTR_INC
.elseif VL == 64
	vbroadcasti32x4	.Linc_4blocks(%rip), LE_CTR_INC
.else
	.error "Unsupported vector length"
.endif

	// If there are at least 4*VL bytes of data, then continue into the loop
	// that processes 4*VL bytes of data at a time.  Otherwise skip it.
	//
	// Pre-subtracting 4*VL from DATALEN saves an instruction from the main
	// loop and also ensures that at least one write always occurs to
	// DATALEN, zero-extending it and allowing DATALEN64 to be used later.
	sub		$4*VL, DATALEN
	jl		.Lcrypt_loop_4x_done\@

	// Load powers of the hash key.
	vmovdqu8	OFFSETOFEND_H_POWERS-4*VL(KEY), H_POW4
	vmovdqu8	OFFSETOFEND_H_POWERS-3*VL(KEY), H_POW3
	vmovdqu8	OFFSETOFEND_H_POWERS-2*VL(KEY), H_POW2
	vmovdqu8	OFFSETOFEND_H_POWERS-1*VL(KEY), H_POW1

	// Main loop: en/decrypt and hash 4 vectors at a time.
	//
	// When possible, interleave the AES encryption of the counter blocks
	// with the GHASH update of the ciphertext blocks.  This improves
	// performance on many CPUs because the execution ports used by the VAES
	// instructions often differ from those used by vpclmulqdq and other
	// instructions used in GHASH.  For example, many Intel CPUs dispatch
	// vaesenc to ports 0 and 1 and vpclmulqdq to port 5.
	//
	// The interleaving is easiest to do during decryption, since during
	// decryption the ciphertext blocks are immediately available.  For
	// encryption, instead encrypt the first set of blocks, then hash those
	// blocks while encrypting the next set of blocks, repeat that as
	// needed, and finally hash the last set of blocks.

.if \enc
	// Encrypt the first 4 vectors of plaintext blocks.  Leave the resulting
	// ciphertext in GHASHDATA[0-3] for GHASH.
	_ctr_begin_4x
	lea		16(KEY), %rax
1:
	vbroadcasti32x4	(%rax), RNDKEY
	_vaesenc_4x	RNDKEY
	add		$16, %rax
	cmp		%rax, RNDKEYLAST_PTR
	jne		1b
	vpxord		0*VL(SRC), RNDKEYLAST, RNDKEYLAST0
	vpxord		1*VL(SRC), RNDKEYLAST, RNDKEYLAST1
	vpxord		2*VL(SRC), RNDKEYLAST, RNDKEYLAST2
	vpxord		3*VL(SRC), RNDKEYLAST, RNDKEYLAST3
	vaesenclast	RNDKEYLAST0, V0, GHASHDATA0
	vaesenclast	RNDKEYLAST1, V1, GHASHDATA1
	vaesenclast	RNDKEYLAST2, V2, GHASHDATA2
	vaesenclast	RNDKEYLAST3, V3, GHASHDATA3
	vmovdqu8	GHASHDATA0, 0*VL(DST)
	vmovdqu8	GHASHDATA1, 1*VL(DST)
	vmovdqu8	GHASHDATA2, 2*VL(DST)
	vmovdqu8	GHASHDATA3, 3*VL(DST)
	add		$4*VL, SRC
	add		$4*VL, DST
	sub		$4*VL, DATALEN
	jl		.Lghash_last_ciphertext_4x\@
.endif

	// Cache as many additional AES round keys as possible.
.irp i, 9,8,7,6,5
	vbroadcasti32x4	-\i*16(RNDKEYLAST_PTR), RNDKEY_M\i
.endr

.Lcrypt_loop_4x\@:

	// If decrypting, load more ciphertext blocks into GHASHDATA[0-3].  If
	// encrypting, GHASHDATA[0-3] already contain the previous ciphertext.
.if !\enc
	vmovdqu8	0*VL(SRC), GHASHDATA0
	vmovdqu8	1*VL(SRC), GHASHDATA1
	vmovdqu8	2*VL(SRC), GHASHDATA2
	vmovdqu8	3*VL(SRC), GHASHDATA3
.endif

	// Start the AES encryption of the counter blocks.
	_ctr_begin_4x
	cmp		$24, AESKEYLEN
	jl		128f	// AES-128?
	je		192f	// AES-192?
	// AES-256
	vbroadcasti32x4	-13*16(RNDKEYLAST_PTR), RNDKEY
	_vaesenc_4x	RNDKEY
	vbroadcasti32x4	-12*16(RNDKEYLAST_PTR), RNDKEY
	_vaesenc_4x	RNDKEY
192:
	vbroadcasti32x4	-11*16(RNDKEYLAST_PTR), RNDKEY
	_vaesenc_4x	RNDKEY
	vbroadcasti32x4	-10*16(RNDKEYLAST_PTR), RNDKEY
	_vaesenc_4x	RNDKEY
128:

	// XOR the source data with the last round key, saving the result in
	// RNDKEYLAST[0-3].  This reduces latency by taking advantage of the
	// property vaesenclast(key, a) ^ b == vaesenclast(key ^ b, a).
.if \enc
	vpxord		0*VL(SRC), RNDKEYLAST, RNDKEYLAST0
	vpxord		1*VL(SRC), RNDKEYLAST, RNDKEYLAST1
	vpxord		2*VL(SRC), RNDKEYLAST, RNDKEYLAST2
	vpxord		3*VL(SRC), RNDKEYLAST, RNDKEYLAST3
.else
	vpxord		GHASHDATA0, RNDKEYLAST, RNDKEYLAST0
	vpxord		GHASHDATA1, RNDKEYLAST, RNDKEYLAST1
	vpxord		GHASHDATA2, RNDKEYLAST, RNDKEYLAST2
	vpxord		GHASHDATA3, RNDKEYLAST, RNDKEYLAST3
.endif

	// Finish the AES encryption of the counter blocks in V0-V3, interleaved
	// with the GHASH update of the ciphertext blocks in GHASHDATA[0-3].
.irp i, 9,8,7,6,5
	_vaesenc_4x	RNDKEY_M\i
	_ghash_step_4x	(9 - \i)
.endr
.irp i, 4,3,2,1
	vbroadcasti32x4	-\i*16(RNDKEYLAST_PTR), RNDKEY
	_vaesenc_4x	RNDKEY
	_ghash_step_4x	(9 - \i)
.endr
	_ghash_step_4x	9

	// Do the last AES round.  This handles the XOR with the source data
	// too, as per the optimization described above.
	vaesenclast	RNDKEYLAST0, V0, GHASHDATA0
	vaesenclast	RNDKEYLAST1, V1, GHASHDATA1
	vaesenclast	RNDKEYLAST2, V2, GHASHDATA2
	vaesenclast	RNDKEYLAST3, V3, GHASHDATA3

	// Store the en/decrypted data to DST.
	vmovdqu8	GHASHDATA0, 0*VL(DST)
	vmovdqu8	GHASHDATA1, 1*VL(DST)
	vmovdqu8	GHASHDATA2, 2*VL(DST)
	vmovdqu8	GHASHDATA3, 3*VL(DST)

	add		$4*VL, SRC
	add		$4*VL, DST
	sub		$4*VL, DATALEN
	jge		.Lcrypt_loop_4x\@

.if \enc
.Lghash_last_ciphertext_4x\@:
	// Update GHASH with the last set of ciphertext blocks.
.irp i, 0,1,2,3,4,5,6,7,8,9
	_ghash_step_4x	\i
.endr
.endif

.Lcrypt_loop_4x_done\@:

	// Undo the extra subtraction by 4*VL and check whether data remains.
	add		$4*VL, DATALEN
	jz		.Ldone\@

	// The data length isn't a multiple of 4*VL.  Process the remaining data
	// of length 1 <= DATALEN < 4*VL, up to one vector (VL bytes) at a time.
	// Going one vector at a time may seem inefficient compared to having
	// separate code paths for each possible number of vectors remaining.
	// However, using a loop keeps the code size down, and it performs
	// surprising well; modern CPUs will start executing the next iteration
	// before the previous one finishes and also predict the number of loop
	// iterations.  For a similar reason, we roll up the AES rounds.
	//
	// On the last iteration, the remaining length may be less than VL.
	// Handle this using masking.
	//
	// Since there are enough key powers available for all remaining data,
	// there is no need to do a GHASH reduction after each iteration.
	// Instead, multiply each remaining block by its own key power, and only
	// do a GHASH reduction at the very end.

	// Make POWERS_PTR point to the key powers [H^N, H^(N-1), ...] where N
	// is the number of blocks that remain.
	.set		POWERS_PTR, LE_CTR_PTR	// LE_CTR_PTR is free to be reused.
	mov		DATALEN, %eax
	neg		%rax
	and		$~15, %rax  // -round_up(DATALEN, 16)
	lea		OFFSETOFEND_H_POWERS(KEY,%rax), POWERS_PTR

	// Start collecting the unreduced GHASH intermediate value LO, MI, HI.
	.set		LO, GHASHDATA0
	.set		LO_XMM, GHASHDATA0_XMM
	.set		MI, GHASHDATA1
	.set		MI_XMM, GHASHDATA1_XMM
	.set		HI, GHASHDATA2
	.set		HI_XMM, GHASHDATA2_XMM
	vpxor		LO_XMM, LO_XMM, LO_XMM
	vpxor		MI_XMM, MI_XMM, MI_XMM
	vpxor		HI_XMM, HI_XMM, HI_XMM

.Lcrypt_loop_1x\@:

	// Select the appropriate mask for this iteration: all 1's if
	// DATALEN >= VL, otherwise DATALEN 1's.  Do this branchlessly using the
	// bzhi instruction from BMI2.  (This relies on DATALEN <= 255.)
.if VL < 64
	mov		$-1, %eax
	bzhi		DATALEN, %eax, %eax
	kmovd		%eax, %k1
.else
	mov		$-1, %rax
	bzhi		DATALEN64, %rax, %rax
	kmovq		%rax, %k1
.endif

	// Encrypt a vector of counter blocks.  This does not need to be masked.
	vpshufb		BSWAP_MASK, LE_CTR, V0
	vpaddd		LE_CTR_INC, LE_CTR, LE_CTR
	vpxord		RNDKEY0, V0, V0
	lea		16(KEY), %rax
1:
	vbroadcasti32x4	(%rax), RNDKEY
	vaesenc		RNDKEY, V0, V0
	add		$16, %rax
	cmp		%rax, RNDKEYLAST_PTR
	jne		1b
	vaesenclast	RNDKEYLAST, V0, V0

	// XOR the data with the appropriate number of keystream bytes.
	vmovdqu8	(SRC), V1{%k1}{z}
	vpxord		V1, V0, V0
	vmovdqu8	V0, (DST){%k1}

	// Update GHASH with the ciphertext block(s), without reducing.
	//
	// In the case of DATALEN < VL, the ciphertext is zero-padded to VL.
	// (If decrypting, it's done by the above masked load.  If encrypting,
	// it's done by the below masked register-to-register move.)  Note that
	// if DATALEN <= VL - 16, there will be additional padding beyond the
	// padding of the last block specified by GHASH itself; i.e., there may
	// be whole block(s) that get processed by the GHASH multiplication and
	// reduction instructions but should not actually be included in the
	// GHASH.  However, any such blocks are all-zeroes, and the values that
	// they're multiplied with are also all-zeroes.  Therefore they just add
	// 0 * 0 = 0 to the final GHASH result, which makes no difference.
	vmovdqu8        (POWERS_PTR), H_POW1
.if \enc
	vmovdqu8	V0, V1{%k1}{z}
.endif
	vpshufb		BSWAP_MASK, V1, V0
	vpxord		GHASH_ACC, V0, V0
	_ghash_mul_noreduce	H_POW1, V0, LO, MI, HI, GHASHDATA3, V1, V2, V3
	vpxor		GHASH_ACC_XMM, GHASH_ACC_XMM, GHASH_ACC_XMM

	add		$VL, POWERS_PTR
	add		$VL, SRC
	add		$VL, DST
	sub		$VL, DATALEN
	jg		.Lcrypt_loop_1x\@

	// Finally, do the GHASH reduction.
	_ghash_reduce	LO, MI, HI, GFPOLY, V0
	_horizontal_xor	HI, HI_XMM, GHASH_ACC_XMM, %xmm0, %xmm1, %xmm2

.Ldone\@:
	// Store the updated GHASH accumulator back to memory.
	vmovdqu		GHASH_ACC_XMM, (GHASH_ACC_PTR)

	vzeroupper	// This is needed after using ymm or zmm registers.
	RET
.endm

// void aes_gcm_enc_final_vaes_avx10(const struct aes_gcm_key_avx10 *key,
//				     const u32 le_ctr[4], u8 ghash_acc[16],
//				     u64 total_aadlen, u64 total_datalen);
// bool aes_gcm_dec_final_vaes_avx10(const struct aes_gcm_key_avx10 *key,
//				     const u32 le_ctr[4],
//				     const u8 ghash_acc[16],
//				     u64 total_aadlen, u64 total_datalen,
//				     const u8 tag[16], int taglen);
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

	// Additional local variables.
	// %rax, %xmm0-%xmm3, and %k1 are used as temporary registers.
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
	vmovdqu8	OFFSETOFEND_H_POWERS-16(KEY), H_POW1

.if !\enc
	// Prepare a mask of TAGLEN one bits.
	movl		8(%rsp), TAGLEN
	mov		$-1, %eax
	bzhi		TAGLEN, %eax, %eax
	kmovd		%eax, %k1
.endif

	// Make %rax point to the last AES round key for the chosen AES variant.
	lea		6*16(KEY,AESKEYLEN64,4), %rax

	// Start the AES encryption of the counter block by swapping the counter
	// block to big-endian and XOR-ing it with the zero-th AES round key.
	vpshufb		BSWAP_MASK, LE_CTR, %xmm0
	vpxor		(KEY), %xmm0, %xmm0

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
	// value XOR'd too, using vpternlogd to XOR the last round key, GHASH
	// accumulator, and transmitted auth tag together in one instruction.
.if \enc
	vpxor		(%rax), GHASH_ACC, %xmm1
	vaesenclast	%xmm1, %xmm0, GHASH_ACC
	vmovdqu		GHASH_ACC, (GHASH_ACC_PTR)
.else
	vmovdqu		(TAG), %xmm1
	vpternlogd	$0x96, (%rax), GHASH_ACC, %xmm1
	vaesenclast	%xmm1, %xmm0, %xmm0
	xor		%eax, %eax
	vmovdqu8	%xmm0, %xmm0{%k1}{z}	// Truncate to TAGLEN bytes
	vptest		%xmm0, %xmm0
	sete		%al
.endif
	// No need for vzeroupper here, since only used xmm registers were used.
	RET
.endm

_set_veclen 32
SYM_FUNC_START(aes_gcm_precompute_vaes_avx10_256)
	_aes_gcm_precompute
SYM_FUNC_END(aes_gcm_precompute_vaes_avx10_256)
SYM_FUNC_START(aes_gcm_enc_update_vaes_avx10_256)
	_aes_gcm_update	1
SYM_FUNC_END(aes_gcm_enc_update_vaes_avx10_256)
SYM_FUNC_START(aes_gcm_dec_update_vaes_avx10_256)
	_aes_gcm_update	0
SYM_FUNC_END(aes_gcm_dec_update_vaes_avx10_256)

_set_veclen 64
SYM_FUNC_START(aes_gcm_precompute_vaes_avx10_512)
	_aes_gcm_precompute
SYM_FUNC_END(aes_gcm_precompute_vaes_avx10_512)
SYM_FUNC_START(aes_gcm_enc_update_vaes_avx10_512)
	_aes_gcm_update	1
SYM_FUNC_END(aes_gcm_enc_update_vaes_avx10_512)
SYM_FUNC_START(aes_gcm_dec_update_vaes_avx10_512)
	_aes_gcm_update	0
SYM_FUNC_END(aes_gcm_dec_update_vaes_avx10_512)

// void aes_gcm_aad_update_vaes_avx10(const struct aes_gcm_key_avx10 *key,
//				      u8 ghash_acc[16],
//				      const u8 *aad, int aadlen);
//
// This function processes the AAD (Additional Authenticated Data) in GCM.
// Using the key |key|, it updates the GHASH accumulator |ghash_acc| with the
// data given by |aad| and |aadlen|.  |key->ghash_key_powers| must have been
// initialized.  On the first call, |ghash_acc| must be all zeroes.  |aadlen|
// must be a multiple of 16, except on the last call where it can be any length.
// The caller must do any buffering needed to ensure this.
//
// AES-GCM is almost always used with small amounts of AAD, less than 32 bytes.
// Therefore, for AAD processing we currently only provide this implementation
// which uses 256-bit vectors (ymm registers) and only has a 1x-wide loop.  This
// keeps the code size down, and it enables some micro-optimizations, e.g. using
// VEX-coded instructions instead of EVEX-coded to save some instruction bytes.
// To optimize for large amounts of AAD, we could implement a 4x-wide loop and
// provide a version using 512-bit vectors, but that doesn't seem to be useful.
SYM_FUNC_START(aes_gcm_aad_update_vaes_avx10)

	// Function arguments
	.set	KEY,		%rdi
	.set	GHASH_ACC_PTR,	%rsi
	.set	AAD,		%rdx
	.set	AADLEN,		%ecx
	.set	AADLEN64,	%rcx	// Zero-extend AADLEN before using!

	// Additional local variables.
	// %rax, %ymm0-%ymm3, and %k1 are used as temporary registers.
	.set	BSWAP_MASK,	%ymm4
	.set	GFPOLY,		%ymm5
	.set	GHASH_ACC,	%ymm6
	.set	GHASH_ACC_XMM,	%xmm6
	.set	H_POW1,		%ymm7

	// Load some constants.
	vbroadcasti128	.Lbswap_mask(%rip), BSWAP_MASK
	vbroadcasti128	.Lgfpoly(%rip), GFPOLY

	// Load the GHASH accumulator.
	vmovdqu		(GHASH_ACC_PTR), GHASH_ACC_XMM

	// Update GHASH with 32 bytes of AAD at a time.
	//
	// Pre-subtracting 32 from AADLEN saves an instruction from the loop and
	// also ensures that at least one write always occurs to AADLEN,
	// zero-extending it and allowing AADLEN64 to be used later.
	sub		$32, AADLEN
	jl		.Laad_loop_1x_done
	vmovdqu8	OFFSETOFEND_H_POWERS-32(KEY), H_POW1	// [H^2, H^1]
.Laad_loop_1x:
	vmovdqu		(AAD), %ymm0
	vpshufb		BSWAP_MASK, %ymm0, %ymm0
	vpxor		%ymm0, GHASH_ACC, GHASH_ACC
	_ghash_mul	H_POW1, GHASH_ACC, GHASH_ACC, GFPOLY, \
			%ymm0, %ymm1, %ymm2
	vextracti128	$1, GHASH_ACC, %xmm0
	vpxor		%xmm0, GHASH_ACC_XMM, GHASH_ACC_XMM
	add		$32, AAD
	sub		$32, AADLEN
	jge		.Laad_loop_1x
.Laad_loop_1x_done:
	add		$32, AADLEN
	jz		.Laad_done

	// Update GHASH with the remaining 1 <= AADLEN < 32 bytes of AAD.
	mov		$-1, %eax
	bzhi		AADLEN, %eax, %eax
	kmovd		%eax, %k1
	vmovdqu8	(AAD), %ymm0{%k1}{z}
	neg		AADLEN64
	and		$~15, AADLEN64  // -round_up(AADLEN, 16)
	vmovdqu8	OFFSETOFEND_H_POWERS(KEY,AADLEN64), H_POW1
	vpshufb		BSWAP_MASK, %ymm0, %ymm0
	vpxor		%ymm0, GHASH_ACC, GHASH_ACC
	_ghash_mul	H_POW1, GHASH_ACC, GHASH_ACC, GFPOLY, \
			%ymm0, %ymm1, %ymm2
	vextracti128	$1, GHASH_ACC, %xmm0
	vpxor		%xmm0, GHASH_ACC_XMM, GHASH_ACC_XMM

.Laad_done:
	// Store the updated GHASH accumulator back to memory.
	vmovdqu		GHASH_ACC_XMM, (GHASH_ACC_PTR)

	vzeroupper	// This is needed after using ymm or zmm registers.
	RET
SYM_FUNC_END(aes_gcm_aad_update_vaes_avx10)

SYM_FUNC_START(aes_gcm_enc_final_vaes_avx10)
	_aes_gcm_final	1
SYM_FUNC_END(aes_gcm_enc_final_vaes_avx10)
SYM_FUNC_START(aes_gcm_dec_final_vaes_avx10)
	_aes_gcm_final	0
SYM_FUNC_END(aes_gcm_dec_final_vaes_avx10)
