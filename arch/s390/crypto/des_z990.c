/*
 * Cryptographic API.
 *
 * z990 implementation of the DES Cipher Algorithm.
 *
 * Copyright (c) 2003 IBM Deutschland Entwicklung GmbH, IBM Corporation
 * Author(s): Thomas Spatzier (tspat@de.ibm.com)
 *
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
#include <linux/errno.h>
#include <asm/scatterlist.h>
#include <linux/crypto.h>
#include "crypt_z990.h"
#include "crypto_des.h"

#define DES_BLOCK_SIZE 8
#define DES_KEY_SIZE 8

#define DES3_128_KEY_SIZE	(2 * DES_KEY_SIZE)
#define DES3_128_BLOCK_SIZE	DES_BLOCK_SIZE

#define DES3_192_KEY_SIZE	(3 * DES_KEY_SIZE)
#define DES3_192_BLOCK_SIZE	DES_BLOCK_SIZE

struct crypt_z990_des_ctx {
	u8 iv[DES_BLOCK_SIZE];
	u8 key[DES_KEY_SIZE];
};

struct crypt_z990_des3_128_ctx {
	u8 iv[DES_BLOCK_SIZE];
	u8 key[DES3_128_KEY_SIZE];
};

struct crypt_z990_des3_192_ctx {
	u8 iv[DES_BLOCK_SIZE];
	u8 key[DES3_192_KEY_SIZE];
};

static int
des_setkey(void *ctx, const u8 *key, unsigned int keylen, u32 *flags)
{
	struct crypt_z990_des_ctx *dctx;
	int ret;

	dctx = ctx;
	//test if key is valid (not a weak key)
	ret = crypto_des_check_key(key, keylen, flags);
	if (ret == 0){
		memcpy(dctx->key, key, keylen);
	}
	return ret;
}


static void
des_encrypt(void *ctx, u8 *dst, const u8 *src)
{
	struct crypt_z990_des_ctx *dctx;

	dctx = ctx;
	crypt_z990_km(KM_DEA_ENCRYPT, dctx->key, dst, src, DES_BLOCK_SIZE);
}

static void
des_decrypt(void *ctx, u8 *dst, const u8 *src)
{
	struct crypt_z990_des_ctx *dctx;

	dctx = ctx;
	crypt_z990_km(KM_DEA_DECRYPT, dctx->key, dst, src, DES_BLOCK_SIZE);
}

static struct crypto_alg des_alg = {
	.cra_name		=	"des",
	.cra_flags		=	CRYPTO_ALG_TYPE_CIPHER,
	.cra_blocksize		=	DES_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct crypt_z990_des_ctx),
	.cra_module		=	THIS_MODULE,
	.cra_list		=	LIST_HEAD_INIT(des_alg.cra_list),
	.cra_u			=	{ .cipher = {
	.cia_min_keysize	=	DES_KEY_SIZE,
	.cia_max_keysize	=	DES_KEY_SIZE,
	.cia_setkey		= 	des_setkey,
	.cia_encrypt		=	des_encrypt,
	.cia_decrypt		=	des_decrypt } }
};

/*
 * RFC2451:
 *
 *   For DES-EDE3, there is no known need to reject weak or
 *   complementation keys.  Any weakness is obviated by the use of
 *   multiple keys.
 *
 *   However, if the two  independent 64-bit keys are equal,
 *   then the DES3 operation is simply the same as DES.
 *   Implementers MUST reject keys that exhibit this property.
 *
 */
static int
des3_128_setkey(void *ctx, const u8 *key, unsigned int keylen, u32 *flags)
{
	int i, ret;
	struct crypt_z990_des3_128_ctx *dctx;
	const u8* temp_key = key;

	dctx = ctx;
	if (!(memcmp(key, &key[DES_KEY_SIZE], DES_KEY_SIZE))) {

		*flags |= CRYPTO_TFM_RES_BAD_KEY_SCHED;
		return -EINVAL;
	}
	for (i = 0; i < 2; i++,	temp_key += DES_KEY_SIZE) {
		ret = crypto_des_check_key(temp_key, DES_KEY_SIZE, flags);
		if (ret < 0)
			return ret;
	}
	memcpy(dctx->key, key, keylen);
	return 0;
}

static void
des3_128_encrypt(void *ctx, u8 *dst, const u8 *src)
{
	struct crypt_z990_des3_128_ctx *dctx;

	dctx = ctx;
	crypt_z990_km(KM_TDEA_128_ENCRYPT, dctx->key, dst, (void*)src,
			DES3_128_BLOCK_SIZE);
}

static void
des3_128_decrypt(void *ctx, u8 *dst, const u8 *src)
{
	struct crypt_z990_des3_128_ctx *dctx;

	dctx = ctx;
	crypt_z990_km(KM_TDEA_128_DECRYPT, dctx->key, dst, (void*)src,
			DES3_128_BLOCK_SIZE);
}

static struct crypto_alg des3_128_alg = {
	.cra_name		=	"des3_ede128",
	.cra_flags		=	CRYPTO_ALG_TYPE_CIPHER,
	.cra_blocksize		=	DES3_128_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct crypt_z990_des3_128_ctx),
	.cra_module		=	THIS_MODULE,
	.cra_list		=	LIST_HEAD_INIT(des3_128_alg.cra_list),
	.cra_u			=	{ .cipher = {
	.cia_min_keysize	=	DES3_128_KEY_SIZE,
	.cia_max_keysize	=	DES3_128_KEY_SIZE,
	.cia_setkey		= 	des3_128_setkey,
	.cia_encrypt		=	des3_128_encrypt,
	.cia_decrypt		=	des3_128_decrypt } }
};

/*
 * RFC2451:
 *
 *   For DES-EDE3, there is no known need to reject weak or
 *   complementation keys.  Any weakness is obviated by the use of
 *   multiple keys.
 *
 *   However, if the first two or last two independent 64-bit keys are
 *   equal (k1 == k2 or k2 == k3), then the DES3 operation is simply the
 *   same as DES.  Implementers MUST reject keys that exhibit this
 *   property.
 *
 */
static int
des3_192_setkey(void *ctx, const u8 *key, unsigned int keylen, u32 *flags)
{
	int i, ret;
	struct crypt_z990_des3_192_ctx *dctx;
	const u8* temp_key;

	dctx = ctx;
	temp_key = key;
	if (!(memcmp(key, &key[DES_KEY_SIZE], DES_KEY_SIZE) &&
	    memcmp(&key[DES_KEY_SIZE], &key[DES_KEY_SIZE * 2],
	    					DES_KEY_SIZE))) {

		*flags |= CRYPTO_TFM_RES_BAD_KEY_SCHED;
		return -EINVAL;
	}
	for (i = 0; i < 3; i++, temp_key += DES_KEY_SIZE) {
		ret = crypto_des_check_key(temp_key, DES_KEY_SIZE, flags);
		if (ret < 0){
			return ret;
		}
	}
	memcpy(dctx->key, key, keylen);
	return 0;
}

static void
des3_192_encrypt(void *ctx, u8 *dst, const u8 *src)
{
	struct crypt_z990_des3_192_ctx *dctx;

	dctx = ctx;
	crypt_z990_km(KM_TDEA_192_ENCRYPT, dctx->key, dst, (void*)src,
			DES3_192_BLOCK_SIZE);
}

static void
des3_192_decrypt(void *ctx, u8 *dst, const u8 *src)
{
	struct crypt_z990_des3_192_ctx *dctx;

	dctx = ctx;
	crypt_z990_km(KM_TDEA_192_DECRYPT, dctx->key, dst, (void*)src,
			DES3_192_BLOCK_SIZE);
}

static struct crypto_alg des3_192_alg = {
	.cra_name		=	"des3_ede",
	.cra_flags		=	CRYPTO_ALG_TYPE_CIPHER,
	.cra_blocksize		=	DES3_192_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct crypt_z990_des3_192_ctx),
	.cra_module		=	THIS_MODULE,
	.cra_list		=	LIST_HEAD_INIT(des3_192_alg.cra_list),
	.cra_u			=	{ .cipher = {
	.cia_min_keysize	=	DES3_192_KEY_SIZE,
	.cia_max_keysize	=	DES3_192_KEY_SIZE,
	.cia_setkey		= 	des3_192_setkey,
	.cia_encrypt		=	des3_192_encrypt,
	.cia_decrypt		=	des3_192_decrypt } }
};



static int
init(void)
{
	int ret;

	if (!crypt_z990_func_available(KM_DEA_ENCRYPT) ||
	    !crypt_z990_func_available(KM_TDEA_128_ENCRYPT) ||
	    !crypt_z990_func_available(KM_TDEA_192_ENCRYPT)){
		return -ENOSYS;
	}

	ret = 0;
	ret |= (crypto_register_alg(&des_alg) == 0)? 0:1;
	ret |= (crypto_register_alg(&des3_128_alg) == 0)? 0:2;
	ret |= (crypto_register_alg(&des3_192_alg) == 0)? 0:4;
	if (ret){
		crypto_unregister_alg(&des3_192_alg);
		crypto_unregister_alg(&des3_128_alg);
		crypto_unregister_alg(&des_alg);
		return -EEXIST;
	}

	printk(KERN_INFO "crypt_z990: des_z990 loaded.\n");
	return 0;
}

static void __exit
fini(void)
{
	crypto_unregister_alg(&des3_192_alg);
	crypto_unregister_alg(&des3_128_alg);
	crypto_unregister_alg(&des_alg);
}

module_init(init);
module_exit(fini);

MODULE_ALIAS("des");
MODULE_ALIAS("des3_ede");

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("DES & Triple DES EDE Cipher Algorithms");
