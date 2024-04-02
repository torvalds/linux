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
// - RISC-V Vector ('V') with VLEN >= 128 && VLEN < 2048
// - RISC-V Vector AES block cipher extension ('Zvkned')
// - RISC-V Vector Bit-manipulation extension ('Zvbb')
// - RISC-V Vector GCM/GMAC extension ('Zvkg')

#include <linux/linkage.h>

.text
.option arch, +zvkned, +zvbb, +zvkg

#include "aes-macros.S"

#define KEYP		a0
#define INP		a1
#define OUTP		a2
#define LEN		a3
#define TWEAKP		a4

#define LEN32		a5
#define TAIL_LEN	a6
#define VL		a7
#define VLMAX		t4

// v1-v15 contain the AES round keys, but they are used for temporaries before
// the AES round keys have been loaded.
#define TWEAKS		v16	// LMUL=4 (most of the time)
#define TWEAKS_BREV	v20	// LMUL=4 (most of the time)
#define MULTS_BREV	v24	// LMUL=4 (most of the time)
#define TMP0		v28
#define TMP1		v29
#define TMP2		v30
#define TMP3		v31

// xts_init initializes the following values:
//
//	TWEAKS: N 128-bit tweaks T*(x^i) for i in 0..(N - 1)
//	TWEAKS_BREV: same as TWEAKS, but bit-reversed
//	MULTS_BREV: N 128-bit values x^N, bit-reversed.  Only if N > 1.
//
// N is the maximum number of blocks that will be processed per loop iteration,
// computed using vsetvli.
//
// The field convention used by XTS is the same as that of GHASH, but with the
// bits reversed within each byte.  The zvkg extension provides the vgmul
// instruction which does multiplication in this field.  Therefore, for tweak
// computation we use vgmul to do multiplications in parallel, instead of
// serially multiplying by x using shifting+xoring.  Note that for this to work,
// the inputs and outputs to vgmul must be bit-reversed (we do it with vbrev8).
.macro	xts_init

	// Load the first tweak T.
	vsetivli	zero, 4, e32, m1, ta, ma
	vle32.v		TWEAKS, (TWEAKP)

	// If there's only one block (or no blocks at all), then skip the tweak
	// sequence computation because (at most) T itself is needed.
	li		t0, 16
	ble		LEN, t0, .Linit_single_block\@

	// Save a copy of T bit-reversed in v12.
	vbrev8.v	v12, TWEAKS

	//
	// Generate x^i for i in 0..(N - 1), i.e. 128-bit values 1 << i assuming
	// that N <= 128.  Though, this code actually requires N < 64 (or
	// equivalently VLEN < 2048) due to the use of 64-bit intermediate
	// values here and in the x^N computation later.
	//
	vsetvli		VL, LEN32, e32, m4, ta, ma
	srli		t0, VL, 2	// t0 = N (num blocks)
	// Generate two sequences, each with N 32-bit values:
	// v0=[1, 1, 1, ...] and v1=[0, 1, 2, ...].
	vsetvli		zero, t0, e32, m1, ta, ma
	vmv.v.i		v0, 1
	vid.v		v1
	// Use vzext to zero-extend the sequences to 64 bits.  Reinterpret them
	// as two sequences, each with 2*N 32-bit values:
	// v2=[1, 0, 1, 0, 1, 0, ...] and v4=[0, 0, 1, 0, 2, 0, ...].
	vsetvli		zero, t0, e64, m2, ta, ma
	vzext.vf2	v2, v0
	vzext.vf2	v4, v1
	slli		t1, t0, 1	// t1 = 2*N
	vsetvli		zero, t1, e32, m2, ta, ma
	// Use vwsll to compute [1<<0, 0<<0, 1<<1, 0<<0, 1<<2, 0<<0, ...],
	// widening to 64 bits per element.  When reinterpreted as N 128-bit
	// values, this is the needed sequence of 128-bit values 1 << i (x^i).
	vwsll.vv	v8, v2, v4

	// Copy the bit-reversed T to all N elements of TWEAKS_BREV, then
	// multiply by x^i.  This gives the sequence T*(x^i), bit-reversed.
	vsetvli		zero, LEN32, e32, m4, ta, ma
	vmv.v.i		TWEAKS_BREV, 0
	vaesz.vs	TWEAKS_BREV, v12
	vbrev8.v	v8, v8
	vgmul.vv	TWEAKS_BREV, v8

	// Save a copy of the sequence T*(x^i) with the bit reversal undone.
	vbrev8.v	TWEAKS, TWEAKS_BREV

	// Generate N copies of x^N, i.e. 128-bit values 1 << N, bit-reversed.
	li		t1, 1
	sll		t1, t1, t0	// t1 = 1 << N
	vsetivli	zero, 2, e64, m1, ta, ma
	vmv.v.i		v0, 0
	vsetivli	zero, 1, e64, m1, tu, ma
	vmv.v.x		v0, t1
	vbrev8.v	v0, v0
	vsetvli		zero, LEN32, e32, m4, ta, ma
	vmv.v.i		MULTS_BREV, 0
	vaesz.vs	MULTS_BREV, v0

	j		.Linit_done\@

.Linit_single_block\@:
	vbrev8.v	TWEAKS_BREV, TWEAKS
.Linit_done\@:
.endm

// Set the first 128 bits of MULTS_BREV to 0x40, i.e. 'x' bit-reversed.  This is
// the multiplier required to advance the tweak by one.
.macro	load_x
	li		t0, 0x40
	vsetivli	zero, 4, e32, m1, ta, ma
	vmv.v.i		MULTS_BREV, 0
	vsetivli	zero, 1, e8, m1, tu, ma
	vmv.v.x		MULTS_BREV, t0
.endm

.macro	__aes_xts_crypt	enc, keylen
	// With 16 < len <= 31, there's no main loop, just ciphertext stealing.
	beqz		LEN32, .Lcts_without_main_loop\@

	vsetvli		VLMAX, zero, e32, m4, ta, ma
1:
	vsetvli		VL, LEN32, e32, m4, ta, ma
2:
	// Encrypt or decrypt VL/4 blocks.
	vle32.v		TMP0, (INP)
	vxor.vv		TMP0, TMP0, TWEAKS
	aes_crypt	TMP0, \enc, \keylen
	vxor.vv		TMP0, TMP0, TWEAKS
	vse32.v		TMP0, (OUTP)

	// Update the pointers and the remaining length.
	slli		t0, VL, 2
	add		INP, INP, t0
	add		OUTP, OUTP, t0
	sub		LEN32, LEN32, VL

	// Check whether more blocks remain.
	beqz		LEN32, .Lmain_loop_done\@

	// Compute the next sequence of tweaks by multiplying the previous
	// sequence by x^N.  Store the result in both bit-reversed order and
	// regular order (i.e. with the bit reversal undone).
	vgmul.vv	TWEAKS_BREV, MULTS_BREV
	vbrev8.v	TWEAKS, TWEAKS_BREV

	// Since we compute the tweak multipliers x^N in advance, we require
	// that each iteration process the same length except possibly the last.
	// This conflicts slightly with the behavior allowed by RISC-V Vector
	// Extension, where CPUs can select a lower length for both of the last
	// two iterations.  E.g., vl might take the sequence of values
	// [16, 16, 16, 12, 12], whereas we need [16, 16, 16, 16, 8] so that we
	// can use x^4 again instead of computing x^3.  Therefore, we explicitly
	// keep the vl at VLMAX if there is at least VLMAX remaining.
	bge		LEN32, VLMAX, 2b
	j		1b

.Lmain_loop_done\@:
	load_x

	// Compute the next tweak.
	addi		t0, VL, -4
	vsetivli	zero, 4, e32, m4, ta, ma
	vslidedown.vx	TWEAKS_BREV, TWEAKS_BREV, t0	// Extract last tweak
	vsetivli	zero, 4, e32, m1, ta, ma
	vgmul.vv	TWEAKS_BREV, MULTS_BREV		// Advance to next tweak

	bnez		TAIL_LEN, .Lcts\@

	// Update *TWEAKP to contain the next tweak.
	vbrev8.v	TWEAKS, TWEAKS_BREV
	vse32.v		TWEAKS, (TWEAKP)
	ret

.Lcts_without_main_loop\@:
	load_x
.Lcts\@:
	// TWEAKS_BREV now contains the next tweak.  Compute the one after that.
	vsetivli	zero, 4, e32, m1, ta, ma
	vmv.v.v		TMP0, TWEAKS_BREV
	vgmul.vv	TMP0, MULTS_BREV
	// Undo the bit reversal of the next two tweaks and store them in TMP1
	// and TMP2, such that TMP1 is the first needed and TMP2 the second.
.if \enc
	vbrev8.v	TMP1, TWEAKS_BREV
	vbrev8.v	TMP2, TMP0
.else
	vbrev8.v	TMP1, TMP0
	vbrev8.v	TMP2, TWEAKS_BREV
.endif

	// Encrypt/decrypt the last full block.
	vle32.v		TMP0, (INP)
	vxor.vv		TMP0, TMP0, TMP1
	aes_crypt	TMP0, \enc, \keylen
	vxor.vv		TMP0, TMP0, TMP1

	// Swap the first TAIL_LEN bytes of the above result with the tail.
	// Note that to support in-place encryption/decryption, the load from
	// the input tail must happen before the store to the output tail.
	addi		t0, INP, 16
	addi		t1, OUTP, 16
	vmv.v.v		TMP3, TMP0
	vsetvli		zero, TAIL_LEN, e8, m1, tu, ma
	vle8.v		TMP0, (t0)
	vse8.v		TMP3, (t1)

	// Encrypt/decrypt again and store the last full block.
	vsetivli	zero, 4, e32, m1, ta, ma
	vxor.vv		TMP0, TMP0, TMP2
	aes_crypt	TMP0, \enc, \keylen
	vxor.vv		TMP0, TMP0, TMP2
	vse32.v		TMP0, (OUTP)

	ret
.endm

.macro	aes_xts_crypt	enc

	// Check whether the length is a multiple of the AES block size.
	andi		TAIL_LEN, LEN, 15
	beqz		TAIL_LEN, 1f

	// The length isn't a multiple of the AES block size, so ciphertext
	// stealing will be required.  Ciphertext stealing involves special
	// handling of the partial block and the last full block, so subtract
	// the length of both from the length to be processed in the main loop.
	sub		LEN, LEN, TAIL_LEN
	addi		LEN, LEN, -16
1:
	srli		LEN32, LEN, 2
	// LEN and LEN32 now contain the total length of the blocks that will be
	// processed in the main loop, in bytes and 32-bit words respectively.

	xts_init
	aes_begin	KEYP, 128f, 192f
	__aes_xts_crypt	\enc, 256
128:
	__aes_xts_crypt	\enc, 128
192:
	__aes_xts_crypt	\enc, 192
.endm

// void aes_xts_encrypt_zvkned_zvbb_zvkg(const struct crypto_aes_ctx *key,
//					 const u8 *in, u8 *out, size_t len,
//					 u8 tweak[16]);
//
// |key| is the data key.  |tweak| contains the next tweak; the encryption of
// the original IV with the tweak key was already done.  This function supports
// incremental computation, but |len| must always be >= 16 (AES_BLOCK_SIZE), and
// |len| must be a multiple of 16 except on the last call.  If |len| is a
// multiple of 16, then this function updates |tweak| to contain the next tweak.
SYM_FUNC_START(aes_xts_encrypt_zvkned_zvbb_zvkg)
	aes_xts_crypt	1
SYM_FUNC_END(aes_xts_encrypt_zvkned_zvbb_zvkg)

// Same prototype and calling convention as the encryption function
SYM_FUNC_START(aes_xts_decrypt_zvkned_zvbb_zvkg)
	aes_xts_crypt	0
SYM_FUNC_END(aes_xts_decrypt_zvkned_zvbb_zvkg)
