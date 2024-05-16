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
// - RISC-V Vector SHA-2 Secure Hash extension ('Zvknha' or 'Zvknhb')
// - RISC-V Vector Cryptography Bit-manipulation extension ('Zvkb')

#include <linux/cfi_types.h>

.text
.option arch, +zvknha, +zvkb

#define STATEP		a0
#define DATA		a1
#define NUM_BLOCKS	a2

#define STATEP_C	a3

#define MASK		v0
#define INDICES		v1
#define W0		v2
#define W1		v3
#define W2		v4
#define W3		v5
#define VTMP		v6
#define FEBA		v7
#define HGDC		v8
#define K0		v10
#define K1		v11
#define K2		v12
#define K3		v13
#define K4		v14
#define K5		v15
#define K6		v16
#define K7		v17
#define K8		v18
#define K9		v19
#define K10		v20
#define K11		v21
#define K12		v22
#define K13		v23
#define K14		v24
#define K15		v25
#define PREV_FEBA	v26
#define PREV_HGDC	v27

// Do 4 rounds of SHA-256.  w0 contains the current 4 message schedule words.
//
// If not all the message schedule words have been computed yet, then this also
// computes 4 more message schedule words.  w1-w3 contain the next 3 groups of 4
// message schedule words; this macro computes the group after w3 and writes it
// to w0.  This means that the next (w0, w1, w2, w3) is the current (w1, w2, w3,
// w0), so the caller must cycle through the registers accordingly.
.macro	sha256_4rounds	last, k, w0, w1, w2, w3
	vadd.vv		VTMP, \k, \w0
	vsha2cl.vv	HGDC, FEBA, VTMP
	vsha2ch.vv	FEBA, HGDC, VTMP
.if !\last
	vmerge.vvm	VTMP, \w2, \w1, MASK
	vsha2ms.vv	\w0, VTMP, \w3
.endif
.endm

.macro	sha256_16rounds	last, k0, k1, k2, k3
	sha256_4rounds	\last, \k0, W0, W1, W2, W3
	sha256_4rounds	\last, \k1, W1, W2, W3, W0
	sha256_4rounds	\last, \k2, W2, W3, W0, W1
	sha256_4rounds	\last, \k3, W3, W0, W1, W2
.endm

// void sha256_transform_zvknha_or_zvknhb_zvkb(u32 state[8], const u8 *data,
//					       int num_blocks);
SYM_TYPED_FUNC_START(sha256_transform_zvknha_or_zvknhb_zvkb)

	// Load the round constants into K0-K15.
	vsetivli	zero, 4, e32, m1, ta, ma
	la		t0, K256
	vle32.v		K0, (t0)
	addi		t0, t0, 16
	vle32.v		K1, (t0)
	addi		t0, t0, 16
	vle32.v		K2, (t0)
	addi		t0, t0, 16
	vle32.v		K3, (t0)
	addi		t0, t0, 16
	vle32.v		K4, (t0)
	addi		t0, t0, 16
	vle32.v		K5, (t0)
	addi		t0, t0, 16
	vle32.v		K6, (t0)
	addi		t0, t0, 16
	vle32.v		K7, (t0)
	addi		t0, t0, 16
	vle32.v		K8, (t0)
	addi		t0, t0, 16
	vle32.v		K9, (t0)
	addi		t0, t0, 16
	vle32.v		K10, (t0)
	addi		t0, t0, 16
	vle32.v		K11, (t0)
	addi		t0, t0, 16
	vle32.v		K12, (t0)
	addi		t0, t0, 16
	vle32.v		K13, (t0)
	addi		t0, t0, 16
	vle32.v		K14, (t0)
	addi		t0, t0, 16
	vle32.v		K15, (t0)

	// Setup mask for the vmerge to replace the first word (idx==0) in
	// message scheduling.  There are 4 words, so an 8-bit mask suffices.
	vsetivli	zero, 1, e8, m1, ta, ma
	vmv.v.i		MASK, 0x01

	// Load the state.  The state is stored as {a,b,c,d,e,f,g,h}, but we
	// need {f,e,b,a},{h,g,d,c}.  The dst vtype is e32m1 and the index vtype
	// is e8mf4.  We use index-load with the i8 indices {20, 16, 4, 0},
	// loaded using the 32-bit little endian value 0x00041014.
	li		t0, 0x00041014
	vsetivli	zero, 1, e32, m1, ta, ma
	vmv.v.x		INDICES, t0
	addi		STATEP_C, STATEP, 8
	vsetivli	zero, 4, e32, m1, ta, ma
	vluxei8.v	FEBA, (STATEP), INDICES
	vluxei8.v	HGDC, (STATEP_C), INDICES

.Lnext_block:
	addi		NUM_BLOCKS, NUM_BLOCKS, -1

	// Save the previous state, as it's needed later.
	vmv.v.v		PREV_FEBA, FEBA
	vmv.v.v		PREV_HGDC, HGDC

	// Load the next 512-bit message block and endian-swap each 32-bit word.
	vle32.v		W0, (DATA)
	vrev8.v		W0, W0
	addi		DATA, DATA, 16
	vle32.v		W1, (DATA)
	vrev8.v		W1, W1
	addi		DATA, DATA, 16
	vle32.v		W2, (DATA)
	vrev8.v		W2, W2
	addi		DATA, DATA, 16
	vle32.v		W3, (DATA)
	vrev8.v		W3, W3
	addi		DATA, DATA, 16

	// Do the 64 rounds of SHA-256.
	sha256_16rounds	0, K0, K1, K2, K3
	sha256_16rounds	0, K4, K5, K6, K7
	sha256_16rounds	0, K8, K9, K10, K11
	sha256_16rounds	1, K12, K13, K14, K15

	// Add the previous state.
	vadd.vv		FEBA, FEBA, PREV_FEBA
	vadd.vv		HGDC, HGDC, PREV_HGDC

	// Repeat if more blocks remain.
	bnez		NUM_BLOCKS, .Lnext_block

	// Store the new state and return.
	vsuxei8.v	FEBA, (STATEP), INDICES
	vsuxei8.v	HGDC, (STATEP_C), INDICES
	ret
SYM_FUNC_END(sha256_transform_zvknha_or_zvknhb_zvkb)

.section ".rodata"
.p2align 2
.type K256, @object
K256:
	.word		0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5
	.word		0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5
	.word		0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3
	.word		0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174
	.word		0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc
	.word		0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da
	.word		0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7
	.word		0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967
	.word		0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13
	.word		0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85
	.word		0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3
	.word		0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070
	.word		0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5
	.word		0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3
	.word		0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208
	.word		0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
.size K256, . - K256
