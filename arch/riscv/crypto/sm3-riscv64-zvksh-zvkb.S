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
// - RISC-V Vector SM3 Secure Hash extension ('Zvksh')
// - RISC-V Vector Cryptography Bit-manipulation extension ('Zvkb')

#include <linux/cfi_types.h>

.text
.option arch, +zvksh, +zvkb

#define STATEP		a0
#define DATA		a1
#define NUM_BLOCKS	a2

#define STATE		v0	// LMUL=2
#define PREV_STATE	v2	// LMUL=2
#define W0		v4	// LMUL=2
#define W1		v6	// LMUL=2
#define VTMP		v8	// LMUL=2

.macro	sm3_8rounds	i, w0, w1
	// Do 4 rounds using W_{0+i}..W_{7+i}.
	vsm3c.vi	STATE, \w0, \i + 0
	vslidedown.vi	VTMP, \w0, 2
	vsm3c.vi	STATE, VTMP, \i + 1

	// Compute W_{4+i}..W_{11+i}.
	vslidedown.vi	VTMP, \w0, 4
	vslideup.vi	VTMP, \w1, 4

	// Do 4 rounds using W_{4+i}..W_{11+i}.
	vsm3c.vi	STATE, VTMP, \i + 2
	vslidedown.vi	VTMP, VTMP, 2
	vsm3c.vi	STATE, VTMP, \i + 3

.if \i < 28
	// Compute W_{16+i}..W_{23+i}.
	vsm3me.vv	\w0, \w1, \w0
.endif
	// For the next 8 rounds, w0 and w1 are swapped.
.endm

// void sm3_transform_zvksh_zvkb(u32 state[8], const u8 *data, int num_blocks);
SYM_TYPED_FUNC_START(sm3_transform_zvksh_zvkb)

	// Load the state and endian-swap each 32-bit word.
	vsetivli	zero, 8, e32, m2, ta, ma
	vle32.v		STATE, (STATEP)
	vrev8.v		STATE, STATE

.Lnext_block:
	addi		NUM_BLOCKS, NUM_BLOCKS, -1

	// Save the previous state, as it's needed later.
	vmv.v.v		PREV_STATE, STATE

	// Load the next 512-bit message block into W0-W1.
	vle32.v		W0, (DATA)
	addi		DATA, DATA, 32
	vle32.v		W1, (DATA)
	addi		DATA, DATA, 32

	// Do the 64 rounds of SM3.
	sm3_8rounds	0, W0, W1
	sm3_8rounds	4, W1, W0
	sm3_8rounds	8, W0, W1
	sm3_8rounds	12, W1, W0
	sm3_8rounds	16, W0, W1
	sm3_8rounds	20, W1, W0
	sm3_8rounds	24, W0, W1
	sm3_8rounds	28, W1, W0

	// XOR in the previous state.
	vxor.vv		STATE, STATE, PREV_STATE

	// Repeat if more blocks remain.
	bnez		NUM_BLOCKS, .Lnext_block

	// Store the new state and return.
	vrev8.v		STATE, STATE
	vse32.v		STATE, (STATEP)
	ret
SYM_FUNC_END(sm3_transform_zvksh_zvkb)
