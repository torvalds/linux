// SPDX-License-Identifier: GPL-2.0-only
/*
 * AMD Cryptographic Coprocessor (CCP) AES crypto API support
 *
 * Copyright (C) 2013-2019 Advanced Micro Devices, Inc.
 *
 * Author: Tom Lendacky <thomas.lendacky@amd.com>
 */

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/scatterlist.h>
#include <linux/crypto.h>
#include <crypto/algapi.h>
#include <crypto/aes.h>
#include <crypto/ctr.h>
#include <crypto/scatterwalk.h>

#include "ccp-crypto.h"

static int ccp_aes_complete(struct crypto_async_request *async_req, int ret)
{
	struct skcipher_request *req = skcipher_request_cast(async_req);
	struct ccp_ctx *ctx = crypto_tfm_ctx(req->base.tfm);
	struct ccp_aes_req_ctx *rctx = skcipher_request_ctx(req);

	if (ret)
		return ret;

	if (ctx->u.aes.mode != CCP_AES_MODE_ECB)
		memcpy(req->iv, rctx->iv, AES_BLOCK_SIZE);

	return 0;
}

static int ccp_aes_setkey(struct crypto_skcipher *tfm, const u8 *key,
			  unsigned int key_len)
{
	struct ccp_crypto_skcipher_alg *alg = ccp_crypto_skcipher_alg(tfm);
	struct ccp_ctx *ctx = crypto_skcipher_ctx(tfm);

	switch (key_len) {
	case AES_KEYSIZE_128:
		ctx->u.aes.type = CCP_AES_TYPE_128;
		break;
	case AES_KEYSIZE_192:
		ctx->u.aes.type = CCP_AES_TYPE_192;
		break;
	case AES_KEYSIZE_256:
		ctx->u.aes.type = CCP_AES_TYPE_256;
		break;
	default:
		return -EINVAL;
	}
	ctx->u.aes.mode = alg->mode;
	ctx->u.aes.key_len = key_len;

	memcpy(ctx->u.aes.key, key, key_len);
	sg_init_one(&ctx->u.aes.key_sg, ctx->u.aes.key, key_len);

	return 0;
}

static int ccp_aes_crypt(struct skcipher_request *req, bool encrypt)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct ccp_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct ccp_aes_req_ctx *rctx = skcipher_request_ctx(req);
	struct scatterlist *iv_sg = NULL;
	unsigned int iv_len = 0;
	int ret;

	if (!ctx->u.aes.key_len)
		return -EINVAL;

	if (((ctx->u.aes.mode == CCP_AES_MODE_ECB) ||
	     (ctx->u.aes.mode == CCP_AES_MODE_CBC)) &&
	    (req->cryptlen & (AES_BLOCK_SIZE - 1)))
		return -EINVAL;

	if (ctx->u.aes.mode != CCP_AES_MODE_ECB) {
		if (!req->iv)
			return -EINVAL;

		memcpy(rctx->iv, req->iv, AES_BLOCK_SIZE);
		iv_sg = &rctx->iv_sg;
		iv_len = AES_BLOCK_SIZE;
		sg_init_one(iv_sg, rctx->iv, iv_len);
	}

	memset(&rctx->cmd, 0, sizeof(rctx->cmd));
	INIT_LIST_HEAD(&rctx->cmd.entry);
	rctx->cmd.engine = CCP_ENGINE_AES;
	rctx->cmd.u.aes.type = ctx->u.aes.type;
	rctx->cmd.u.aes.mode = ctx->u.aes.mode;
	rctx->cmd.u.aes.action =
		(encrypt) ? CCP_AES_ACTION_ENCRYPT : CCP_AES_ACTION_DECRYPT;
	rctx->cmd.u.aes.key = &ctx->u.aes.key_sg;
	rctx->cmd.u.aes.key_len = ctx->u.aes.key_len;
	rctx->cmd.u.aes.iv = iv_sg;
	rctx->cmd.u.aes.iv_len = iv_len;
	rctx->cmd.u.aes.src = req->src;
	rctx->cmd.u.aes.src_len = req->cryptlen;
	rctx->cmd.u.aes.dst = req->dst;

	ret = ccp_crypto_enqueue_request(&req->base, &rctx->cmd);

	return ret;
}

static int ccp_aes_encrypt(struct skcipher_request *req)
{
	return ccp_aes_crypt(req, true);
}

static int ccp_aes_decrypt(struct skcipher_request *req)
{
	return ccp_aes_crypt(req, false);
}

static int ccp_aes_init_tfm(struct crypto_skcipher *tfm)
{
	struct ccp_ctx *ctx = crypto_skcipher_ctx(tfm);

	ctx->complete = ccp_aes_complete;
	ctx->u.aes.key_len = 0;

	crypto_skcipher_set_reqsize(tfm, sizeof(struct ccp_aes_req_ctx));

	return 0;
}

static int ccp_aes_rfc3686_complete(struct crypto_async_request *async_req,
				    int ret)
{
	struct skcipher_request *req = skcipher_request_cast(async_req);
	struct ccp_aes_req_ctx *rctx = skcipher_request_ctx(req);

	/* Restore the original pointer */
	req->iv = rctx->rfc3686_info;

	return ccp_aes_complete(async_req, ret);
}

static int ccp_aes_rfc3686_setkey(struct crypto_skcipher *tfm, const u8 *key,
				  unsigned int key_len)
{
	struct ccp_ctx *ctx = crypto_skcipher_ctx(tfm);

	if (key_len < CTR_RFC3686_NONCE_SIZE)
		return -EINVAL;

	key_len -= CTR_RFC3686_NONCE_SIZE;
	memcpy(ctx->u.aes.nonce, key + key_len, CTR_RFC3686_NONCE_SIZE);

	return ccp_aes_setkey(tfm, key, key_len);
}

static int ccp_aes_rfc3686_crypt(struct skcipher_request *req, bool encrypt)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct ccp_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct ccp_aes_req_ctx *rctx = skcipher_request_ctx(req);
	u8 *iv;

	/* Initialize the CTR block */
	iv = rctx->rfc3686_iv;
	memcpy(iv, ctx->u.aes.nonce, CTR_RFC3686_NONCE_SIZE);

	iv += CTR_RFC3686_NONCE_SIZE;
	memcpy(iv, req->iv, CTR_RFC3686_IV_SIZE);

	iv += CTR_RFC3686_IV_SIZE;
	*(__be32 *)iv = cpu_to_be32(1);

	/* Point to the new IV */
	rctx->rfc3686_info = req->iv;
	req->iv = rctx->rfc3686_iv;

	return ccp_aes_crypt(req, encrypt);
}

static int ccp_aes_rfc3686_encrypt(struct skcipher_request *req)
{
	return ccp_aes_rfc3686_crypt(req, true);
}

static int ccp_aes_rfc3686_decrypt(struct skcipher_request *req)
{
	return ccp_aes_rfc3686_crypt(req, false);
}

static int ccp_aes_rfc3686_init_tfm(struct crypto_skcipher *tfm)
{
	struct ccp_ctx *ctx = crypto_skcipher_ctx(tfm);

	ctx->complete = ccp_aes_rfc3686_complete;
	ctx->u.aes.key_len = 0;

	crypto_skcipher_set_reqsize(tfm, sizeof(struct ccp_aes_req_ctx));

	return 0;
}

static const struct skcipher_alg ccp_aes_defaults = {
	.setkey			= ccp_aes_setkey,
	.encrypt		= ccp_aes_encrypt,
	.decrypt		= ccp_aes_decrypt,
	.min_keysize		= AES_MIN_KEY_SIZE,
	.max_keysize		= AES_MAX_KEY_SIZE,
	.init			= ccp_aes_init_tfm,

	.base.cra_flags		= CRYPTO_ALG_ASYNC |
				  CRYPTO_ALG_ALLOCATES_MEMORY |
				  CRYPTO_ALG_KERN_DRIVER_ONLY |
				  CRYPTO_ALG_NEED_FALLBACK,
	.base.cra_blocksize	= AES_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ccp_ctx),
	.base.cra_priority	= CCP_CRA_PRIORITY,
	.base.cra_module	= THIS_MODULE,
};

static const struct skcipher_alg ccp_aes_rfc3686_defaults = {
	.setkey			= ccp_aes_rfc3686_setkey,
	.encrypt		= ccp_aes_rfc3686_encrypt,
	.decrypt		= ccp_aes_rfc3686_decrypt,
	.min_keysize		= AES_MIN_KEY_SIZE + CTR_RFC3686_NONCE_SIZE,
	.max_keysize		= AES_MAX_KEY_SIZE + CTR_RFC3686_NONCE_SIZE,
	.init			= ccp_aes_rfc3686_init_tfm,

	.base.cra_flags		= CRYPTO_ALG_ASYNC |
				  CRYPTO_ALG_ALLOCATES_MEMORY |
				  CRYPTO_ALG_KERN_DRIVER_ONLY |
				  CRYPTO_ALG_NEED_FALLBACK,
	.base.cra_blocksize	= CTR_RFC3686_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ccp_ctx),
	.base.cra_priority	= CCP_CRA_PRIORITY,
	.base.cra_module	= THIS_MODULE,
};

struct ccp_aes_def {
	enum ccp_aes_mode mode;
	unsigned int version;
	const char *name;
	const char *driver_name;
	unsigned int blocksize;
	unsigned int ivsize;
	const struct skcipher_alg *alg_defaults;
};

static struct ccp_aes_def aes_algs[] = {
	{
		.mode		= CCP_AES_MODE_ECB,
		.version	= CCP_VERSION(3, 0),
		.name		= "ecb(aes)",
		.driver_name	= "ecb-aes-ccp",
		.blocksize	= AES_BLOCK_SIZE,
		.ivsize		= 0,
		.alg_defaults	= &ccp_aes_defaults,
	},
	{
		.mode		= CCP_AES_MODE_CBC,
		.version	= CCP_VERSION(3, 0),
		.name		= "cbc(aes)",
		.driver_name	= "cbc-aes-ccp",
		.blocksize	= AES_BLOCK_SIZE,
		.ivsize		= AES_BLOCK_SIZE,
		.alg_defaults	= &ccp_aes_defaults,
	},
	{
		.mode		= CCP_AES_MODE_CFB,
		.version	= CCP_VERSION(3, 0),
		.name		= "cfb(aes)",
		.driver_name	= "cfb-aes-ccp",
		.blocksize	= 1,
		.ivsize		= AES_BLOCK_SIZE,
		.alg_defaults	= &ccp_aes_defaults,
	},
	{
		.mode		= CCP_AES_MODE_OFB,
		.version	= CCP_VERSION(3, 0),
		.name		= "ofb(aes)",
		.driver_name	= "ofb-aes-ccp",
		.blocksize	= 1,
		.ivsize		= AES_BLOCK_SIZE,
		.alg_defaults	= &ccp_aes_defaults,
	},
	{
		.mode		= CCP_AES_MODE_CTR,
		.version	= CCP_VERSION(3, 0),
		.name		= "ctr(aes)",
		.driver_name	= "ctr-aes-ccp",
		.blocksize	= 1,
		.ivsize		= AES_BLOCK_SIZE,
		.alg_defaults	= &ccp_aes_defaults,
	},
	{
		.mode		= CCP_AES_MODE_CTR,
		.version	= CCP_VERSION(3, 0),
		.name		= "rfc3686(ctr(aes))",
		.driver_name	= "rfc3686-ctr-aes-ccp",
		.blocksize	= 1,
		.ivsize		= CTR_RFC3686_IV_SIZE,
		.alg_defaults	= &ccp_aes_rfc3686_defaults,
	},
};

static int ccp_register_aes_alg(struct list_head *head,
				const struct ccp_aes_def *def)
{
	struct ccp_crypto_skcipher_alg *ccp_alg;
	struct skcipher_alg *alg;
	int ret;

	ccp_alg = kzalloc(sizeof(*ccp_alg), GFP_KERNEL);
	if (!ccp_alg)
		return -ENOMEM;

	INIT_LIST_HEAD(&ccp_alg->entry);

	ccp_alg->mode = def->mode;

	/* Copy the defaults and override as necessary */
	alg = &ccp_alg->alg;
	*alg = *def->alg_defaults;
	snprintf(alg->base.cra_name, CRYPTO_MAX_ALG_NAME, "%s", def->name);
	snprintf(alg->base.cra_driver_name, CRYPTO_MAX_ALG_NAME, "%s",
		 def->driver_name);
	alg->base.cra_blocksize = def->blocksize;
	alg->ivsize = def->ivsize;

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

int ccp_register_aes_algs(struct list_head *head)
{
	int i, ret;
	unsigned int ccpversion = ccp_version();

	for (i = 0; i < ARRAY_SIZE(aes_algs); i++) {
		if (aes_algs[i].version > ccpversion)
			continue;
		ret = ccp_register_aes_alg(head, &aes_algs[i]);
		if (ret)
			return ret;
	}

	return 0;
}
