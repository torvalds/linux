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
	vle32.v		v16, (IVP)	// Load IV
1:
	vle32.v		v17, (INP)	// Load ciphertext block
	vmv.v.v		v18, v17	// Save ciphertext block
	aes_decrypt	v17, \keylen	// Decrypt
	vxor.vv		v17, v17, v16	// XOR with IV or prev ciphertext block
	vse32.v		v17, (OUTP)	// Store plaintext block
	vmv.v.v		v16, v18	// Next "IV" is prev ciphertext block
	addi		INP, INP, 16
	addi		OUTP, OUTP, 16
	addi		LEN, LEN, -16
	bnez		LEN, 1b

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
