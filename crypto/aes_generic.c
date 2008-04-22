/* 
 * Cryptographic API.
 *
 * AES Cipher Algorithm.
 *
 * Based on Brian Gladman's code.
 *
 * Linux developers:
 *  Alexander Kjeldaas <astor@fast.no>
 *  Herbert Valerio Riedel <hvr@hvrlab.org>
 *  Kyle McMartin <kyle@debian.org>
 *  Adam J. Richter <adam@yggdrasil.com> (conversion to 2.5 API).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * ---------------------------------------------------------------------------
 * Copyright (c) 2002, Dr Brian Gladman <brg@gladman.me.uk>, Worcester, UK.
 * All rights reserved.
 *
 * LICENSE TERMS
 *
 * The free distribution and use of this software in both source and binary
 * form is allowed (with or without changes) provided that:
 *
 *   1. distributions of this source code include the above copyright
 *      notice, this list of conditions and the following disclaimer;
 *
 *   2. distributions in binary form include the above copyright
 *      notice, this list of conditions and the following disclaimer
 *      in the documentation and/or other associated materials;
 *
 *   3. the copyright holder's name is not used to endorse products
 *      built using this software without specific written permission.
 *
 * ALTERNATIVELY, provided that this notice is retained in full, this product
 * may be distributed under the terms of the GNU General Public License (GPL),
 * in which case the provisions of the GPL apply INSTEAD OF those given above.
 *
 * DISCLAIMER
 *
 * This software is provided 'as is' with no explicit or implied warranties
 * in respect of its properties, including, but not limited to, correctness
 * and/or fitness for purpose.
 * ---------------------------------------------------------------------------
 */

#include <crypto/aes.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/crypto.h>
#include <asm/byteorder.h>

static inline u8 byte(const u32 x, const unsigned n)
{
	return x >> (n << 3);
}

static u8 pow_tab[256] __initdata;
static u8 log_tab[256] __initdata;
static u8 sbx_tab[256] __initdata;
static u8 isb_tab[256] __initdata;
static u32 rco_tab[10];

u32 crypto_ft_tab[4][256];
u32 crypto_fl_tab[4][256];
u32 crypto_it_tab[4][256];
u32 crypto_il_tab[4][256];

EXPORT_SYMBOL_GPL(crypto_ft_tab);
EXPORT_SYMBOL_GPL(crypto_fl_tab);
EXPORT_SYMBOL_GPL(crypto_it_tab);
EXPORT_SYMBOL_GPL(crypto_il_tab);

static inline u8 __init f_mult(u8 a, u8 b)
{
	u8 aa = log_tab[a], cc = aa + log_tab[b];

	return pow_tab[cc + (cc < aa ? 1 : 0)];
}

#define ff_mult(a, b)	(a && b ? f_mult(a, b) : 0)

static void __init gen_tabs(void)
{
	u32 i, t;
	u8 p, q;

	/*
	 * log and power tables for GF(2**8) finite field with
	 * 0x011b as modular polynomial - the simplest primitive
	 * root is 0x03, used here to generate the tables
	 */

	for (i = 0, p = 1; i < 256; ++i) {
		pow_tab[i] = (u8) p;
		log_tab[p] = (u8) i;

		p ^= (p << 1) ^ (p & 0x80 ? 0x01b : 0);
	}

	log_tab[1] = 0;

	for (i = 0, p = 1; i < 10; ++i) {
		rco_tab[i] = p;

		p = (p << 1) ^ (p & 0x80 ? 0x01b : 0);
	}

	for (i = 0; i < 256; ++i) {
		p = (i ? pow_tab[255 - log_tab[i]] : 0);
		q = ((p >> 7) | (p << 1)) ^ ((p >> 6) | (p << 2));
		p ^= 0x63 ^ q ^ ((q >> 6) | (q << 2));
		sbx_tab[i] = p;
		isb_tab[p] = (u8) i;
	}

	for (i = 0; i < 256; ++i) {
		p = sbx_tab[i];

		t = p;
		crypto_fl_tab[0][i] = t;
		crypto_fl_tab[1][i] = rol32(t, 8);
		crypto_fl_tab[2][i] = rol32(t, 16);
		crypto_fl_tab[3][i] = rol32(t, 24);

		t = ((u32) ff_mult(2, p)) |
		    ((u32) p << 8) |
		    ((u32) p << 16) | ((u32) ff_mult(3, p) << 24);

		crypto_ft_tab[0][i] = t;
		crypto_ft_tab[1][i] = rol32(t, 8);
		crypto_ft_tab[2][i] = rol32(t, 16);
		crypto_ft_tab[3][i] = rol32(t, 24);

		p = isb_tab[i];

		t = p;
		crypto_il_tab[0][i] = t;
		crypto_il_tab[1][i] = rol32(t, 8);
		crypto_il_tab[2][i] = rol32(t, 16);
		crypto_il_tab[3][i] = rol32(t, 24);

		t = ((u32) ff_mult(14, p)) |
		    ((u32) ff_mult(9, p) << 8) |
		    ((u32) ff_mult(13, p) << 16) |
		    ((u32) ff_mult(11, p) << 24);

		crypto_it_tab[0][i] = t;
		crypto_it_tab[1][i] = rol32(t, 8);
		crypto_it_tab[2][i] = rol32(t, 16);
		crypto_it_tab[3][i] = rol32(t, 24);
	}
}

/* initialise the key schedule from the user supplied key */

#define star_x(x) (((x) & 0x7f7f7f7f) << 1) ^ ((((x) & 0x80808080) >> 7) * 0x1b)

#define imix_col(y,x)	do {		\
	u	= star_x(x);		\
	v	= star_x(u);		\
	w	= star_x(v);		\
	t	= w ^ (x);		\
	(y)	= u ^ v ^ w;		\
	(y)	^= ror32(u ^ t, 8) ^	\
		ror32(v ^ t, 16) ^	\
		ror32(t, 24);		\
} while (0)

#define ls_box(x)		\
	crypto_fl_tab[0][byte(x, 0)] ^	\
	crypto_fl_tab[1][byte(x, 1)] ^	\
	crypto_fl_tab[2][byte(x, 2)] ^	\
	crypto_fl_tab[3][byte(x, 3)]

#define loop4(i)	do {		\
	t = ror32(t, 8);		\
	t = ls_box(t) ^ rco_tab[i];	\
	t ^= ctx->key_enc[4 * i];		\
	ctx->key_enc[4 * i + 4] = t;		\
	t ^= ctx->key_enc[4 * i + 1];		\
	ctx->key_enc[4 * i + 5] = t;		\
	t ^= ctx->key_enc[4 * i + 2];		\
	ctx->key_enc[4 * i + 6] = t;		\
	t ^= ctx->key_enc[4 * i + 3];		\
	ctx->key_enc[4 * i + 7] = t;		\
} while (0)

#define loop6(i)	do {		\
	t = ror32(t, 8);		\
	t = ls_box(t) ^ rco_tab[i];	\
	t ^= ctx->key_enc[6 * i];		\
	ctx->key_enc[6 * i + 6] = t;		\
	t ^= ctx->key_enc[6 * i + 1];		\
	ctx->key_enc[6 * i + 7] = t;		\
	t ^= ctx->key_enc[6 * i + 2];		\
	ctx->key_enc[6 * i + 8] = t;		\
	t ^= ctx->key_enc[6 * i + 3];		\
	ctx->key_enc[6 * i + 9] = t;		\
	t ^= ctx->key_enc[6 * i + 4];		\
	ctx->key_enc[6 * i + 10] = t;		\
	t ^= ctx->key_enc[6 * i + 5];		\
	ctx->key_enc[6 * i + 11] = t;		\
} while (0)

#define loop8(i)	do {			\
	t = ror32(t, 8);			\
	t = ls_box(t) ^ rco_tab[i];		\
	t ^= ctx->key_enc[8 * i];			\
	ctx->key_enc[8 * i + 8] = t;			\
	t ^= ctx->key_enc[8 * i + 1];			\
	ctx->key_enc[8 * i + 9] = t;			\
	t ^= ctx->key_enc[8 * i + 2];			\
	ctx->key_enc[8 * i + 10] = t;			\
	t ^= ctx->key_enc[8 * i + 3];			\
	ctx->key_enc[8 * i + 11] = t;			\
	t  = ctx->key_enc[8 * i + 4] ^ ls_box(t);	\
	ctx->key_enc[8 * i + 12] = t;			\
	t ^= ctx->key_enc[8 * i + 5];			\
	ctx->key_enc[8 * i + 13] = t;			\
	t ^= ctx->key_enc[8 * i + 6];			\
	ctx->key_enc[8 * i + 14] = t;			\
	t ^= ctx->key_enc[8 * i + 7];			\
	ctx->key_enc[8 * i + 15] = t;			\
} while (0)

/**
 * crypto_aes_expand_key - Expands the AES key as described in FIPS-197
 * @ctx:	The location where the computed key will be stored.
 * @in_key:	The supplied key.
 * @key_len:	The length of the supplied key.
 *
 * Returns 0 on success. The function fails only if an invalid key size (or
 * pointer) is supplied.
 * The expanded key size is 240 bytes (max of 14 rounds with a unique 16 bytes
 * key schedule plus a 16 bytes key which is used before the first round).
 * The decryption key is prepared for the "Equivalent Inverse Cipher" as
 * described in FIPS-197. The first slot (16 bytes) of each key (enc or dec) is
 * for the initial combination, the second slot for the first round and so on.
 */
int crypto_aes_expand_key(struct crypto_aes_ctx *ctx, const u8 *in_key,
		unsigned int key_len)
{
	const __le32 *key = (const __le32 *)in_key;
	u32 i, t, u, v, w, j;

	if (key_len != AES_KEYSIZE_128 && key_len != AES_KEYSIZE_192 &&
			key_len != AES_KEYSIZE_256)
		return -EINVAL;

	ctx->key_length = key_len;

	ctx->key_dec[key_len + 24] = ctx->key_enc[0] = le32_to_cpu(key[0]);
	ctx->key_dec[key_len + 25] = ctx->key_enc[1] = le32_to_cpu(key[1]);
	ctx->key_dec[key_len + 26] = ctx->key_enc[2] = le32_to_cpu(key[2]);
	ctx->key_dec[key_len + 27] = ctx->key_enc[3] = le32_to_cpu(key[3]);

	switch (key_len) {
	case AES_KEYSIZE_128:
		t = ctx->key_enc[3];
		for (i = 0; i < 10; ++i)
			loop4(i);
		break;

	case AES_KEYSIZE_192:
		ctx->key_enc[4] = le32_to_cpu(key[4]);
		t = ctx->key_enc[5] = le32_to_cpu(key[5]);
		for (i = 0; i < 8; ++i)
			loop6(i);
		break;

	case AES_KEYSIZE_256:
		ctx->key_enc[4] = le32_to_cpu(key[4]);
		ctx->key_enc[5] = le32_to_cpu(key[5]);
		ctx->key_enc[6] = le32_to_cpu(key[6]);
		t = ctx->key_enc[7] = le32_to_cpu(key[7]);
		for (i = 0; i < 7; ++i)
			loop8(i);
		break;
	}

	ctx->key_dec[0] = ctx->key_enc[key_len + 24];
	ctx->key_dec[1] = ctx->key_enc[key_len + 25];
	ctx->key_dec[2] = ctx->key_enc[key_len + 26];
	ctx->key_dec[3] = ctx->key_enc[key_len + 27];

	for (i = 4; i < key_len + 24; ++i) {
		j = key_len + 24 - (i & ~3) + (i & 3);
		imix_col(ctx->key_dec[j], ctx->key_enc[i]);
	}
	return 0;
}
EXPORT_SYMBOL_GPL(crypto_aes_expand_key);

/**
 * crypto_aes_set_key - Set the AES key.
 * @tfm:	The %crypto_tfm that is used in the context.
 * @in_key:	The input key.
 * @key_len:	The size of the key.
 *
 * Returns 0 on success, on failure the %CRYPTO_TFM_RES_BAD_KEY_LEN flag in tfm
 * is set. The function uses crypto_aes_expand_key() to expand the key.
 * &crypto_aes_ctx _must_ be the private data embedded in @tfm which is
 * retrieved with crypto_tfm_ctx().
 */
int crypto_aes_set_key(struct crypto_tfm *tfm, const u8 *in_key,
		unsigned int key_len)
{
	struct crypto_aes_ctx *ctx = crypto_tfm_ctx(tfm);
	u32 *flags = &tfm->crt_flags;
	int ret;

	ret = crypto_aes_expand_key(ctx, in_key, key_len);
	if (!ret)
		return 0;

	*flags |= CRYPTO_TFM_RES_BAD_KEY_LEN;
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(crypto_aes_set_key);

/* encrypt a block of text */

#define f_rn(bo, bi, n, k)	do {				\
	bo[n] = crypto_ft_tab[0][byte(bi[n], 0)] ^			\
		crypto_ft_tab[1][byte(bi[(n + 1) & 3], 1)] ^		\
		crypto_ft_tab[2][byte(bi[(n + 2) & 3], 2)] ^		\
		crypto_ft_tab[3][byte(bi[(n + 3) & 3], 3)] ^ *(k + n);	\
} while (0)

#define f_nround(bo, bi, k)	do {\
	f_rn(bo, bi, 0, k);	\
	f_rn(bo, bi, 1, k);	\
	f_rn(bo, bi, 2, k);	\
	f_rn(bo, bi, 3, k);	\
	k += 4;			\
} while (0)

#define f_rl(bo, bi, n, k)	do {				\
	bo[n] = crypto_fl_tab[0][byte(bi[n], 0)] ^			\
		crypto_fl_tab[1][byte(bi[(n + 1) & 3], 1)] ^		\
		crypto_fl_tab[2][byte(bi[(n + 2) & 3], 2)] ^		\
		crypto_fl_tab[3][byte(bi[(n + 3) & 3], 3)] ^ *(k + n);	\
} while (0)

#define f_lround(bo, bi, k)	do {\
	f_rl(bo, bi, 0, k);	\
	f_rl(bo, bi, 1, k);	\
	f_rl(bo, bi, 2, k);	\
	f_rl(bo, bi, 3, k);	\
} while (0)

static void aes_encrypt(struct crypto_tfm *tfm, u8 *out, const u8 *in)
{
	const struct crypto_aes_ctx *ctx = crypto_tfm_ctx(tfm);
	const __le32 *src = (const __le32 *)in;
	__le32 *dst = (__le32 *)out;
	u32 b0[4], b1[4];
	const u32 *kp = ctx->key_enc + 4;
	const int key_len = ctx->key_length;

	b0[0] = le32_to_cpu(src[0]) ^ ctx->key_enc[0];
	b0[1] = le32_to_cpu(src[1]) ^ ctx->key_enc[1];
	b0[2] = le32_to_cpu(src[2]) ^ ctx->key_enc[2];
	b0[3] = le32_to_cpu(src[3]) ^ ctx->key_enc[3];

	if (key_len > 24) {
		f_nround(b1, b0, kp);
		f_nround(b0, b1, kp);
	}

	if (key_len > 16) {
		f_nround(b1, b0, kp);
		f_nround(b0, b1, kp);
	}

	f_nround(b1, b0, kp);
	f_nround(b0, b1, kp);
	f_nround(b1, b0, kp);
	f_nround(b0, b1, kp);
	f_nround(b1, b0, kp);
	f_nround(b0, b1, kp);
	f_nround(b1, b0, kp);
	f_nround(b0, b1, kp);
	f_nround(b1, b0, kp);
	f_lround(b0, b1, kp);

	dst[0] = cpu_to_le32(b0[0]);
	dst[1] = cpu_to_le32(b0[1]);
	dst[2] = cpu_to_le32(b0[2]);
	dst[3] = cpu_to_le32(b0[3]);
}

/* decrypt a block of text */

#define i_rn(bo, bi, n, k)	do {				\
	bo[n] = crypto_it_tab[0][byte(bi[n], 0)] ^			\
		crypto_it_tab[1][byte(bi[(n + 3) & 3], 1)] ^		\
		crypto_it_tab[2][byte(bi[(n + 2) & 3], 2)] ^		\
		crypto_it_tab[3][byte(bi[(n + 1) & 3], 3)] ^ *(k + n);	\
} while (0)

#define i_nround(bo, bi, k)	do {\
	i_rn(bo, bi, 0, k);	\
	i_rn(bo, bi, 1, k);	\
	i_rn(bo, bi, 2, k);	\
	i_rn(bo, bi, 3, k);	\
	k += 4;			\
} while (0)

#define i_rl(bo, bi, n, k)	do {			\
	bo[n] = crypto_il_tab[0][byte(bi[n], 0)] ^		\
	crypto_il_tab[1][byte(bi[(n + 3) & 3], 1)] ^		\
	crypto_il_tab[2][byte(bi[(n + 2) & 3], 2)] ^		\
	crypto_il_tab[3][byte(bi[(n + 1) & 3], 3)] ^ *(k + n);	\
} while (0)

#define i_lround(bo, bi, k)	do {\
	i_rl(bo, bi, 0, k);	\
	i_rl(bo, bi, 1, k);	\
	i_rl(bo, bi, 2, k);	\
	i_rl(bo, bi, 3, k);	\
} while (0)

static void aes_decrypt(struct crypto_tfm *tfm, u8 *out, const u8 *in)
{
	const struct crypto_aes_ctx *ctx = crypto_tfm_ctx(tfm);
	const __le32 *src = (const __le32 *)in;
	__le32 *dst = (__le32 *)out;
	u32 b0[4], b1[4];
	const int key_len = ctx->key_length;
	const u32 *kp = ctx->key_dec + 4;

	b0[0] = le32_to_cpu(src[0]) ^  ctx->key_dec[0];
	b0[1] = le32_to_cpu(src[1]) ^  ctx->key_dec[1];
	b0[2] = le32_to_cpu(src[2]) ^  ctx->key_dec[2];
	b0[3] = le32_to_cpu(src[3]) ^  ctx->key_dec[3];

	if (key_len > 24) {
		i_nround(b1, b0, kp);
		i_nround(b0, b1, kp);
	}

	if (key_len > 16) {
		i_nround(b1, b0, kp);
		i_nround(b0, b1, kp);
	}

	i_nround(b1, b0, kp);
	i_nround(b0, b1, kp);
	i_nround(b1, b0, kp);
	i_nround(b0, b1, kp);
	i_nround(b1, b0, kp);
	i_nround(b0, b1, kp);
	i_nround(b1, b0, kp);
	i_nround(b0, b1, kp);
	i_nround(b1, b0, kp);
	i_lround(b0, b1, kp);

	dst[0] = cpu_to_le32(b0[0]);
	dst[1] = cpu_to_le32(b0[1]);
	dst[2] = cpu_to_le32(b0[2]);
	dst[3] = cpu_to_le32(b0[3]);
}

static struct crypto_alg aes_alg = {
	.cra_name		=	"aes",
	.cra_driver_name	=	"aes-generic",
	.cra_priority		=	100,
	.cra_flags		=	CRYPTO_ALG_TYPE_CIPHER,
	.cra_blocksize		=	AES_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct crypto_aes_ctx),
	.cra_alignmask		=	3,
	.cra_module		=	THIS_MODULE,
	.cra_list		=	LIST_HEAD_INIT(aes_alg.cra_list),
	.cra_u			=	{
		.cipher = {
			.cia_min_keysize	=	AES_MIN_KEY_SIZE,
			.cia_max_keysize	=	AES_MAX_KEY_SIZE,
			.cia_setkey		=	crypto_aes_set_key,
			.cia_encrypt		=	aes_encrypt,
			.cia_decrypt		=	aes_decrypt
		}
	}
};

static int __init aes_init(void)
{
	gen_tabs();
	return crypto_register_alg(&aes_alg);
}

static void __exit aes_fini(void)
{
	crypto_unregister_alg(&aes_alg);
}

module_init(aes_init);
module_exit(aes_fini);

MODULE_DESCRIPTION("Rijndael (AES) Cipher Algorithm");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_ALIAS("aes");
