// SPDX-License-Identifier: GPL-2.0+
/*
 * Cryptographic API.
 *
 * s390 implementation of the AES Cipher Algorithm.
 *
 * s390 Version:
 *   Copyright IBM Corp. 2005, 2017
 *   Author(s): Jan Glauber (jang@de.ibm.com)
 *		Sebastian Siewior (sebastian@breakpoint.cc> SW-Fallback
 *		Patrick Steuer <patrick.steuer@de.ibm.com>
 *		Harald Freudenberger <freude@de.ibm.com>
 *
 * Derived from "crypto/aes_generic.c"
 */

#define KMSG_COMPONENT "aes_s390"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <crypto/aes.h>
#include <crypto/algapi.h>
#include <crypto/ghash.h>
#include <crypto/internal/aead.h>
#include <crypto/internal/skcipher.h>
#include <crypto/scatterwalk.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/cpufeature.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/fips.h>
#include <linux/string.h>
#include <crypto/xts.h>
#include <asm/cpacf.h>

static u8 *ctrblk;
static DEFINE_SPINLOCK(ctrblk_lock);

static cpacf_mask_t km_functions, kmc_functions, kmctr_functions,
		    kma_functions;

struct s390_aes_ctx {
	u8 key[AES_MAX_KEY_SIZE];
	int key_len;
	unsigned long fc;
	union {
		struct crypto_skcipher *blk;
		struct crypto_cipher *cip;
	} fallback;
};

struct s390_xts_ctx {
	u8 key[32];
	u8 pcc_key[32];
	int key_len;
	unsigned long fc;
	struct crypto_skcipher *fallback;
};

struct gcm_sg_walk {
	struct scatter_walk walk;
	unsigned int walk_bytes;
	u8 *walk_ptr;
	unsigned int walk_bytes_remain;
	u8 buf[AES_BLOCK_SIZE];
	unsigned int buf_bytes;
	u8 *ptr;
	unsigned int nbytes;
};

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
	unsigned long fc;

	/* Pick the correct function code based on the key length */
	fc = (key_len == 16) ? CPACF_KM_AES_128 :
	     (key_len == 24) ? CPACF_KM_AES_192 :
	     (key_len == 32) ? CPACF_KM_AES_256 : 0;

	/* Check if the function code is available */
	sctx->fc = (fc && cpacf_test_func(&km_functions, fc)) ? fc : 0;
	if (!sctx->fc)
		return setkey_fallback_cip(tfm, in_key, key_len);

	sctx->key_len = key_len;
	memcpy(sctx->key, in_key, key_len);
	return 0;
}

static void aes_encrypt(struct crypto_tfm *tfm, u8 *out, const u8 *in)
{
	struct s390_aes_ctx *sctx = crypto_tfm_ctx(tfm);

	if (unlikely(!sctx->fc)) {
		crypto_cipher_encrypt_one(sctx->fallback.cip, out, in);
		return;
	}
	cpacf_km(sctx->fc, &sctx->key, out, in, AES_BLOCK_SIZE);
}

static void aes_decrypt(struct crypto_tfm *tfm, u8 *out, const u8 *in)
{
	struct s390_aes_ctx *sctx = crypto_tfm_ctx(tfm);

	if (unlikely(!sctx->fc)) {
		crypto_cipher_decrypt_one(sctx->fallback.cip, out, in);
		return;
	}
	cpacf_km(sctx->fc | CPACF_DECRYPT,
		 &sctx->key, out, in, AES_BLOCK_SIZE);
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
	unsigned long fc;

	/* Pick the correct function code based on the key length */
	fc = (key_len == 16) ? CPACF_KM_AES_128 :
	     (key_len == 24) ? CPACF_KM_AES_192 :
	     (key_len == 32) ? CPACF_KM_AES_256 : 0;

	/* Check if the function code is available */
	sctx->fc = (fc && cpacf_test_func(&km_functions, fc)) ? fc : 0;
	if (!sctx->fc)
		return setkey_fallback_blk(tfm, in_key, key_len);

	sctx->key_len = key_len;
	memcpy(sctx->key, in_key, key_len);
	return 0;
}

static int ecb_aes_crypt(struct blkcipher_desc *desc, unsigned long modifier,
			 struct blkcipher_walk *walk)
{
	struct s390_aes_ctx *sctx = crypto_blkcipher_ctx(desc->tfm);
	unsigned int nbytes, n;
	int ret;

	ret = blkcipher_walk_virt(desc, walk);
	while ((nbytes = walk->nbytes) >= AES_BLOCK_SIZE) {
		/* only use complete blocks */
		n = nbytes & ~(AES_BLOCK_SIZE - 1);
		cpacf_km(sctx->fc | modifier, sctx->key,
			 walk->dst.virt.addr, walk->src.virt.addr, n);
		ret = blkcipher_walk_done(desc, walk, nbytes - n);
	}

	return ret;
}

static int ecb_aes_encrypt(struct blkcipher_desc *desc,
			   struct scatterlist *dst, struct scatterlist *src,
			   unsigned int nbytes)
{
	struct s390_aes_ctx *sctx = crypto_blkcipher_ctx(desc->tfm);
	struct blkcipher_walk walk;

	if (unlikely(!sctx->fc))
		return fallback_blk_enc(desc, dst, src, nbytes);

	blkcipher_walk_init(&walk, dst, src, nbytes);
	return ecb_aes_crypt(desc, 0, &walk);
}

static int ecb_aes_decrypt(struct blkcipher_desc *desc,
			   struct scatterlist *dst, struct scatterlist *src,
			   unsigned int nbytes)
{
	struct s390_aes_ctx *sctx = crypto_blkcipher_ctx(desc->tfm);
	struct blkcipher_walk walk;

	if (unlikely(!sctx->fc))
		return fallback_blk_dec(desc, dst, src, nbytes);

	blkcipher_walk_init(&walk, dst, src, nbytes);
	return ecb_aes_crypt(desc, CPACF_DECRYPT, &walk);
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
	.cra_priority		=	401,	/* combo: aes + ecb + 1 */
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
	unsigned long fc;

	/* Pick the correct function code based on the key length */
	fc = (key_len == 16) ? CPACF_KMC_AES_128 :
	     (key_len == 24) ? CPACF_KMC_AES_192 :
	     (key_len == 32) ? CPACF_KMC_AES_256 : 0;

	/* Check if the function code is available */
	sctx->fc = (fc && cpacf_test_func(&kmc_functions, fc)) ? fc : 0;
	if (!sctx->fc)
		return setkey_fallback_blk(tfm, in_key, key_len);

	sctx->key_len = key_len;
	memcpy(sctx->key, in_key, key_len);
	return 0;
}

static int cbc_aes_crypt(struct blkcipher_desc *desc, unsigned long modifier,
			 struct blkcipher_walk *walk)
{
	struct s390_aes_ctx *sctx = crypto_blkcipher_ctx(desc->tfm);
	unsigned int nbytes, n;
	int ret;
	struct {
		u8 iv[AES_BLOCK_SIZE];
		u8 key[AES_MAX_KEY_SIZE];
	} param;

	ret = blkcipher_walk_virt(desc, walk);
	memcpy(param.iv, walk->iv, AES_BLOCK_SIZE);
	memcpy(param.key, sctx->key, sctx->key_len);
	while ((nbytes = walk->nbytes) >= AES_BLOCK_SIZE) {
		/* only use complete blocks */
		n = nbytes & ~(AES_BLOCK_SIZE - 1);
		cpacf_kmc(sctx->fc | modifier, &param,
			  walk->dst.virt.addr, walk->src.virt.addr, n);
		ret = blkcipher_walk_done(desc, walk, nbytes - n);
	}
	memcpy(walk->iv, param.iv, AES_BLOCK_SIZE);
	return ret;
}

static int cbc_aes_encrypt(struct blkcipher_desc *desc,
			   struct scatterlist *dst, struct scatterlist *src,
			   unsigned int nbytes)
{
	struct s390_aes_ctx *sctx = crypto_blkcipher_ctx(desc->tfm);
	struct blkcipher_walk walk;

	if (unlikely(!sctx->fc))
		return fallback_blk_enc(desc, dst, src, nbytes);

	blkcipher_walk_init(&walk, dst, src, nbytes);
	return cbc_aes_crypt(desc, 0, &walk);
}

static int cbc_aes_decrypt(struct blkcipher_desc *desc,
			   struct scatterlist *dst, struct scatterlist *src,
			   unsigned int nbytes)
{
	struct s390_aes_ctx *sctx = crypto_blkcipher_ctx(desc->tfm);
	struct blkcipher_walk walk;

	if (unlikely(!sctx->fc))
		return fallback_blk_dec(desc, dst, src, nbytes);

	blkcipher_walk_init(&walk, dst, src, nbytes);
	return cbc_aes_crypt(desc, CPACF_DECRYPT, &walk);
}

static struct crypto_alg cbc_aes_alg = {
	.cra_name		=	"cbc(aes)",
	.cra_driver_name	=	"cbc-aes-s390",
	.cra_priority		=	402,	/* ecb-aes-s390 + 1 */
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
	unsigned long fc;
	int err;

	err = xts_check_key(tfm, in_key, key_len);
	if (err)
		return err;

	/* In fips mode only 128 bit or 256 bit keys are valid */
	if (fips_enabled && key_len != 32 && key_len != 64) {
		tfm->crt_flags |= CRYPTO_TFM_RES_BAD_KEY_LEN;
		return -EINVAL;
	}

	/* Pick the correct function code based on the key length */
	fc = (key_len == 32) ? CPACF_KM_XTS_128 :
	     (key_len == 64) ? CPACF_KM_XTS_256 : 0;

	/* Check if the function code is available */
	xts_ctx->fc = (fc && cpacf_test_func(&km_functions, fc)) ? fc : 0;
	if (!xts_ctx->fc)
		return xts_fallback_setkey(tfm, in_key, key_len);

	/* Split the XTS key into the two subkeys */
	key_len = key_len / 2;
	xts_ctx->key_len = key_len;
	memcpy(xts_ctx->key, in_key, key_len);
	memcpy(xts_ctx->pcc_key, in_key + key_len, key_len);
	return 0;
}

static int xts_aes_crypt(struct blkcipher_desc *desc, unsigned long modifier,
			 struct blkcipher_walk *walk)
{
	struct s390_xts_ctx *xts_ctx = crypto_blkcipher_ctx(desc->tfm);
	unsigned int offset, nbytes, n;
	int ret;
	struct {
		u8 key[32];
		u8 tweak[16];
		u8 block[16];
		u8 bit[16];
		u8 xts[16];
	} pcc_param;
	struct {
		u8 key[32];
		u8 init[16];
	} xts_param;

	ret = blkcipher_walk_virt(desc, walk);
	offset = xts_ctx->key_len & 0x10;
	memset(pcc_param.block, 0, sizeof(pcc_param.block));
	memset(pcc_param.bit, 0, sizeof(pcc_param.bit));
	memset(pcc_param.xts, 0, sizeof(pcc_param.xts));
	memcpy(pcc_param.tweak, walk->iv, sizeof(pcc_param.tweak));
	memcpy(pcc_param.key + offset, xts_ctx->pcc_key, xts_ctx->key_len);
	cpacf_pcc(xts_ctx->fc, pcc_param.key + offset);

	memcpy(xts_param.key + offset, xts_ctx->key, xts_ctx->key_len);
	memcpy(xts_param.init, pcc_param.xts, 16);

	while ((nbytes = walk->nbytes) >= AES_BLOCK_SIZE) {
		/* only use complete blocks */
		n = nbytes & ~(AES_BLOCK_SIZE - 1);
		cpacf_km(xts_ctx->fc | modifier, xts_param.key + offset,
			 walk->dst.virt.addr, walk->src.virt.addr, n);
		ret = blkcipher_walk_done(desc, walk, nbytes - n);
	}
	return ret;
}

static int xts_aes_encrypt(struct blkcipher_desc *desc,
			   struct scatterlist *dst, struct scatterlist *src,
			   unsigned int nbytes)
{
	struct s390_xts_ctx *xts_ctx = crypto_blkcipher_ctx(desc->tfm);
	struct blkcipher_walk walk;

	if (unlikely(!xts_ctx->fc))
		return xts_fallback_encrypt(desc, dst, src, nbytes);

	blkcipher_walk_init(&walk, dst, src, nbytes);
	return xts_aes_crypt(desc, 0, &walk);
}

static int xts_aes_decrypt(struct blkcipher_desc *desc,
			   struct scatterlist *dst, struct scatterlist *src,
			   unsigned int nbytes)
{
	struct s390_xts_ctx *xts_ctx = crypto_blkcipher_ctx(desc->tfm);
	struct blkcipher_walk walk;

	if (unlikely(!xts_ctx->fc))
		return xts_fallback_decrypt(desc, dst, src, nbytes);

	blkcipher_walk_init(&walk, dst, src, nbytes);
	return xts_aes_crypt(desc, CPACF_DECRYPT, &walk);
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
	.cra_priority		=	402,	/* ecb-aes-s390 + 1 */
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

static int ctr_aes_set_key(struct crypto_tfm *tfm, const u8 *in_key,
			   unsigned int key_len)
{
	struct s390_aes_ctx *sctx = crypto_tfm_ctx(tfm);
	unsigned long fc;

	/* Pick the correct function code based on the key length */
	fc = (key_len == 16) ? CPACF_KMCTR_AES_128 :
	     (key_len == 24) ? CPACF_KMCTR_AES_192 :
	     (key_len == 32) ? CPACF_KMCTR_AES_256 : 0;

	/* Check if the function code is available */
	sctx->fc = (fc && cpacf_test_func(&kmctr_functions, fc)) ? fc : 0;
	if (!sctx->fc)
		return setkey_fallback_blk(tfm, in_key, key_len);

	sctx->key_len = key_len;
	memcpy(sctx->key, in_key, key_len);
	return 0;
}

static unsigned int __ctrblk_init(u8 *ctrptr, u8 *iv, unsigned int nbytes)
{
	unsigned int i, n;

	/* only use complete blocks, max. PAGE_SIZE */
	memcpy(ctrptr, iv, AES_BLOCK_SIZE);
	n = (nbytes > PAGE_SIZE) ? PAGE_SIZE : nbytes & ~(AES_BLOCK_SIZE - 1);
	for (i = (n / AES_BLOCK_SIZE) - 1; i > 0; i--) {
		memcpy(ctrptr + AES_BLOCK_SIZE, ctrptr, AES_BLOCK_SIZE);
		crypto_inc(ctrptr + AES_BLOCK_SIZE, AES_BLOCK_SIZE);
		ctrptr += AES_BLOCK_SIZE;
	}
	return n;
}

static int ctr_aes_crypt(struct blkcipher_desc *desc, unsigned long modifier,
			 struct blkcipher_walk *walk)
{
	struct s390_aes_ctx *sctx = crypto_blkcipher_ctx(desc->tfm);
	u8 buf[AES_BLOCK_SIZE], *ctrptr;
	unsigned int n, nbytes;
	int ret, locked;

	locked = spin_trylock(&ctrblk_lock);

	ret = blkcipher_walk_virt_block(desc, walk, AES_BLOCK_SIZE);
	while ((nbytes = walk->nbytes) >= AES_BLOCK_SIZE) {
		n = AES_BLOCK_SIZE;
		if (nbytes >= 2*AES_BLOCK_SIZE && locked)
			n = __ctrblk_init(ctrblk, walk->iv, nbytes);
		ctrptr = (n > AES_BLOCK_SIZE) ? ctrblk : walk->iv;
		cpacf_kmctr(sctx->fc | modifier, sctx->key,
			    walk->dst.virt.addr, walk->src.virt.addr,
			    n, ctrptr);
		if (ctrptr == ctrblk)
			memcpy(walk->iv, ctrptr + n - AES_BLOCK_SIZE,
			       AES_BLOCK_SIZE);
		crypto_inc(walk->iv, AES_BLOCK_SIZE);
		ret = blkcipher_walk_done(desc, walk, nbytes - n);
	}
	if (locked)
		spin_unlock(&ctrblk_lock);
	/*
	 * final block may be < AES_BLOCK_SIZE, copy only nbytes
	 */
	if (nbytes) {
		cpacf_kmctr(sctx->fc | modifier, sctx->key,
			    buf, walk->src.virt.addr,
			    AES_BLOCK_SIZE, walk->iv);
		memcpy(walk->dst.virt.addr, buf, nbytes);
		crypto_inc(walk->iv, AES_BLOCK_SIZE);
		ret = blkcipher_walk_done(desc, walk, 0);
	}

	return ret;
}

static int ctr_aes_encrypt(struct blkcipher_desc *desc,
			   struct scatterlist *dst, struct scatterlist *src,
			   unsigned int nbytes)
{
	struct s390_aes_ctx *sctx = crypto_blkcipher_ctx(desc->tfm);
	struct blkcipher_walk walk;

	if (unlikely(!sctx->fc))
		return fallback_blk_enc(desc, dst, src, nbytes);

	blkcipher_walk_init(&walk, dst, src, nbytes);
	return ctr_aes_crypt(desc, 0, &walk);
}

static int ctr_aes_decrypt(struct blkcipher_desc *desc,
			   struct scatterlist *dst, struct scatterlist *src,
			   unsigned int nbytes)
{
	struct s390_aes_ctx *sctx = crypto_blkcipher_ctx(desc->tfm);
	struct blkcipher_walk walk;

	if (unlikely(!sctx->fc))
		return fallback_blk_dec(desc, dst, src, nbytes);

	blkcipher_walk_init(&walk, dst, src, nbytes);
	return ctr_aes_crypt(desc, CPACF_DECRYPT, &walk);
}

static struct crypto_alg ctr_aes_alg = {
	.cra_name		=	"ctr(aes)",
	.cra_driver_name	=	"ctr-aes-s390",
	.cra_priority		=	402,	/* ecb-aes-s390 + 1 */
	.cra_flags		=	CRYPTO_ALG_TYPE_BLKCIPHER |
					CRYPTO_ALG_NEED_FALLBACK,
	.cra_blocksize		=	1,
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
			.setkey			=	ctr_aes_set_key,
			.encrypt		=	ctr_aes_encrypt,
			.decrypt		=	ctr_aes_decrypt,
		}
	}
};

static int gcm_aes_setkey(struct crypto_aead *tfm, const u8 *key,
			  unsigned int keylen)
{
	struct s390_aes_ctx *ctx = crypto_aead_ctx(tfm);

	switch (keylen) {
	case AES_KEYSIZE_128:
		ctx->fc = CPACF_KMA_GCM_AES_128;
		break;
	case AES_KEYSIZE_192:
		ctx->fc = CPACF_KMA_GCM_AES_192;
		break;
	case AES_KEYSIZE_256:
		ctx->fc = CPACF_KMA_GCM_AES_256;
		break;
	default:
		return -EINVAL;
	}

	memcpy(ctx->key, key, keylen);
	ctx->key_len = keylen;
	return 0;
}

static int gcm_aes_setauthsize(struct crypto_aead *tfm, unsigned int authsize)
{
	switch (authsize) {
	case 4:
	case 8:
	case 12:
	case 13:
	case 14:
	case 15:
	case 16:
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void gcm_sg_walk_start(struct gcm_sg_walk *gw, struct scatterlist *sg,
			      unsigned int len)
{
	memset(gw, 0, sizeof(*gw));
	gw->walk_bytes_remain = len;
	scatterwalk_start(&gw->walk, sg);
}

static int gcm_sg_walk_go(struct gcm_sg_walk *gw, unsigned int minbytesneeded)
{
	int n;

	/* minbytesneeded <= AES_BLOCK_SIZE */
	if (gw->buf_bytes && gw->buf_bytes >= minbytesneeded) {
		gw->ptr = gw->buf;
		gw->nbytes = gw->buf_bytes;
		goto out;
	}

	if (gw->walk_bytes_remain == 0) {
		gw->ptr = NULL;
		gw->nbytes = 0;
		goto out;
	}

	gw->walk_bytes = scatterwalk_clamp(&gw->walk, gw->walk_bytes_remain);
	if (!gw->walk_bytes) {
		scatterwalk_start(&gw->walk, sg_next(gw->walk.sg));
		gw->walk_bytes = scatterwalk_clamp(&gw->walk,
						   gw->walk_bytes_remain);
	}
	gw->walk_ptr = scatterwalk_map(&gw->walk);

	if (!gw->buf_bytes && gw->walk_bytes >= minbytesneeded) {
		gw->ptr = gw->walk_ptr;
		gw->nbytes = gw->walk_bytes;
		goto out;
	}

	while (1) {
		n = min(gw->walk_bytes, AES_BLOCK_SIZE - gw->buf_bytes);
		memcpy(gw->buf + gw->buf_bytes, gw->walk_ptr, n);
		gw->buf_bytes += n;
		gw->walk_bytes_remain -= n;
		scatterwalk_unmap(&gw->walk);
		scatterwalk_advance(&gw->walk, n);
		scatterwalk_done(&gw->walk, 0, gw->walk_bytes_remain);

		if (gw->buf_bytes >= minbytesneeded) {
			gw->ptr = gw->buf;
			gw->nbytes = gw->buf_bytes;
			goto out;
		}

		gw->walk_bytes = scatterwalk_clamp(&gw->walk,
						   gw->walk_bytes_remain);
		if (!gw->walk_bytes) {
			scatterwalk_start(&gw->walk, sg_next(gw->walk.sg));
			gw->walk_bytes = scatterwalk_clamp(&gw->walk,
							gw->walk_bytes_remain);
		}
		gw->walk_ptr = scatterwalk_map(&gw->walk);
	}

out:
	return gw->nbytes;
}

static void gcm_sg_walk_done(struct gcm_sg_walk *gw, unsigned int bytesdone)
{
	int n;

	if (gw->ptr == NULL)
		return;

	if (gw->ptr == gw->buf) {
		n = gw->buf_bytes - bytesdone;
		if (n > 0) {
			memmove(gw->buf, gw->buf + bytesdone, n);
			gw->buf_bytes -= n;
		} else
			gw->buf_bytes = 0;
	} else {
		gw->walk_bytes_remain -= bytesdone;
		scatterwalk_unmap(&gw->walk);
		scatterwalk_advance(&gw->walk, bytesdone);
		scatterwalk_done(&gw->walk, 0, gw->walk_bytes_remain);
	}
}

static int gcm_aes_crypt(struct aead_request *req, unsigned int flags)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct s390_aes_ctx *ctx = crypto_aead_ctx(tfm);
	unsigned int ivsize = crypto_aead_ivsize(tfm);
	unsigned int taglen = crypto_aead_authsize(tfm);
	unsigned int aadlen = req->assoclen;
	unsigned int pclen = req->cryptlen;
	int ret = 0;

	unsigned int len, in_bytes, out_bytes,
		     min_bytes, bytes, aad_bytes, pc_bytes;
	struct gcm_sg_walk gw_in, gw_out;
	u8 tag[GHASH_DIGEST_SIZE];

	struct {
		u32 _[3];		/* reserved */
		u32 cv;			/* Counter Value */
		u8 t[GHASH_DIGEST_SIZE];/* Tag */
		u8 h[AES_BLOCK_SIZE];	/* Hash-subkey */
		u64 taadl;		/* Total AAD Length */
		u64 tpcl;		/* Total Plain-/Cipher-text Length */
		u8 j0[GHASH_BLOCK_SIZE];/* initial counter value */
		u8 k[AES_MAX_KEY_SIZE];	/* Key */
	} param;

	/*
	 * encrypt
	 *   req->src: aad||plaintext
	 *   req->dst: aad||ciphertext||tag
	 * decrypt
	 *   req->src: aad||ciphertext||tag
	 *   req->dst: aad||plaintext, return 0 or -EBADMSG
	 * aad, plaintext and ciphertext may be empty.
	 */
	if (flags & CPACF_DECRYPT)
		pclen -= taglen;
	len = aadlen + pclen;

	memset(&param, 0, sizeof(param));
	param.cv = 1;
	param.taadl = aadlen * 8;
	param.tpcl = pclen * 8;
	memcpy(param.j0, req->iv, ivsize);
	*(u32 *)(param.j0 + ivsize) = 1;
	memcpy(param.k, ctx->key, ctx->key_len);

	gcm_sg_walk_start(&gw_in, req->src, len);
	gcm_sg_walk_start(&gw_out, req->dst, len);

	do {
		min_bytes = min_t(unsigned int,
				  aadlen > 0 ? aadlen : pclen, AES_BLOCK_SIZE);
		in_bytes = gcm_sg_walk_go(&gw_in, min_bytes);
		out_bytes = gcm_sg_walk_go(&gw_out, min_bytes);
		bytes = min(in_bytes, out_bytes);

		if (aadlen + pclen <= bytes) {
			aad_bytes = aadlen;
			pc_bytes = pclen;
			flags |= CPACF_KMA_LAAD | CPACF_KMA_LPC;
		} else {
			if (aadlen <= bytes) {
				aad_bytes = aadlen;
				pc_bytes = (bytes - aadlen) &
					   ~(AES_BLOCK_SIZE - 1);
				flags |= CPACF_KMA_LAAD;
			} else {
				aad_bytes = bytes & ~(AES_BLOCK_SIZE - 1);
				pc_bytes = 0;
			}
		}

		if (aad_bytes > 0)
			memcpy(gw_out.ptr, gw_in.ptr, aad_bytes);

		cpacf_kma(ctx->fc | flags, &param,
			  gw_out.ptr + aad_bytes,
			  gw_in.ptr + aad_bytes, pc_bytes,
			  gw_in.ptr, aad_bytes);

		gcm_sg_walk_done(&gw_in, aad_bytes + pc_bytes);
		gcm_sg_walk_done(&gw_out, aad_bytes + pc_bytes);
		aadlen -= aad_bytes;
		pclen -= pc_bytes;
	} while (aadlen + pclen > 0);

	if (flags & CPACF_DECRYPT) {
		scatterwalk_map_and_copy(tag, req->src, len, taglen, 0);
		if (crypto_memneq(tag, param.t, taglen))
			ret = -EBADMSG;
	} else
		scatterwalk_map_and_copy(param.t, req->dst, len, taglen, 1);

	memzero_explicit(&param, sizeof(param));
	return ret;
}

static int gcm_aes_encrypt(struct aead_request *req)
{
	return gcm_aes_crypt(req, CPACF_ENCRYPT);
}

static int gcm_aes_decrypt(struct aead_request *req)
{
	return gcm_aes_crypt(req, CPACF_DECRYPT);
}

static struct aead_alg gcm_aes_aead = {
	.setkey			= gcm_aes_setkey,
	.setauthsize		= gcm_aes_setauthsize,
	.encrypt		= gcm_aes_encrypt,
	.decrypt		= gcm_aes_decrypt,

	.ivsize			= GHASH_BLOCK_SIZE - sizeof(u32),
	.maxauthsize		= GHASH_DIGEST_SIZE,
	.chunksize		= AES_BLOCK_SIZE,

	.base			= {
		.cra_flags		= CRYPTO_ALG_TYPE_AEAD,
		.cra_blocksize		= 1,
		.cra_ctxsize		= sizeof(struct s390_aes_ctx),
		.cra_priority		= 900,
		.cra_name		= "gcm(aes)",
		.cra_driver_name	= "gcm-aes-s390",
		.cra_module		= THIS_MODULE,
	},
};

static struct crypto_alg *aes_s390_algs_ptr[5];
static int aes_s390_algs_num;
static struct aead_alg *aes_s390_aead_alg;

static int aes_s390_register_alg(struct crypto_alg *alg)
{
	int ret;

	ret = crypto_register_alg(alg);
	if (!ret)
		aes_s390_algs_ptr[aes_s390_algs_num++] = alg;
	return ret;
}

static void aes_s390_fini(void)
{
	while (aes_s390_algs_num--)
		crypto_unregister_alg(aes_s390_algs_ptr[aes_s390_algs_num]);
	if (ctrblk)
		free_page((unsigned long) ctrblk);

	if (aes_s390_aead_alg)
		crypto_unregister_aead(aes_s390_aead_alg);
}

static int __init aes_s390_init(void)
{
	int ret;

	/* Query available functions for KM, KMC, KMCTR and KMA */
	cpacf_query(CPACF_KM, &km_functions);
	cpacf_query(CPACF_KMC, &kmc_functions);
	cpacf_query(CPACF_KMCTR, &kmctr_functions);
	cpacf_query(CPACF_KMA, &kma_functions);

	if (cpacf_test_func(&km_functions, CPACF_KM_AES_128) ||
	    cpacf_test_func(&km_functions, CPACF_KM_AES_192) ||
	    cpacf_test_func(&km_functions, CPACF_KM_AES_256)) {
		ret = aes_s390_register_alg(&aes_alg);
		if (ret)
			goto out_err;
		ret = aes_s390_register_alg(&ecb_aes_alg);
		if (ret)
			goto out_err;
	}

	if (cpacf_test_func(&kmc_functions, CPACF_KMC_AES_128) ||
	    cpacf_test_func(&kmc_functions, CPACF_KMC_AES_192) ||
	    cpacf_test_func(&kmc_functions, CPACF_KMC_AES_256)) {
		ret = aes_s390_register_alg(&cbc_aes_alg);
		if (ret)
			goto out_err;
	}

	if (cpacf_test_func(&km_functions, CPACF_KM_XTS_128) ||
	    cpacf_test_func(&km_functions, CPACF_KM_XTS_256)) {
		ret = aes_s390_register_alg(&xts_aes_alg);
		if (ret)
			goto out_err;
	}

	if (cpacf_test_func(&kmctr_functions, CPACF_KMCTR_AES_128) ||
	    cpacf_test_func(&kmctr_functions, CPACF_KMCTR_AES_192) ||
	    cpacf_test_func(&kmctr_functions, CPACF_KMCTR_AES_256)) {
		ctrblk = (u8 *) __get_free_page(GFP_KERNEL);
		if (!ctrblk) {
			ret = -ENOMEM;
			goto out_err;
		}
		ret = aes_s390_register_alg(&ctr_aes_alg);
		if (ret)
			goto out_err;
	}

	if (cpacf_test_func(&kma_functions, CPACF_KMA_GCM_AES_128) ||
	    cpacf_test_func(&kma_functions, CPACF_KMA_GCM_AES_192) ||
	    cpacf_test_func(&kma_functions, CPACF_KMA_GCM_AES_256)) {
		ret = crypto_register_aead(&gcm_aes_aead);
		if (ret)
			goto out_err;
		aes_s390_aead_alg = &gcm_aes_aead;
	}

	return 0;
out_err:
	aes_s390_fini();
	return ret;
}

module_cpu_feature_match(MSA, aes_s390_init);
module_exit(aes_s390_fini);

MODULE_ALIAS_CRYPTO("aes-all");

MODULE_DESCRIPTION("Rijndael (AES) Cipher Algorithm");
MODULE_LICENSE("GPL");
