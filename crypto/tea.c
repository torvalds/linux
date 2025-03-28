// SPDX-License-Identifier: GPL-2.0-or-later
/* 
 * Cryptographic API.
 *
 * TEA, XTEA, and XETA crypto alogrithms
 *
 * The TEA and Xtended TEA algorithms were developed by David Wheeler 
 * and Roger Needham at the Computer Laboratory of Cambridge University.
 *
 * Due to the order of evaluation in XTEA many people have incorrectly
 * implemented it.  XETA (XTEA in the wrong order), exists for
 * compatibility with these implementations.
 *
 * Copyright (c) 2004 Aaron Grothe ajgrothe@yahoo.com
 */

#include <crypto/algapi.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/unaligned.h>
#include <linux/types.h>

#define TEA_KEY_SIZE		16
#define TEA_BLOCK_SIZE		8
#define TEA_ROUNDS		32
#define TEA_DELTA		0x9e3779b9

#define XTEA_KEY_SIZE		16
#define XTEA_BLOCK_SIZE		8
#define XTEA_ROUNDS		32
#define XTEA_DELTA		0x9e3779b9

struct tea_ctx {
	u32 KEY[4];
};

struct xtea_ctx {
	u32 KEY[4];
};

static int tea_setkey(struct crypto_tfm *tfm, const u8 *in_key,
		      unsigned int key_len)
{
	struct tea_ctx *ctx = crypto_tfm_ctx(tfm);

	ctx->KEY[0] = get_unaligned_le32(&in_key[0]);
	ctx->KEY[1] = get_unaligned_le32(&in_key[4]);
	ctx->KEY[2] = get_unaligned_le32(&in_key[8]);
	ctx->KEY[3] = get_unaligned_le32(&in_key[12]);

	return 0; 

}

static void tea_encrypt(struct crypto_tfm *tfm, u8 *dst, const u8 *src)
{
	u32 y, z, n, sum = 0;
	u32 k0, k1, k2, k3;
	struct tea_ctx *ctx = crypto_tfm_ctx(tfm);

	y = get_unaligned_le32(&src[0]);
	z = get_unaligned_le32(&src[4]);

	k0 = ctx->KEY[0];
	k1 = ctx->KEY[1];
	k2 = ctx->KEY[2];
	k3 = ctx->KEY[3];

	n = TEA_ROUNDS;

	while (n-- > 0) {
		sum += TEA_DELTA;
		y += ((z << 4) + k0) ^ (z + sum) ^ ((z >> 5) + k1);
		z += ((y << 4) + k2) ^ (y + sum) ^ ((y >> 5) + k3);
	}
	
	put_unaligned_le32(y, &dst[0]);
	put_unaligned_le32(z, &dst[4]);
}

static void tea_decrypt(struct crypto_tfm *tfm, u8 *dst, const u8 *src)
{
	u32 y, z, n, sum;
	u32 k0, k1, k2, k3;
	struct tea_ctx *ctx = crypto_tfm_ctx(tfm);

	y = get_unaligned_le32(&src[0]);
	z = get_unaligned_le32(&src[4]);

	k0 = ctx->KEY[0];
	k1 = ctx->KEY[1];
	k2 = ctx->KEY[2];
	k3 = ctx->KEY[3];

	sum = TEA_DELTA << 5;

	n = TEA_ROUNDS;

	while (n-- > 0) {
		z -= ((y << 4) + k2) ^ (y + sum) ^ ((y >> 5) + k3);
		y -= ((z << 4) + k0) ^ (z + sum) ^ ((z >> 5) + k1);
		sum -= TEA_DELTA;
	}
	
	put_unaligned_le32(y, &dst[0]);
	put_unaligned_le32(z, &dst[4]);
}

static int xtea_setkey(struct crypto_tfm *tfm, const u8 *in_key,
		       unsigned int key_len)
{
	struct xtea_ctx *ctx = crypto_tfm_ctx(tfm);

	ctx->KEY[0] = get_unaligned_le32(&in_key[0]);
	ctx->KEY[1] = get_unaligned_le32(&in_key[4]);
	ctx->KEY[2] = get_unaligned_le32(&in_key[8]);
	ctx->KEY[3] = get_unaligned_le32(&in_key[12]);

	return 0; 

}

static void xtea_encrypt(struct crypto_tfm *tfm, u8 *dst, const u8 *src)
{
	u32 y, z, sum = 0;
	u32 limit = XTEA_DELTA * XTEA_ROUNDS;
	struct xtea_ctx *ctx = crypto_tfm_ctx(tfm);

	y = get_unaligned_le32(&src[0]);
	z = get_unaligned_le32(&src[4]);

	while (sum != limit) {
		y += ((z << 4 ^ z >> 5) + z) ^ (sum + ctx->KEY[sum&3]); 
		sum += XTEA_DELTA;
		z += ((y << 4 ^ y >> 5) + y) ^ (sum + ctx->KEY[sum>>11 &3]); 
	}
	
	put_unaligned_le32(y, &dst[0]);
	put_unaligned_le32(z, &dst[4]);
}

static void xtea_decrypt(struct crypto_tfm *tfm, u8 *dst, const u8 *src)
{
	u32 y, z, sum;
	struct tea_ctx *ctx = crypto_tfm_ctx(tfm);

	y = get_unaligned_le32(&src[0]);
	z = get_unaligned_le32(&src[4]);

	sum = XTEA_DELTA * XTEA_ROUNDS;

	while (sum) {
		z -= ((y << 4 ^ y >> 5) + y) ^ (sum + ctx->KEY[sum>>11 & 3]);
		sum -= XTEA_DELTA;
		y -= ((z << 4 ^ z >> 5) + z) ^ (sum + ctx->KEY[sum & 3]);
	}
	
	put_unaligned_le32(y, &dst[0]);
	put_unaligned_le32(z, &dst[4]);
}


static void xeta_encrypt(struct crypto_tfm *tfm, u8 *dst, const u8 *src)
{
	u32 y, z, sum = 0;
	u32 limit = XTEA_DELTA * XTEA_ROUNDS;
	struct xtea_ctx *ctx = crypto_tfm_ctx(tfm);

	y = get_unaligned_le32(&src[0]);
	z = get_unaligned_le32(&src[4]);

	while (sum != limit) {
		y += (z << 4 ^ z >> 5) + (z ^ sum) + ctx->KEY[sum&3];
		sum += XTEA_DELTA;
		z += (y << 4 ^ y >> 5) + (y ^ sum) + ctx->KEY[sum>>11 &3];
	}
	
	put_unaligned_le32(y, &dst[0]);
	put_unaligned_le32(z, &dst[4]);
}

static void xeta_decrypt(struct crypto_tfm *tfm, u8 *dst, const u8 *src)
{
	u32 y, z, sum;
	struct tea_ctx *ctx = crypto_tfm_ctx(tfm);

	y = get_unaligned_le32(&src[0]);
	z = get_unaligned_le32(&src[4]);

	sum = XTEA_DELTA * XTEA_ROUNDS;

	while (sum) {
		z -= (y << 4 ^ y >> 5) + (y ^ sum) + ctx->KEY[sum>>11 & 3];
		sum -= XTEA_DELTA;
		y -= (z << 4 ^ z >> 5) + (z ^ sum) + ctx->KEY[sum & 3];
	}
	
	put_unaligned_le32(y, &dst[0]);
	put_unaligned_le32(z, &dst[4]);
}

static struct crypto_alg tea_algs[3] = { {
	.cra_name		=	"tea",
	.cra_driver_name	=	"tea-generic",
	.cra_flags		=	CRYPTO_ALG_TYPE_CIPHER,
	.cra_blocksize		=	TEA_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof (struct tea_ctx),
	.cra_module		=	THIS_MODULE,
	.cra_u			=	{ .cipher = {
	.cia_min_keysize	=	TEA_KEY_SIZE,
	.cia_max_keysize	=	TEA_KEY_SIZE,
	.cia_setkey		= 	tea_setkey,
	.cia_encrypt		=	tea_encrypt,
	.cia_decrypt		=	tea_decrypt } }
}, {
	.cra_name		=	"xtea",
	.cra_driver_name	=	"xtea-generic",
	.cra_flags		=	CRYPTO_ALG_TYPE_CIPHER,
	.cra_blocksize		=	XTEA_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof (struct xtea_ctx),
	.cra_module		=	THIS_MODULE,
	.cra_u			=	{ .cipher = {
	.cia_min_keysize	=	XTEA_KEY_SIZE,
	.cia_max_keysize	=	XTEA_KEY_SIZE,
	.cia_setkey		= 	xtea_setkey,
	.cia_encrypt		=	xtea_encrypt,
	.cia_decrypt		=	xtea_decrypt } }
}, {
	.cra_name		=	"xeta",
	.cra_driver_name	=	"xeta-generic",
	.cra_flags		=	CRYPTO_ALG_TYPE_CIPHER,
	.cra_blocksize		=	XTEA_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof (struct xtea_ctx),
	.cra_module		=	THIS_MODULE,
	.cra_u			=	{ .cipher = {
	.cia_min_keysize	=	XTEA_KEY_SIZE,
	.cia_max_keysize	=	XTEA_KEY_SIZE,
	.cia_setkey		= 	xtea_setkey,
	.cia_encrypt		=	xeta_encrypt,
	.cia_decrypt		=	xeta_decrypt } }
} };

static int __init tea_mod_init(void)
{
	return crypto_register_algs(tea_algs, ARRAY_SIZE(tea_algs));
}

static void __exit tea_mod_fini(void)
{
	crypto_unregister_algs(tea_algs, ARRAY_SIZE(tea_algs));
}

MODULE_ALIAS_CRYPTO("tea");
MODULE_ALIAS_CRYPTO("xtea");
MODULE_ALIAS_CRYPTO("xeta");

subsys_initcall(tea_mod_init);
module_exit(tea_mod_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("TEA, XTEA & XETA Cryptographic Algorithms");
