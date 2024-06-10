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
// - RISC-V Vector Cryptography Bit-manipulation extension ('Zvkb')

#include <linux/linkage.h>

.text
.option arch, +zvkb

#define KEYP		a0
#define INP		a1
#define OUTP		a2
#define LEN		a3
#define IVP		a4

#define CONSTS0		a5
#define CONSTS1		a6
#define CONSTS2		a7
#define CONSTS3		t0
#define TMP		t1
#define VL		t2
#define STRIDE		t3
#define NROUNDS		t4
#define KEY0		s0
#define KEY1		s1
#define KEY2		s2
#define KEY3		s3
#define KEY4		s4
#define KEY5		s5
#define KEY6		s6
#define KEY7		s7
#define COUNTER		s8
#define NONCE0		s9
#define NONCE1		s10
#define NONCE2		s11

.macro	chacha_round	a0, b0, c0, d0,  a1, b1, c1, d1, \
			a2, b2, c2, d2,  a3, b3, c3, d3
	// a += b; d ^= a; d = rol(d, 16);
	vadd.vv		\a0, \a0, \b0
	vadd.vv		\a1, \a1, \b1
	vadd.vv		\a2, \a2, \b2
	vadd.vv		\a3, \a3, \b3
	vxor.vv		\d0, \d0, \a0
	vxor.vv		\d1, \d1, \a1
	vxor.vv		\d2, \d2, \a2
	vxor.vv		\d3, \d3, \a3
	vror.vi		\d0, \d0, 32 - 16
	vror.vi		\d1, \d1, 32 - 16
	vror.vi		\d2, \d2, 32 - 16
	vror.vi		\d3, \d3, 32 - 16

	// c += d; b ^= c; b = rol(b, 12);
	vadd.vv		\c0, \c0, \d0
	vadd.vv		\c1, \c1, \d1
	vadd.vv		\c2, \c2, \d2
	vadd.vv		\c3, \c3, \d3
	vxor.vv		\b0, \b0, \c0
	vxor.vv		\b1, \b1, \c1
	vxor.vv		\b2, \b2, \c2
	vxor.vv		\b3, \b3, \c3
	vror.vi		\b0, \b0, 32 - 12
	vror.vi		\b1, \b1, 32 - 12
	vror.vi		\b2, \b2, 32 - 12
	vror.vi		\b3, \b3, 32 - 12

	// a += b; d ^= a; d = rol(d, 8);
	vadd.vv		\a0, \a0, \b0
	vadd.vv		\a1, \a1, \b1
	vadd.vv		\a2, \a2, \b2
	vadd.vv		\a3, \a3, \b3
	vxor.vv		\d0, \d0, \a0
	vxor.vv		\d1, \d1, \a1
	vxor.vv		\d2, \d2, \a2
	vxor.vv		\d3, \d3, \a3
	vror.vi		\d0, \d0, 32 - 8
	vror.vi		\d1, \d1, 32 - 8
	vror.vi		\d2, \d2, 32 - 8
	vror.vi		\d3, \d3, 32 - 8

	// c += d; b ^= c; b = rol(b, 7);
	vadd.vv		\c0, \c0, \d0
	vadd.vv		\c1, \c1, \d1
	vadd.vv		\c2, \c2, \d2
	vadd.vv		\c3, \c3, \d3
	vxor.vv		\b0, \b0, \c0
	vxor.vv		\b1, \b1, \c1
	vxor.vv		\b2, \b2, \c2
	vxor.vv		\b3, \b3, \c3
	vror.vi		\b0, \b0, 32 - 7
	vror.vi		\b1, \b1, 32 - 7
	vror.vi		\b2, \b2, 32 - 7
	vror.vi		\b3, \b3, 32 - 7
.endm

// void chacha20_zvkb(const u32 key[8], const u8 *in, u8 *out, size_t len,
//		      const u32 iv[4]);
//
// |len| must be nonzero and a multiple of 64 (CHACHA_BLOCK_SIZE).
// The counter is treated as 32-bit, following the RFC7539 convention.
SYM_FUNC_START(chacha20_zvkb)
	srli		LEN, LEN, 6	// Bytes to blocks

	addi		sp, sp, -96
	sd		s0, 0(sp)
	sd		s1, 8(sp)
	sd		s2, 16(sp)
	sd		s3, 24(sp)
	sd		s4, 32(sp)
	sd		s5, 40(sp)
	sd		s6, 48(sp)
	sd		s7, 56(sp)
	sd		s8, 64(sp)
	sd		s9, 72(sp)
	sd		s10, 80(sp)
	sd		s11, 88(sp)

	li		STRIDE, 64

	// Set up the initial state matrix in scalar registers.
	li		CONSTS0, 0x61707865	// "expa" little endian
	li		CONSTS1, 0x3320646e	// "nd 3" little endian
	li		CONSTS2, 0x79622d32	// "2-by" little endian
	li		CONSTS3, 0x6b206574	// "te k" little endian
	lw		KEY0, 0(KEYP)
	lw		KEY1, 4(KEYP)
	lw		KEY2, 8(KEYP)
	lw		KEY3, 12(KEYP)
	lw		KEY4, 16(KEYP)
	lw		KEY5, 20(KEYP)
	lw		KEY6, 24(KEYP)
	lw		KEY7, 28(KEYP)
	lw		COUNTER, 0(IVP)
	lw		NONCE0, 4(IVP)
	lw		NONCE1, 8(IVP)
	lw		NONCE2, 12(IVP)

.Lblock_loop:
	// Set vl to the number of blocks to process in this iteration.
	vsetvli		VL, LEN, e32, m1, ta, ma

	// Set up the initial state matrix for the next VL blocks in v0-v15.
	// v{i} holds the i'th 32-bit word of the state matrix for all blocks.
	// Note that only the counter word, at index 12, differs across blocks.
	vmv.v.x		v0, CONSTS0
	vmv.v.x		v1, CONSTS1
	vmv.v.x		v2, CONSTS2
	vmv.v.x		v3, CONSTS3
	vmv.v.x		v4, KEY0
	vmv.v.x		v5, KEY1
	vmv.v.x		v6, KEY2
	vmv.v.x		v7, KEY3
	vmv.v.x		v8, KEY4
	vmv.v.x		v9, KEY5
	vmv.v.x		v10, KEY6
	vmv.v.x		v11, KEY7
	vid.v		v12
	vadd.vx		v12, v12, COUNTER
	vmv.v.x		v13, NONCE0
	vmv.v.x		v14, NONCE1
	vmv.v.x		v15, NONCE2

	// Load the first half of the input data for each block into v16-v23.
	// v{16+i} holds the i'th 32-bit word for all blocks.
	vlsseg8e32.v	v16, (INP), STRIDE

	li		NROUNDS, 20
.Lnext_doubleround:
	addi		NROUNDS, NROUNDS, -2
	// column round
	chacha_round	v0, v4, v8, v12, v1, v5, v9, v13, \
			v2, v6, v10, v14, v3, v7, v11, v15
	// diagonal round
	chacha_round	v0, v5, v10, v15, v1, v6, v11, v12, \
			v2, v7, v8, v13, v3, v4, v9, v14
	bnez		NROUNDS, .Lnext_doubleround

	// Load the second half of the input data for each block into v24-v31.
	// v{24+i} holds the {8+i}'th 32-bit word for all blocks.
	addi		TMP, INP, 32
	vlsseg8e32.v	v24, (TMP), STRIDE

	// Finalize the first half of the keystream for each block.
	vadd.vx		v0, v0, CONSTS0
	vadd.vx		v1, v1, CONSTS1
	vadd.vx		v2, v2, CONSTS2
	vadd.vx		v3, v3, CONSTS3
	vadd.vx		v4, v4, KEY0
	vadd.vx		v5, v5, KEY1
	vadd.vx		v6, v6, KEY2
	vadd.vx		v7, v7, KEY3

	// Encrypt/decrypt the first half of the data for each block.
	vxor.vv		v16, v16, v0
	vxor.vv		v17, v17, v1
	vxor.vv		v18, v18, v2
	vxor.vv		v19, v19, v3
	vxor.vv		v20, v20, v4
	vxor.vv		v21, v21, v5
	vxor.vv		v22, v22, v6
	vxor.vv		v23, v23, v7

	// Store the first half of the output data for each block.
	vssseg8e32.v	v16, (OUTP), STRIDE

	// Finalize the second half of the keystream for each block.
	vadd.vx		v8, v8, KEY4
	vadd.vx		v9, v9, KEY5
	vadd.vx		v10, v10, KEY6
	vadd.vx		v11, v11, KEY7
	vid.v		v0
	vadd.vx		v12, v12, COUNTER
	vadd.vx		v13, v13, NONCE0
	vadd.vx		v14, v14, NONCE1
	vadd.vx		v15, v15, NONCE2
	vadd.vv		v12, v12, v0

	// Encrypt/decrypt the second half of the data for each block.
	vxor.vv		v24, v24, v8
	vxor.vv		v25, v25, v9
	vxor.vv		v26, v26, v10
	vxor.vv		v27, v27, v11
	vxor.vv		v29, v29, v13
	vxor.vv		v28, v28, v12
	vxor.vv		v30, v30, v14
	vxor.vv		v31, v31, v15

	// Store the second half of the output data for each block.
	addi		TMP, OUTP, 32
	vssseg8e32.v	v24, (TMP), STRIDE

	// Update the counter, the remaining number of blocks, and the input and
	// output pointers according to the number of blocks processed (VL).
	add		COUNTER, COUNTER, VL
	sub		LEN, LEN, VL
	slli		TMP, VL, 6
	add		OUTP, OUTP, TMP
	add		INP, INP, TMP
	bnez		LEN, .Lblock_loop

	ld		s0, 0(sp)
	ld		s1, 8(sp)
	ld		s2, 16(sp)
	ld		s3, 24(sp)
	ld		s4, 32(sp)
	ld		s5, 40(sp)
	ld		s6, 48(sp)
	ld		s7, 56(sp)
	ld		s8, 64(sp)
	ld		s9, 72(sp)
	ld		s10, 80(sp)
	ld		s11, 88(sp)
	addi		sp, sp, 96
	ret
SYM_FUNC_END(chacha20_zvkb)
