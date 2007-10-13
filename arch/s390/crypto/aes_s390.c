/*
 * Cryptographic API.
 *
 * s390 implementation of the AES Cipher Algorithm.
 *
 * s390 Version:
 *   Copyright IBM Corp. 2005,2007
 *   Author(s): Jan Glauber (jang@de.ibm.com)
 *
 * Derived from "crypto/aes_generic.c"
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */

#include <crypto/algapi.h>
#include <linux/module.h>
#include <linux/init.h>
#include "crypt_s390.h"

#define AES_MIN_KEY_SIZE	16
#define AES_MAX_KEY_SIZE	32

/* data block size for all key lengths */
#define AES_BLOCK_SIZE		16

#define AES_KEYLEN_128		1
#define AES_KEYLEN_192		2
#define AES_KEYLEN_256		4

static char keylen_flag = 0;

struct s390_aes_ctx {
	u8 iv[AES_BLOCK_SIZE];
	u8 key[AES_MAX_KEY_SIZE];
	long enc;
	long dec;
	int key_len;
};

static int aes_set_key(struct crypto_tfm *tfm, const u8 *in_key,
		       unsigned int key_len)
{
	struct s390_aes_ctx *sctx = crypto_tfm_ctx(tfm);
	u32 *flags = &tfm->crt_flags;

	switch (key_len) {
	case 16:
		if (!(keylen_flag & AES_KEYLEN_128))
			goto fail;
		break;
	case 24:
		if (!(keylen_flag & AES_KEYLEN_192))
			goto fail;

		break;
	case 32:
		if (!(keylen_flag & AES_KEYLEN_256))
			goto fail;
		break;
	default:
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

static void aes_encrypt(struct crypto_tfm *tfm, u8 *out, const u8 *in)
{
	const struct s390_aes_ctx *sctx = crypto_tfm_ctx(tfm);

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

static void aes_decrypt(struct crypto_tfm *tfm, u8 *out, const u8 *in)
{
	const struct s390_aes_ctx *sctx = crypto_tfm_ctx(tfm);

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


static struct crypto_alg aes_alg = {
	.cra_name		=	"aes",
	.cra_driver_name	=	"aes-s390",
	.cra_priority		=	CRYPT_S390_PRIORITY,
	.cra_flags		=	CRYPTO_ALG_TYPE_CIPHER |
					CRYPTO_ALG_NEED_FALLBACK,
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
		}
	}
};

static int ecb_aes_set_key(struct crypto_tfm *tfm, const u8 *in_key,
			   unsigned int key_len)
{
	struct s390_aes_ctx *sctx = crypto_tfm_ctx(tfm);

	switch (key_len) {
	case 16:
		sctx->enc = KM_AES_128_ENCRYPT;
		sctx->dec = KM_AES_128_DECRYPT;
		break;
	case 24:
		sctx->enc = KM_AES_192_ENCRYPT;
		sctx->dec = KM_AES_192_DECRYPT;
		break;
	case 32:
		sctx->enc = KM_AES_256_ENCRYPT;
		sctx->dec = KM_AES_256_DECRYPT;
		break;
	}

	return aes_set_key(tfm, in_key, key_len);
}

static int ecb_aes_crypt(struct blkcipher_desc *desc, long func, void *param,
			 struct blkcipher_walk *walk)
{
	int ret = blkcipher_walk_virt(desc, walk);
	unsigned int nbytes;

	while ((nbytes = walk->nbytes)) {
		/* only use complete blocks */
		unsigned int n = nbytes & ~(AES_BLOCK_SIZE - 1);
		u8 *out = walk->dst.virt.addr;
		u8 *in = walk->src.virt.addr;

		ret = crypt_s390_km(func, param, out, in, n);
		BUG_ON((ret < 0) || (ret != n));

		nbytes &= AES_BLOCK_SIZE - 1;
		ret = blkcipher_walk_done(desc, walk, nbytes);
	}

	return ret;
}

static int ecb_aes_encrypt(struct blkcipher_desc *desc,
			   struct scatterlist *dst, struct scatterlist *src,
			   unsigned int nbytes)
{
	struct s390_aes_ctx *sctx = crypto_blkcipher_ctx(desc->tfm);
	struct blkcipher_walk walk;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	return ecb_aes_crypt(desc, sctx->enc, sctx->key, &walk);
}

static int ecb_aes_decrypt(struct blkcipher_desc *desc,
			   struct scatterlist *dst, struct scatterlist *src,
			   unsigned int nbytes)
{
	struct s390_aes_ctx *sctx = crypto_blkcipher_ctx(desc->tfm);
	struct blkcipher_walk walk;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	return ecb_aes_crypt(desc, sctx->dec, sctx->key, &walk);
}

static struct crypto_alg ecb_aes_alg = {
	.cra_name		=	"ecb(aes)",
	.cra_driver_name	=	"ecb-aes-s390",
	.cra_priority		=	CRYPT_S390_COMPOSITE_PRIORITY,
	.cra_flags		=	CRYPTO_ALG_TYPE_BLKCIPHER |
					CRYPTO_ALG_NEED_FALLBACK,
	.cra_blocksize		=	AES_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct s390_aes_ctx),
	.cra_type		=	&crypto_blkcipher_type,
	.cra_module		=	THIS_MODULE,
	.cra_list		=	LIST_HEAD_INIT(ecb_aes_alg.cra_list),
	.cra_u			=	{
		.blkcipher = {
			.min_keysize		=	AES_MIN_KEY_SIZE,
			.max_keysize		=	AES_MAX_KEY_SIZE,
			.setkey			=	ecb_aes_set_key,
			.encrypt		=	ecb_aes_encrypt,
			.decrypt		=	ecb_aes_decrypt,
		}
	}
};

static int cbc_aes_set_key(struct crypto_tfm *tfm, const u8 *in_key,
			   unsigned int key_len)
{
	struct s390_aes_ctx *sctx = crypto_tfm_ctx(tfm);

	switch (key_len) {
	case 16:
		sctx->enc = KMC_AES_128_ENCRYPT;
		sctx->dec = KMC_AES_128_DECRYPT;
		break;
	case 24:
		sctx->enc = KMC_AES_192_ENCRYPT;
		sctx->dec = KMC_AES_192_DECRYPT;
		break;
	case 32:
		sctx->enc = KMC_AES_256_ENCRYPT;
		sctx->dec = KMC_AES_256_DECRYPT;
		break;
	}

	return aes_set_key(tfm, in_key, key_len);
}

static int cbc_aes_crypt(struct blkcipher_desc *desc, long func, void *param,
			 struct blkcipher_walk *walk)
{
	int ret = blkcipher_walk_virt(desc, walk);
	unsigned int nbytes = walk->nbytes;

	if (!nbytes)
		goto out;

	memcpy(param, walk->iv, AES_BLOCK_SIZE);
	do {
		/* only use complete blocks */
		unsigned int n = nbytes & ~(AES_BLOCK_SIZE - 1);
		u8 *out = walk->dst.virt.addr;
		u8 *in = walk->src.virt.addr;

		ret = crypt_s390_kmc(func, param, out, in, n);
		BUG_ON((ret < 0) || (ret != n));

		nbytes &= AES_BLOCK_SIZE - 1;
		ret = blkcipher_walk_done(desc, walk, nbytes);
	} while ((nbytes = walk->nbytes));
	memcpy(walk->iv, param, AES_BLOCK_SIZE);

out:
	return ret;
}

static int cbc_aes_encrypt(struct blkcipher_desc *desc,
			   struct scatterlist *dst, struct scatterlist *src,
			   unsigned int nbytes)
{
	struct s390_aes_ctx *sctx = crypto_blkcipher_ctx(desc->tfm);
	struct blkcipher_walk walk;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	return cbc_aes_crypt(desc, sctx->enc, sctx->iv, &walk);
}

static int cbc_aes_decrypt(struct blkcipher_desc *desc,
			   struct scatterlist *dst, struct scatterlist *src,
			   unsigned int nbytes)
{
	struct s390_aes_ctx *sctx = crypto_blkcipher_ctx(desc->tfm);
	struct blkcipher_walk walk;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	return cbc_aes_crypt(desc, sctx->dec, sctx->iv, &walk);
}

static struct crypto_alg cbc_aes_alg = {
	.cra_name		=	"cbc(aes)",
	.cra_driver_name	=	"cbc-aes-s390",
	.cra_priority		=	CRYPT_S390_COMPOSITE_PRIORITY,
	.cra_flags		=	CRYPTO_ALG_TYPE_BLKCIPHER |
					CRYPTO_ALG_NEED_FALLBACK,
	.cra_blocksize		=	AES_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct s390_aes_ctx),
	.cra_type		=	&crypto_blkcipher_type,
	.cra_module		=	THIS_MODULE,
	.cra_list		=	LIST_HEAD_INIT(cbc_aes_alg.cra_list),
	.cra_u			=	{
		.blkcipher = {
			.min_keysize		=	AES_MIN_KEY_SIZE,
			.max_keysize		=	AES_MAX_KEY_SIZE,
			.ivsize			=	AES_BLOCK_SIZE,
			.setkey			=	cbc_aes_set_key,
			.encrypt		=	cbc_aes_encrypt,
			.decrypt		=	cbc_aes_decrypt,
		}
	}
};

static int __init aes_init(void)
{
	int ret;

	if (crypt_s390_func_available(KM_AES_128_ENCRYPT))
		keylen_flag |= AES_KEYLEN_128;
	if (crypt_s390_func_available(KM_AES_192_ENCRYPT))
		keylen_flag |= AES_KEYLEN_192;
	if (crypt_s390_func_available(KM_AES_256_ENCRYPT))
		keylen_flag |= AES_KEYLEN_256;

	if (!keylen_flag)
		return -EOPNOTSUPP;

	/* z9 109 and z9 BC/EC only support 128 bit key length */
	if (keylen_flag == AES_KEYLEN_128) {
		aes_alg.cra_u.cipher.cia_max_keysize = AES_MIN_KEY_SIZE;
		ecb_aes_alg.cra_u.blkcipher.max_keysize = AES_MIN_KEY_SIZE;
		cbc_aes_alg.cra_u.blkcipher.max_keysize = AES_MIN_KEY_SIZE;
		printk(KERN_INFO
		       "aes_s390: hardware acceleration only available for"
		       "128 bit keys\n");
	}

	ret = crypto_register_alg(&aes_alg);
	if (ret)
		goto aes_err;

	ret = crypto_register_alg(&ecb_aes_alg);
	if (ret)
		goto ecb_aes_err;

	ret = crypto_register_alg(&cbc_aes_alg);
	if (ret)
		goto cbc_aes_err;

out:
	return ret;

cbc_aes_err:
	crypto_unregister_alg(&ecb_aes_alg);
ecb_aes_err:
	crypto_unregister_alg(&aes_alg);
aes_err:
	goto out;
}

static void __exit aes_fini(void)
{
	crypto_unregister_alg(&cbc_aes_alg);
	crypto_unregister_alg(&ecb_aes_alg);
	crypto_unregister_alg(&aes_alg);
}

module_init(aes_init);
module_exit(aes_fini);

MODULE_ALIAS("aes");

MODULE_DESCRIPTION("Rijndael (AES) Cipher Algorithm");
MODULE_LICENSE("GPL");

