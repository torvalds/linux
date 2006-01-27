/*
 * Cryptographic API.
 *
 * s390 implementation of the DES Cipher Algorithm.
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
#include <linux/crypto.h>

#include "crypt_s390.h"
#include "crypto_des.h"

#define DES_BLOCK_SIZE 8
#define DES_KEY_SIZE 8

#define DES3_128_KEY_SIZE	(2 * DES_KEY_SIZE)
#define DES3_128_BLOCK_SIZE	DES_BLOCK_SIZE

#define DES3_192_KEY_SIZE	(3 * DES_KEY_SIZE)
#define DES3_192_BLOCK_SIZE	DES_BLOCK_SIZE

struct crypt_s390_des_ctx {
	u8 iv[DES_BLOCK_SIZE];
	u8 key[DES_KEY_SIZE];
};

struct crypt_s390_des3_128_ctx {
	u8 iv[DES_BLOCK_SIZE];
	u8 key[DES3_128_KEY_SIZE];
};

struct crypt_s390_des3_192_ctx {
	u8 iv[DES_BLOCK_SIZE];
	u8 key[DES3_192_KEY_SIZE];
};

static int des_setkey(void *ctx, const u8 *key, unsigned int keylen,
		      u32 *flags)
{
	struct crypt_s390_des_ctx *dctx = ctx;
	int ret;

	/* test if key is valid (not a weak key) */
	ret = crypto_des_check_key(key, keylen, flags);
	if (ret == 0)
		memcpy(dctx->key, key, keylen);
	return ret;
}

static void des_encrypt(void *ctx, u8 *out, const u8 *in)
{
	struct crypt_s390_des_ctx *dctx = ctx;

	crypt_s390_km(KM_DEA_ENCRYPT, dctx->key, out, in, DES_BLOCK_SIZE);
}

static void des_decrypt(void *ctx, u8 *out, const u8 *in)
{
	struct crypt_s390_des_ctx *dctx = ctx;

	crypt_s390_km(KM_DEA_DECRYPT, dctx->key, out, in, DES_BLOCK_SIZE);
}

static unsigned int des_encrypt_ecb(const struct cipher_desc *desc, u8 *out,
				    const u8 *in, unsigned int nbytes)
{
	struct crypt_s390_des_ctx *sctx = crypto_tfm_ctx(desc->tfm);
	int ret;

	/* only use complete blocks */
	nbytes &= ~(DES_BLOCK_SIZE - 1);
	ret = crypt_s390_km(KM_DEA_ENCRYPT, sctx->key, out, in, nbytes);
	BUG_ON((ret < 0) || (ret != nbytes));

	return nbytes;
}

static unsigned int des_decrypt_ecb(const struct cipher_desc *desc, u8 *out,
				    const u8 *in, unsigned int nbytes)
{
	struct crypt_s390_des_ctx *sctx = crypto_tfm_ctx(desc->tfm);
	int ret;

	/* only use complete blocks */
	nbytes &= ~(DES_BLOCK_SIZE - 1);
	ret = crypt_s390_km(KM_DEA_DECRYPT, sctx->key, out, in, nbytes);
	BUG_ON((ret < 0) || (ret != nbytes));

	return nbytes;
}

static unsigned int des_encrypt_cbc(const struct cipher_desc *desc, u8 *out,
				    const u8 *in, unsigned int nbytes)
{
	struct crypt_s390_des_ctx *sctx = crypto_tfm_ctx(desc->tfm);
	int ret;

	/* only use complete blocks */
	nbytes &= ~(DES_BLOCK_SIZE - 1);

	memcpy(sctx->iv, desc->info, DES_BLOCK_SIZE);
	ret = crypt_s390_kmc(KMC_DEA_ENCRYPT, &sctx->iv, out, in, nbytes);
	BUG_ON((ret < 0) || (ret != nbytes));

	memcpy(desc->info, sctx->iv, DES_BLOCK_SIZE);
	return nbytes;
}

static unsigned int des_decrypt_cbc(const struct cipher_desc *desc, u8 *out,
				    const u8 *in, unsigned int nbytes)
{
	struct crypt_s390_des_ctx *sctx = crypto_tfm_ctx(desc->tfm);
	int ret;

	/* only use complete blocks */
	nbytes &= ~(DES_BLOCK_SIZE - 1);

	memcpy(&sctx->iv, desc->info, DES_BLOCK_SIZE);
	ret = crypt_s390_kmc(KMC_DEA_DECRYPT, &sctx->iv, out, in, nbytes);
	BUG_ON((ret < 0) || (ret != nbytes));

	return nbytes;
}

static struct crypto_alg des_alg = {
	.cra_name		=	"des",
	.cra_flags		=	CRYPTO_ALG_TYPE_CIPHER,
	.cra_blocksize		=	DES_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct crypt_s390_des_ctx),
	.cra_module		=	THIS_MODULE,
	.cra_list		=	LIST_HEAD_INIT(des_alg.cra_list),
	.cra_u			=	{
		.cipher = {
			.cia_min_keysize	=	DES_KEY_SIZE,
			.cia_max_keysize	=	DES_KEY_SIZE,
			.cia_setkey		=	des_setkey,
			.cia_encrypt		=	des_encrypt,
			.cia_decrypt		=	des_decrypt,
			.cia_encrypt_ecb	=	des_encrypt_ecb,
			.cia_decrypt_ecb	=	des_decrypt_ecb,
			.cia_encrypt_cbc	=	des_encrypt_cbc,
			.cia_decrypt_cbc	=	des_decrypt_cbc,
		}
	}
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
static int des3_128_setkey(void *ctx, const u8 *key, unsigned int keylen,
			   u32 *flags)
{
	int i, ret;
	struct crypt_s390_des3_128_ctx *dctx = ctx;
	const u8* temp_key = key;

	if (!(memcmp(key, &key[DES_KEY_SIZE], DES_KEY_SIZE))) {
		*flags |= CRYPTO_TFM_RES_BAD_KEY_SCHED;
		return -EINVAL;
	}
	for (i = 0; i < 2; i++, temp_key += DES_KEY_SIZE) {
		ret = crypto_des_check_key(temp_key, DES_KEY_SIZE, flags);
		if (ret < 0)
			return ret;
	}
	memcpy(dctx->key, key, keylen);
	return 0;
}

static void des3_128_encrypt(void *ctx, u8 *dst, const u8 *src)
{
	struct crypt_s390_des3_128_ctx *dctx = ctx;

	crypt_s390_km(KM_TDEA_128_ENCRYPT, dctx->key, dst, (void*)src,
		      DES3_128_BLOCK_SIZE);
}

static void des3_128_decrypt(void *ctx, u8 *dst, const u8 *src)
{
	struct crypt_s390_des3_128_ctx *dctx = ctx;

	crypt_s390_km(KM_TDEA_128_DECRYPT, dctx->key, dst, (void*)src,
		      DES3_128_BLOCK_SIZE);
}

static unsigned int des3_128_encrypt_ecb(const struct cipher_desc *desc,
					 u8 *out, const u8 *in,
					 unsigned int nbytes)
{
	struct crypt_s390_des3_128_ctx *sctx = crypto_tfm_ctx(desc->tfm);
	int ret;

	/* only use complete blocks */
	nbytes &= ~(DES3_128_BLOCK_SIZE - 1);
	ret = crypt_s390_km(KM_TDEA_128_ENCRYPT, sctx->key, out, in, nbytes);
	BUG_ON((ret < 0) || (ret != nbytes));

	return nbytes;
}

static unsigned int des3_128_decrypt_ecb(const struct cipher_desc *desc,
					 u8 *out, const u8 *in,
					 unsigned int nbytes)
{
	struct crypt_s390_des3_128_ctx *sctx = crypto_tfm_ctx(desc->tfm);
	int ret;

	/* only use complete blocks */
	nbytes &= ~(DES3_128_BLOCK_SIZE - 1);
	ret = crypt_s390_km(KM_TDEA_128_DECRYPT, sctx->key, out, in, nbytes);
	BUG_ON((ret < 0) || (ret != nbytes));

	return nbytes;
}

static unsigned int des3_128_encrypt_cbc(const struct cipher_desc *desc,
					 u8 *out, const u8 *in,
					 unsigned int nbytes)
{
	struct crypt_s390_des3_128_ctx *sctx = crypto_tfm_ctx(desc->tfm);
	int ret;

	/* only use complete blocks */
	nbytes &= ~(DES3_128_BLOCK_SIZE - 1);

	memcpy(sctx->iv, desc->info, DES3_128_BLOCK_SIZE);
	ret = crypt_s390_kmc(KMC_TDEA_128_ENCRYPT, &sctx->iv, out, in, nbytes);
	BUG_ON((ret < 0) || (ret != nbytes));

	memcpy(desc->info, sctx->iv, DES3_128_BLOCK_SIZE);
	return nbytes;
}

static unsigned int des3_128_decrypt_cbc(const struct cipher_desc *desc,
					 u8 *out, const u8 *in,
					 unsigned int nbytes)
{
	struct crypt_s390_des3_128_ctx *sctx = crypto_tfm_ctx(desc->tfm);
	int ret;

	/* only use complete blocks */
	nbytes &= ~(DES3_128_BLOCK_SIZE - 1);

	memcpy(&sctx->iv, desc->info, DES3_128_BLOCK_SIZE);
	ret = crypt_s390_kmc(KMC_TDEA_128_DECRYPT, &sctx->iv, out, in, nbytes);
	BUG_ON((ret < 0) || (ret != nbytes));

	return nbytes;
}

static struct crypto_alg des3_128_alg = {
	.cra_name		=	"des3_ede128",
	.cra_flags		=	CRYPTO_ALG_TYPE_CIPHER,
	.cra_blocksize		=	DES3_128_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct crypt_s390_des3_128_ctx),
	.cra_module		=	THIS_MODULE,
	.cra_list		=	LIST_HEAD_INIT(des3_128_alg.cra_list),
	.cra_u			=	{
		.cipher = {
			.cia_min_keysize	=	DES3_128_KEY_SIZE,
			.cia_max_keysize	=	DES3_128_KEY_SIZE,
			.cia_setkey		=	des3_128_setkey,
			.cia_encrypt		=	des3_128_encrypt,
			.cia_decrypt		=	des3_128_decrypt,
			.cia_encrypt_ecb	=	des3_128_encrypt_ecb,
			.cia_decrypt_ecb	=	des3_128_decrypt_ecb,
			.cia_encrypt_cbc	=	des3_128_encrypt_cbc,
			.cia_decrypt_cbc	=	des3_128_decrypt_cbc,
		}
	}
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
static int des3_192_setkey(void *ctx, const u8 *key, unsigned int keylen,
			   u32 *flags)
{
	int i, ret;
	struct crypt_s390_des3_192_ctx *dctx = ctx;
	const u8* temp_key = key;

	if (!(memcmp(key, &key[DES_KEY_SIZE], DES_KEY_SIZE) &&
	    memcmp(&key[DES_KEY_SIZE], &key[DES_KEY_SIZE * 2],
		   DES_KEY_SIZE))) {

		*flags |= CRYPTO_TFM_RES_BAD_KEY_SCHED;
		return -EINVAL;
	}
	for (i = 0; i < 3; i++, temp_key += DES_KEY_SIZE) {
		ret = crypto_des_check_key(temp_key, DES_KEY_SIZE, flags);
		if (ret < 0)
			return ret;
	}
	memcpy(dctx->key, key, keylen);
	return 0;
}

static void des3_192_encrypt(void *ctx, u8 *dst, const u8 *src)
{
	struct crypt_s390_des3_192_ctx *dctx = ctx;

	crypt_s390_km(KM_TDEA_192_ENCRYPT, dctx->key, dst, (void*)src,
		      DES3_192_BLOCK_SIZE);
}

static void des3_192_decrypt(void *ctx, u8 *dst, const u8 *src)
{
	struct crypt_s390_des3_192_ctx *dctx = ctx;

	crypt_s390_km(KM_TDEA_192_DECRYPT, dctx->key, dst, (void*)src,
		      DES3_192_BLOCK_SIZE);
}

static unsigned int des3_192_encrypt_ecb(const struct cipher_desc *desc,
					 u8 *out, const u8 *in,
					 unsigned int nbytes)
{
	struct crypt_s390_des3_192_ctx *sctx = crypto_tfm_ctx(desc->tfm);
	int ret;

	/* only use complete blocks */
	nbytes &= ~(DES3_192_BLOCK_SIZE - 1);
	ret = crypt_s390_km(KM_TDEA_192_ENCRYPT, sctx->key, out, in, nbytes);
	BUG_ON((ret < 0) || (ret != nbytes));

	return nbytes;
}

static unsigned int des3_192_decrypt_ecb(const struct cipher_desc *desc,
					 u8 *out, const u8 *in,
					 unsigned int nbytes)
{
	struct crypt_s390_des3_192_ctx *sctx = crypto_tfm_ctx(desc->tfm);
	int ret;

	/* only use complete blocks */
	nbytes &= ~(DES3_192_BLOCK_SIZE - 1);
	ret = crypt_s390_km(KM_TDEA_192_DECRYPT, sctx->key, out, in, nbytes);
	BUG_ON((ret < 0) || (ret != nbytes));

	return nbytes;
}

static unsigned int des3_192_encrypt_cbc(const struct cipher_desc *desc,
					 u8 *out, const u8 *in,
					 unsigned int nbytes)
{
	struct crypt_s390_des3_192_ctx *sctx = crypto_tfm_ctx(desc->tfm);
	int ret;

	/* only use complete blocks */
	nbytes &= ~(DES3_192_BLOCK_SIZE - 1);

	memcpy(sctx->iv, desc->info, DES3_192_BLOCK_SIZE);
	ret = crypt_s390_kmc(KMC_TDEA_192_ENCRYPT, &sctx->iv, out, in, nbytes);
	BUG_ON((ret < 0) || (ret != nbytes));

	memcpy(desc->info, sctx->iv, DES3_192_BLOCK_SIZE);
	return nbytes;
}

static unsigned int des3_192_decrypt_cbc(const struct cipher_desc *desc,
					 u8 *out, const u8 *in,
					 unsigned int nbytes)
{
	struct crypt_s390_des3_192_ctx *sctx = crypto_tfm_ctx(desc->tfm);
	int ret;

	/* only use complete blocks */
	nbytes &= ~(DES3_192_BLOCK_SIZE - 1);

	memcpy(&sctx->iv, desc->info, DES3_192_BLOCK_SIZE);
	ret = crypt_s390_kmc(KMC_TDEA_192_DECRYPT, &sctx->iv, out, in, nbytes);
	BUG_ON((ret < 0) || (ret != nbytes));

	return nbytes;
}

static struct crypto_alg des3_192_alg = {
	.cra_name		=	"des3_ede",
	.cra_flags		=	CRYPTO_ALG_TYPE_CIPHER,
	.cra_blocksize		=	DES3_192_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct crypt_s390_des3_192_ctx),
	.cra_module		=	THIS_MODULE,
	.cra_list		=	LIST_HEAD_INIT(des3_192_alg.cra_list),
	.cra_u			=	{
		.cipher = {
			.cia_min_keysize	=	DES3_192_KEY_SIZE,
			.cia_max_keysize	=	DES3_192_KEY_SIZE,
			.cia_setkey		=	des3_192_setkey,
			.cia_encrypt		=	des3_192_encrypt,
			.cia_decrypt		=	des3_192_decrypt,
			.cia_encrypt_ecb	=	des3_192_encrypt_ecb,
			.cia_decrypt_ecb	=	des3_192_decrypt_ecb,
			.cia_encrypt_cbc	=	des3_192_encrypt_cbc,
			.cia_decrypt_cbc	=	des3_192_decrypt_cbc,
		}
	}
};

static int init(void)
{
	int ret = 0;

	if (!crypt_s390_func_available(KM_DEA_ENCRYPT) ||
	    !crypt_s390_func_available(KM_TDEA_128_ENCRYPT) ||
	    !crypt_s390_func_available(KM_TDEA_192_ENCRYPT))
		return -ENOSYS;

	ret |= (crypto_register_alg(&des_alg) == 0) ? 0:1;
	ret |= (crypto_register_alg(&des3_128_alg) == 0) ? 0:2;
	ret |= (crypto_register_alg(&des3_192_alg) == 0) ? 0:4;
	if (ret) {
		crypto_unregister_alg(&des3_192_alg);
		crypto_unregister_alg(&des3_128_alg);
		crypto_unregister_alg(&des_alg);
		return -EEXIST;
	}
	return 0;
}

static void __exit fini(void)
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
