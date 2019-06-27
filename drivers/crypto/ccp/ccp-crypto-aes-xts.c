// SPDX-License-Identifier: GPL-2.0
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

static struct ccp_aes_xts_def aes_xts_algs[] = {
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
	struct ablkcipher_request *req = ablkcipher_request_cast(async_req);
	struct ccp_aes_req_ctx *rctx = ablkcipher_request_ctx(req);

	if (ret)
		return ret;

	memcpy(req->info, rctx->iv, AES_BLOCK_SIZE);

	return 0;
}

static int ccp_aes_xts_setkey(struct crypto_ablkcipher *tfm, const u8 *key,
			      unsigned int key_len)
{
	struct crypto_tfm *xfm = crypto_ablkcipher_tfm(tfm);
	struct ccp_ctx *ctx = crypto_tfm_ctx(xfm);
	unsigned int ccpversion = ccp_version();
	int ret;

	ret = xts_check_key(xfm, key, key_len);
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

	return crypto_sync_skcipher_setkey(ctx->u.aes.tfm_skcipher, key, key_len);
}

static int ccp_aes_xts_crypt(struct ablkcipher_request *req,
			     unsigned int encrypt)
{
	struct ccp_ctx *ctx = crypto_tfm_ctx(req->base.tfm);
	struct ccp_aes_req_ctx *rctx = ablkcipher_request_ctx(req);
	unsigned int ccpversion = ccp_version();
	unsigned int fallback = 0;
	unsigned int unit;
	u32 unit_size;
	int ret;

	if (!ctx->u.aes.key_len)
		return -EINVAL;

	if (req->nbytes & (AES_BLOCK_SIZE - 1))
		return -EINVAL;

	if (!req->info)
		return -EINVAL;

	/* Check conditions under which the CCP can fulfill a request. The
	 * device can handle input plaintext of a length that is a multiple
	 * of the unit_size, bug the crypto implementation only supports
	 * the unit_size being equal to the input length. This limits the
	 * number of scenarios we can handle.
	 */
	unit_size = CCP_XTS_AES_UNIT_SIZE__LAST;
	for (unit = 0; unit < ARRAY_SIZE(xts_unit_sizes); unit++) {
		if (req->nbytes == xts_unit_sizes[unit].size) {
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
		SYNC_SKCIPHER_REQUEST_ON_STACK(subreq,
					       ctx->u.aes.tfm_skcipher);

		/* Use the fallback to process the request for any
		 * unsupported unit sizes or key sizes
		 */
		skcipher_request_set_sync_tfm(subreq, ctx->u.aes.tfm_skcipher);
		skcipher_request_set_callback(subreq, req->base.flags,
					      NULL, NULL);
		skcipher_request_set_crypt(subreq, req->src, req->dst,
					   req->nbytes, req->info);
		ret = encrypt ? crypto_skcipher_encrypt(subreq) :
				crypto_skcipher_decrypt(subreq);
		skcipher_request_zero(subreq);
		return ret;
	}

	memcpy(rctx->iv, req->info, AES_BLOCK_SIZE);
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
	rctx->cmd.u.xts.src_len = req->nbytes;
	rctx->cmd.u.xts.dst = req->dst;

	ret = ccp_crypto_enqueue_request(&req->base, &rctx->cmd);

	return ret;
}

static int ccp_aes_xts_encrypt(struct ablkcipher_request *req)
{
	return ccp_aes_xts_crypt(req, 1);
}

static int ccp_aes_xts_decrypt(struct ablkcipher_request *req)
{
	return ccp_aes_xts_crypt(req, 0);
}

static int ccp_aes_xts_cra_init(struct crypto_tfm *tfm)
{
	struct ccp_ctx *ctx = crypto_tfm_ctx(tfm);
	struct crypto_sync_skcipher *fallback_tfm;

	ctx->complete = ccp_aes_xts_complete;
	ctx->u.aes.key_len = 0;

	fallback_tfm = crypto_alloc_sync_skcipher("xts(aes)", 0,
					     CRYPTO_ALG_ASYNC |
					     CRYPTO_ALG_NEED_FALLBACK);
	if (IS_ERR(fallback_tfm)) {
		pr_warn("could not load fallback driver xts(aes)\n");
		return PTR_ERR(fallback_tfm);
	}
	ctx->u.aes.tfm_skcipher = fallback_tfm;

	tfm->crt_ablkcipher.reqsize = sizeof(struct ccp_aes_req_ctx);

	return 0;
}

static void ccp_aes_xts_cra_exit(struct crypto_tfm *tfm)
{
	struct ccp_ctx *ctx = crypto_tfm_ctx(tfm);

	crypto_free_sync_skcipher(ctx->u.aes.tfm_skcipher);
}

static int ccp_register_aes_xts_alg(struct list_head *head,
				    const struct ccp_aes_xts_def *def)
{
	struct ccp_crypto_ablkcipher_alg *ccp_alg;
	struct crypto_alg *alg;
	int ret;

	ccp_alg = kzalloc(sizeof(*ccp_alg), GFP_KERNEL);
	if (!ccp_alg)
		return -ENOMEM;

	INIT_LIST_HEAD(&ccp_alg->entry);

	alg = &ccp_alg->alg;

	snprintf(alg->cra_name, CRYPTO_MAX_ALG_NAME, "%s", def->name);
	snprintf(alg->cra_driver_name, CRYPTO_MAX_ALG_NAME, "%s",
		 def->drv_name);
	alg->cra_flags = CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC |
			 CRYPTO_ALG_KERN_DRIVER_ONLY |
			 CRYPTO_ALG_NEED_FALLBACK;
	alg->cra_blocksize = AES_BLOCK_SIZE;
	alg->cra_ctxsize = sizeof(struct ccp_ctx);
	alg->cra_priority = CCP_CRA_PRIORITY;
	alg->cra_type = &crypto_ablkcipher_type;
	alg->cra_ablkcipher.setkey = ccp_aes_xts_setkey;
	alg->cra_ablkcipher.encrypt = ccp_aes_xts_encrypt;
	alg->cra_ablkcipher.decrypt = ccp_aes_xts_decrypt;
	alg->cra_ablkcipher.min_keysize = AES_MIN_KEY_SIZE * 2;
	alg->cra_ablkcipher.max_keysize = AES_MAX_KEY_SIZE * 2;
	alg->cra_ablkcipher.ivsize = AES_BLOCK_SIZE;
	alg->cra_init = ccp_aes_xts_cra_init;
	alg->cra_exit = ccp_aes_xts_cra_exit;
	alg->cra_module = THIS_MODULE;

	ret = crypto_register_alg(alg);
	if (ret) {
		pr_err("%s ablkcipher algorithm registration error (%d)\n",
		       alg->cra_name, ret);
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
