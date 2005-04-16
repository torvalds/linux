/* 
 * Cryptographic API.
 *
 * Null algorithms, aka Much Ado About Nothing.
 *
 * These are needed for IPsec, and may be useful in general for
 * testing & debugging.
 * 
 * The null cipher is compliant with RFC2410.
 *
 * Copyright (c) 2002 James Morris <jmorris@intercode.com.au>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <asm/scatterlist.h>
#include <linux/crypto.h>

#define NULL_KEY_SIZE		0
#define NULL_BLOCK_SIZE		1
#define NULL_DIGEST_SIZE	0

static int null_compress(void *ctx, const u8 *src, unsigned int slen,
                         u8 *dst, unsigned int *dlen)
{ return 0; }

static int null_decompress(void *ctx, const u8 *src, unsigned int slen,
                           u8 *dst, unsigned int *dlen)
{ return 0; }

static void null_init(void *ctx)
{ }

static void null_update(void *ctx, const u8 *data, unsigned int len)
{ }

static void null_final(void *ctx, u8 *out)
{ }

static int null_setkey(void *ctx, const u8 *key,
                       unsigned int keylen, u32 *flags)
{ return 0; }

static void null_encrypt(void *ctx, u8 *dst, const u8 *src)
{ }

static void null_decrypt(void *ctx, u8 *dst, const u8 *src)
{ }

static struct crypto_alg compress_null = {
	.cra_name		=	"compress_null",
	.cra_flags		=	CRYPTO_ALG_TYPE_COMPRESS,
	.cra_blocksize		=	NULL_BLOCK_SIZE,
	.cra_ctxsize		=	0,
	.cra_module		=	THIS_MODULE,
	.cra_list		=       LIST_HEAD_INIT(compress_null.cra_list),
	.cra_u			=	{ .compress = {
	.coa_compress 		=	null_compress,
	.coa_decompress		=	null_decompress } }
};

static struct crypto_alg digest_null = {
	.cra_name		=	"digest_null",
	.cra_flags		=	CRYPTO_ALG_TYPE_DIGEST,
	.cra_blocksize		=	NULL_BLOCK_SIZE,
	.cra_ctxsize		=	0,
	.cra_module		=	THIS_MODULE,
	.cra_list		=       LIST_HEAD_INIT(digest_null.cra_list),	
	.cra_u			=	{ .digest = {
	.dia_digestsize		=	NULL_DIGEST_SIZE,
	.dia_init   		=	null_init,
	.dia_update 		=	null_update,
	.dia_final  		=	null_final } }
};

static struct crypto_alg cipher_null = {
	.cra_name		=	"cipher_null",
	.cra_flags		=	CRYPTO_ALG_TYPE_CIPHER,
	.cra_blocksize		=	NULL_BLOCK_SIZE,
	.cra_ctxsize		=	0,
	.cra_module		=	THIS_MODULE,
	.cra_list		=	LIST_HEAD_INIT(cipher_null.cra_list),
	.cra_u			=	{ .cipher = {
	.cia_min_keysize	=	NULL_KEY_SIZE,
	.cia_max_keysize	=	NULL_KEY_SIZE,
	.cia_setkey		= 	null_setkey,
	.cia_encrypt		=	null_encrypt,
	.cia_decrypt		=	null_decrypt } }
};

MODULE_ALIAS("compress_null");
MODULE_ALIAS("digest_null");
MODULE_ALIAS("cipher_null");

static int __init init(void)
{
	int ret = 0;
	
	ret = crypto_register_alg(&cipher_null);
	if (ret < 0)
		goto out;

	ret = crypto_register_alg(&digest_null);
	if (ret < 0) {
		crypto_unregister_alg(&cipher_null);
		goto out;
	}

	ret = crypto_register_alg(&compress_null);
	if (ret < 0) {
		crypto_unregister_alg(&digest_null);
		crypto_unregister_alg(&cipher_null);
		goto out;
	}

out:	
	return ret;
}

static void __exit fini(void)
{
	crypto_unregister_alg(&compress_null);
	crypto_unregister_alg(&digest_null);
	crypto_unregister_alg(&cipher_null);
}

module_init(init);
module_exit(fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Null Cryptographic Algorithms");
