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

// This file contains macros that are shared by the other aes-*.S files.  The
// generated code of these macros depends on the following RISC-V extensions:
// - RV64I
// - RISC-V Vector ('V') with VLEN >= 128
// - RISC-V Vector AES block cipher extension ('Zvkned')

// Loads the AES round keys from \keyp into vector registers and jumps to code
// specific to the length of the key.  Specifically:
//   - If AES-128, loads round keys into v1-v11 and jumps to \label128.
//   - If AES-192, loads round keys into v1-v13 and jumps to \label192.
//   - If AES-256, loads round keys into v1-v15 and continues onwards.
//
// Also sets vl=4 and vtype=e32,m1,ta,ma.  Clobbers t0 and t1.
.macro	aes_begin	keyp, label128, label192
	lwu		t0, 480(\keyp)	// t0 = key length in bytes
	li		t1, 24		// t1 = key length for AES-192
	vsetivli	zero, 4, e32, m1, ta, ma
	vle32.v		v1, (\keyp)
	addi		\keyp, \keyp, 16
	vle32.v		v2, (\keyp)
	addi		\keyp, \keyp, 16
	vle32.v		v3, (\keyp)
	addi		\keyp, \keyp, 16
	vle32.v		v4, (\keyp)
	addi		\keyp, \keyp, 16
	vle32.v		v5, (\keyp)
	addi		\keyp, \keyp, 16
	vle32.v		v6, (\keyp)
	addi		\keyp, \keyp, 16
	vle32.v		v7, (\keyp)
	addi		\keyp, \keyp, 16
	vle32.v		v8, (\keyp)
	addi		\keyp, \keyp, 16
	vle32.v		v9, (\keyp)
	addi		\keyp, \keyp, 16
	vle32.v		v10, (\keyp)
	addi		\keyp, \keyp, 16
	vle32.v		v11, (\keyp)
	blt		t0, t1, \label128	// If AES-128, goto label128.
	addi		\keyp, \keyp, 16
	vle32.v		v12, (\keyp)
	addi		\keyp, \keyp, 16
	vle32.v		v13, (\keyp)
	beq		t0, t1, \label192	// If AES-192, goto label192.
	// Else, it's AES-256.
	addi		\keyp, \keyp, 16
	vle32.v		v14, (\keyp)
	addi		\keyp, \keyp, 16
	vle32.v		v15, (\keyp)
.endm

// Encrypts \data using zvkned instructions, using the round keys loaded into
// v1-v11 (for AES-128), v1-v13 (for AES-192), or v1-v15 (for AES-256).  \keylen
// is the AES key length in bits.  vl and vtype must already be set
// appropriately.  Note that if vl > 4, multiple blocks are encrypted.
.macro	aes_encrypt	data, keylen
	vaesz.vs	\data, v1
	vaesem.vs	\data, v2
	vaesem.vs	\data, v3
	vaesem.vs	\data, v4
	vaesem.vs	\data, v5
	vaesem.vs	\data, v6
	vaesem.vs	\data, v7
	vaesem.vs	\data, v8
	vaesem.vs	\data, v9
	vaesem.vs	\data, v10
.if \keylen == 128
	vaesef.vs	\data, v11
.elseif \keylen == 192
	vaesem.vs	\data, v11
	vaesem.vs	\data, v12
	vaesef.vs	\data, v13
.else
	vaesem.vs	\data, v11
	vaesem.vs	\data, v12
	vaesem.vs	\data, v13
	vaesem.vs	\data, v14
	vaesef.vs	\data, v15
.endif
.endm

// Same as aes_encrypt, but decrypts instead of encrypts.
.macro	aes_decrypt	data, keylen
.if \keylen == 128
	vaesz.vs	\data, v11
.elseif \keylen == 192
	vaesz.vs	\data, v13
	vaesdm.vs	\data, v12
	vaesdm.vs	\data, v11
.else
	vaesz.vs	\data, v15
	vaesdm.vs	\data, v14
	vaesdm.vs	\data, v13
	vaesdm.vs	\data, v12
	vaesdm.vs	\data, v11
.endif
	vaesdm.vs	\data, v10
	vaesdm.vs	\data, v9
	vaesdm.vs	\data, v8
	vaesdm.vs	\data, v7
	vaesdm.vs	\data, v6
	vaesdm.vs	\data, v5
	vaesdm.vs	\data, v4
	vaesdm.vs	\data, v3
	vaesdm.vs	\data, v2
	vaesdf.vs	\data, v1
.endm

// Expands to aes_encrypt or aes_decrypt according to \enc, which is 1 or 0.
.macro	aes_crypt	data, enc, keylen
.if \enc
	aes_encrypt	\data, \keylen
.else
	aes_decrypt	\data, \keylen
.endif
.endm
