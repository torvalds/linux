/* 
 * Cryptographic API.
 *
 * Support for VIA PadLock hardware crypto engine.
 *
 * Copyright (c) 2004  Michal Ludvig <michal@logix.cz>
 *
 * Key expansion routine taken from crypto/aes.c
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

#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/crypto.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <asm/byteorder.h>
#include "padlock.h"

#define AES_MIN_KEY_SIZE	16	/* in uint8_t units */
#define AES_MAX_KEY_SIZE	32	/* ditto */
#define AES_BLOCK_SIZE		16	/* ditto */
#define AES_EXTENDED_KEY_SIZE	64	/* in uint32_t units */
#define AES_EXTENDED_KEY_SIZE_B	(AES_EXTENDED_KEY_SIZE * sizeof(uint32_t))

struct aes_ctx {
	uint32_t e_data[AES_EXTENDED_KEY_SIZE];
	uint32_t d_data[AES_EXTENDED_KEY_SIZE];
	struct {
		struct cword encrypt;
		struct cword decrypt;
	} cword;
	uint32_t *E;
	uint32_t *D;
	int key_length;
};

/* ====== Key management routines ====== */

static inline uint32_t
generic_rotr32 (const uint32_t x, const unsigned bits)
{
	const unsigned n = bits % 32;
	return (x >> n) | (x << (32 - n));
}

static inline uint32_t
generic_rotl32 (const uint32_t x, const unsigned bits)
{
	const unsigned n = bits % 32;
	return (x << n) | (x >> (32 - n));
}

#define rotl generic_rotl32
#define rotr generic_rotr32

/*
 * #define byte(x, nr) ((unsigned char)((x) >> (nr*8))) 
 */
static inline uint8_t
byte(const uint32_t x, const unsigned n)
{
	return x >> (n << 3);
}

#define E_KEY ctx->E
#define D_KEY ctx->D

static uint8_t pow_tab[256];
static uint8_t log_tab[256];
static uint8_t sbx_tab[256];
static uint8_t isb_tab[256];
static uint32_t rco_tab[10];
static uint32_t ft_tab[4][256];
static uint32_t it_tab[4][256];

static uint32_t fl_tab[4][256];
static uint32_t il_tab[4][256];

static inline uint8_t
f_mult (uint8_t a, uint8_t b)
{
	uint8_t aa = log_tab[a], cc = aa + log_tab[b];

	return pow_tab[cc + (cc < aa ? 1 : 0)];
}

#define ff_mult(a,b)    (a && b ? f_mult(a, b) : 0)

#define f_rn(bo, bi, n, k)					\
    bo[n] =  ft_tab[0][byte(bi[n],0)] ^				\
             ft_tab[1][byte(bi[(n + 1) & 3],1)] ^		\
             ft_tab[2][byte(bi[(n + 2) & 3],2)] ^		\
             ft_tab[3][byte(bi[(n + 3) & 3],3)] ^ *(k + n)

#define i_rn(bo, bi, n, k)					\
    bo[n] =  it_tab[0][byte(bi[n],0)] ^				\
             it_tab[1][byte(bi[(n + 3) & 3],1)] ^		\
             it_tab[2][byte(bi[(n + 2) & 3],2)] ^		\
             it_tab[3][byte(bi[(n + 1) & 3],3)] ^ *(k + n)

#define ls_box(x)				\
    ( fl_tab[0][byte(x, 0)] ^			\
      fl_tab[1][byte(x, 1)] ^			\
      fl_tab[2][byte(x, 2)] ^			\
      fl_tab[3][byte(x, 3)] )

#define f_rl(bo, bi, n, k)					\
    bo[n] =  fl_tab[0][byte(bi[n],0)] ^				\
             fl_tab[1][byte(bi[(n + 1) & 3],1)] ^		\
             fl_tab[2][byte(bi[(n + 2) & 3],2)] ^		\
             fl_tab[3][byte(bi[(n + 3) & 3],3)] ^ *(k + n)

#define i_rl(bo, bi, n, k)					\
    bo[n] =  il_tab[0][byte(bi[n],0)] ^				\
             il_tab[1][byte(bi[(n + 3) & 3],1)] ^		\
             il_tab[2][byte(bi[(n + 2) & 3],2)] ^		\
             il_tab[3][byte(bi[(n + 1) & 3],3)] ^ *(k + n)

static void
gen_tabs (void)
{
	uint32_t i, t;
	uint8_t p, q;

	/* log and power tables for GF(2**8) finite field with
	   0x011b as modular polynomial - the simplest prmitive
	   root is 0x03, used here to generate the tables */

	for (i = 0, p = 1; i < 256; ++i) {
		pow_tab[i] = (uint8_t) p;
		log_tab[p] = (uint8_t) i;

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
		isb_tab[p] = (uint8_t) i;
	}

	for (i = 0; i < 256; ++i) {
		p = sbx_tab[i];

		t = p;
		fl_tab[0][i] = t;
		fl_tab[1][i] = rotl (t, 8);
		fl_tab[2][i] = rotl (t, 16);
		fl_tab[3][i] = rotl (t, 24);

		t = ((uint32_t) ff_mult (2, p)) |
		    ((uint32_t) p << 8) |
		    ((uint32_t) p << 16) | ((uint32_t) ff_mult (3, p) << 24);

		ft_tab[0][i] = t;
		ft_tab[1][i] = rotl (t, 8);
		ft_tab[2][i] = rotl (t, 16);
		ft_tab[3][i] = rotl (t, 24);

		p = isb_tab[i];

		t = p;
		il_tab[0][i] = t;
		il_tab[1][i] = rotl (t, 8);
		il_tab[2][i] = rotl (t, 16);
		il_tab[3][i] = rotl (t, 24);

		t = ((uint32_t) ff_mult (14, p)) |
		    ((uint32_t) ff_mult (9, p) << 8) |
		    ((uint32_t) ff_mult (13, p) << 16) |
		    ((uint32_t) ff_mult (11, p) << 24);

		it_tab[0][i] = t;
		it_tab[1][i] = rotl (t, 8);
		it_tab[2][i] = rotl (t, 16);
		it_tab[3][i] = rotl (t, 24);
	}
}

#define star_x(x) (((x) & 0x7f7f7f7f) << 1) ^ ((((x) & 0x80808080) >> 7) * 0x1b)

#define imix_col(y,x)       \
    u   = star_x(x);        \
    v   = star_x(u);        \
    w   = star_x(v);        \
    t   = w ^ (x);          \
   (y)  = u ^ v ^ w;        \
   (y) ^= rotr(u ^ t,  8) ^ \
          rotr(v ^ t, 16) ^ \
          rotr(t,24)

/* initialise the key schedule from the user supplied key */

#define loop4(i)                                    \
{   t = rotr(t,  8); t = ls_box(t) ^ rco_tab[i];    \
    t ^= E_KEY[4 * i];     E_KEY[4 * i + 4] = t;    \
    t ^= E_KEY[4 * i + 1]; E_KEY[4 * i + 5] = t;    \
    t ^= E_KEY[4 * i + 2]; E_KEY[4 * i + 6] = t;    \
    t ^= E_KEY[4 * i + 3]; E_KEY[4 * i + 7] = t;    \
}

#define loop6(i)                                    \
{   t = rotr(t,  8); t = ls_box(t) ^ rco_tab[i];    \
    t ^= E_KEY[6 * i];     E_KEY[6 * i + 6] = t;    \
    t ^= E_KEY[6 * i + 1]; E_KEY[6 * i + 7] = t;    \
    t ^= E_KEY[6 * i + 2]; E_KEY[6 * i + 8] = t;    \
    t ^= E_KEY[6 * i + 3]; E_KEY[6 * i + 9] = t;    \
    t ^= E_KEY[6 * i + 4]; E_KEY[6 * i + 10] = t;   \
    t ^= E_KEY[6 * i + 5]; E_KEY[6 * i + 11] = t;   \
}

#define loop8(i)                                    \
{   t = rotr(t,  8); ; t = ls_box(t) ^ rco_tab[i];  \
    t ^= E_KEY[8 * i];     E_KEY[8 * i + 8] = t;    \
    t ^= E_KEY[8 * i + 1]; E_KEY[8 * i + 9] = t;    \
    t ^= E_KEY[8 * i + 2]; E_KEY[8 * i + 10] = t;   \
    t ^= E_KEY[8 * i + 3]; E_KEY[8 * i + 11] = t;   \
    t  = E_KEY[8 * i + 4] ^ ls_box(t);    \
    E_KEY[8 * i + 12] = t;                \
    t ^= E_KEY[8 * i + 5]; E_KEY[8 * i + 13] = t;   \
    t ^= E_KEY[8 * i + 6]; E_KEY[8 * i + 14] = t;   \
    t ^= E_KEY[8 * i + 7]; E_KEY[8 * i + 15] = t;   \
}

/* Tells whether the ACE is capable to generate
   the extended key for a given key_len. */
static inline int
aes_hw_extkey_available(uint8_t key_len)
{
	/* TODO: We should check the actual CPU model/stepping
	         as it's possible that the capability will be
	         added in the next CPU revisions. */
	if (key_len == 16)
		return 1;
	return 0;
}

static inline struct aes_ctx *aes_ctx(void *ctx)
{
	unsigned long align = PADLOCK_ALIGNMENT;

	if (align <= crypto_tfm_ctx_alignment())
		align = 1;
	return (struct aes_ctx *)ALIGN((unsigned long)ctx, align);
}

static int
aes_set_key(void *ctx_arg, const uint8_t *in_key, unsigned int key_len, uint32_t *flags)
{
	struct aes_ctx *ctx = aes_ctx(ctx_arg);
	const __le32 *key = (const __le32 *)in_key;
	uint32_t i, t, u, v, w;
	uint32_t P[AES_EXTENDED_KEY_SIZE];
	uint32_t rounds;

	if (key_len != 16 && key_len != 24 && key_len != 32) {
		*flags |= CRYPTO_TFM_RES_BAD_KEY_LEN;
		return -EINVAL;
	}

	ctx->key_length = key_len;

	/*
	 * If the hardware is capable of generating the extended key
	 * itself we must supply the plain key for both encryption
	 * and decryption.
	 */
	ctx->E = ctx->e_data;
	ctx->D = ctx->e_data;

	E_KEY[0] = le32_to_cpu(key[0]);
	E_KEY[1] = le32_to_cpu(key[1]);
	E_KEY[2] = le32_to_cpu(key[2]);
	E_KEY[3] = le32_to_cpu(key[3]);

	/* Prepare control words. */
	memset(&ctx->cword, 0, sizeof(ctx->cword));

	ctx->cword.decrypt.encdec = 1;
	ctx->cword.encrypt.rounds = 10 + (key_len - 16) / 4;
	ctx->cword.decrypt.rounds = ctx->cword.encrypt.rounds;
	ctx->cword.encrypt.ksize = (key_len - 16) / 8;
	ctx->cword.decrypt.ksize = ctx->cword.encrypt.ksize;

	/* Don't generate extended keys if the hardware can do it. */
	if (aes_hw_extkey_available(key_len))
		return 0;

	ctx->D = ctx->d_data;
	ctx->cword.encrypt.keygen = 1;
	ctx->cword.decrypt.keygen = 1;

	switch (key_len) {
	case 16:
		t = E_KEY[3];
		for (i = 0; i < 10; ++i)
			loop4 (i);
		break;

	case 24:
		E_KEY[4] = le32_to_cpu(key[4]);
		t = E_KEY[5] = le32_to_cpu(key[5]);
		for (i = 0; i < 8; ++i)
			loop6 (i);
		break;

	case 32:
		E_KEY[4] = le32_to_cpu(key[4]);
		E_KEY[5] = le32_to_cpu(key[5]);
		E_KEY[6] = le32_to_cpu(key[6]);
		t = E_KEY[7] = le32_to_cpu(key[7]);
		for (i = 0; i < 7; ++i)
			loop8 (i);
		break;
	}

	D_KEY[0] = E_KEY[0];
	D_KEY[1] = E_KEY[1];
	D_KEY[2] = E_KEY[2];
	D_KEY[3] = E_KEY[3];

	for (i = 4; i < key_len + 24; ++i) {
		imix_col (D_KEY[i], E_KEY[i]);
	}

	/* PadLock needs a different format of the decryption key. */
	rounds = 10 + (key_len - 16) / 4;

	for (i = 0; i < rounds; i++) {
		P[((i + 1) * 4) + 0] = D_KEY[((rounds - i - 1) * 4) + 0];
		P[((i + 1) * 4) + 1] = D_KEY[((rounds - i - 1) * 4) + 1];
		P[((i + 1) * 4) + 2] = D_KEY[((rounds - i - 1) * 4) + 2];
		P[((i + 1) * 4) + 3] = D_KEY[((rounds - i - 1) * 4) + 3];
	}

	P[0] = E_KEY[(rounds * 4) + 0];
	P[1] = E_KEY[(rounds * 4) + 1];
	P[2] = E_KEY[(rounds * 4) + 2];
	P[3] = E_KEY[(rounds * 4) + 3];

	memcpy(D_KEY, P, AES_EXTENDED_KEY_SIZE_B);

	return 0;
}

/* ====== Encryption/decryption routines ====== */

/* These are the real call to PadLock. */
static inline void padlock_xcrypt_ecb(const u8 *input, u8 *output, void *key,
				      void *control_word, u32 count)
{
	asm volatile ("pushfl; popfl");		/* enforce key reload. */
	asm volatile (".byte 0xf3,0x0f,0xa7,0xc8"	/* rep xcryptecb */
		      : "+S"(input), "+D"(output)
		      : "d"(control_word), "b"(key), "c"(count));
}

static inline u8 *padlock_xcrypt_cbc(const u8 *input, u8 *output, void *key,
				     u8 *iv, void *control_word, u32 count)
{
	/* Enforce key reload. */
	asm volatile ("pushfl; popfl");
	/* rep xcryptcbc */
	asm volatile (".byte 0xf3,0x0f,0xa7,0xd0"
		      : "+S" (input), "+D" (output), "+a" (iv)
		      : "d" (control_word), "b" (key), "c" (count));
	return iv;
}

static void
aes_encrypt(void *ctx_arg, uint8_t *out, const uint8_t *in)
{
	struct aes_ctx *ctx = aes_ctx(ctx_arg);
	padlock_xcrypt_ecb(in, out, ctx->E, &ctx->cword.encrypt, 1);
}

static void
aes_decrypt(void *ctx_arg, uint8_t *out, const uint8_t *in)
{
	struct aes_ctx *ctx = aes_ctx(ctx_arg);
	padlock_xcrypt_ecb(in, out, ctx->D, &ctx->cword.decrypt, 1);
}

static unsigned int aes_encrypt_ecb(const struct cipher_desc *desc, u8 *out,
				    const u8 *in, unsigned int nbytes)
{
	struct aes_ctx *ctx = aes_ctx(crypto_tfm_ctx(desc->tfm));
	padlock_xcrypt_ecb(in, out, ctx->E, &ctx->cword.encrypt,
			   nbytes / AES_BLOCK_SIZE);
	return nbytes & ~(AES_BLOCK_SIZE - 1);
}

static unsigned int aes_decrypt_ecb(const struct cipher_desc *desc, u8 *out,
				    const u8 *in, unsigned int nbytes)
{
	struct aes_ctx *ctx = aes_ctx(crypto_tfm_ctx(desc->tfm));
	padlock_xcrypt_ecb(in, out, ctx->D, &ctx->cword.decrypt,
			   nbytes / AES_BLOCK_SIZE);
	return nbytes & ~(AES_BLOCK_SIZE - 1);
}

static unsigned int aes_encrypt_cbc(const struct cipher_desc *desc, u8 *out,
				    const u8 *in, unsigned int nbytes)
{
	struct aes_ctx *ctx = aes_ctx(crypto_tfm_ctx(desc->tfm));
	u8 *iv;

	iv = padlock_xcrypt_cbc(in, out, ctx->E, desc->info,
				&ctx->cword.encrypt, nbytes / AES_BLOCK_SIZE);
	memcpy(desc->info, iv, AES_BLOCK_SIZE);

	return nbytes & ~(AES_BLOCK_SIZE - 1);
}

static unsigned int aes_decrypt_cbc(const struct cipher_desc *desc, u8 *out,
				    const u8 *in, unsigned int nbytes)
{
	struct aes_ctx *ctx = aes_ctx(crypto_tfm_ctx(desc->tfm));
	padlock_xcrypt_cbc(in, out, ctx->D, desc->info, &ctx->cword.decrypt,
			   nbytes / AES_BLOCK_SIZE);
	return nbytes & ~(AES_BLOCK_SIZE - 1);
}

static struct crypto_alg aes_alg = {
	.cra_name		=	"aes",
	.cra_driver_name	=	"aes-padlock",
	.cra_priority		=	300,
	.cra_flags		=	CRYPTO_ALG_TYPE_CIPHER,
	.cra_blocksize		=	AES_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct aes_ctx),
	.cra_alignmask		=	PADLOCK_ALIGNMENT - 1,
	.cra_module		=	THIS_MODULE,
	.cra_list		=	LIST_HEAD_INIT(aes_alg.cra_list),
	.cra_u			=	{
		.cipher = {
			.cia_min_keysize	=	AES_MIN_KEY_SIZE,
			.cia_max_keysize	=	AES_MAX_KEY_SIZE,
			.cia_setkey	   	= 	aes_set_key,
			.cia_encrypt	 	=	aes_encrypt,
			.cia_decrypt	  	=	aes_decrypt,
			.cia_encrypt_ecb 	=	aes_encrypt_ecb,
			.cia_decrypt_ecb  	=	aes_decrypt_ecb,
			.cia_encrypt_cbc 	=	aes_encrypt_cbc,
			.cia_decrypt_cbc  	=	aes_decrypt_cbc,
		}
	}
};

int __init padlock_init_aes(void)
{
	printk(KERN_NOTICE PFX "Using VIA PadLock ACE for AES algorithm.\n");

	gen_tabs();
	return crypto_register_alg(&aes_alg);
}

void __exit padlock_fini_aes(void)
{
	crypto_unregister_alg(&aes_alg);
}
