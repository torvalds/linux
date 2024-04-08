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
// - RISC-V Vector GCM/GMAC extension ('Zvkg')

#include <linux/linkage.h>

.text
.option arch, +zvkg

#define ACCUMULATOR	a0
#define KEY		a1
#define DATA		a2
#define LEN		a3

// void ghash_zvkg(be128 *accumulator, const be128 *key, const u8 *data,
//		   size_t len);
//
// |len| must be nonzero and a multiple of 16 (GHASH_BLOCK_SIZE).
SYM_FUNC_START(ghash_zvkg)
	vsetivli	zero, 4, e32, m1, ta, ma
	vle32.v		v1, (ACCUMULATOR)
	vle32.v		v2, (KEY)
.Lnext_block:
	vle32.v		v3, (DATA)
	vghsh.vv	v1, v2, v3
	addi		DATA, DATA, 16
	addi		LEN, LEN, -16
	bnez		LEN, .Lnext_block

	vse32.v		v1, (ACCUMULATOR)
	ret
SYM_FUNC_END(ghash_zvkg)
