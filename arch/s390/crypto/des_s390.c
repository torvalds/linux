/*
 * Cryptographic API.
 *
 * s390 implementation of the DES Cipher Algorithm.
 *
 * Copyright IBM Corp. 2003,2007
 * Author(s): Thomas Spatzier
 *	      Jan Glauber (jan.glauber@de.ibm.com)
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
#include <crypto/algapi.h>
#include <crypto/des.h>

#include "crypt_s390.h"

#define DES3_KEY_SIZE	(3 * DES_KEY_SIZE)

struct s390_des_ctx {
	u8 iv[DES_BLOCK_SIZE];
	u8 key[DES3_KEY_SIZE];
};

static int des_setkey(struct crypto_tfm *tfm, const u8 *key,
		      unsigned int key_len)
{
	struct s390_des_ctx *ctx = crypto_tfm_ctx(tfm);
	u32 *flags = &tfm->crt_flags;
	u32 tmp[DES_EXPKEY_WORDS];

	/* check for weak keys */
	if (!des_ekey(tmp, key) && (*flags & CRYPTO_TFM_REQ_WEAK_KEY)) {
		*flags |= CRYPTO_TFM_RES_WEAK_KEY;
		return -EINVAL;
	}

	memcpy(ctx->key, key, key_len);
	return 0;
}

static void des_encrypt(struct crypto_tfm *tfm, u8 *out, const u8 *in)
{
	struct s390_des_ctx *ctx = crypto_tfm_ctx(tfm);

	crypt_s390_km(KM_DEA_ENCRYPT, ctx->key, out, in, DES_BLOCK_SIZE);
}

static void des_decrypt(struct crypto_tfm *tfm, u8 *out, const u8 *in)
{
	struct s390_des_ctx *ctx = crypto_tfm_ctx(tfm);

	crypt_s390_km(KM_DEA_DECRYPT, ctx->key, out, in, DES_BLOCK_SIZE);
}

static struct crypto_alg des_alg = {
	.cra_name		=	"des",
	.cra_driver_name	=	"des-s390",
	.cra_priority		=	CRYPT_S390_PRIORITY,
	.cra_flags		=	CRYPTO_ALG_TYPE_CIPHER,
	.cra_blocksize		=	DES_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct s390_des_ctx),
	.cra_module		=	THIS_MODULE,
	.cra_list		=	LIST_HEAD_INIT(des_alg.cra_list),
	.cra_u			=	{
		.cipher = {
			.cia_min_keysize	=	DES_KEY_SIZE,
			.cia_max_keysize	=	DES_KEY_SIZE,
			.cia_setkey		=	des_setkey,
			.cia_encrypt		=	des_encrypt,
			.cia_decrypt		=	des_decrypt,
		}
	}
};

static int ecb_desall_crypt(struct blkcipher_desc *desc, long func,
			    u8 *key, struct blkcipher_walk *walk)
{
	int ret = blkcipher_walk_virt(desc, walk);
	unsigned int nbytes;

	while ((nbytes = walk->nbytes)) {
		/* only use complete blocks */
		unsigned int n = nbytes & ~(DES_BLOCK_SIZE - 1);
		u8 *out = walk->dst.virt.addr;
		u8 *in = walk->src.virt.addr;

		ret = crypt_s390_km(func, key, out, in, n);
		BUG_ON((ret < 0) || (ret != n));

		nbytes &= DES_BLOCK_SIZE - 1;
		ret = blkcipher_walk_done(desc, walk, nbytes);
	}

	return ret;
}

static int cbc_desall_crypt(struct blkcipher_desc *desc, long func,
			    u8 *iv, struct blkcipher_walk *walk)
{
	int ret = blkcipher_walk_virt(desc, walk);
	unsigned int nbytes = walk->nbytes;

	if (!nbytes)
		goto out;

	memcpy(iv, walk->iv, DES_BLOCK_SIZE);
	do {
		/* only use complete blocks */
		unsigned int n = nbytes & ~(DES_BLOCK_SIZE - 1);
		u8 *out = walk->dst.virt.addr;
		u8 *in = walk->src.virt.addr;

		ret = crypt_s390_kmc(func, iv, out, in, n);
		BUG_ON((ret < 0) || (ret != n));

		nbytes &= DES_BLOCK_SIZE - 1;
		ret = blkcipher_walk_done(desc, walk, nbytes);
	} while ((nbytes = walk->nbytes));
	memcpy(walk->iv, iv, DES_BLOCK_SIZE);

out:
	return ret;
}

static int ecb_des_encrypt(struct blkcipher_desc *desc,
			   struct scatterlist *dst, struct scatterlist *src,
			   unsigned int nbytes)
{
	struct s390_des_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
	struct blkcipher_walk walk;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	return ecb_desall_crypt(desc, KM_DEA_ENCRYPT, ctx->key, &walk);
}

static int ecb_des_decrypt(struct blkcipher_desc *desc,
			   struct scatterlist *dst, struct scatterlist *src,
			   unsigned int nbytes)
{
	struct s390_des_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
	struct blkcipher_walk walk;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	return ecb_desall_crypt(desc, KM_DEA_DECRYPT, ctx->key, &walk);
}

static struct crypto_alg ecb_des_alg = {
	.cra_name		=	"ecb(des)",
	.cra_driver_name	=	"ecb-des-s390",
	.cra_priority		=	CRYPT_S390_COMPOSITE_PRIORITY,
	.cra_flags		=	CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		=	DES_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct s390_des_ctx),
	.cra_type		=	&crypto_blkcipher_type,
	.cra_module		=	THIS_MODULE,
	.cra_list		=	LIST_HEAD_INIT(ecb_des_alg.cra_list),
	.cra_u			=	{
		.blkcipher = {
			.min_keysize		=	DES_KEY_SIZE,
			.max_keysize		=	DES_KEY_SIZE,
			.setkey			=	des_setkey,
			.encrypt		=	ecb_des_encrypt,
			.decrypt		=	ecb_des_decrypt,
		}
	}
};

static int cbc_des_encrypt(struct blkcipher_desc *desc,
			   struct scatterlist *dst, struct scatterlist *src,
			   unsigned int nbytes)
{
	struct s390_des_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
	struct blkcipher_walk walk;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	return cbc_desall_crypt(desc, KMC_DEA_ENCRYPT, ctx->iv, &walk);
}

static int cbc_des_decrypt(struct blkcipher_desc *desc,
			   struct scatterlist *dst, struct scatterlist *src,
			   unsigned int nbytes)
{
	struct s390_des_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
	struct blkcipher_walk walk;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	return cbc_desall_crypt(desc, KMC_DEA_DECRYPT, ctx->iv, &walk);
}

static struct crypto_alg cbc_des_alg = {
	.cra_name		=	"cbc(des)",
	.cra_driver_name	=	"cbc-des-s390",
	.cra_priority		=	CRYPT_S390_COMPOSITE_PRIORITY,
	.cra_flags		=	CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		=	DES_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct s390_des_ctx),
	.cra_type		=	&crypto_blkcipher_type,
	.cra_module		=	THIS_MODULE,
	.cra_list		=	LIST_HEAD_INIT(cbc_des_alg.cra_list),
	.cra_u			=	{
		.blkcipher = {
			.min_keysize		=	DES_KEY_SIZE,
			.max_keysize		=	DES_KEY_SIZE,
			.ivsize			=	DES_BLOCK_SIZE,
			.setkey			=	des_setkey,
			.encrypt		=	cbc_des_encrypt,
			.decrypt		=	cbc_des_decrypt,
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
static int des3_setkey(struct crypto_tfm *tfm, const u8 *key,
		       unsigned int key_len)
{
	struct s390_des_ctx *ctx = crypto_tfm_ctx(tfm);
	u32 *flags = &tfm->crt_flags;

	if (!(memcmp(key, &key[DES_KEY_SIZE], DES_KEY_SIZE) &&
	    memcmp(&key[DES_KEY_SIZE], &key[DES_KEY_SIZE * 2],
		   DES_KEY_SIZE)) &&
	    (*flags & CRYPTO_TFM_REQ_WEAK_KEY)) {
		*flags |= CRYPTO_TFM_RES_WEAK_KEY;
		return -EINVAL;
	}
	memcpy(ctx->key, key, key_len);
	return 0;
}

static void des3_encrypt(struct crypto_tfm *tfm, u8 *dst, const u8 *src)
{
	struct s390_des_ctx *ctx = crypto_tfm_ctx(tfm);

	crypt_s390_km(KM_TDEA_192_ENCRYPT, ctx->key, dst, src, DES_BLOCK_SIZE);
}

static void des3_decrypt(struct crypto_tfm *tfm, u8 *dst, const u8 *src)
{
	struct s390_des_ctx *ctx = crypto_tfm_ctx(tfm);

	crypt_s390_km(KM_TDEA_192_DECRYPT, ctx->key, dst, src, DES_BLOCK_SIZE);
}

static struct crypto_alg des3_alg = {
	.cra_name		=	"des3_ede",
	.cra_driver_name	=	"des3_ede-s390",
	.cra_priority		=	CRYPT_S390_PRIORITY,
	.cra_flags		=	CRYPTO_ALG_TYPE_CIPHER,
	.cra_blocksize		=	DES_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct s390_des_ctx),
	.cra_module		=	THIS_MODULE,
	.cra_list		=	LIST_HEAD_INIT(des3_alg.cra_list),
	.cra_u			=	{
		.cipher = {
			.cia_min_keysize	=	DES3_KEY_SIZE,
			.cia_max_keysize	=	DES3_KEY_SIZE,
			.cia_setkey		=	des3_setkey,
			.cia_encrypt		=	des3_encrypt,
			.cia_decrypt		=	des3_decrypt,
		}
	}
};

static int ecb_des3_encrypt(struct blkcipher_desc *desc,
			    struct scatterlist *dst, struct scatterlist *src,
			    unsigned int nbytes)
{
	struct s390_des_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
	struct blkcipher_walk walk;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	return ecb_desall_crypt(desc, KM_TDEA_192_ENCRYPT, ctx->key, &walk);
}

static int ecb_des3_decrypt(struct blkcipher_desc *desc,
			    struct scatterlist *dst, struct scatterlist *src,
			    unsigned int nbytes)
{
	struct s390_des_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
	struct blkcipher_walk walk;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	return ecb_desall_crypt(desc, KM_TDEA_192_DECRYPT, ctx->key, &walk);
}

static struct crypto_alg ecb_des3_alg = {
	.cra_name		=	"ecb(des3_ede)",
	.cra_driver_name	=	"ecb-des3_ede-s390",
	.cra_priority		=	CRYPT_S390_COMPOSITE_PRIORITY,
	.cra_flags		=	CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		=	DES_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct s390_des_ctx),
	.cra_type		=	&crypto_blkcipher_type,
	.cra_module		=	THIS_MODULE,
	.cra_list		=	LIST_HEAD_INIT(
						ecb_des3_alg.cra_list),
	.cra_u			=	{
		.blkcipher = {
			.min_keysize		=	DES3_KEY_SIZE,
			.max_keysize		=	DES3_KEY_SIZE,
			.setkey			=	des3_setkey,
			.encrypt		=	ecb_des3_encrypt,
			.decrypt		=	ecb_des3_decrypt,
		}
	}
};

static int cbc_des3_encrypt(struct blkcipher_desc *desc,
			    struct scatterlist *dst, struct scatterlist *src,
			    unsigned int nbytes)
{
	struct s390_des_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
	struct blkcipher_walk walk;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	return cbc_desall_crypt(desc, KMC_TDEA_192_ENCRYPT, ctx->iv, &walk);
}

static int cbc_des3_decrypt(struct blkcipher_desc *desc,
			    struct scatterlist *dst, struct scatterlist *src,
			    unsigned int nbytes)
{
	struct s390_des_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
	struct blkcipher_walk walk;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	return cbc_desall_crypt(desc, KMC_TDEA_192_DECRYPT, ctx->iv, &walk);
}

static struct crypto_alg cbc_des3_alg = {
	.cra_name		=	"cbc(des3_ede)",
	.cra_driver_name	=	"cbc-des3_ede-s390",
	.cra_priority		=	CRYPT_S390_COMPOSITE_PRIORITY,
	.cra_flags		=	CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		=	DES_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct s390_des_ctx),
	.cra_type		=	&crypto_blkcipher_type,
	.cra_module		=	THIS_MODULE,
	.cra_list		=	LIST_HEAD_INIT(
						cbc_des3_alg.cra_list),
	.cra_u			=	{
		.blkcipher = {
			.min_keysize		=	DES3_KEY_SIZE,
			.max_keysize		=	DES3_KEY_SIZE,
			.ivsize			=	DES_BLOCK_SIZE,
			.setkey			=	des3_setkey,
			.encrypt		=	cbc_des3_encrypt,
			.decrypt		=	cbc_des3_decrypt,
		}
	}
};

static int __init des_s390_init(void)
{
	int ret;

	if (!crypt_s390_func_available(KM_DEA_ENCRYPT, CRYPT_S390_MSA) ||
	    !crypt_s390_func_available(KM_TDEA_192_ENCRYPT, CRYPT_S390_MSA))
		return -EOPNOTSUPP;

	ret = crypto_register_alg(&des_alg);
	if (ret)
		goto des_err;
	ret = crypto_register_alg(&ecb_des_alg);
	if (ret)
		goto ecb_des_err;
	ret = crypto_register_alg(&cbc_des_alg);
	if (ret)
		goto cbc_des_err;
	ret = crypto_register_alg(&des3_alg);
	if (ret)
		goto des3_err;
	ret = crypto_register_alg(&ecb_des3_alg);
	if (ret)
		goto ecb_des3_err;
	ret = crypto_register_alg(&cbc_des3_alg);
	if (ret)
		goto cbc_des3_err;
out:
	return ret;

cbc_des3_err:
	crypto_unregister_alg(&ecb_des3_alg);
ecb_des3_err:
	crypto_unregister_alg(&des3_alg);
des3_err:
	crypto_unregister_alg(&cbc_des_alg);
cbc_des_err:
	crypto_unregister_alg(&ecb_des_alg);
ecb_des_err:
	crypto_unregister_alg(&des_alg);
des_err:
	goto out;
}

static void __exit des_s390_exit(void)
{
	crypto_unregister_alg(&cbc_des3_alg);
	crypto_unregister_alg(&ecb_des3_alg);
	crypto_unregister_alg(&des3_alg);
	crypto_unregister_alg(&cbc_des_alg);
	crypto_unregister_alg(&ecb_des_alg);
	crypto_unregister_alg(&des_alg);
}

module_init(des_s390_init);
module_exit(des_s390_exit);

MODULE_ALIAS("des");
MODULE_ALIAS("des3_ede");

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("DES & Triple DES EDE Cipher Algorithms");
