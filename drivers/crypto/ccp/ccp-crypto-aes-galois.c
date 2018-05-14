/*
 * AMD Cryptographic Coprocessor (CCP) AES GCM crypto API support
 *
 * Copyright (C) 2016,2017 Advanced Micro Devices, Inc.
 *
 * Author: Gary R Hook <gary.hook@amd.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/scatterlist.h>
#include <linux/crypto.h>
#include <crypto/internal/aead.h>
#include <crypto/algapi.h>
#include <crypto/aes.h>
#include <crypto/ctr.h>
#include <crypto/gcm.h>
#include <crypto/scatterwalk.h>

#include "ccp-crypto.h"

static int ccp_aes_gcm_complete(struct crypto_async_request *async_req, int ret)
{
	return ret;
}

static int ccp_aes_gcm_setkey(struct crypto_aead *tfm, const u8 *key,
			      unsigned int key_len)
{
	struct ccp_ctx *ctx = crypto_aead_ctx(tfm);

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
		crypto_aead_set_flags(tfm, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}

	ctx->u.aes.mode = CCP_AES_MODE_GCM;
	ctx->u.aes.key_len = key_len;

	memcpy(ctx->u.aes.key, key, key_len);
	sg_init_one(&ctx->u.aes.key_sg, ctx->u.aes.key, key_len);

	return 0;
}

static int ccp_aes_gcm_setauthsize(struct crypto_aead *tfm,
				   unsigned int authsize)
{
	return 0;
}

static int ccp_aes_gcm_crypt(struct aead_request *req, bool encrypt)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct ccp_ctx *ctx = crypto_aead_ctx(tfm);
	struct ccp_aes_req_ctx *rctx = aead_request_ctx(req);
	struct scatterlist *iv_sg = NULL;
	unsigned int iv_len = 0;
	int i;
	int ret = 0;

	if (!ctx->u.aes.key_len)
		return -EINVAL;

	if (ctx->u.aes.mode != CCP_AES_MODE_GCM)
		return -EINVAL;

	if (!req->iv)
		return -EINVAL;

	/*
	 * 5 parts:
	 *   plaintext/ciphertext input
	 *   AAD
	 *   key
	 *   IV
	 *   Destination+tag buffer
	 */

	/* Prepare the IV: 12 bytes + an integer (counter) */
	memcpy(rctx->iv, req->iv, GCM_AES_IV_SIZE);
	for (i = 0; i < 3; i++)
		rctx->iv[i + GCM_AES_IV_SIZE] = 0;
	rctx->iv[AES_BLOCK_SIZE - 1] = 1;

	/* Set up a scatterlist for the IV */
	iv_sg = &rctx->iv_sg;
	iv_len = AES_BLOCK_SIZE;
	sg_init_one(iv_sg, rctx->iv, iv_len);

	/* The AAD + plaintext are concatenated in the src buffer */
	memset(&rctx->cmd, 0, sizeof(rctx->cmd));
	INIT_LIST_HEAD(&rctx->cmd.entry);
	rctx->cmd.engine = CCP_ENGINE_AES;
	rctx->cmd.u.aes.type = ctx->u.aes.type;
	rctx->cmd.u.aes.mode = ctx->u.aes.mode;
	rctx->cmd.u.aes.action = encrypt;
	rctx->cmd.u.aes.key = &ctx->u.aes.key_sg;
	rctx->cmd.u.aes.key_len = ctx->u.aes.key_len;
	rctx->cmd.u.aes.iv = iv_sg;
	rctx->cmd.u.aes.iv_len = iv_len;
	rctx->cmd.u.aes.src = req->src;
	rctx->cmd.u.aes.src_len = req->cryptlen;
	rctx->cmd.u.aes.aad_len = req->assoclen;

	/* The cipher text + the tag are in the dst buffer */
	rctx->cmd.u.aes.dst = req->dst;

	ret = ccp_crypto_enqueue_request(&req->base, &rctx->cmd);

	return ret;
}

static int ccp_aes_gcm_encrypt(struct aead_request *req)
{
	return ccp_aes_gcm_crypt(req, CCP_AES_ACTION_ENCRYPT);
}

static int ccp_aes_gcm_decrypt(struct aead_request *req)
{
	return ccp_aes_gcm_crypt(req, CCP_AES_ACTION_DECRYPT);
}

static int ccp_aes_gcm_cra_init(struct crypto_aead *tfm)
{
	struct ccp_ctx *ctx = crypto_aead_ctx(tfm);

	ctx->complete = ccp_aes_gcm_complete;
	ctx->u.aes.key_len = 0;

	crypto_aead_set_reqsize(tfm, sizeof(struct ccp_aes_req_ctx));

	return 0;
}

static void ccp_aes_gcm_cra_exit(struct crypto_tfm *tfm)
{
}

static struct aead_alg ccp_aes_gcm_defaults = {
	.setkey = ccp_aes_gcm_setkey,
	.setauthsize = ccp_aes_gcm_setauthsize,
	.encrypt = ccp_aes_gcm_encrypt,
	.decrypt = ccp_aes_gcm_decrypt,
	.init = ccp_aes_gcm_cra_init,
	.ivsize = GCM_AES_IV_SIZE,
	.maxauthsize = AES_BLOCK_SIZE,
	.base = {
		.cra_flags	= CRYPTO_ALG_TYPE_ABLKCIPHER |
				  CRYPTO_ALG_ASYNC |
				  CRYPTO_ALG_KERN_DRIVER_ONLY |
				  CRYPTO_ALG_NEED_FALLBACK,
		.cra_blocksize	= AES_BLOCK_SIZE,
		.cra_ctxsize	= sizeof(struct ccp_ctx),
		.cra_priority	= CCP_CRA_PRIORITY,
		.cra_type	= &crypto_ablkcipher_type,
		.cra_exit	= ccp_aes_gcm_cra_exit,
		.cra_module	= THIS_MODULE,
	},
};

struct ccp_aes_aead_def {
	enum ccp_aes_mode mode;
	unsigned int version;
	const char *name;
	const char *driver_name;
	unsigned int blocksize;
	unsigned int ivsize;
	struct aead_alg *alg_defaults;
};

static struct ccp_aes_aead_def aes_aead_algs[] = {
	{
		.mode		= CCP_AES_MODE_GHASH,
		.version	= CCP_VERSION(5, 0),
		.name		= "gcm(aes)",
		.driver_name	= "gcm-aes-ccp",
		.blocksize	= 1,
		.ivsize		= AES_BLOCK_SIZE,
		.alg_defaults	= &ccp_aes_gcm_defaults,
	},
};

static int ccp_register_aes_aead(struct list_head *head,
				 const struct ccp_aes_aead_def *def)
{
	struct ccp_crypto_aead *ccp_aead;
	struct aead_alg *alg;
	int ret;

	ccp_aead = kzalloc(sizeof(*ccp_aead), GFP_KERNEL);
	if (!ccp_aead)
		return -ENOMEM;

	INIT_LIST_HEAD(&ccp_aead->entry);

	ccp_aead->mode = def->mode;

	/* Copy the defaults and override as necessary */
	alg = &ccp_aead->alg;
	*alg = *def->alg_defaults;
	snprintf(alg->base.cra_name, CRYPTO_MAX_ALG_NAME, "%s", def->name);
	snprintf(alg->base.cra_driver_name, CRYPTO_MAX_ALG_NAME, "%s",
		 def->driver_name);
	alg->base.cra_blocksize = def->blocksize;
	alg->base.cra_ablkcipher.ivsize = def->ivsize;

	ret = crypto_register_aead(alg);
	if (ret) {
		pr_err("%s ablkcipher algorithm registration error (%d)\n",
		       alg->base.cra_name, ret);
		kfree(ccp_aead);
		return ret;
	}

	list_add(&ccp_aead->entry, head);

	return 0;
}

int ccp_register_aes_aeads(struct list_head *head)
{
	int i, ret;
	unsigned int ccpversion = ccp_version();

	for (i = 0; i < ARRAY_SIZE(aes_aead_algs); i++) {
		if (aes_aead_algs[i].version > ccpversion)
			continue;
		ret = ccp_register_aes_aead(head, &aes_aead_algs[i]);
		if (ret)
			return ret;
	}

	return 0;
}
