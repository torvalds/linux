/* SPDX-License-Identifier: Apache-2.0 OR BSD-2-Clause */
//
// Copyright 2025 Google LLC
//
// Author: Eric Biggers <ebiggers@google.com>
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
// This file contains x86_64 assembly implementations of AES-CTR and AES-XCTR
// using the following sets of CPU features:
//	- AES-NI && AVX
//	- VAES && AVX2
//	- VAES && (AVX10/256 || (AVX512BW && AVX512VL)) && BMI2
//	- VAES && (AVX10/512 || (AVX512BW && AVX512VL)) && BMI2
//
// See the function definitions at the bottom of the file for more information.

#include <linux/linkage.h>
#include <linux/cfi_types.h>

.section .rodata
.p2align 4

.Lbswap_mask:
	.octa	0x000102030405060708090a0b0c0d0e0f

.Lctr_pattern:
	.quad	0, 0
.Lone:
	.quad	1, 0
.Ltwo:
	.quad	2, 0
	.quad	3, 0

.Lfour:
	.quad	4, 0

.text

// Move a vector between memory and a register.
// The register operand must be in the first 16 vector registers.
.macro	_vmovdqu	src, dst
.if VL < 64
	vmovdqu		\src, \dst
.else
	vmovdqu8	\src, \dst
.endif
.endm

// Move a vector between registers.
// The registers must be in the first 16 vector registers.
.macro	_vmovdqa	src, dst
.if VL < 64
	vmovdqa		\src, \dst
.else
	vmovdqa64	\src, \dst
.endif
.endm

// Broadcast a 128-bit value from memory to all 128-bit lanes of a vector
// register.  The register operand must be in the first 16 vector registers.
.macro	_vbroadcast128	src, dst
.if VL == 16
	vmovdqu		\src, \dst
.elseif VL == 32
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

// Load 1 <= %ecx <= 15 bytes from the pointer \src into the xmm register \dst
// and zeroize any remaining bytes.  Clobbers %rax, %rcx, and \tmp{64,32}.
.macro	_load_partial_block	src, dst, tmp64, tmp32
	sub		$8, %ecx		// LEN - 8
	jle		.Lle8\@

	// Load 9 <= LEN <= 15 bytes.
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

// Store 1 <= %ecx <= 15 bytes from the xmm register \src to the pointer \dst.
// Clobbers %rax, %rcx, and \tmp{64,32}.
.macro	_store_partial_block	src, dst, tmp64, tmp32
	sub		$8, %ecx		// LEN - 8
	jl		.Llt8\@

	// Store 8 <= LEN <= 15 bytes.
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

// Prepare the next two vectors of AES inputs in AESDATA\i0 and AESDATA\i1, and
// XOR each with the zero-th round key.  Also update LE_CTR if !\final.
.macro	_prepare_2_ctr_vecs	is_xctr, i0, i1, final=0
.if \is_xctr
  .if USE_AVX10
	_vmovdqa	LE_CTR, AESDATA\i0
	vpternlogd	$0x96, XCTR_IV, RNDKEY0, AESDATA\i0
  .else
	vpxor		XCTR_IV, LE_CTR, AESDATA\i0
	vpxor		RNDKEY0, AESDATA\i0, AESDATA\i0
  .endif
	vpaddq		LE_CTR_INC1, LE_CTR, AESDATA\i1

  .if USE_AVX10
	vpternlogd	$0x96, XCTR_IV, RNDKEY0, AESDATA\i1
  .else
	vpxor		XCTR_IV, AESDATA\i1, AESDATA\i1
	vpxor		RNDKEY0, AESDATA\i1, AESDATA\i1
  .endif
.else
	vpshufb		BSWAP_MASK, LE_CTR, AESDATA\i0
	_vpxor		RNDKEY0, AESDATA\i0, AESDATA\i0
	vpaddq		LE_CTR_INC1, LE_CTR, AESDATA\i1
	vpshufb		BSWAP_MASK, AESDATA\i1, AESDATA\i1
	_vpxor		RNDKEY0, AESDATA\i1, AESDATA\i1
.endif
.if !\final
	vpaddq		LE_CTR_INC2, LE_CTR, LE_CTR
.endif
.endm

// Do all AES rounds on the data in the given AESDATA vectors, excluding the
// zero-th and last rounds.
.macro	_aesenc_loop	vecs:vararg
	mov		KEY, %rax
1:
	_vbroadcast128	(%rax), RNDKEY
.irp i, \vecs
	vaesenc		RNDKEY, AESDATA\i, AESDATA\i
.endr
	add		$16, %rax
	cmp		%rax, RNDKEYLAST_PTR
	jne		1b
.endm

// Finalize the keystream blocks in the given AESDATA vectors by doing the last
// AES round, then XOR those keystream blocks with the corresponding data.
// Reduce latency by doing the XOR before the vaesenclast, utilizing the
// property vaesenclast(key, a) ^ b == vaesenclast(key ^ b, a).
.macro	_aesenclast_and_xor	vecs:vararg
.irp i, \vecs
	_vpxor		\i*VL(SRC), RNDKEYLAST, RNDKEY
	vaesenclast	RNDKEY, AESDATA\i, AESDATA\i
.endr
.irp i, \vecs
	_vmovdqu	AESDATA\i, \i*VL(DST)
.endr
.endm

// XOR the keystream blocks in the specified AESDATA vectors with the
// corresponding data.
.macro	_xor_data	vecs:vararg
.irp i, \vecs
	_vpxor		\i*VL(SRC), AESDATA\i, AESDATA\i
.endr
.irp i, \vecs
	_vmovdqu	AESDATA\i, \i*VL(DST)
.endr
.endm

.macro	_aes_ctr_crypt		is_xctr

	// Define register aliases V0-V15 that map to the xmm, ymm, or zmm
	// registers according to the selected Vector Length (VL).
.irp i, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
  .if VL == 16
	.set	V\i,		%xmm\i
  .elseif VL == 32
	.set	V\i,		%ymm\i
  .elseif VL == 64
	.set	V\i,		%zmm\i
  .else
	.error "Unsupported Vector Length (VL)"
  .endif
.endr

	// Function arguments
	.set	KEY,		%rdi	// Initially points to the start of the
					// crypto_aes_ctx, then is advanced to
					// point to the index 1 round key
	.set	KEY32,		%edi	// Available as temp register after all
					// keystream blocks have been generated
	.set	SRC,		%rsi	// Pointer to next source data
	.set	DST,		%rdx	// Pointer to next destination data
	.set	LEN,		%ecx	// Remaining length in bytes.
					// Note: _load_partial_block relies on
					// this being in %ecx.
	.set	LEN64,		%rcx	// Zero-extend LEN before using!
	.set	LEN8,		%cl
.if \is_xctr
	.set	XCTR_IV_PTR,	%r8	// const u8 iv[AES_BLOCK_SIZE];
	.set	XCTR_CTR,	%r9	// u64 ctr;
.else
	.set	LE_CTR_PTR,	%r8	// const u64 le_ctr[2];
.endif

	// Additional local variables
	.set	RNDKEYLAST_PTR,	%r10
	.set	AESDATA0,	V0
	.set	AESDATA0_XMM,	%xmm0
	.set	AESDATA1,	V1
	.set	AESDATA1_XMM,	%xmm1
	.set	AESDATA2,	V2
	.set	AESDATA3,	V3
	.set	AESDATA4,	V4
	.set	AESDATA5,	V5
	.set	AESDATA6,	V6
	.set	AESDATA7,	V7
.if \is_xctr
	.set	XCTR_IV,	V8
.else
	.set	BSWAP_MASK,	V8
.endif
	.set	LE_CTR,		V9
	.set	LE_CTR_XMM,	%xmm9
	.set	LE_CTR_INC1,	V10
	.set	LE_CTR_INC2,	V11
	.set	RNDKEY0,	V12
	.set	RNDKEYLAST,	V13
	.set	RNDKEY,		V14

	// Create the first vector of counters.
.if \is_xctr
  .if VL == 16
	vmovq		XCTR_CTR, LE_CTR
  .elseif VL == 32
	vmovq		XCTR_CTR, LE_CTR_XMM
	inc		XCTR_CTR
	vmovq		XCTR_CTR, AESDATA0_XMM
	vinserti128	$1, AESDATA0_XMM, LE_CTR, LE_CTR
  .else
	vpbroadcastq	XCTR_CTR, LE_CTR
	vpsrldq		$8, LE_CTR, LE_CTR
	vpaddq		.Lctr_pattern(%rip), LE_CTR, LE_CTR
  .endif
	_vbroadcast128	(XCTR_IV_PTR), XCTR_IV
.else
	_vbroadcast128	(LE_CTR_PTR), LE_CTR
  .if VL > 16
	vpaddq		.Lctr_pattern(%rip), LE_CTR, LE_CTR
  .endif
	_vbroadcast128	.Lbswap_mask(%rip), BSWAP_MASK
.endif

.if VL == 16
	_vbroadcast128	.Lone(%rip), LE_CTR_INC1
.elseif VL == 32
	_vbroadcast128	.Ltwo(%rip), LE_CTR_INC1
.else
	_vbroadcast128	.Lfour(%rip), LE_CTR_INC1
.endif
	vpsllq		$1, LE_CTR_INC1, LE_CTR_INC2

	// Load the AES key length: 16 (AES-128), 24 (AES-192), or 32 (AES-256).
	movl		480(KEY), %eax

	// Compute the pointer to the last round key.
	lea		6*16(KEY, %rax, 4), RNDKEYLAST_PTR

	// Load the zero-th and last round keys.
	_vbroadcast128	(KEY), RNDKEY0
	_vbroadcast128	(RNDKEYLAST_PTR), RNDKEYLAST

	// Make KEY point to the first round key.
	add		$16, KEY

	// This is the main loop, which encrypts 8 vectors of data at a time.
	add		$-8*VL, LEN
	jl		.Lloop_8x_done\@
.Lloop_8x\@:
	_prepare_2_ctr_vecs	\is_xctr, 0, 1
	_prepare_2_ctr_vecs	\is_xctr, 2, 3
	_prepare_2_ctr_vecs	\is_xctr, 4, 5
	_prepare_2_ctr_vecs	\is_xctr, 6, 7
	_aesenc_loop	0,1,2,3,4,5,6,7
	_aesenclast_and_xor 0,1,2,3,4,5,6,7
	sub		$-8*VL, SRC
	sub		$-8*VL, DST
	add		$-8*VL, LEN
	jge		.Lloop_8x\@
.Lloop_8x_done\@:
	sub		$-8*VL, LEN
	jz		.Ldone\@

	// 1 <= LEN < 8*VL.  Generate 2, 4, or 8 more vectors of keystream
	// blocks, depending on the remaining LEN.

	_prepare_2_ctr_vecs	\is_xctr, 0, 1
	_prepare_2_ctr_vecs	\is_xctr, 2, 3
	cmp		$4*VL, LEN
	jle		.Lenc_tail_atmost4vecs\@

	// 4*VL < LEN < 8*VL.  Generate 8 vectors of keystream blocks.  Use the
	// first 4 to XOR 4 full vectors of data.  Then XOR the remaining data.
	_prepare_2_ctr_vecs	\is_xctr, 4, 5
	_prepare_2_ctr_vecs	\is_xctr, 6, 7, final=1
	_aesenc_loop	0,1,2,3,4,5,6,7
	_aesenclast_and_xor 0,1,2,3
	vaesenclast	RNDKEYLAST, AESDATA4, AESDATA0
	vaesenclast	RNDKEYLAST, AESDATA5, AESDATA1
	vaesenclast	RNDKEYLAST, AESDATA6, AESDATA2
	vaesenclast	RNDKEYLAST, AESDATA7, AESDATA3
	sub		$-4*VL, SRC
	sub		$-4*VL, DST
	add		$-4*VL, LEN
	cmp		$1*VL-1, LEN
	jle		.Lxor_tail_partial_vec_0\@
	_xor_data	0
	cmp		$2*VL-1, LEN
	jle		.Lxor_tail_partial_vec_1\@
	_xor_data	1
	cmp		$3*VL-1, LEN
	jle		.Lxor_tail_partial_vec_2\@
	_xor_data	2
	cmp		$4*VL-1, LEN
	jle		.Lxor_tail_partial_vec_3\@
	_xor_data	3
	jmp		.Ldone\@

.Lenc_tail_atmost4vecs\@:
	cmp		$2*VL, LEN
	jle		.Lenc_tail_atmost2vecs\@

	// 2*VL < LEN <= 4*VL.  Generate 4 vectors of keystream blocks.  Use the
	// first 2 to XOR 2 full vectors of data.  Then XOR the remaining data.
	_aesenc_loop	0,1,2,3
	_aesenclast_and_xor 0,1
	vaesenclast	RNDKEYLAST, AESDATA2, AESDATA0
	vaesenclast	RNDKEYLAST, AESDATA3, AESDATA1
	sub		$-2*VL, SRC
	sub		$-2*VL, DST
	add		$-2*VL, LEN
	jmp		.Lxor_tail_upto2vecs\@

.Lenc_tail_atmost2vecs\@:
	// 1 <= LEN <= 2*VL.  Generate 2 vectors of keystream blocks.  Then XOR
	// the remaining data.
	_aesenc_loop	0,1
	vaesenclast	RNDKEYLAST, AESDATA0, AESDATA0
	vaesenclast	RNDKEYLAST, AESDATA1, AESDATA1

.Lxor_tail_upto2vecs\@:
	cmp		$1*VL-1, LEN
	jle		.Lxor_tail_partial_vec_0\@
	_xor_data	0
	cmp		$2*VL-1, LEN
	jle		.Lxor_tail_partial_vec_1\@
	_xor_data	1
	jmp		.Ldone\@

.Lxor_tail_partial_vec_1\@:
	add		$-1*VL, LEN
	jz		.Ldone\@
	sub		$-1*VL, SRC
	sub		$-1*VL, DST
	_vmovdqa	AESDATA1, AESDATA0
	jmp		.Lxor_tail_partial_vec_0\@

.Lxor_tail_partial_vec_2\@:
	add		$-2*VL, LEN
	jz		.Ldone\@
	sub		$-2*VL, SRC
	sub		$-2*VL, DST
	_vmovdqa	AESDATA2, AESDATA0
	jmp		.Lxor_tail_partial_vec_0\@

.Lxor_tail_partial_vec_3\@:
	add		$-3*VL, LEN
	jz		.Ldone\@
	sub		$-3*VL, SRC
	sub		$-3*VL, DST
	_vmovdqa	AESDATA3, AESDATA0

.Lxor_tail_partial_vec_0\@:
	// XOR the remaining 1 <= LEN < VL bytes.  It's easy if masked
	// loads/stores are available; otherwise it's a bit harder...
.if USE_AVX10
  .if VL <= 32
	mov		$-1, %eax
	bzhi		LEN, %eax, %eax
	kmovd		%eax, %k1
  .else
	mov		$-1, %rax
	bzhi		LEN64, %rax, %rax
	kmovq		%rax, %k1
  .endif
	vmovdqu8	(SRC), AESDATA1{%k1}{z}
	_vpxor		AESDATA1, AESDATA0, AESDATA0
	vmovdqu8	AESDATA0, (DST){%k1}
.else
  .if VL == 32
	cmp		$16, LEN
	jl		1f
	vpxor		(SRC), AESDATA0_XMM, AESDATA1_XMM
	vmovdqu		AESDATA1_XMM, (DST)
	add		$16, SRC
	add		$16, DST
	sub		$16, LEN
	jz		.Ldone\@
	vextracti128	$1, AESDATA0, AESDATA0_XMM
1:
  .endif
	mov		LEN, %r10d
	_load_partial_block	SRC, AESDATA1_XMM, KEY, KEY32
	vpxor		AESDATA1_XMM, AESDATA0_XMM, AESDATA0_XMM
	mov		%r10d, %ecx
	_store_partial_block	AESDATA0_XMM, DST, KEY, KEY32
.endif

.Ldone\@:
.if VL > 16
	vzeroupper
.endif
	RET
.endm

// Below are the definitions of the functions generated by the above macro.
// They have the following prototypes:
//
//
// void aes_ctr64_crypt_##suffix(const struct crypto_aes_ctx *key,
//				 const u8 *src, u8 *dst, int len,
//				 const u64 le_ctr[2]);
//
// void aes_xctr_crypt_##suffix(const struct crypto_aes_ctx *key,
//				const u8 *src, u8 *dst, int len,
//				const u8 iv[AES_BLOCK_SIZE], u64 ctr);
//
// Both functions generate |len| bytes of keystream, XOR it with the data from
// |src|, and write the result to |dst|.  On non-final calls, |len| must be a
// multiple of 16.  On the final call, |len| can be any value.
//
// aes_ctr64_crypt_* implement "regular" CTR, where the keystream is generated
// from a 128-bit big endian counter that increments by 1 for each AES block.
// HOWEVER, to keep the assembly code simple, some of the counter management is
// left to the caller.  aes_ctr64_crypt_* take the counter in little endian
// form, only increment the low 64 bits internally, do the conversion to big
// endian internally, and don't write the updated counter back to memory.  The
// caller is responsible for converting the starting IV to the little endian
// le_ctr, detecting the (very rare) case of a carry out of the low 64 bits
// being needed and splitting at that point with a carry done in between, and
// updating le_ctr after each part if the message is multi-part.
//
// aes_xctr_crypt_* implement XCTR as specified in "Length-preserving encryption
// with HCTR2" (https://eprint.iacr.org/2021/1441.pdf).  XCTR is an
// easier-to-implement variant of CTR that uses little endian byte order and
// eliminates carries.  |ctr| is the per-message block counter starting at 1.

.set	VL, 16
.set	USE_AVX10, 0
SYM_TYPED_FUNC_START(aes_ctr64_crypt_aesni_avx)
	_aes_ctr_crypt	0
SYM_FUNC_END(aes_ctr64_crypt_aesni_avx)
SYM_TYPED_FUNC_START(aes_xctr_crypt_aesni_avx)
	_aes_ctr_crypt	1
SYM_FUNC_END(aes_xctr_crypt_aesni_avx)

#if defined(CONFIG_AS_VAES) && defined(CONFIG_AS_VPCLMULQDQ)
.set	VL, 32
.set	USE_AVX10, 0
SYM_TYPED_FUNC_START(aes_ctr64_crypt_vaes_avx2)
	_aes_ctr_crypt	0
SYM_FUNC_END(aes_ctr64_crypt_vaes_avx2)
SYM_TYPED_FUNC_START(aes_xctr_crypt_vaes_avx2)
	_aes_ctr_crypt	1
SYM_FUNC_END(aes_xctr_crypt_vaes_avx2)

.set	VL, 32
.set	USE_AVX10, 1
SYM_TYPED_FUNC_START(aes_ctr64_crypt_vaes_avx10_256)
	_aes_ctr_crypt	0
SYM_FUNC_END(aes_ctr64_crypt_vaes_avx10_256)
SYM_TYPED_FUNC_START(aes_xctr_crypt_vaes_avx10_256)
	_aes_ctr_crypt	1
SYM_FUNC_END(aes_xctr_crypt_vaes_avx10_256)

.set	VL, 64
.set	USE_AVX10, 1
SYM_TYPED_FUNC_START(aes_ctr64_crypt_vaes_avx10_512)
	_aes_ctr_crypt	0
SYM_FUNC_END(aes_ctr64_crypt_vaes_avx10_512)
SYM_TYPED_FUNC_START(aes_xctr_crypt_vaes_avx10_512)
	_aes_ctr_crypt	1
SYM_FUNC_END(aes_xctr_crypt_vaes_avx10_512)
#endif // CONFIG_AS_VAES && CONFIG_AS_VPCLMULQDQ
