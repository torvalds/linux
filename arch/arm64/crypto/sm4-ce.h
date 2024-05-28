/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * SM4 common functions for Crypto Extensions
 * Copyright (C) 2022 Tianjia Zhang <tianjia.zhang@linux.alibaba.com>
 */

void sm4_ce_expand_key(const u8 *key, u32 *rkey_enc, u32 *rkey_dec,
		       const u32 *fk, const u32 *ck);

void sm4_ce_crypt_block(const u32 *rkey, u8 *dst, const u8 *src);

void sm4_ce_cbc_enc(const u32 *rkey_enc, u8 *dst, const u8 *src,
		    u8 *iv, unsigned int nblocks);
