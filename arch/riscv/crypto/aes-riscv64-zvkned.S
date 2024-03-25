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

#include <linux/linkage.h>

.text
.option arch, +zvkned

#include "aes-macros.S"

#define KEYP		a0
#define INP		a1
#define OUTP		a2
#define LEN		a3
#define IVP		a4

.macro	__aes_crypt_zvkned	enc, keylen
	vle32.v		v16, (INP)
	aes_crypt	v16, \enc, \keylen
	vse32.v		v16, (OUTP)
	ret
.endm

.macro	aes_crypt_zvkned	enc
	aes_begin	KEYP, 128f, 192f
	__aes_crypt_zvkned	\enc, 256
128:
	__aes_crypt_zvkned	\enc, 128
192:
	__aes_crypt_zvkned	\enc, 192
.endm

// void aes_encrypt_zvkned(const struct crypto_aes_ctx *key,
//			   const u8 in[16], u8 out[16]);
SYM_FUNC_START(aes_encrypt_zvkned)
	aes_crypt_zvkned	1
SYM_FUNC_END(aes_encrypt_zvkned)

// Same prototype and calling convention as the encryption function
SYM_FUNC_START(aes_decrypt_zvkned)
	aes_crypt_zvkned	0
SYM_FUNC_END(aes_decrypt_zvkned)

.macro	__aes_ecb_crypt	enc, keylen
	srli		t0, LEN, 2
	// t0 is the remaining length in 32-bit words.  It's a multiple of 4.
1:
	vsetvli		t1, t0, e32, m8, ta, ma
	sub		t0, t0, t1	// Subtract number of words processed
	slli		t1, t1, 2	// Words to bytes
	vle32.v		v16, (INP)
	aes_crypt	v16, \enc, \keylen
	vse32.v		v16, (OUTP)
	add		INP, INP, t1
	add		OUTP, OUTP, t1
	bnez		t0, 1b

	ret
.endm

.macro	aes_ecb_crypt	enc
	aes_begin	KEYP, 128f, 192f
	__aes_ecb_crypt	\enc, 256
128:
	__aes_ecb_crypt	\enc, 128
192:
	__aes_ecb_crypt	\enc, 192
.endm

// void aes_ecb_encrypt_zvkned(const struct crypto_aes_ctx *key,
//			       const u8 *in, u8 *out, size_t len);
//
// |len| must be nonzero and a multiple of 16 (AES_BLOCK_SIZE).
SYM_FUNC_START(aes_ecb_encrypt_zvkned)
	aes_ecb_crypt	1
SYM_FUNC_END(aes_ecb_encrypt_zvkned)

// Same prototype and calling convention as the encryption function
SYM_FUNC_START(aes_ecb_decrypt_zvkned)
	aes_ecb_crypt	0
SYM_FUNC_END(aes_ecb_decrypt_zvkned)

.macro	aes_cbc_encrypt	keylen
	vle32.v		v16, (IVP)	// Load IV
1:
	vle32.v		v17, (INP)	// Load plaintext block
	vxor.vv		v16, v16, v17	// XOR with IV or prev ciphertext block
	aes_encrypt	v16, \keylen	// Encrypt
	vse32.v		v16, (OUTP)	// Store ciphertext block
	addi		INP, INP, 16
	addi		OUTP, OUTP, 16
	addi		LEN, LEN, -16
	bnez		LEN, 1b

	vse32.v		v16, (IVP)	// Store next IV
	ret
.endm

.macro	aes_cbc_decrypt	keylen
	srli		LEN, LEN, 2	// Convert LEN from bytes to words
	vle32.v		v16, (IVP)	// Load IV
1:
	vsetvli		t0, LEN, e32, m4, ta, ma
	vle32.v		v20, (INP)	// Load ciphertext blocks
	vslideup.vi	v16, v20, 4	// Setup prev ciphertext blocks
	addi		t1, t0, -4
	vslidedown.vx	v24, v20, t1	// Save last ciphertext block
	aes_decrypt	v20, \keylen	// Decrypt the blocks
	vxor.vv		v20, v20, v16	// XOR with prev ciphertext blocks
	vse32.v		v20, (OUTP)	// Store plaintext blocks
	vmv.v.v		v16, v24	// Next "IV" is last ciphertext block
	slli		t1, t0, 2	// Words to bytes
	add		INP, INP, t1
	add		OUTP, OUTP, t1
	sub		LEN, LEN, t0
	bnez		LEN, 1b

	vsetivli	zero, 4, e32, m1, ta, ma
	vse32.v		v16, (IVP)	// Store next IV
	ret
.endm

// void aes_cbc_encrypt_zvkned(const struct crypto_aes_ctx *key,
//			       const u8 *in, u8 *out, size_t len, u8 iv[16]);
//
// |len| must be nonzero and a multiple of 16 (AES_BLOCK_SIZE).
SYM_FUNC_START(aes_cbc_encrypt_zvkned)
	aes_begin	KEYP, 128f, 192f
	aes_cbc_encrypt	256
128:
	aes_cbc_encrypt	128
192:
	aes_cbc_encrypt	192
SYM_FUNC_END(aes_cbc_encrypt_zvkned)

// Same prototype and calling convention as the encryption function
SYM_FUNC_START(aes_cbc_decrypt_zvkned)
	aes_begin	KEYP, 128f, 192f
	aes_cbc_decrypt	256
128:
	aes_cbc_decrypt	128
192:
	aes_cbc_decrypt	192
SYM_FUNC_END(aes_cbc_decrypt_zvkned)

.macro	aes_cbc_cts_encrypt	keylen

	// CBC-encrypt all blocks except the last.  But don't store the
	// second-to-last block to the output buffer yet, since it will be
	// handled specially in the ciphertext stealing step.  Exception: if the
	// message is single-block, still encrypt the last (and only) block.
	li		t0, 16
	j		2f
1:
	vse32.v		v16, (OUTP)	// Store ciphertext block
	addi		OUTP, OUTP, 16
2:
	vle32.v		v17, (INP)	// Load plaintext block
	vxor.vv		v16, v16, v17	// XOR with IV or prev ciphertext block
	aes_encrypt	v16, \keylen	// Encrypt
	addi		INP, INP, 16
	addi		LEN, LEN, -16
	bgt		LEN, t0, 1b	// Repeat if more than one block remains

	// Special case: if the message is a single block, just do CBC.
	beqz		LEN, .Lcts_encrypt_done\@

	// Encrypt the last two blocks using ciphertext stealing as follows:
	//	C[n-1] = Encrypt(Encrypt(P[n-1] ^ C[n-2]) ^ P[n])
	//	C[n] = Encrypt(P[n-1] ^ C[n-2])[0..LEN]
	//
	// C[i] denotes the i'th ciphertext block, and likewise P[i] the i'th
	// plaintext block.  Block n, the last block, may be partial; its length
	// is 1 <= LEN <= 16.  If there are only 2 blocks, C[n-2] means the IV.
	//
	// v16 already contains Encrypt(P[n-1] ^ C[n-2]).
	// INP points to P[n].  OUTP points to where C[n-1] should go.
	// To support in-place encryption, load P[n] before storing C[n].
	addi		t0, OUTP, 16	// Get pointer to where C[n] should go
	vsetvli		zero, LEN, e8, m1, tu, ma
	vle8.v		v17, (INP)	// Load P[n]
	vse8.v		v16, (t0)	// Store C[n]
	vxor.vv		v16, v16, v17	// v16 = Encrypt(P[n-1] ^ C[n-2]) ^ P[n]
	vsetivli	zero, 4, e32, m1, ta, ma
	aes_encrypt	v16, \keylen
.Lcts_encrypt_done\@:
	vse32.v		v16, (OUTP)	// Store C[n-1] (or C[n] in single-block case)
	ret
.endm

#define LEN32		t4 // Length of remaining full blocks in 32-bit words
#define LEN_MOD16	t5 // Length of message in bytes mod 16

.macro	aes_cbc_cts_decrypt	keylen
	andi		LEN32, LEN, ~15
	srli		LEN32, LEN32, 2
	andi		LEN_MOD16, LEN, 15

	// Save C[n-2] in v28 so that it's available later during the ciphertext
	// stealing step.  If there are fewer than three blocks, C[n-2] means
	// the IV, otherwise it means the third-to-last ciphertext block.
	vmv.v.v		v28, v16	// IV
	add		t0, LEN, -33
	bltz		t0, .Lcts_decrypt_loop\@
	andi		t0, t0, ~15
	add		t0, t0, INP
	vle32.v		v28, (t0)

	// CBC-decrypt all full blocks.  For the last full block, or the last 2
	// full blocks if the message is block-aligned, this doesn't write the
	// correct output blocks (unless the message is only a single block),
	// because it XORs the wrong values with the raw AES plaintexts.  But we
	// fix this after this loop without redoing the AES decryptions.  This
	// approach allows more of the AES decryptions to be parallelized.
.Lcts_decrypt_loop\@:
	vsetvli		t0, LEN32, e32, m4, ta, ma
	addi		t1, t0, -4
	vle32.v		v20, (INP)	// Load next set of ciphertext blocks
	vmv.v.v		v24, v16	// Get IV or last ciphertext block of prev set
	vslideup.vi	v24, v20, 4	// Setup prev ciphertext blocks
	vslidedown.vx	v16, v20, t1	// Save last ciphertext block of this set
	aes_decrypt	v20, \keylen	// Decrypt this set of blocks
	vxor.vv		v24, v24, v20	// XOR prev ciphertext blocks with decrypted blocks
	vse32.v		v24, (OUTP)	// Store this set of plaintext blocks
	sub		LEN32, LEN32, t0
	slli		t0, t0, 2	// Words to bytes
	add		INP, INP, t0
	add		OUTP, OUTP, t0
	bnez		LEN32, .Lcts_decrypt_loop\@

	vsetivli	zero, 4, e32, m4, ta, ma
	vslidedown.vx	v20, v20, t1	// Extract raw plaintext of last full block
	addi		t0, OUTP, -16	// Get pointer to last full plaintext block
	bnez		LEN_MOD16, .Lcts_decrypt_non_block_aligned\@

	// Special case: if the message is a single block, just do CBC.
	li		t1, 16
	beq		LEN, t1, .Lcts_decrypt_done\@

	// Block-aligned message.  Just fix up the last 2 blocks.  We need:
	//
	//	P[n-1] = Decrypt(C[n]) ^ C[n-2]
	//	P[n] = Decrypt(C[n-1]) ^ C[n]
	//
	// We have C[n] in v16, Decrypt(C[n]) in v20, and C[n-2] in v28.
	// Together with Decrypt(C[n-1]) ^ C[n-2] from the output buffer, this
	// is everything needed to fix the output without re-decrypting blocks.
	addi		t1, OUTP, -32	// Get pointer to where P[n-1] should go
	vxor.vv		v20, v20, v28	// Decrypt(C[n]) ^ C[n-2] == P[n-1]
	vle32.v		v24, (t1)	// Decrypt(C[n-1]) ^ C[n-2]
	vse32.v		v20, (t1)	// Store P[n-1]
	vxor.vv		v20, v24, v16	// Decrypt(C[n-1]) ^ C[n-2] ^ C[n] == P[n] ^ C[n-2]
	j		.Lcts_decrypt_finish\@

.Lcts_decrypt_non_block_aligned\@:
	// Decrypt the last two blocks using ciphertext stealing as follows:
	//
	//	P[n-1] = Decrypt(C[n] || Decrypt(C[n-1])[LEN_MOD16..16]) ^ C[n-2]
	//	P[n] = (Decrypt(C[n-1]) ^ C[n])[0..LEN_MOD16]
	//
	// We already have Decrypt(C[n-1]) in v20 and C[n-2] in v28.
	vmv.v.v		v16, v20	// v16 = Decrypt(C[n-1])
	vsetvli		zero, LEN_MOD16, e8, m1, tu, ma
	vle8.v		v20, (INP)	// v20 = C[n] || Decrypt(C[n-1])[LEN_MOD16..16]
	vxor.vv		v16, v16, v20	// v16 = Decrypt(C[n-1]) ^ C[n]
	vse8.v		v16, (OUTP)	// Store P[n]
	vsetivli	zero, 4, e32, m1, ta, ma
	aes_decrypt	v20, \keylen	// v20 = Decrypt(C[n] || Decrypt(C[n-1])[LEN_MOD16..16])
.Lcts_decrypt_finish\@:
	vxor.vv		v20, v20, v28	// XOR with C[n-2]
	vse32.v		v20, (t0)	// Store last full plaintext block
.Lcts_decrypt_done\@:
	ret
.endm

.macro	aes_cbc_cts_crypt	keylen
	vle32.v		v16, (IVP)	// Load IV
	beqz		a5, .Lcts_decrypt\@
	aes_cbc_cts_encrypt \keylen
.Lcts_decrypt\@:
	aes_cbc_cts_decrypt \keylen
.endm

// void aes_cbc_cts_crypt_zvkned(const struct crypto_aes_ctx *key,
//			         const u8 *in, u8 *out, size_t len,
//				 const u8 iv[16], bool enc);
//
// Encrypts or decrypts a message with the CS3 variant of AES-CBC-CTS.
// This is the variant that unconditionally swaps the last two blocks.
SYM_FUNC_START(aes_cbc_cts_crypt_zvkned)
	aes_begin	KEYP, 128f, 192f
	aes_cbc_cts_crypt 256
128:
	aes_cbc_cts_crypt 128
192:
	aes_cbc_cts_crypt 192
SYM_FUNC_END(aes_cbc_cts_crypt_zvkned)
