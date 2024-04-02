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
// Copyright (c) 2023, Jerry Shih <jerry.shih@sifive.com>
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
// - RISC-V Vector AES block cipher extension ('Zvkned')
// - RISC-V Vector Cryptography Bit-manipulation extension ('Zvkb')

#include <linux/linkage.h>

.text
.option arch, +zvkned, +zvkb

#include "aes-macros.S"

#define KEYP		a0
#define INP		a1
#define OUTP		a2
#define LEN		a3
#define IVP		a4

#define LEN32		a5
#define VL_E32		a6
#define VL_BLOCKS	a7

.macro	aes_ctr32_crypt	keylen
	// LEN32 = number of blocks, rounded up, in 32-bit words.
	addi		t0, LEN, 15
	srli		t0, t0, 4
	slli		LEN32, t0, 2

	// Create a mask that selects the last 32-bit word of each 128-bit
	// block.  This is the word that contains the (big-endian) counter.
	li		t0, 0x88
	vsetvli		t1, zero, e8, m1, ta, ma
	vmv.v.x		v0, t0

	// Load the IV into v31.  The last 32-bit word contains the counter.
	vsetivli	zero, 4, e32, m1, ta, ma
	vle32.v		v31, (IVP)

	// Convert the big-endian counter into little-endian.
	vsetivli	zero, 4, e32, m1, ta, mu
	vrev8.v		v31, v31, v0.t

	// Splat the IV to v16 (with LMUL=4).  The number of copies is the
	// maximum number of blocks that will be processed per iteration.
	vsetvli		zero, LEN32, e32, m4, ta, ma
	vmv.v.i		v16, 0
	vaesz.vs	v16, v31

	// v20 = [x, x, x, 0, x, x, x, 1, ...]
	viota.m		v20, v0, v0.t
	// v16 = [IV0, IV1, IV2, counter+0, IV0, IV1, IV2, counter+1, ...]
	vsetvli		VL_E32, LEN32, e32, m4, ta, mu
	vadd.vv		v16, v16, v20, v0.t

	j 2f
1:
	// Set the number of blocks to process in this iteration.  vl=VL_E32 is
	// the length in 32-bit words, i.e. 4 times the number of blocks.
	vsetvli		VL_E32, LEN32, e32, m4, ta, mu

	// Increment the counters by the number of blocks processed in the
	// previous iteration.
	vadd.vx		v16, v16, VL_BLOCKS, v0.t
2:
	// Prepare the AES inputs into v24.
	vmv.v.v		v24, v16
	vrev8.v		v24, v24, v0.t	// Convert counters back to big-endian.

	// Encrypt the AES inputs to create the next portion of the keystream.
	aes_encrypt	v24, \keylen

	// XOR the data with the keystream.
	vsetvli		t0, LEN, e8, m4, ta, ma
	vle8.v		v20, (INP)
	vxor.vv		v20, v20, v24
	vse8.v		v20, (OUTP)

	// Advance the pointers and update the remaining length.
	add		INP, INP, t0
	add		OUTP, OUTP, t0
	sub		LEN, LEN, t0
	sub		LEN32, LEN32, VL_E32
	srli		VL_BLOCKS, VL_E32, 2

	// Repeat if more data remains.
	bnez		LEN, 1b

	// Update *IVP to contain the next counter.
	vsetivli	zero, 4, e32, m1, ta, mu
	vadd.vx		v16, v16, VL_BLOCKS, v0.t
	vrev8.v		v16, v16, v0.t	// Convert counters back to big-endian.
	vse32.v		v16, (IVP)

	ret
.endm

// void aes_ctr32_crypt_zvkned_zvkb(const struct crypto_aes_ctx *key,
//				    const u8 *in, u8 *out, size_t len,
//				    u8 iv[16]);
SYM_FUNC_START(aes_ctr32_crypt_zvkned_zvkb)
	aes_begin	KEYP, 128f, 192f
	aes_ctr32_crypt	256
128:
	aes_ctr32_crypt	128
192:
	aes_ctr32_crypt	192
SYM_FUNC_END(aes_ctr32_crypt_zvkned_zvkb)
