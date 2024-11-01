// SPDX-License-Identifier: GPL-2.0-only
/*
 * AMD Cryptographic Coprocessor (CCP) AES XTS crypto API support
 *
 * Copyright (C) 2013,2017 Advanced Micro Devices, Inc.
 *
 * Author: Gary R Hook <gary.hook@amd.com>
 * Author: Tom Lendacky <thomas.lendacky@amd.com>
 */

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/scatterlist.h>
#include <crypto/aes.h>
#include <crypto/xts.h>
#include <crypto/internal/skcipher.h>
#include <crypto/scatterwalk.h>

#include "ccp-crypto.h"

struct ccp_aes_xts_def {
	const char *name;
	const char *drv_name;
};

static const struct ccp_aes_xts_def aes_xts_algs[] = {
	{
		.name		= "xts(aes)",
		.drv_name	= "xts-aes-ccp",
	},
};

struct ccp_unit_size_map {
	unsigned int size;
	u32 value;
};

static struct ccp_unit_size_map xts_unit_sizes[] = {
	{
		.size   = 16,
		.value	= CCP_XTS_AES_UNIT_SIZE_16,
	},
	{
		.size   = 512,
		.value	= CCP_XTS_AES_UNIT_SIZE_512,
	},
	{
		.size   = 1024,
		.value	= CCP_XTS_AES_UNIT_SIZE_1024,
	},
	{
		.size   = 2048,
		.value	= CCP_XTS_AES_UNIT_SIZE_2048,
	},
	{
		.size   = 4096,
		.value	= CCP_XTS_AES_UNIT_SIZE_4096,
	},
};

static int ccp_aes_xts_complete(struct crypto_async_request *async_req, int ret)
{
	struct skcipher_request *req = skcipher_request_cast(async_req);
	struct ccp_aes_req_ctx *rctx = skcipher_request_ctx(req);

	if (ret)
		return ret;

	memcpy(req->iv, rctx->iv, AES_BLOCK_SIZE);

	return 0;
}

static int ccp_aes_xts_setkey(struct crypto_skcipher *tfm, const u8 *key,
			      unsigned int key_len)
{
	struct ccp_ctx *ctx = crypto_skcipher_ctx(tfm);
	unsigned int ccpversion = ccp_version();
	int ret;

	ret = xts_verify_key(tfm, key, key_len);
	if (ret)
		return ret;

	/* Version 3 devices support 128-bit keys; version 5 devices can
	 * accommodate 128- and 256-bit keys.
	 */
	switch (key_len) {
	case AES_KEYSIZE_128 * 2:
		memcpy(ctx->u.aes.key, key, key_len);
		break;
	case AES_KEYSIZE_256 * 2:
		if (ccpversion > CCP_VERSION(3, 0))
			memcpy(ctx->u.aes.key, key, key_len);
		break;
	}
	ctx->u.aes.key_len = key_len / 2;
	sg_init_one(&ctx->u.aes.key_sg, ctx->u.aes.key, key_len);

	return crypto_skcipher_setkey(ctx->u.aes.tfm_skcipher, key, key_len);
}

static int ccp_aes_xts_crypt(struct skcipher_request *req,
			     unsigned int encrypt)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct ccp_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct ccp_aes_req_ctx *rctx = skcipher_request_ctx(req);
	unsigned int ccpversion = ccp_version();
	unsigned int fallback = 0;
	unsigned int unit;
	u32 unit_size;
	int ret;

	if (!ctx->u.aes.key_len)
		return -EINVAL;

	if (!req->iv)
		return -EINVAL;

	/* Check conditions under which the CCP can fulfill a request. The
	 * device can handle input plaintext of a length that is a multiple
	 * of the unit_size, bug the crypto implementation only supports
	 * the unit_size being equal to the input length. This limits the
	 * number of scenarios we can handle.
	 */
	unit_size = CCP_XTS_AES_UNIT_SIZE__LAST;
	for (unit = 0; unit < ARRAY_SIZE(xts_unit_sizes); unit++) {
		if (req->cryptlen == xts_unit_sizes[unit].size) {
			unit_size = unit;
			break;
		}
	}
	/* The CCP has restrictions on block sizes. Also, a version 3 device
	 * only supports AES-128 operations; version 5 CCPs support both
	 * AES-128 and -256 operations.
	 */
	if (unit_size == CCP_XTS_AES_UNIT_SIZE__LAST)
		fallback = 1;
	if ((ccpversion < CCP_VERSION(5, 0)) &&
	    (ctx->u.aes.key_len != AES_KEYSIZE_128))
		fallback = 1;
	if ((ctx->u.aes.key_len != AES_KEYSIZE_128) &&
	    (ctx->u.aes.key_len != AES_KEYSIZE_256))
		fallback = 1;
	if (fallback) {
		/* Use the fallback to process the request for any
		 * unsupported unit sizes or key sizes
		 */
		skcipher_request_set_tfm(&rctx->fallback_req,
					 ctx->u.aes.tfm_skcipher);
		skcipher_request_set_callback(&rctx->fallback_req,
					      req->base.flags,
					      req->base.complete,
					      req->base.data);
		skcipher_request_set_crypt(&rctx->fallback_req, req->src,
					   req->dst, req->cryptlen, req->iv);
		ret = encrypt ? crypto_skcipher_encrypt(&rctx->fallback_req) :
				crypto_skcipher_decrypt(&rctx->fallback_req);
		return ret;
	}

	memcpy(rctx->iv, req->iv, AES_BLOCK_SIZE);
	sg_init_one(&rctx->iv_sg, rctx->iv, AES_BLOCK_SIZE);

	memset(&rctx->cmd, 0, sizeof(rctx->cmd));
	INIT_LIST_HEAD(&rctx->cmd.entry);
	rctx->cmd.engine = CCP_ENGINE_XTS_AES_128;
	rctx->cmd.u.xts.type = CCP_AES_TYPE_128;
	rctx->cmd.u.xts.action = (encrypt) ? CCP_AES_ACTION_ENCRYPT
					   : CCP_AES_ACTION_DECRYPT;
	rctx->cmd.u.xts.unit_size = unit_size;
	rctx->cmd.u.xts.key = &ctx->u.aes.key_sg;
	rctx->cmd.u.xts.key_len = ctx->u.aes.key_len;
	rctx->cmd.u.xts.iv = &rctx->iv_sg;
	rctx->cmd.u.xts.iv_len = AES_BLOCK_SIZE;
	rctx->cmd.u.xts.src = req->src;
	rctx->cmd.u.xts.src_len = req->cryptlen;
	rctx->cmd.u.xts.dst = req->dst;

	ret = ccp_crypto_enqueue_request(&req->base, &rctx->cmd);

	return ret;
}

static int ccp_aes_xts_encrypt(struct skcipher_request *req)
{
	return ccp_aes_xts_crypt(req, 1);
}

static int ccp_aes_xts_decrypt(struct skcipher_request *req)
{
	return ccp_aes_xts_crypt(req, 0);
}

static int ccp_aes_xts_init_tfm(struct crypto_skcipher *tfm)
{
	struct ccp_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct crypto_skcipher *fallback_tfm;

	ctx->complete = ccp_aes_xts_complete;
	ctx->u.aes.key_len = 0;

	fallback_tfm = crypto_alloc_skcipher("xts(aes)", 0,
					     CRYPTO_ALG_NEED_FALLBACK);
	if (IS_ERR(fallback_tfm)) {
		pr_warn("could not load fallback driver xts(aes)\n");
		return PTR_ERR(fallback_tfm);
	}
	ctx->u.aes.tfm_skcipher = fallback_tfm;

	crypto_skcipher_set_reqsize(tfm, sizeof(struct ccp_aes_req_ctx) +
					 crypto_skcipher_reqsize(fallback_tfm));

	return 0;
}

static void ccp_aes_xts_exit_tfm(struct crypto_skcipher *tfm)
{
	struct ccp_ctx *ctx = crypto_skcipher_ctx(tfm);

	crypto_free_skcipher(ctx->u.aes.tfm_skcipher);
}

static int ccp_register_aes_xts_alg(struct list_head *head,
				    const struct ccp_aes_xts_def *def)
{
	struct ccp_crypto_skcipher_alg *ccp_alg;
	struct skcipher_alg *alg;
	int ret;

	ccp_alg = kzalloc(sizeof(*ccp_alg), GFP_KERNEL);
	if (!ccp_alg)
		return -ENOMEM;

	INIT_LIST_HEAD(&ccp_alg->entry);

	alg = &ccp_alg->alg;

	snprintf(alg->base.cra_name, CRYPTO_MAX_ALG_NAME, "%s", def->name);
	snprintf(alg->base.cra_driver_name, CRYPTO_MAX_ALG_NAME, "%s",
		 def->drv_name);
	alg->base.cra_flags	= CRYPTO_ALG_ASYNC |
				  CRYPTO_ALG_ALLOCATES_MEMORY |
				  CRYPTO_ALG_KERN_DRIVER_ONLY |
				  CRYPTO_ALG_NEED_FALLBACK;
	alg->base.cra_blocksize	= AES_BLOCK_SIZE;
	alg->base.cra_ctxsize	= sizeof(struct ccp_ctx);
	alg->base.cra_priority	= CCP_CRA_PRIORITY;
	alg->base.cra_module	= THIS_MODULE;

	alg->setkey		= ccp_aes_xts_setkey;
	alg->encrypt		= ccp_aes_xts_encrypt;
	alg->decrypt		= ccp_aes_xts_decrypt;
	alg->min_keysize	= AES_MIN_KEY_SIZE * 2;
	alg->max_keysize	= AES_MAX_KEY_SIZE * 2;
	alg->ivsize		= AES_BLOCK_SIZE;
	alg->init		= ccp_aes_xts_init_tfm;
	alg->exit		= ccp_aes_xts_exit_tfm;

	ret = crypto_register_skcipher(alg);
	if (ret) {
		pr_err("%s skcipher algorithm registration error (%d)\n",
		       alg->base.cra_name, ret);
		kfree(ccp_alg);
		return ret;
	}

	list_add(&ccp_alg->entry, head);

	return 0;
}

int ccp_register_aes_xts_algs(struct list_head *head)
{
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(aes_xts_algs); i++) {
		ret = ccp_register_aes_xts_alg(head, &aes_xts_algs[i]);
		if (ret)
			return ret;
	}

	return 0;
}
