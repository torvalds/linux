// SPDX-License-Identifier: GPL-2.0+
/*
 * Cryptographic API.
 *
 * s390 implementation of the DES Cipher Algorithm.
 *
 * Copyright IBM Corp. 2003, 2011
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
#include <linux/cpufeature.h>
#include <linux/crypto.h>
#include <linux/fips.h>
#include <crypto/algapi.h>
#include <crypto/des.h>
#include <asm/cpacf.h>

#define DES3_KEY_SIZE	(3 * DES_KEY_SIZE)

static u8 *ctrblk;
static DEFINE_SPINLOCK(ctrblk_lock);

static cpacf_mask_t km_functions, kmc_functions, kmctr_functions;

struct s390_des_ctx {
	u8 iv[DES_BLOCK_SIZE];
	u8 key[DES3_KEY_SIZE];
};

static int des_setkey(struct crypto_tfm *tfm, const u8 *key,
		      unsigned int key_len)
{
	struct s390_des_ctx *ctx = crypto_tfm_ctx(tfm);
	u32 tmp[DES_EXPKEY_WORDS];

	/* check for weak keys */
	if (!des_ekey(tmp, key) &&
	    (tfm->crt_flags & CRYPTO_TFM_REQ_WEAK_KEY)) {
		tfm->crt_flags |= CRYPTO_TFM_RES_WEAK_KEY;
		return -EINVAL;
	}

	memcpy(ctx->key, key, key_len);
	return 0;
}

static void des_encrypt(struct crypto_tfm *tfm, u8 *out, const u8 *in)
{
	struct s390_des_ctx *ctx = crypto_tfm_ctx(tfm);

	cpacf_km(CPACF_KM_DEA, ctx->key, out, in, DES_BLOCK_SIZE);
}

static void des_decrypt(struct crypto_tfm *tfm, u8 *out, const u8 *in)
{
	struct s390_des_ctx *ctx = crypto_tfm_ctx(tfm);

	cpacf_km(CPACF_KM_DEA | CPACF_DECRYPT,
		 ctx->key, out, in, DES_BLOCK_SIZE);
}

static struct crypto_alg des_alg = {
	.cra_name		=	"des",
	.cra_driver_name	=	"des-s390",
	.cra_priority		=	300,
	.cra_flags		=	CRYPTO_ALG_TYPE_CIPHER,
	.cra_blocksize		=	DES_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct s390_des_ctx),
	.cra_module		=	THIS_MODULE,
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

static int ecb_desall_crypt(struct blkcipher_desc *desc, unsigned long fc,
			    struct blkcipher_walk *walk)
{
	struct s390_des_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
	unsigned int nbytes, n;
	int ret;

	ret = blkcipher_walk_virt(desc, walk);
	while ((nbytes = walk->nbytes) >= DES_BLOCK_SIZE) {
		/* only use complete blocks */
		n = nbytes & ~(DES_BLOCK_SIZE - 1);
		cpacf_km(fc, ctx->key, walk->dst.virt.addr,
			 walk->src.virt.addr, n);
		ret = blkcipher_walk_done(desc, walk, nbytes - n);
	}
	return ret;
}

static int cbc_desall_crypt(struct blkcipher_desc *desc, unsigned long fc,
			    struct blkcipher_walk *walk)
{
	struct s390_des_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
	unsigned int nbytes, n;
	int ret;
	struct {
		u8 iv[DES_BLOCK_SIZE];
		u8 key[DES3_KEY_SIZE];
	} param;

	ret = blkcipher_walk_virt(desc, walk);
	memcpy(param.iv, walk->iv, DES_BLOCK_SIZE);
	memcpy(param.key, ctx->key, DES3_KEY_SIZE);
	while ((nbytes = walk->nbytes) >= DES_BLOCK_SIZE) {
		/* only use complete blocks */
		n = nbytes & ~(DES_BLOCK_SIZE - 1);
		cpacf_kmc(fc, &param, walk->dst.virt.addr,
			  walk->src.virt.addr, n);
		ret = blkcipher_walk_done(desc, walk, nbytes - n);
	}
	memcpy(walk->iv, param.iv, DES_BLOCK_SIZE);
	return ret;
}

static int ecb_des_encrypt(struct blkcipher_desc *desc,
			   struct scatterlist *dst, struct scatterlist *src,
			   unsigned int nbytes)
{
	struct blkcipher_walk walk;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	return ecb_desall_crypt(desc, CPACF_KM_DEA, &walk);
}

static int ecb_des_decrypt(struct blkcipher_desc *desc,
			   struct scatterlist *dst, struct scatterlist *src,
			   unsigned int nbytes)
{
	struct blkcipher_walk walk;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	return ecb_desall_crypt(desc, CPACF_KM_DEA | CPACF_DECRYPT, &walk);
}

static struct crypto_alg ecb_des_alg = {
	.cra_name		=	"ecb(des)",
	.cra_driver_name	=	"ecb-des-s390",
	.cra_priority		=	400,	/* combo: des + ecb */
	.cra_flags		=	CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		=	DES_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct s390_des_ctx),
	.cra_type		=	&crypto_blkcipher_type,
	.cra_module		=	THIS_MODULE,
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
	struct blkcipher_walk walk;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	return cbc_desall_crypt(desc, CPACF_KMC_DEA, &walk);
}

static int cbc_des_decrypt(struct blkcipher_desc *desc,
			   struct scatterlist *dst, struct scatterlist *src,
			   unsigned int nbytes)
{
	struct blkcipher_walk walk;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	return cbc_desall_crypt(desc, CPACF_KMC_DEA | CPACF_DECRYPT, &walk);
}

static struct crypto_alg cbc_des_alg = {
	.cra_name		=	"cbc(des)",
	.cra_driver_name	=	"cbc-des-s390",
	.cra_priority		=	400,	/* combo: des + cbc */
	.cra_flags		=	CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		=	DES_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct s390_des_ctx),
	.cra_type		=	&crypto_blkcipher_type,
	.cra_module		=	THIS_MODULE,
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
 *   In fips mode additinally check for all 3 keys are unique.
 *
 */
static int des3_setkey(struct crypto_tfm *tfm, const u8 *key,
		       unsigned int key_len)
{
	struct s390_des_ctx *ctx = crypto_tfm_ctx(tfm);

	if (!(crypto_memneq(key, &key[DES_KEY_SIZE], DES_KEY_SIZE) &&
	    crypto_memneq(&key[DES_KEY_SIZE], &key[DES_KEY_SIZE * 2],
			  DES_KEY_SIZE)) &&
	    (tfm->crt_flags & CRYPTO_TFM_REQ_WEAK_KEY)) {
		tfm->crt_flags |= CRYPTO_TFM_RES_WEAK_KEY;
		return -EINVAL;
	}

	/* in fips mode, ensure k1 != k2 and k2 != k3 and k1 != k3 */
	if (fips_enabled &&
	    !(crypto_memneq(key, &key[DES_KEY_SIZE], DES_KEY_SIZE) &&
	      crypto_memneq(&key[DES_KEY_SIZE], &key[DES_KEY_SIZE * 2],
			    DES_KEY_SIZE) &&
	      crypto_memneq(key, &key[DES_KEY_SIZE * 2], DES_KEY_SIZE))) {
		tfm->crt_flags |= CRYPTO_TFM_RES_WEAK_KEY;
		return -EINVAL;
	}

	memcpy(ctx->key, key, key_len);
	return 0;
}

static void des3_encrypt(struct crypto_tfm *tfm, u8 *dst, const u8 *src)
{
	struct s390_des_ctx *ctx = crypto_tfm_ctx(tfm);

	cpacf_km(CPACF_KM_TDEA_192, ctx->key, dst, src, DES_BLOCK_SIZE);
}

static void des3_decrypt(struct crypto_tfm *tfm, u8 *dst, const u8 *src)
{
	struct s390_des_ctx *ctx = crypto_tfm_ctx(tfm);

	cpacf_km(CPACF_KM_TDEA_192 | CPACF_DECRYPT,
		 ctx->key, dst, src, DES_BLOCK_SIZE);
}

static struct crypto_alg des3_alg = {
	.cra_name		=	"des3_ede",
	.cra_driver_name	=	"des3_ede-s390",
	.cra_priority		=	300,
	.cra_flags		=	CRYPTO_ALG_TYPE_CIPHER,
	.cra_blocksize		=	DES_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct s390_des_ctx),
	.cra_module		=	THIS_MODULE,
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
	struct blkcipher_walk walk;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	return ecb_desall_crypt(desc, CPACF_KM_TDEA_192, &walk);
}

static int ecb_des3_decrypt(struct blkcipher_desc *desc,
			    struct scatterlist *dst, struct scatterlist *src,
			    unsigned int nbytes)
{
	struct blkcipher_walk walk;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	return ecb_desall_crypt(desc, CPACF_KM_TDEA_192 | CPACF_DECRYPT,
				&walk);
}

static struct crypto_alg ecb_des3_alg = {
	.cra_name		=	"ecb(des3_ede)",
	.cra_driver_name	=	"ecb-des3_ede-s390",
	.cra_priority		=	400,	/* combo: des3 + ecb */
	.cra_flags		=	CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		=	DES_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct s390_des_ctx),
	.cra_type		=	&crypto_blkcipher_type,
	.cra_module		=	THIS_MODULE,
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
	struct blkcipher_walk walk;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	return cbc_desall_crypt(desc, CPACF_KMC_TDEA_192, &walk);
}

static int cbc_des3_decrypt(struct blkcipher_desc *desc,
			    struct scatterlist *dst, struct scatterlist *src,
			    unsigned int nbytes)
{
	struct blkcipher_walk walk;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	return cbc_desall_crypt(desc, CPACF_KMC_TDEA_192 | CPACF_DECRYPT,
				&walk);
}

static struct crypto_alg cbc_des3_alg = {
	.cra_name		=	"cbc(des3_ede)",
	.cra_driver_name	=	"cbc-des3_ede-s390",
	.cra_priority		=	400,	/* combo: des3 + cbc */
	.cra_flags		=	CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		=	DES_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct s390_des_ctx),
	.cra_type		=	&crypto_blkcipher_type,
	.cra_module		=	THIS_MODULE,
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

static unsigned int __ctrblk_init(u8 *ctrptr, u8 *iv, unsigned int nbytes)
{
	unsigned int i, n;

	/* align to block size, max. PAGE_SIZE */
	n = (nbytes > PAGE_SIZE) ? PAGE_SIZE : nbytes & ~(DES_BLOCK_SIZE - 1);
	memcpy(ctrptr, iv, DES_BLOCK_SIZE);
	for (i = (n / DES_BLOCK_SIZE) - 1; i > 0; i--) {
		memcpy(ctrptr + DES_BLOCK_SIZE, ctrptr, DES_BLOCK_SIZE);
		crypto_inc(ctrptr + DES_BLOCK_SIZE, DES_BLOCK_SIZE);
		ctrptr += DES_BLOCK_SIZE;
	}
	return n;
}

static int ctr_desall_crypt(struct blkcipher_desc *desc, unsigned long fc,
			    struct blkcipher_walk *walk)
{
	struct s390_des_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
	u8 buf[DES_BLOCK_SIZE], *ctrptr;
	unsigned int n, nbytes;
	int ret, locked;

	locked = spin_trylock(&ctrblk_lock);

	ret = blkcipher_walk_virt_block(desc, walk, DES_BLOCK_SIZE);
	while ((nbytes = walk->nbytes) >= DES_BLOCK_SIZE) {
		n = DES_BLOCK_SIZE;
		if (nbytes >= 2*DES_BLOCK_SIZE && locked)
			n = __ctrblk_init(ctrblk, walk->iv, nbytes);
		ctrptr = (n > DES_BLOCK_SIZE) ? ctrblk : walk->iv;
		cpacf_kmctr(fc, ctx->key, walk->dst.virt.addr,
			    walk->src.virt.addr, n, ctrptr);
		if (ctrptr == ctrblk)
			memcpy(walk->iv, ctrptr + n - DES_BLOCK_SIZE,
				DES_BLOCK_SIZE);
		crypto_inc(walk->iv, DES_BLOCK_SIZE);
		ret = blkcipher_walk_done(desc, walk, nbytes - n);
	}
	if (locked)
		spin_unlock(&ctrblk_lock);
	/* final block may be < DES_BLOCK_SIZE, copy only nbytes */
	if (nbytes) {
		cpacf_kmctr(fc, ctx->key, buf, walk->src.virt.addr,
			    DES_BLOCK_SIZE, walk->iv);
		memcpy(walk->dst.virt.addr, buf, nbytes);
		crypto_inc(walk->iv, DES_BLOCK_SIZE);
		ret = blkcipher_walk_done(desc, walk, 0);
	}
	return ret;
}

static int ctr_des_encrypt(struct blkcipher_desc *desc,
			   struct scatterlist *dst, struct scatterlist *src,
			   unsigned int nbytes)
{
	struct blkcipher_walk walk;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	return ctr_desall_crypt(desc, CPACF_KMCTR_DEA, &walk);
}

static int ctr_des_decrypt(struct blkcipher_desc *desc,
			   struct scatterlist *dst, struct scatterlist *src,
			   unsigned int nbytes)
{
	struct blkcipher_walk walk;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	return ctr_desall_crypt(desc, CPACF_KMCTR_DEA | CPACF_DECRYPT, &walk);
}

static struct crypto_alg ctr_des_alg = {
	.cra_name		=	"ctr(des)",
	.cra_driver_name	=	"ctr-des-s390",
	.cra_priority		=	400,	/* combo: des + ctr */
	.cra_flags		=	CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		=	1,
	.cra_ctxsize		=	sizeof(struct s390_des_ctx),
	.cra_type		=	&crypto_blkcipher_type,
	.cra_module		=	THIS_MODULE,
	.cra_u			=	{
		.blkcipher = {
			.min_keysize		=	DES_KEY_SIZE,
			.max_keysize		=	DES_KEY_SIZE,
			.ivsize			=	DES_BLOCK_SIZE,
			.setkey			=	des_setkey,
			.encrypt		=	ctr_des_encrypt,
			.decrypt		=	ctr_des_decrypt,
		}
	}
};

static int ctr_des3_encrypt(struct blkcipher_desc *desc,
			    struct scatterlist *dst, struct scatterlist *src,
			    unsigned int nbytes)
{
	struct blkcipher_walk walk;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	return ctr_desall_crypt(desc, CPACF_KMCTR_TDEA_192, &walk);
}

static int ctr_des3_decrypt(struct blkcipher_desc *desc,
			    struct scatterlist *dst, struct scatterlist *src,
			    unsigned int nbytes)
{
	struct blkcipher_walk walk;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	return ctr_desall_crypt(desc, CPACF_KMCTR_TDEA_192 | CPACF_DECRYPT,
				&walk);
}

static struct crypto_alg ctr_des3_alg = {
	.cra_name		=	"ctr(des3_ede)",
	.cra_driver_name	=	"ctr-des3_ede-s390",
	.cra_priority		=	400,	/* combo: des3 + ede */
	.cra_flags		=	CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		=	1,
	.cra_ctxsize		=	sizeof(struct s390_des_ctx),
	.cra_type		=	&crypto_blkcipher_type,
	.cra_module		=	THIS_MODULE,
	.cra_u			=	{
		.blkcipher = {
			.min_keysize		=	DES3_KEY_SIZE,
			.max_keysize		=	DES3_KEY_SIZE,
			.ivsize			=	DES_BLOCK_SIZE,
			.setkey			=	des3_setkey,
			.encrypt		=	ctr_des3_encrypt,
			.decrypt		=	ctr_des3_decrypt,
		}
	}
};

static struct crypto_alg *des_s390_algs_ptr[8];
static int des_s390_algs_num;

static int des_s390_register_alg(struct crypto_alg *alg)
{
	int ret;

	ret = crypto_register_alg(alg);
	if (!ret)
		des_s390_algs_ptr[des_s390_algs_num++] = alg;
	return ret;
}

static void des_s390_exit(void)
{
	while (des_s390_algs_num--)
		crypto_unregister_alg(des_s390_algs_ptr[des_s390_algs_num]);
	if (ctrblk)
		free_page((unsigned long) ctrblk);
}

static int __init des_s390_init(void)
{
	int ret;

	/* Query available functions for KM, KMC and KMCTR */
	cpacf_query(CPACF_KM, &km_functions);
	cpacf_query(CPACF_KMC, &kmc_functions);
	cpacf_query(CPACF_KMCTR, &kmctr_functions);

	if (cpacf_test_func(&km_functions, CPACF_KM_DEA)) {
		ret = des_s390_register_alg(&des_alg);
		if (ret)
			goto out_err;
		ret = des_s390_register_alg(&ecb_des_alg);
		if (ret)
			goto out_err;
	}
	if (cpacf_test_func(&kmc_functions, CPACF_KMC_DEA)) {
		ret = des_s390_register_alg(&cbc_des_alg);
		if (ret)
			goto out_err;
	}
	if (cpacf_test_func(&km_functions, CPACF_KM_TDEA_192)) {
		ret = des_s390_register_alg(&des3_alg);
		if (ret)
			goto out_err;
		ret = des_s390_register_alg(&ecb_des3_alg);
		if (ret)
			goto out_err;
	}
	if (cpacf_test_func(&kmc_functions, CPACF_KMC_TDEA_192)) {
		ret = des_s390_register_alg(&cbc_des3_alg);
		if (ret)
			goto out_err;
	}

	if (cpacf_test_func(&kmctr_functions, CPACF_KMCTR_DEA) ||
	    cpacf_test_func(&kmctr_functions, CPACF_KMCTR_TDEA_192)) {
		ctrblk = (u8 *) __get_free_page(GFP_KERNEL);
		if (!ctrblk) {
			ret = -ENOMEM;
			goto out_err;
		}
	}

	if (cpacf_test_func(&kmctr_functions, CPACF_KMCTR_DEA)) {
		ret = des_s390_register_alg(&ctr_des_alg);
		if (ret)
			goto out_err;
	}
	if (cpacf_test_func(&kmctr_functions, CPACF_KMCTR_TDEA_192)) {
		ret = des_s390_register_alg(&ctr_des3_alg);
		if (ret)
			goto out_err;
	}

	return 0;
out_err:
	des_s390_exit();
	return ret;
}

module_cpu_feature_match(MSA, des_s390_init);
module_exit(des_s390_exit);

MODULE_ALIAS_CRYPTO("des");
MODULE_ALIAS_CRYPTO("des3_ede");

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("DES & Triple DES EDE Cipher Algorithms");
