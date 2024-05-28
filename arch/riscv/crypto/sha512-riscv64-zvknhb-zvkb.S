/* SPDX-License-Identifier: Apache-2.0 OR BSD-2-Clause */
//
// This file is dual-licensed, meaning that you can use it under your
// choice of either of the following two licenses:
//
// Copyright 2023 The OpenSSL Project Authors. All Rights Reserved.
//
// Licensed under the Apache License 2.0 (the "License"). You can obtain
// a copy in the file LICENSE in the source distribution or at
// https://www.openssl.org/source/license.html
//
// or
//
// Copyright (c) 2023, Christoph Müllner <christoph.muellner@vrull.eu>
// Copyright (c) 2023, Phoebe Chen <phoebe.chen@sifive.com>
// Copyright 2024 Google LLC
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// The generated code of this file depends on the following RISC-V extensions:
// - RV64I
// - RISC-V Vector ('V') with VLEN >= 128
// - RISC-V Vector SHA-2 Secure Hash extension ('Zvknhb')
// - RISC-V Vector Cryptography Bit-manipulation extension ('Zvkb')

#include <linux/cfi_types.h>

.text
.option arch, +zvknhb, +zvkb

#define STATEP		a0
#define DATA		a1
#define NUM_BLOCKS	a2

#define STATEP_C	a3
#define K		a4

#define MASK		v0
#define INDICES		v1
#define W0		v10	// LMUL=2
#define W1		v12	// LMUL=2
#define W2		v14	// LMUL=2
#define W3		v16	// LMUL=2
#define VTMP		v20	// LMUL=2
#define FEBA		v22	// LMUL=2
#define HGDC		v24	// LMUL=2
#define PREV_FEBA	v26	// LMUL=2
#define PREV_HGDC	v28	// LMUL=2

// Do 4 rounds of SHA-512.  w0 contains the current 4 message schedule words.
//
// If not all the message schedule words have been computed yet, then this also
// computes 4 more message schedule words.  w1-w3 contain the next 3 groups of 4
// message schedule words; this macro computes the group after w3 and writes it
// to w0.  This means that the next (w0, w1, w2, w3) is the current (w1, w2, w3,
// w0), so the caller must cycle through the registers accordingly.
.macro	sha512_4rounds	last, w0, w1, w2, w3
	vle64.v		VTMP, (K)
	addi		K, K, 32
	vadd.vv		VTMP, VTMP, \w0
	vsha2cl.vv	HGDC, FEBA, VTMP
	vsha2ch.vv	FEBA, HGDC, VTMP
.if !\last
	vmerge.vvm	VTMP, \w2, \w1, MASK
	vsha2ms.vv	\w0, VTMP, \w3
.endif
.endm

.macro	sha512_16rounds	last
	sha512_4rounds	\last, W0, W1, W2, W3
	sha512_4rounds	\last, W1, W2, W3, W0
	sha512_4rounds	\last, W2, W3, W0, W1
	sha512_4rounds	\last, W3, W0, W1, W2
.endm

// void sha512_transform_zvknhb_zvkb(u64 state[8], const u8 *data,
//				     int num_blocks);
SYM_TYPED_FUNC_START(sha512_transform_zvknhb_zvkb)

	// Setup mask for the vmerge to replace the first word (idx==0) in
	// message scheduling.  There are 4 words, so an 8-bit mask suffices.
	vsetivli	zero, 1, e8, m1, ta, ma
	vmv.v.i		MASK, 0x01

	// Load the state.  The state is stored as {a,b,c,d,e,f,g,h}, but we
	// need {f,e,b,a},{h,g,d,c}.  The dst vtype is e64m2 and the index vtype
	// is e8mf4.  We use index-load with the i8 indices {40, 32, 8, 0},
	// loaded using the 32-bit little endian value 0x00082028.
	li		t0, 0x00082028
	vsetivli	zero, 1, e32, m1, ta, ma
	vmv.v.x		INDICES, t0
	addi		STATEP_C, STATEP, 16
	vsetivli	zero, 4, e64, m2, ta, ma
	vluxei8.v	FEBA, (STATEP), INDICES
	vluxei8.v	HGDC, (STATEP_C), INDICES

.Lnext_block:
	la		K, K512
	addi		NUM_BLOCKS, NUM_BLOCKS, -1

	// Save the previous state, as it's needed later.
	vmv.v.v		PREV_FEBA, FEBA
	vmv.v.v		PREV_HGDC, HGDC

	// Load the next 1024-bit message block and endian-swap each 64-bit word
	vle64.v		W0, (DATA)
	vrev8.v		W0, W0
	addi		DATA, DATA, 32
	vle64.v		W1, (DATA)
	vrev8.v		W1, W1
	addi		DATA, DATA, 32
	vle64.v		W2, (DATA)
	vrev8.v		W2, W2
	addi		DATA, DATA, 32
	vle64.v		W3, (DATA)
	vrev8.v		W3, W3
	addi		DATA, DATA, 32

	// Do the 80 rounds of SHA-512.
	sha512_16rounds 0
	sha512_16rounds 0
	sha512_16rounds 0
	sha512_16rounds 0
	sha512_16rounds 1

	// Add the previous state.
	vadd.vv		FEBA, FEBA, PREV_FEBA
	vadd.vv		HGDC, HGDC, PREV_HGDC

	// Repeat if more blocks remain.
	bnez		NUM_BLOCKS, .Lnext_block

	// Store the new state and return.
	vsuxei8.v	FEBA, (STATEP), INDICES
	vsuxei8.v	HGDC, (STATEP_C), INDICES
	ret
SYM_FUNC_END(sha512_transform_zvknhb_zvkb)

.section ".rodata"
.p2align 3
.type K512, @object
K512:
	.dword		0x428a2f98d728ae22, 0x7137449123ef65cd
	.dword		0xb5c0fbcfec4d3b2f, 0xe9b5dba58189dbbc
	.dword		0x3956c25bf348b538, 0x59f111f1b605d019
	.dword		0x923f82a4af194f9b, 0xab1c5ed5da6d8118
	.dword		0xd807aa98a3030242, 0x12835b0145706fbe
	.dword		0x243185be4ee4b28c, 0x550c7dc3d5ffb4e2
	.dword		0x72be5d74f27b896f, 0x80deb1fe3b1696b1
	.dword		0x9bdc06a725c71235, 0xc19bf174cf692694
	.dword		0xe49b69c19ef14ad2, 0xefbe4786384f25e3
	.dword		0x0fc19dc68b8cd5b5, 0x240ca1cc77ac9c65
	.dword		0x2de92c6f592b0275, 0x4a7484aa6ea6e483
	.dword		0x5cb0a9dcbd41fbd4, 0x76f988da831153b5
	.dword		0x983e5152ee66dfab, 0xa831c66d2db43210
	.dword		0xb00327c898fb213f, 0xbf597fc7beef0ee4
	.dword		0xc6e00bf33da88fc2, 0xd5a79147930aa725
	.dword		0x06ca6351e003826f, 0x142929670a0e6e70
	.dword		0x27b70a8546d22ffc, 0x2e1b21385c26c926
	.dword		0x4d2c6dfc5ac42aed, 0x53380d139d95b3df
	.dword		0x650a73548baf63de, 0x766a0abb3c77b2a8
	.dword		0x81c2c92e47edaee6, 0x92722c851482353b
	.dword		0xa2bfe8a14cf10364, 0xa81a664bbc423001
	.dword		0xc24b8b70d0f89791, 0xc76c51a30654be30
	.dword		0xd192e819d6ef5218, 0xd69906245565a910
	.dword		0xf40e35855771202a, 0x106aa07032bbd1b8
	.dword		0x19a4c116b8d2d0c8, 0x1e376c085141ab53
	.dword		0x2748774cdf8eeb99, 0x34b0bcb5e19b48a8
	.dword		0x391c0cb3c5c95a63, 0x4ed8aa4ae3418acb
	.dword		0x5b9cca4f7763e373, 0x682e6ff3d6b2b8a3
	.dword		0x748f82ee5defb2fc, 0x78a5636f43172f60
	.dword		0x84c87814a1f0ab72, 0x8cc702081a6439ec
	.dword		0x90befffa23631e28, 0xa4506cebde82bde9
	.dword		0xbef9a3f7b2c67915, 0xc67178f2e372532b
	.dword		0xca273eceea26619c, 0xd186b8c721c0c207
	.dword		0xeada7dd6cde0eb1e, 0xf57d4f7fee6ed178
	.dword		0x06f067aa72176fba, 0x0a637dc5a2c898a6
	.dword		0x113f9804bef90dae, 0x1b710b35131c471b
	.dword		0x28db77f523047d84, 0x32caab7b40c72493
	.dword		0x3c9ebe0a15c9bebc, 0x431d67c49c100d4c
	.dword		0x4cc5d4becb3e42b6, 0x597f299cfc657e2a
	.dword		0x5fcb6fab3ad6faec, 0x6c44198c4a475817
.size K512, . - K512
