/*
 * AMD Cryptographic Coprocessor (CCP) RSA crypto API support
 *
 * Copyright (C) 2017 Advanced Micro Devices, Inc.
 *
 * Author: Gary R Hook <gary.hook@amd.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/scatterlist.h>
#include <linux/crypto.h>
#include <crypto/algapi.h>
#include <crypto/internal/rsa.h>
#include <crypto/internal/akcipher.h>
#include <crypto/akcipher.h>
#include <crypto/scatterwalk.h>

#include "ccp-crypto.h"

static inline struct akcipher_request *akcipher_request_cast(
	struct crypto_async_request *req)
{
	return container_of(req, struct akcipher_request, base);
}

static inline int ccp_copy_and_save_keypart(u8 **kpbuf, unsigned int *kplen,
					    const u8 *buf, size_t sz)
{
	int nskip;

	for (nskip = 0; nskip < sz; nskip++)
		if (buf[nskip])
			break;
	*kplen = sz - nskip;
	*kpbuf = kmemdup(buf + nskip, *kplen, GFP_KERNEL);
	if (!*kpbuf)
		return -ENOMEM;

	return 0;
}

static int ccp_rsa_complete(struct crypto_async_request *async_req, int ret)
{
	struct akcipher_request *req = akcipher_request_cast(async_req);
	struct ccp_rsa_req_ctx *rctx = akcipher_request_ctx(req);

	if (ret)
		return ret;

	req->dst_len = rctx->cmd.u.rsa.key_size >> 3;

	return 0;
}

static unsigned int ccp_rsa_maxsize(struct crypto_akcipher *tfm)
{
	struct ccp_ctx *ctx = akcipher_tfm_ctx(tfm);

	return ctx->u.rsa.n_len;
}

static int ccp_rsa_crypt(struct akcipher_request *req, bool encrypt)
{
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	struct ccp_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct ccp_rsa_req_ctx *rctx = akcipher_request_ctx(req);
	int ret = 0;

	memset(&rctx->cmd, 0, sizeof(rctx->cmd));
	INIT_LIST_HEAD(&rctx->cmd.entry);
	rctx->cmd.engine = CCP_ENGINE_RSA;

	rctx->cmd.u.rsa.key_size = ctx->u.rsa.key_len; /* in bits */
	if (encrypt) {
		rctx->cmd.u.rsa.exp = &ctx->u.rsa.e_sg;
		rctx->cmd.u.rsa.exp_len = ctx->u.rsa.e_len;
	} else {
		rctx->cmd.u.rsa.exp = &ctx->u.rsa.d_sg;
		rctx->cmd.u.rsa.exp_len = ctx->u.rsa.d_len;
	}
	rctx->cmd.u.rsa.mod = &ctx->u.rsa.n_sg;
	rctx->cmd.u.rsa.mod_len = ctx->u.rsa.n_len;
	rctx->cmd.u.rsa.src = req->src;
	rctx->cmd.u.rsa.src_len = req->src_len;
	rctx->cmd.u.rsa.dst = req->dst;

	ret = ccp_crypto_enqueue_request(&req->base, &rctx->cmd);

	return ret;
}

static int ccp_rsa_encrypt(struct akcipher_request *req)
{
	return ccp_rsa_crypt(req, true);
}

static int ccp_rsa_decrypt(struct akcipher_request *req)
{
	return ccp_rsa_crypt(req, false);
}

static int ccp_check_key_length(unsigned int len)
{
	/* In bits */
	if (len < 8 || len > 4096)
		return -EINVAL;
	return 0;
}

static void ccp_rsa_free_key_bufs(struct ccp_ctx *ctx)
{
	/* Clean up old key data */
	kzfree(ctx->u.rsa.e_buf);
	ctx->u.rsa.e_buf = NULL;
	ctx->u.rsa.e_len = 0;
	kzfree(ctx->u.rsa.n_buf);
	ctx->u.rsa.n_buf = NULL;
	ctx->u.rsa.n_len = 0;
	kzfree(ctx->u.rsa.d_buf);
	ctx->u.rsa.d_buf = NULL;
	ctx->u.rsa.d_len = 0;
}

static int ccp_rsa_setkey(struct crypto_akcipher *tfm, const void *key,
			  unsigned int keylen, bool private)
{
	struct ccp_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct rsa_key raw_key;
	int ret;

	ccp_rsa_free_key_bufs(ctx);
	memset(&raw_key, 0, sizeof(raw_key));

	/* Code borrowed from crypto/rsa.c */
	if (private)
		ret = rsa_parse_priv_key(&raw_key, key, keylen);
	else
		ret = rsa_parse_pub_key(&raw_key, key, keylen);
	if (ret)
		goto n_key;

	ret = ccp_copy_and_save_keypart(&ctx->u.rsa.n_buf, &ctx->u.rsa.n_len,
					raw_key.n, raw_key.n_sz);
	if (ret)
		goto key_err;
	sg_init_one(&ctx->u.rsa.n_sg, ctx->u.rsa.n_buf, ctx->u.rsa.n_len);

	ctx->u.rsa.key_len = ctx->u.rsa.n_len << 3; /* convert to bits */
	if (ccp_check_key_length(ctx->u.rsa.key_len)) {
		ret = -EINVAL;
		goto key_err;
	}

	ret = ccp_copy_and_save_keypart(&ctx->u.rsa.e_buf, &ctx->u.rsa.e_len,
					raw_key.e, raw_key.e_sz);
	if (ret)
		goto key_err;
	sg_init_one(&ctx->u.rsa.e_sg, ctx->u.rsa.e_buf, ctx->u.rsa.e_len);

	if (private) {
		ret = ccp_copy_and_save_keypart(&ctx->u.rsa.d_buf,
						&ctx->u.rsa.d_len,
						raw_key.d, raw_key.d_sz);
		if (ret)
			goto key_err;
		sg_init_one(&ctx->u.rsa.d_sg,
			    ctx->u.rsa.d_buf, ctx->u.rsa.d_len);
	}

	return 0;

key_err:
	ccp_rsa_free_key_bufs(ctx);

n_key:
	return ret;
}

static int ccp_rsa_setprivkey(struct crypto_akcipher *tfm, const void *key,
			      unsigned int keylen)
{
	return ccp_rsa_setkey(tfm, key, keylen, true);
}

static int ccp_rsa_setpubkey(struct crypto_akcipher *tfm, const void *key,
			     unsigned int keylen)
{
	return ccp_rsa_setkey(tfm, key, keylen, false);
}

static int ccp_rsa_init_tfm(struct crypto_akcipher *tfm)
{
	struct ccp_ctx *ctx = akcipher_tfm_ctx(tfm);

	akcipher_set_reqsize(tfm, sizeof(struct ccp_rsa_req_ctx));
	ctx->complete = ccp_rsa_complete;

	return 0;
}

static void ccp_rsa_exit_tfm(struct crypto_akcipher *tfm)
{
	struct ccp_ctx *ctx = crypto_tfm_ctx(&tfm->base);

	ccp_rsa_free_key_bufs(ctx);
}

static struct akcipher_alg ccp_rsa_defaults = {
	.encrypt = ccp_rsa_encrypt,
	.decrypt = ccp_rsa_decrypt,
	.sign = ccp_rsa_decrypt,
	.verify = ccp_rsa_encrypt,
	.set_pub_key = ccp_rsa_setpubkey,
	.set_priv_key = ccp_rsa_setprivkey,
	.max_size = ccp_rsa_maxsize,
	.init = ccp_rsa_init_tfm,
	.exit = ccp_rsa_exit_tfm,
	.base = {
		.cra_name = "rsa",
		.cra_driver_name = "rsa-ccp",
		.cra_priority = CCP_CRA_PRIORITY,
		.cra_module = THIS_MODULE,
		.cra_ctxsize = 2 * sizeof(struct ccp_ctx),
	},
};

struct ccp_rsa_def {
	unsigned int version;
	const char *name;
	const char *driver_name;
	unsigned int reqsize;
	struct akcipher_alg *alg_defaults;
};

static struct ccp_rsa_def rsa_algs[] = {
	{
		.version	= CCP_VERSION(3, 0),
		.name		= "rsa",
		.driver_name	= "rsa-ccp",
		.reqsize	= sizeof(struct ccp_rsa_req_ctx),
		.alg_defaults	= &ccp_rsa_defaults,
	}
};

static int ccp_register_rsa_alg(struct list_head *head,
			        const struct ccp_rsa_def *def)
{
	struct ccp_crypto_akcipher_alg *ccp_alg;
	struct akcipher_alg *alg;
	int ret;

	ccp_alg = kzalloc(sizeof(*ccp_alg), GFP_KERNEL);
	if (!ccp_alg)
		return -ENOMEM;

	INIT_LIST_HEAD(&ccp_alg->entry);

	alg = &ccp_alg->alg;
	*alg = *def->alg_defaults;
	snprintf(alg->base.cra_name, CRYPTO_MAX_ALG_NAME, "%s", def->name);
	snprintf(alg->base.cra_driver_name, CRYPTO_MAX_ALG_NAME, "%s",
		 def->driver_name);
	ret = crypto_register_akcipher(alg);
	if (ret) {
		pr_err("%s akcipher algorithm registration error (%d)\n",
		       alg->base.cra_name, ret);
		kfree(ccp_alg);
		return ret;
	}

	list_add(&ccp_alg->entry, head);

	return 0;
}

int ccp_register_rsa_algs(struct list_head *head)
{
	int i, ret;
	unsigned int ccpversion = ccp_version();

	/* Register the RSA algorithm in standard mode
	 * This works for CCP v3 and later
	 */
	for (i = 0; i < ARRAY_SIZE(rsa_algs); i++) {
		if (rsa_algs[i].version > ccpversion)
			continue;
		ret = ccp_register_rsa_alg(head, &rsa_algs[i]);
		if (ret)
			return ret;
	}

	return 0;
}
