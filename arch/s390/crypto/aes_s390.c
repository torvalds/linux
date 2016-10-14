/*
 * Cryptographic API.
 *
 * s390 implementation of the AES Cipher Algorithm.
 *
 * s390 Version:
 *   Copyright IBM Corp. 2005, 2007
 *   Author(s): Jan Glauber (jang@de.ibm.com)
 *		Sebastian Siewior (sebastian@breakpoint.cc> SW-Fallback
 *
 * Derived from "crypto/aes_generic.c"
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */

#define KMSG_COMPONENT "aes_s390"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <crypto/aes.h>
#include <crypto/algapi.h>
#include <crypto/internal/skcipher.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/cpufeature.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <crypto/xts.h>
#include <asm/cpacf.h>

#define AES_KEYLEN_128		1
#define AES_KEYLEN_192		2
#define AES_KEYLEN_256		4

static u8 *ctrblk;
static DEFINE_SPINLOCK(ctrblk_lock);
static char keylen_flag;

struct s390_aes_ctx {
	u8 key[AES_MAX_KEY_SIZE];
	long enc;
	long dec;
	int key_len;
	union {
		struct crypto_skcipher *blk;
		struct crypto_cipher *cip;
	} fallback;
};

struct pcc_param {
	u8 key[32];
	u8 tweak[16];
	u8 block[16];
	u8 bit[16];
	u8 xts[16];
};

struct s390_xts_ctx {
	u8 key[32];
	u8 pcc_key[32];
	long enc;
	long dec;
	int key_len;
	struct crypto_skcipher *fallback;
};

/*
 * Check if the key_len is supported by the HW.
 * Returns 0 if it is, a positive number if it is not and software fallback is
 * required or a negative number in case the key size is not valid
 */
static int need_fallback(unsigned int key_len)
{
	switch (key_len) {
	case 16:
		if (!(keylen_flag & AES_KEYLEN_128))
			return 1;
		break;
	case 24:
		if (!(keylen_flag & AES_KEYLEN_192))
			return 1;
		break;
	case 32:
		if (!(keylen_flag & AES_KEYLEN_256))
			return 1;
		break;
	default:
		return -1;
		break;
	}
	return 0;
}

static int setkey_fallback_cip(struct crypto_tfm *tfm, const u8 *in_key,
		unsigned int key_len)
{
	struct s390_aes_ctx *sctx = crypto_tfm_ctx(tfm);
	int ret;

	sctx->fallback.cip->base.crt_flags &= ~CRYPTO_TFM_REQ_MASK;
	sctx->fallback.cip->base.crt_flags |= (tfm->crt_flags &
			CRYPTO_TFM_REQ_MASK);

	ret = crypto_cipher_setkey(sctx->fallback.cip, in_key, key_len);
	if (ret) {
		tfm->crt_flags &= ~CRYPTO_TFM_RES_MASK;
		tfm->crt_flags |= (sctx->fallback.cip->base.crt_flags &
				CRYPTO_TFM_RES_MASK);
	}
	return ret;
}

static int aes_set_key(struct crypto_tfm *tfm, const u8 *in_key,
		       unsigned int key_len)
{
	struct s390_aes_ctx *sctx = crypto_tfm_ctx(tfm);
	u32 *flags = &tfm->crt_flags;
	int ret;

	ret = need_fallback(key_len);
	if (ret < 0) {
		*flags |= CRYPTO_TFM_RES_BAD_KEY_LEN;
		return -EINVAL;
	}

	sctx->key_len = key_len;
	if (!ret) {
		memcpy(sctx->key, in_key, key_len);
		return 0;
	}

	return setkey_fallback_cip(tfm, in_key, key_len);
}

static void aes_encrypt(struct crypto_tfm *tfm, u8 *out, const u8 *in)
{
	struct s390_aes_ctx *sctx = crypto_tfm_ctx(tfm);

	if (unlikely(need_fallback(sctx->key_len))) {
		crypto_cipher_encrypt_one(sctx->fallback.cip, out, in);
		return;
	}

	switch (sctx->key_len) {
	case 16:
		cpacf_km(CPACF_KM_AES_128_ENC, &sctx->key, out, in,
			 AES_BLOCK_SIZE);
		break;
	case 24:
		cpacf_km(CPACF_KM_AES_192_ENC, &sctx->key, out, in,
			 AES_BLOCK_SIZE);
		break;
	case 32:
		cpacf_km(CPACF_KM_AES_256_ENC, &sctx->key, out, in,
			 AES_BLOCK_SIZE);
		break;
	}
}

static void aes_decrypt(struct crypto_tfm *tfm, u8 *out, const u8 *in)
{
	struct s390_aes_ctx *sctx = crypto_tfm_ctx(tfm);

	if (unlikely(need_fallback(sctx->key_len))) {
		crypto_cipher_decrypt_one(sctx->fallback.cip, out, in);
		return;
	}

	switch (sctx->key_len) {
	case 16:
		cpacf_km(CPACF_KM_AES_128_DEC, &sctx->key, out, in,
			 AES_BLOCK_SIZE);
		break;
	case 24:
		cpacf_km(CPACF_KM_AES_192_DEC, &sctx->key, out, in,
			 AES_BLOCK_SIZE);
		break;
	case 32:
		cpacf_km(CPACF_KM_AES_256_DEC, &sctx->key, out, in,
			 AES_BLOCK_SIZE);
		break;
	}
}

static int fallback_init_cip(struct crypto_tfm *tfm)
{
	const char *name = tfm->__crt_alg->cra_name;
	struct s390_aes_ctx *sctx = crypto_tfm_ctx(tfm);

	sctx->fallback.cip = crypto_alloc_cipher(name, 0,
			CRYPTO_ALG_ASYNC | CRYPTO_ALG_NEED_FALLBACK);

	if (IS_ERR(sctx->fallback.cip)) {
		pr_err("Allocating AES fallback algorithm %s failed\n",
		       name);
		return PTR_ERR(sctx->fallback.cip);
	}

	return 0;
}

static void fallback_exit_cip(struct crypto_tfm *tfm)
{
	struct s390_aes_ctx *sctx = crypto_tfm_ctx(tfm);

	crypto_free_cipher(sctx->fallback.cip);
	sctx->fallback.cip = NULL;
}

static struct crypto_alg aes_alg = {
	.cra_name		=	"aes",
	.cra_driver_name	=	"aes-s390",
	.cra_priority		=	300,
	.cra_flags		=	CRYPTO_ALG_TYPE_CIPHER |
					CRYPTO_ALG_NEED_FALLBACK,
	.cra_blocksize		=	AES_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct s390_aes_ctx),
	.cra_module		=	THIS_MODULE,
	.cra_init               =       fallback_init_cip,
	.cra_exit               =       fallback_exit_cip,
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

static int setkey_fallback_blk(struct crypto_tfm *tfm, const u8 *key,
		unsigned int len)
{
	struct s390_aes_ctx *sctx = crypto_tfm_ctx(tfm);
	unsigned int ret;

	crypto_skcipher_clear_flags(sctx->fallback.blk, CRYPTO_TFM_REQ_MASK);
	crypto_skcipher_set_flags(sctx->fallback.blk, tfm->crt_flags &
						      CRYPTO_TFM_REQ_MASK);

	ret = crypto_skcipher_setkey(sctx->fallback.blk, key, len);

	tfm->crt_flags &= ~CRYPTO_TFM_RES_MASK;
	tfm->crt_flags |= crypto_skcipher_get_flags(sctx->fallback.blk) &
			  CRYPTO_TFM_RES_MASK;

	return ret;
}

static int fallback_blk_dec(struct blkcipher_desc *desc,
		struct scatterlist *dst, struct scatterlist *src,
		unsigned int nbytes)
{
	unsigned int ret;
	struct crypto_blkcipher *tfm = desc->tfm;
	struct s390_aes_ctx *sctx = crypto_blkcipher_ctx(tfm);
	SKCIPHER_REQUEST_ON_STACK(req, sctx->fallback.blk);

	skcipher_request_set_tfm(req, sctx->fallback.blk);
	skcipher_request_set_callback(req, desc->flags, NULL, NULL);
	skcipher_request_set_crypt(req, src, dst, nbytes, desc->info);

	ret = crypto_skcipher_decrypt(req);

	skcipher_request_zero(req);
	return ret;
}

static int fallback_blk_enc(struct blkcipher_desc *desc,
		struct scatterlist *dst, struct scatterlist *src,
		unsigned int nbytes)
{
	unsigned int ret;
	struct crypto_blkcipher *tfm = desc->tfm;
	struct s390_aes_ctx *sctx = crypto_blkcipher_ctx(tfm);
	SKCIPHER_REQUEST_ON_STACK(req, sctx->fallback.blk);

	skcipher_request_set_tfm(req, sctx->fallback.blk);
	skcipher_request_set_callback(req, desc->flags, NULL, NULL);
	skcipher_request_set_crypt(req, src, dst, nbytes, desc->info);

	ret = crypto_skcipher_encrypt(req);
	return ret;
}

static int ecb_aes_set_key(struct crypto_tfm *tfm, const u8 *in_key,
			   unsigned int key_len)
{
	struct s390_aes_ctx *sctx = crypto_tfm_ctx(tfm);
	int ret;

	ret = need_fallback(key_len);
	if (ret > 0) {
		sctx->key_len = key_len;
		return setkey_fallback_blk(tfm, in_key, key_len);
	}

	switch (key_len) {
	case 16:
		sctx->enc = CPACF_KM_AES_128_ENC;
		sctx->dec = CPACF_KM_AES_128_DEC;
		break;
	case 24:
		sctx->enc = CPACF_KM_AES_192_ENC;
		sctx->dec = CPACF_KM_AES_192_DEC;
		break;
	case 32:
		sctx->enc = CPACF_KM_AES_256_ENC;
		sctx->dec = CPACF_KM_AES_256_DEC;
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

		ret = cpacf_km(func, param, out, in, n);
		if (ret < 0 || ret != n)
			return -EIO;

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

	if (unlikely(need_fallback(sctx->key_len)))
		return fallback_blk_enc(desc, dst, src, nbytes);

	blkcipher_walk_init(&walk, dst, src, nbytes);
	return ecb_aes_crypt(desc, sctx->enc, sctx->key, &walk);
}

static int ecb_aes_decrypt(struct blkcipher_desc *desc,
			   struct scatterlist *dst, struct scatterlist *src,
			   unsigned int nbytes)
{
	struct s390_aes_ctx *sctx = crypto_blkcipher_ctx(desc->tfm);
	struct blkcipher_walk walk;

	if (unlikely(need_fallback(sctx->key_len)))
		return fallback_blk_dec(desc, dst, src, nbytes);

	blkcipher_walk_init(&walk, dst, src, nbytes);
	return ecb_aes_crypt(desc, sctx->dec, sctx->key, &walk);
}

static int fallback_init_blk(struct crypto_tfm *tfm)
{
	const char *name = tfm->__crt_alg->cra_name;
	struct s390_aes_ctx *sctx = crypto_tfm_ctx(tfm);

	sctx->fallback.blk = crypto_alloc_skcipher(name, 0,
						   CRYPTO_ALG_ASYNC |
						   CRYPTO_ALG_NEED_FALLBACK);

	if (IS_ERR(sctx->fallback.blk)) {
		pr_err("Allocating AES fallback algorithm %s failed\n",
		       name);
		return PTR_ERR(sctx->fallback.blk);
	}

	return 0;
}

static void fallback_exit_blk(struct crypto_tfm *tfm)
{
	struct s390_aes_ctx *sctx = crypto_tfm_ctx(tfm);

	crypto_free_skcipher(sctx->fallback.blk);
}

static struct crypto_alg ecb_aes_alg = {
	.cra_name		=	"ecb(aes)",
	.cra_driver_name	=	"ecb-aes-s390",
	.cra_priority		=	400,	/* combo: aes + ecb */
	.cra_flags		=	CRYPTO_ALG_TYPE_BLKCIPHER |
					CRYPTO_ALG_NEED_FALLBACK,
	.cra_blocksize		=	AES_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct s390_aes_ctx),
	.cra_type		=	&crypto_blkcipher_type,
	.cra_module		=	THIS_MODULE,
	.cra_init		=	fallback_init_blk,
	.cra_exit		=	fallback_exit_blk,
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
	int ret;

	ret = need_fallback(key_len);
	if (ret > 0) {
		sctx->key_len = key_len;
		return setkey_fallback_blk(tfm, in_key, key_len);
	}

	switch (key_len) {
	case 16:
		sctx->enc = CPACF_KMC_AES_128_ENC;
		sctx->dec = CPACF_KMC_AES_128_DEC;
		break;
	case 24:
		sctx->enc = CPACF_KMC_AES_192_ENC;
		sctx->dec = CPACF_KMC_AES_192_DEC;
		break;
	case 32:
		sctx->enc = CPACF_KMC_AES_256_ENC;
		sctx->dec = CPACF_KMC_AES_256_DEC;
		break;
	}

	return aes_set_key(tfm, in_key, key_len);
}

static int cbc_aes_crypt(struct blkcipher_desc *desc, long func,
			 struct blkcipher_walk *walk)
{
	struct s390_aes_ctx *sctx = crypto_blkcipher_ctx(desc->tfm);
	int ret = blkcipher_walk_virt(desc, walk);
	unsigned int nbytes = walk->nbytes;
	struct {
		u8 iv[AES_BLOCK_SIZE];
		u8 key[AES_MAX_KEY_SIZE];
	} param;

	if (!nbytes)
		goto out;

	memcpy(param.iv, walk->iv, AES_BLOCK_SIZE);
	memcpy(param.key, sctx->key, sctx->key_len);
	do {
		/* only use complete blocks */
		unsigned int n = nbytes & ~(AES_BLOCK_SIZE - 1);
		u8 *out = walk->dst.virt.addr;
		u8 *in = walk->src.virt.addr;

		ret = cpacf_kmc(func, &param, out, in, n);
		if (ret < 0 || ret != n)
			return -EIO;

		nbytes &= AES_BLOCK_SIZE - 1;
		ret = blkcipher_walk_done(desc, walk, nbytes);
	} while ((nbytes = walk->nbytes));
	memcpy(walk->iv, param.iv, AES_BLOCK_SIZE);

out:
	return ret;
}

static int cbc_aes_encrypt(struct blkcipher_desc *desc,
			   struct scatterlist *dst, struct scatterlist *src,
			   unsigned int nbytes)
{
	struct s390_aes_ctx *sctx = crypto_blkcipher_ctx(desc->tfm);
	struct blkcipher_walk walk;

	if (unlikely(need_fallback(sctx->key_len)))
		return fallback_blk_enc(desc, dst, src, nbytes);

	blkcipher_walk_init(&walk, dst, src, nbytes);
	return cbc_aes_crypt(desc, sctx->enc, &walk);
}

static int cbc_aes_decrypt(struct blkcipher_desc *desc,
			   struct scatterlist *dst, struct scatterlist *src,
			   unsigned int nbytes)
{
	struct s390_aes_ctx *sctx = crypto_blkcipher_ctx(desc->tfm);
	struct blkcipher_walk walk;

	if (unlikely(need_fallback(sctx->key_len)))
		return fallback_blk_dec(desc, dst, src, nbytes);

	blkcipher_walk_init(&walk, dst, src, nbytes);
	return cbc_aes_crypt(desc, sctx->dec, &walk);
}

static struct crypto_alg cbc_aes_alg = {
	.cra_name		=	"cbc(aes)",
	.cra_driver_name	=	"cbc-aes-s390",
	.cra_priority		=	400,	/* combo: aes + cbc */
	.cra_flags		=	CRYPTO_ALG_TYPE_BLKCIPHER |
					CRYPTO_ALG_NEED_FALLBACK,
	.cra_blocksize		=	AES_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct s390_aes_ctx),
	.cra_type		=	&crypto_blkcipher_type,
	.cra_module		=	THIS_MODULE,
	.cra_init		=	fallback_init_blk,
	.cra_exit		=	fallback_exit_blk,
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

static int xts_fallback_setkey(struct crypto_tfm *tfm, const u8 *key,
				   unsigned int len)
{
	struct s390_xts_ctx *xts_ctx = crypto_tfm_ctx(tfm);
	unsigned int ret;

	crypto_skcipher_clear_flags(xts_ctx->fallback, CRYPTO_TFM_REQ_MASK);
	crypto_skcipher_set_flags(xts_ctx->fallback, tfm->crt_flags &
						     CRYPTO_TFM_REQ_MASK);

	ret = crypto_skcipher_setkey(xts_ctx->fallback, key, len);

	tfm->crt_flags &= ~CRYPTO_TFM_RES_MASK;
	tfm->crt_flags |= crypto_skcipher_get_flags(xts_ctx->fallback) &
			  CRYPTO_TFM_RES_MASK;

	return ret;
}

static int xts_fallback_decrypt(struct blkcipher_desc *desc,
		struct scatterlist *dst, struct scatterlist *src,
		unsigned int nbytes)
{
	struct crypto_blkcipher *tfm = desc->tfm;
	struct s390_xts_ctx *xts_ctx = crypto_blkcipher_ctx(tfm);
	SKCIPHER_REQUEST_ON_STACK(req, xts_ctx->fallback);
	unsigned int ret;

	skcipher_request_set_tfm(req, xts_ctx->fallback);
	skcipher_request_set_callback(req, desc->flags, NULL, NULL);
	skcipher_request_set_crypt(req, src, dst, nbytes, desc->info);

	ret = crypto_skcipher_decrypt(req);

	skcipher_request_zero(req);
	return ret;
}

static int xts_fallback_encrypt(struct blkcipher_desc *desc,
		struct scatterlist *dst, struct scatterlist *src,
		unsigned int nbytes)
{
	struct crypto_blkcipher *tfm = desc->tfm;
	struct s390_xts_ctx *xts_ctx = crypto_blkcipher_ctx(tfm);
	SKCIPHER_REQUEST_ON_STACK(req, xts_ctx->fallback);
	unsigned int ret;

	skcipher_request_set_tfm(req, xts_ctx->fallback);
	skcipher_request_set_callback(req, desc->flags, NULL, NULL);
	skcipher_request_set_crypt(req, src, dst, nbytes, desc->info);

	ret = crypto_skcipher_encrypt(req);

	skcipher_request_zero(req);
	return ret;
}

static int xts_aes_set_key(struct crypto_tfm *tfm, const u8 *in_key,
			   unsigned int key_len)
{
	struct s390_xts_ctx *xts_ctx = crypto_tfm_ctx(tfm);
	u32 *flags = &tfm->crt_flags;
	int err;

	err = xts_check_key(tfm, in_key, key_len);
	if (err)
		return err;

	switch (key_len) {
	case 32:
		xts_ctx->enc = CPACF_KM_XTS_128_ENC;
		xts_ctx->dec = CPACF_KM_XTS_128_DEC;
		memcpy(xts_ctx->key + 16, in_key, 16);
		memcpy(xts_ctx->pcc_key + 16, in_key + 16, 16);
		break;
	case 48:
		xts_ctx->enc = 0;
		xts_ctx->dec = 0;
		xts_fallback_setkey(tfm, in_key, key_len);
		break;
	case 64:
		xts_ctx->enc = CPACF_KM_XTS_256_ENC;
		xts_ctx->dec = CPACF_KM_XTS_256_DEC;
		memcpy(xts_ctx->key, in_key, 32);
		memcpy(xts_ctx->pcc_key, in_key + 32, 32);
		break;
	default:
		*flags |= CRYPTO_TFM_RES_BAD_KEY_LEN;
		return -EINVAL;
	}
	xts_ctx->key_len = key_len;
	return 0;
}

static int xts_aes_crypt(struct blkcipher_desc *desc, long func,
			 struct s390_xts_ctx *xts_ctx,
			 struct blkcipher_walk *walk)
{
	unsigned int offset = (xts_ctx->key_len >> 1) & 0x10;
	int ret = blkcipher_walk_virt(desc, walk);
	unsigned int nbytes = walk->nbytes;
	unsigned int n;
	u8 *in, *out;
	struct pcc_param pcc_param;
	struct {
		u8 key[32];
		u8 init[16];
	} xts_param;

	if (!nbytes)
		goto out;

	memset(pcc_param.block, 0, sizeof(pcc_param.block));
	memset(pcc_param.bit, 0, sizeof(pcc_param.bit));
	memset(pcc_param.xts, 0, sizeof(pcc_param.xts));
	memcpy(pcc_param.tweak, walk->iv, sizeof(pcc_param.tweak));
	memcpy(pcc_param.key, xts_ctx->pcc_key, 32);
	/* remove decipher modifier bit from 'func' and call PCC */
	ret = cpacf_pcc(func & 0x7f, &pcc_param.key[offset]);
	if (ret < 0)
		return -EIO;

	memcpy(xts_param.key, xts_ctx->key, 32);
	memcpy(xts_param.init, pcc_param.xts, 16);
	do {
		/* only use complete blocks */
		n = nbytes & ~(AES_BLOCK_SIZE - 1);
		out = walk->dst.virt.addr;
		in = walk->src.virt.addr;

		ret = cpacf_km(func, &xts_param.key[offset], out, in, n);
		if (ret < 0 || ret != n)
			return -EIO;

		nbytes &= AES_BLOCK_SIZE - 1;
		ret = blkcipher_walk_done(desc, walk, nbytes);
	} while ((nbytes = walk->nbytes));
out:
	return ret;
}

static int xts_aes_encrypt(struct blkcipher_desc *desc,
			   struct scatterlist *dst, struct scatterlist *src,
			   unsigned int nbytes)
{
	struct s390_xts_ctx *xts_ctx = crypto_blkcipher_ctx(desc->tfm);
	struct blkcipher_walk walk;

	if (unlikely(xts_ctx->key_len == 48))
		return xts_fallback_encrypt(desc, dst, src, nbytes);

	blkcipher_walk_init(&walk, dst, src, nbytes);
	return xts_aes_crypt(desc, xts_ctx->enc, xts_ctx, &walk);
}

static int xts_aes_decrypt(struct blkcipher_desc *desc,
			   struct scatterlist *dst, struct scatterlist *src,
			   unsigned int nbytes)
{
	struct s390_xts_ctx *xts_ctx = crypto_blkcipher_ctx(desc->tfm);
	struct blkcipher_walk walk;

	if (unlikely(xts_ctx->key_len == 48))
		return xts_fallback_decrypt(desc, dst, src, nbytes);

	blkcipher_walk_init(&walk, dst, src, nbytes);
	return xts_aes_crypt(desc, xts_ctx->dec, xts_ctx, &walk);
}

static int xts_fallback_init(struct crypto_tfm *tfm)
{
	const char *name = tfm->__crt_alg->cra_name;
	struct s390_xts_ctx *xts_ctx = crypto_tfm_ctx(tfm);

	xts_ctx->fallback = crypto_alloc_skcipher(name, 0,
						  CRYPTO_ALG_ASYNC |
						  CRYPTO_ALG_NEED_FALLBACK);

	if (IS_ERR(xts_ctx->fallback)) {
		pr_err("Allocating XTS fallback algorithm %s failed\n",
		       name);
		return PTR_ERR(xts_ctx->fallback);
	}
	return 0;
}

static void xts_fallback_exit(struct crypto_tfm *tfm)
{
	struct s390_xts_ctx *xts_ctx = crypto_tfm_ctx(tfm);

	crypto_free_skcipher(xts_ctx->fallback);
}

static struct crypto_alg xts_aes_alg = {
	.cra_name		=	"xts(aes)",
	.cra_driver_name	=	"xts-aes-s390",
	.cra_priority		=	400,	/* combo: aes + xts */
	.cra_flags		=	CRYPTO_ALG_TYPE_BLKCIPHER |
					CRYPTO_ALG_NEED_FALLBACK,
	.cra_blocksize		=	AES_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct s390_xts_ctx),
	.cra_type		=	&crypto_blkcipher_type,
	.cra_module		=	THIS_MODULE,
	.cra_init		=	xts_fallback_init,
	.cra_exit		=	xts_fallback_exit,
	.cra_u			=	{
		.blkcipher = {
			.min_keysize		=	2 * AES_MIN_KEY_SIZE,
			.max_keysize		=	2 * AES_MAX_KEY_SIZE,
			.ivsize			=	AES_BLOCK_SIZE,
			.setkey			=	xts_aes_set_key,
			.encrypt		=	xts_aes_encrypt,
			.decrypt		=	xts_aes_decrypt,
		}
	}
};

static int xts_aes_alg_reg;

static int ctr_aes_set_key(struct crypto_tfm *tfm, const u8 *in_key,
			   unsigned int key_len)
{
	struct s390_aes_ctx *sctx = crypto_tfm_ctx(tfm);

	switch (key_len) {
	case 16:
		sctx->enc = CPACF_KMCTR_AES_128_ENC;
		sctx->dec = CPACF_KMCTR_AES_128_DEC;
		break;
	case 24:
		sctx->enc = CPACF_KMCTR_AES_192_ENC;
		sctx->dec = CPACF_KMCTR_AES_192_DEC;
		break;
	case 32:
		sctx->enc = CPACF_KMCTR_AES_256_ENC;
		sctx->dec = CPACF_KMCTR_AES_256_DEC;
		break;
	}

	return aes_set_key(tfm, in_key, key_len);
}

static unsigned int __ctrblk_init(u8 *ctrptr, unsigned int nbytes)
{
	unsigned int i, n;

	/* only use complete blocks, max. PAGE_SIZE */
	n = (nbytes > PAGE_SIZE) ? PAGE_SIZE : nbytes & ~(AES_BLOCK_SIZE - 1);
	for (i = AES_BLOCK_SIZE; i < n; i += AES_BLOCK_SIZE) {
		memcpy(ctrptr + i, ctrptr + i - AES_BLOCK_SIZE,
		       AES_BLOCK_SIZE);
		crypto_inc(ctrptr + i, AES_BLOCK_SIZE);
	}
	return n;
}

static int ctr_aes_crypt(struct blkcipher_desc *desc, long func,
			 struct s390_aes_ctx *sctx, struct blkcipher_walk *walk)
{
	int ret = blkcipher_walk_virt_block(desc, walk, AES_BLOCK_SIZE);
	unsigned int n, nbytes;
	u8 buf[AES_BLOCK_SIZE], ctrbuf[AES_BLOCK_SIZE];
	u8 *out, *in, *ctrptr = ctrbuf;

	if (!walk->nbytes)
		return ret;

	if (spin_trylock(&ctrblk_lock))
		ctrptr = ctrblk;

	memcpy(ctrptr, walk->iv, AES_BLOCK_SIZE);
	while ((nbytes = walk->nbytes) >= AES_BLOCK_SIZE) {
		out = walk->dst.virt.addr;
		in = walk->src.virt.addr;
		while (nbytes >= AES_BLOCK_SIZE) {
			if (ctrptr == ctrblk)
				n = __ctrblk_init(ctrptr, nbytes);
			else
				n = AES_BLOCK_SIZE;
			ret = cpacf_kmctr(func, sctx->key, out, in, n, ctrptr);
			if (ret < 0 || ret != n) {
				if (ctrptr == ctrblk)
					spin_unlock(&ctrblk_lock);
				return -EIO;
			}
			if (n > AES_BLOCK_SIZE)
				memcpy(ctrptr, ctrptr + n - AES_BLOCK_SIZE,
				       AES_BLOCK_SIZE);
			crypto_inc(ctrptr, AES_BLOCK_SIZE);
			out += n;
			in += n;
			nbytes -= n;
		}
		ret = blkcipher_walk_done(desc, walk, nbytes);
	}
	if (ctrptr == ctrblk) {
		if (nbytes)
			memcpy(ctrbuf, ctrptr, AES_BLOCK_SIZE);
		else
			memcpy(walk->iv, ctrptr, AES_BLOCK_SIZE);
		spin_unlock(&ctrblk_lock);
	} else {
		if (!nbytes)
			memcpy(walk->iv, ctrptr, AES_BLOCK_SIZE);
	}
	/*
	 * final block may be < AES_BLOCK_SIZE, copy only nbytes
	 */
	if (nbytes) {
		out = walk->dst.virt.addr;
		in = walk->src.virt.addr;
		ret = cpacf_kmctr(func, sctx->key, buf, in,
				  AES_BLOCK_SIZE, ctrbuf);
		if (ret < 0 || ret != AES_BLOCK_SIZE)
			return -EIO;
		memcpy(out, buf, nbytes);
		crypto_inc(ctrbuf, AES_BLOCK_SIZE);
		ret = blkcipher_walk_done(desc, walk, 0);
		memcpy(walk->iv, ctrbuf, AES_BLOCK_SIZE);
	}

	return ret;
}

static int ctr_aes_encrypt(struct blkcipher_desc *desc,
			   struct scatterlist *dst, struct scatterlist *src,
			   unsigned int nbytes)
{
	struct s390_aes_ctx *sctx = crypto_blkcipher_ctx(desc->tfm);
	struct blkcipher_walk walk;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	return ctr_aes_crypt(desc, sctx->enc, sctx, &walk);
}

static int ctr_aes_decrypt(struct blkcipher_desc *desc,
			   struct scatterlist *dst, struct scatterlist *src,
			   unsigned int nbytes)
{
	struct s390_aes_ctx *sctx = crypto_blkcipher_ctx(desc->tfm);
	struct blkcipher_walk walk;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	return ctr_aes_crypt(desc, sctx->dec, sctx, &walk);
}

static struct crypto_alg ctr_aes_alg = {
	.cra_name		=	"ctr(aes)",
	.cra_driver_name	=	"ctr-aes-s390",
	.cra_priority		=	400,	/* combo: aes + ctr */
	.cra_flags		=	CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		=	1,
	.cra_ctxsize		=	sizeof(struct s390_aes_ctx),
	.cra_type		=	&crypto_blkcipher_type,
	.cra_module		=	THIS_MODULE,
	.cra_u			=	{
		.blkcipher = {
			.min_keysize		=	AES_MIN_KEY_SIZE,
			.max_keysize		=	AES_MAX_KEY_SIZE,
			.ivsize			=	AES_BLOCK_SIZE,
			.setkey			=	ctr_aes_set_key,
			.encrypt		=	ctr_aes_encrypt,
			.decrypt		=	ctr_aes_decrypt,
		}
	}
};

static int ctr_aes_alg_reg;

static int __init aes_s390_init(void)
{
	int ret;

	if (cpacf_query(CPACF_KM, CPACF_KM_AES_128_ENC))
		keylen_flag |= AES_KEYLEN_128;
	if (cpacf_query(CPACF_KM, CPACF_KM_AES_192_ENC))
		keylen_flag |= AES_KEYLEN_192;
	if (cpacf_query(CPACF_KM, CPACF_KM_AES_256_ENC))
		keylen_flag |= AES_KEYLEN_256;

	if (!keylen_flag)
		return -EOPNOTSUPP;

	/* z9 109 and z9 BC/EC only support 128 bit key length */
	if (keylen_flag == AES_KEYLEN_128)
		pr_info("AES hardware acceleration is only available for"
			" 128-bit keys\n");

	ret = crypto_register_alg(&aes_alg);
	if (ret)
		goto aes_err;

	ret = crypto_register_alg(&ecb_aes_alg);
	if (ret)
		goto ecb_aes_err;

	ret = crypto_register_alg(&cbc_aes_alg);
	if (ret)
		goto cbc_aes_err;

	if (cpacf_query(CPACF_KM, CPACF_KM_XTS_128_ENC) &&
	    cpacf_query(CPACF_KM, CPACF_KM_XTS_256_ENC)) {
		ret = crypto_register_alg(&xts_aes_alg);
		if (ret)
			goto xts_aes_err;
		xts_aes_alg_reg = 1;
	}

	if (cpacf_query(CPACF_KMCTR, CPACF_KMCTR_AES_128_ENC) &&
	    cpacf_query(CPACF_KMCTR, CPACF_KMCTR_AES_192_ENC) &&
	    cpacf_query(CPACF_KMCTR, CPACF_KMCTR_AES_256_ENC)) {
		ctrblk = (u8 *) __get_free_page(GFP_KERNEL);
		if (!ctrblk) {
			ret = -ENOMEM;
			goto ctr_aes_err;
		}
		ret = crypto_register_alg(&ctr_aes_alg);
		if (ret) {
			free_page((unsigned long) ctrblk);
			goto ctr_aes_err;
		}
		ctr_aes_alg_reg = 1;
	}

out:
	return ret;

ctr_aes_err:
	crypto_unregister_alg(&xts_aes_alg);
xts_aes_err:
	crypto_unregister_alg(&cbc_aes_alg);
cbc_aes_err:
	crypto_unregister_alg(&ecb_aes_alg);
ecb_aes_err:
	crypto_unregister_alg(&aes_alg);
aes_err:
	goto out;
}

static void __exit aes_s390_fini(void)
{
	if (ctr_aes_alg_reg) {
		crypto_unregister_alg(&ctr_aes_alg);
		free_page((unsigned long) ctrblk);
	}
	if (xts_aes_alg_reg)
		crypto_unregister_alg(&xts_aes_alg);
	crypto_unregister_alg(&cbc_aes_alg);
	crypto_unregister_alg(&ecb_aes_alg);
	crypto_unregister_alg(&aes_alg);
}

module_cpu_feature_match(MSA, aes_s390_init);
module_exit(aes_s390_fini);

MODULE_ALIAS_CRYPTO("aes-all");

MODULE_DESCRIPTION("Rijndael (AES) Cipher Algorithm");
MODULE_LICENSE("GPL");
