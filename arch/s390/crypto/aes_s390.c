/*
 * Cryptographic API.
 *
 * s390 implementation of the AES Cipher Algorithm.
 *
 * s390 Version:
 *   Copyright (C) 2005 IBM Deutschland GmbH, IBM Corporation
 *   Author(s): Jan Glauber (jang@de.ibm.com)
 *
 * Derived from "crypto/aes.c"
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/crypto.h>
#include "crypt_s390.h"

#define AES_MIN_KEY_SIZE	16
#define AES_MAX_KEY_SIZE	32

/* data block size for all key lengths */
#define AES_BLOCK_SIZE		16

int has_aes_128 = 0;
int has_aes_192 = 0;
int has_aes_256 = 0;

struct s390_aes_ctx {
	u8 iv[AES_BLOCK_SIZE];
	u8 key[AES_MAX_KEY_SIZE];
	int key_len;
};

static int aes_set_key(void *ctx, const u8 *in_key, unsigned int key_len,
		       u32 *flags)
{
	struct s390_aes_ctx *sctx = ctx;

	switch (key_len) {
	case 16:
		if (!has_aes_128)
			goto fail;
		break;
	case 24:
		if (!has_aes_192)
			goto fail;

		break;
	case 32:
		if (!has_aes_256)
			goto fail;
		break;
	default:
		/* invalid key length */
		goto fail;
		break;
	}

	sctx->key_len = key_len;
	memcpy(sctx->key, in_key, key_len);
	return 0;
fail:
	*flags |= CRYPTO_TFM_RES_BAD_KEY_LEN;
	return -EINVAL;
}

static void aes_encrypt(void *ctx, u8 *out, const u8 *in)
{
	const struct s390_aes_ctx *sctx = ctx;

	switch (sctx->key_len) {
	case 16:
		crypt_s390_km(KM_AES_128_ENCRYPT, &sctx->key, out, in,
			      AES_BLOCK_SIZE);
		break;
	case 24:
		crypt_s390_km(KM_AES_192_ENCRYPT, &sctx->key, out, in,
			      AES_BLOCK_SIZE);
		break;
	case 32:
		crypt_s390_km(KM_AES_256_ENCRYPT, &sctx->key, out, in,
			      AES_BLOCK_SIZE);
		break;
	}
}

static void aes_decrypt(void *ctx, u8 *out, const u8 *in)
{
	const struct s390_aes_ctx *sctx = ctx;

	switch (sctx->key_len) {
	case 16:
		crypt_s390_km(KM_AES_128_DECRYPT, &sctx->key, out, in,
			      AES_BLOCK_SIZE);
		break;
	case 24:
		crypt_s390_km(KM_AES_192_DECRYPT, &sctx->key, out, in,
			      AES_BLOCK_SIZE);
		break;
	case 32:
		crypt_s390_km(KM_AES_256_DECRYPT, &sctx->key, out, in,
			      AES_BLOCK_SIZE);
		break;
	}
}

static unsigned int aes_encrypt_ecb(const struct cipher_desc *desc, u8 *out,
				    const u8 *in, unsigned int nbytes)
{
	struct s390_aes_ctx *sctx = crypto_tfm_ctx(desc->tfm);
	int ret;

	/* only use complete blocks */
	nbytes &= ~(AES_BLOCK_SIZE - 1);

	switch (sctx->key_len) {
	case 16:
		ret = crypt_s390_km(KM_AES_128_ENCRYPT, &sctx->key, out, in, nbytes);
		BUG_ON((ret < 0) || (ret != nbytes));
		break;
	case 24:
		ret = crypt_s390_km(KM_AES_192_ENCRYPT, &sctx->key, out, in, nbytes);
		BUG_ON((ret < 0) || (ret != nbytes));
		break;
	case 32:
		ret = crypt_s390_km(KM_AES_256_ENCRYPT, &sctx->key, out, in, nbytes);
		BUG_ON((ret < 0) || (ret != nbytes));
		break;
	}
	return nbytes;
}

static unsigned int aes_decrypt_ecb(const struct cipher_desc *desc, u8 *out,
				    const u8 *in, unsigned int nbytes)
{
	struct s390_aes_ctx *sctx = crypto_tfm_ctx(desc->tfm);
	int ret;

	/* only use complete blocks */
	nbytes &= ~(AES_BLOCK_SIZE - 1);

	switch (sctx->key_len) {
	case 16:
		ret = crypt_s390_km(KM_AES_128_DECRYPT, &sctx->key, out, in, nbytes);
		BUG_ON((ret < 0) || (ret != nbytes));
		break;
	case 24:
		ret = crypt_s390_km(KM_AES_192_DECRYPT, &sctx->key, out, in, nbytes);
		BUG_ON((ret < 0) || (ret != nbytes));
		break;
	case 32:
		ret = crypt_s390_km(KM_AES_256_DECRYPT, &sctx->key, out, in, nbytes);
		BUG_ON((ret < 0) || (ret != nbytes));
		break;
	}
	return nbytes;
}

static unsigned int aes_encrypt_cbc(const struct cipher_desc *desc, u8 *out,
				    const u8 *in, unsigned int nbytes)
{
	struct s390_aes_ctx *sctx = crypto_tfm_ctx(desc->tfm);
	int ret;

	/* only use complete blocks */
	nbytes &= ~(AES_BLOCK_SIZE - 1);

	memcpy(&sctx->iv, desc->info, AES_BLOCK_SIZE);
	switch (sctx->key_len) {
	case 16:
		ret = crypt_s390_kmc(KMC_AES_128_ENCRYPT, &sctx->iv, out, in, nbytes);
		BUG_ON((ret < 0) || (ret != nbytes));
		break;
	case 24:
		ret = crypt_s390_kmc(KMC_AES_192_ENCRYPT, &sctx->iv, out, in, nbytes);
		BUG_ON((ret < 0) || (ret != nbytes));
		break;
	case 32:
		ret = crypt_s390_kmc(KMC_AES_256_ENCRYPT, &sctx->iv, out, in, nbytes);
		BUG_ON((ret < 0) || (ret != nbytes));
		break;
	}
	memcpy(desc->info, &sctx->iv, AES_BLOCK_SIZE);

	return nbytes;
}

static unsigned int aes_decrypt_cbc(const struct cipher_desc *desc, u8 *out,
				    const u8 *in, unsigned int nbytes)
{
	struct s390_aes_ctx *sctx = crypto_tfm_ctx(desc->tfm);
	int ret;

	/* only use complete blocks */
	nbytes &= ~(AES_BLOCK_SIZE - 1);

	memcpy(&sctx->iv, desc->info, AES_BLOCK_SIZE);
	switch (sctx->key_len) {
	case 16:
		ret = crypt_s390_kmc(KMC_AES_128_DECRYPT, &sctx->iv, out, in, nbytes);
		BUG_ON((ret < 0) || (ret != nbytes));
		break;
	case 24:
		ret = crypt_s390_kmc(KMC_AES_192_DECRYPT, &sctx->iv, out, in, nbytes);
		BUG_ON((ret < 0) || (ret != nbytes));
		break;
	case 32:
		ret = crypt_s390_kmc(KMC_AES_256_DECRYPT, &sctx->iv, out, in, nbytes);
		BUG_ON((ret < 0) || (ret != nbytes));
		break;
	}
	return nbytes;
}


static struct crypto_alg aes_alg = {
	.cra_name		=	"aes",
	.cra_flags		=	CRYPTO_ALG_TYPE_CIPHER,
	.cra_blocksize		=	AES_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct s390_aes_ctx),
	.cra_module		=	THIS_MODULE,
	.cra_list		=	LIST_HEAD_INIT(aes_alg.cra_list),
	.cra_u			=	{
		.cipher = {
			.cia_min_keysize	=	AES_MIN_KEY_SIZE,
			.cia_max_keysize	=	AES_MAX_KEY_SIZE,
			.cia_setkey		=	aes_set_key,
			.cia_encrypt		=	aes_encrypt,
			.cia_decrypt		=	aes_decrypt,
			.cia_encrypt_ecb	=	aes_encrypt_ecb,
			.cia_decrypt_ecb	=	aes_decrypt_ecb,
			.cia_encrypt_cbc	=	aes_encrypt_cbc,
			.cia_decrypt_cbc	=	aes_decrypt_cbc,
		}
	}
};

static int __init aes_init(void)
{
	int ret;

	if (crypt_s390_func_available(KM_AES_128_ENCRYPT))
		has_aes_128 = 1;
	if (crypt_s390_func_available(KM_AES_192_ENCRYPT))
		has_aes_192 = 1;
	if (crypt_s390_func_available(KM_AES_256_ENCRYPT))
		has_aes_256 = 1;

	if (!has_aes_128 && !has_aes_192 && !has_aes_256)
		return -ENOSYS;

	ret = crypto_register_alg(&aes_alg);
	if (ret != 0)
		printk(KERN_INFO "crypt_s390: aes_s390 couldn't be loaded.\n");
	return ret;
}

static void __exit aes_fini(void)
{
	crypto_unregister_alg(&aes_alg);
}

module_init(aes_init);
module_exit(aes_fini);

MODULE_ALIAS("aes");

MODULE_DESCRIPTION("Rijndael (AES) Cipher Algorithm");
MODULE_LICENSE("GPL");

