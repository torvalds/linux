// SPDX-License-Identifier: GPL-2.0-only
/*
 * AMD Cryptographic Coprocessor (CCP) SHA crypto API support
 *
 * Copyright (C) 2013,2018 Advanced Micro Devices, Inc.
 *
 * Author: Tom Lendacky <thomas.lendacky@amd.com>
 * Author: Gary R Hook <gary.hook@amd.com>
 */

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/scatterlist.h>
#include <linux/crypto.h>
#include <crypto/algapi.h>
#include <crypto/hash.h>
#include <crypto/hmac.h>
#include <crypto/internal/hash.h>
#include <crypto/sha1.h>
#include <crypto/sha2.h>
#include <crypto/scatterwalk.h>
#include <linux/string.h>

#include "ccp-crypto.h"

static int ccp_sha_complete(struct crypto_async_request *async_req, int ret)
{
	struct ahash_request *req = ahash_request_cast(async_req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct ccp_sha_req_ctx *rctx = ahash_request_ctx_dma(req);
	unsigned int digest_size = crypto_ahash_digestsize(tfm);

	if (ret)
		goto e_free;

	if (rctx->hash_rem) {
		/* Save remaining data to buffer */
		unsigned int offset = rctx->nbytes - rctx->hash_rem;

		scatterwalk_map_and_copy(rctx->buf, rctx->src,
					 offset, rctx->hash_rem, 0);
		rctx->buf_count = rctx->hash_rem;
	} else {
		rctx->buf_count = 0;
	}

	/* Update result area if supplied */
	if (req->result && rctx->final)
		memcpy(req->result, rctx->ctx, digest_size);

e_free:
	sg_free_table(&rctx->data_sg);

	return ret;
}

static int ccp_do_sha_update(struct ahash_request *req, unsigned int nbytes,
			     unsigned int final)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct ccp_ctx *ctx = crypto_ahash_ctx_dma(tfm);
	struct ccp_sha_req_ctx *rctx = ahash_request_ctx_dma(req);
	struct scatterlist *sg;
	unsigned int block_size =
		crypto_tfm_alg_blocksize(crypto_ahash_tfm(tfm));
	unsigned int sg_count;
	gfp_t gfp;
	u64 len;
	int ret;

	len = (u64)rctx->buf_count + (u64)nbytes;

	if (!final && (len <= block_size)) {
		scatterwalk_map_and_copy(rctx->buf + rctx->buf_count, req->src,
					 0, nbytes, 0);
		rctx->buf_count += nbytes;

		return 0;
	}

	rctx->src = req->src;
	rctx->nbytes = nbytes;

	rctx->final = final;
	rctx->hash_rem = final ? 0 : len & (block_size - 1);
	rctx->hash_cnt = len - rctx->hash_rem;
	if (!final && !rctx->hash_rem) {
		/* CCP can't do zero length final, so keep some data around */
		rctx->hash_cnt -= block_size;
		rctx->hash_rem = block_size;
	}

	/* Initialize the context scatterlist */
	sg_init_one(&rctx->ctx_sg, rctx->ctx, sizeof(rctx->ctx));

	sg = NULL;
	if (rctx->buf_count && nbytes) {
		/* Build the data scatterlist table - allocate enough entries
		 * for both data pieces (buffer and input data)
		 */
		gfp = req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP ?
			GFP_KERNEL : GFP_ATOMIC;
		sg_count = sg_nents(req->src) + 1;
		ret = sg_alloc_table(&rctx->data_sg, sg_count, gfp);
		if (ret)
			return ret;

		sg_init_one(&rctx->buf_sg, rctx->buf, rctx->buf_count);
		sg = ccp_crypto_sg_table_add(&rctx->data_sg, &rctx->buf_sg);
		if (!sg) {
			ret = -EINVAL;
			goto e_free;
		}
		sg = ccp_crypto_sg_table_add(&rctx->data_sg, req->src);
		if (!sg) {
			ret = -EINVAL;
			goto e_free;
		}
		sg_mark_end(sg);

		sg = rctx->data_sg.sgl;
	} else if (rctx->buf_count) {
		sg_init_one(&rctx->buf_sg, rctx->buf, rctx->buf_count);

		sg = &rctx->buf_sg;
	} else if (nbytes) {
		sg = req->src;
	}

	rctx->msg_bits += (rctx->hash_cnt << 3);	/* Total in bits */

	memset(&rctx->cmd, 0, sizeof(rctx->cmd));
	INIT_LIST_HEAD(&rctx->cmd.entry);
	rctx->cmd.engine = CCP_ENGINE_SHA;
	rctx->cmd.u.sha.type = rctx->type;
	rctx->cmd.u.sha.ctx = &rctx->ctx_sg;

	switch (rctx->type) {
	case CCP_SHA_TYPE_1:
		rctx->cmd.u.sha.ctx_len = SHA1_DIGEST_SIZE;
		break;
	case CCP_SHA_TYPE_224:
		rctx->cmd.u.sha.ctx_len = SHA224_DIGEST_SIZE;
		break;
	case CCP_SHA_TYPE_256:
		rctx->cmd.u.sha.ctx_len = SHA256_DIGEST_SIZE;
		break;
	case CCP_SHA_TYPE_384:
		rctx->cmd.u.sha.ctx_len = SHA384_DIGEST_SIZE;
		break;
	case CCP_SHA_TYPE_512:
		rctx->cmd.u.sha.ctx_len = SHA512_DIGEST_SIZE;
		break;
	default:
		/* Should never get here */
		break;
	}

	rctx->cmd.u.sha.src = sg;
	rctx->cmd.u.sha.src_len = rctx->hash_cnt;
	rctx->cmd.u.sha.opad = ctx->u.sha.key_len ?
		&ctx->u.sha.opad_sg : NULL;
	rctx->cmd.u.sha.opad_len = ctx->u.sha.key_len ?
		ctx->u.sha.opad_count : 0;
	rctx->cmd.u.sha.first = rctx->first;
	rctx->cmd.u.sha.final = rctx->final;
	rctx->cmd.u.sha.msg_bits = rctx->msg_bits;

	rctx->first = 0;

	ret = ccp_crypto_enqueue_request(&req->base, &rctx->cmd);

	return ret;

e_free:
	sg_free_table(&rctx->data_sg);

	return ret;
}

static int ccp_sha_init(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct ccp_ctx *ctx = crypto_ahash_ctx_dma(tfm);
	struct ccp_sha_req_ctx *rctx = ahash_request_ctx_dma(req);
	struct ccp_crypto_ahash_alg *alg =
		ccp_crypto_ahash_alg(crypto_ahash_tfm(tfm));
	unsigned int block_size =
		crypto_tfm_alg_blocksize(crypto_ahash_tfm(tfm));

	memset(rctx, 0, sizeof(*rctx));

	rctx->type = alg->type;
	rctx->first = 1;

	if (ctx->u.sha.key_len) {
		/* Buffer the HMAC key for first update */
		memcpy(rctx->buf, ctx->u.sha.ipad, block_size);
		rctx->buf_count = block_size;
	}

	return 0;
}

static int ccp_sha_update(struct ahash_request *req)
{
	return ccp_do_sha_update(req, req->nbytes, 0);
}

static int ccp_sha_final(struct ahash_request *req)
{
	return ccp_do_sha_update(req, 0, 1);
}

static int ccp_sha_finup(struct ahash_request *req)
{
	return ccp_do_sha_update(req, req->nbytes, 1);
}

static int ccp_sha_digest(struct ahash_request *req)
{
	int ret;

	ret = ccp_sha_init(req);
	if (ret)
		return ret;

	return ccp_sha_finup(req);
}

static int ccp_sha_export(struct ahash_request *req, void *out)
{
	struct ccp_sha_req_ctx *rctx = ahash_request_ctx_dma(req);
	struct ccp_sha_exp_ctx state;

	/* Don't let anything leak to 'out' */
	memset(&state, 0, sizeof(state));

	state.type = rctx->type;
	state.msg_bits = rctx->msg_bits;
	state.first = rctx->first;
	memcpy(state.ctx, rctx->ctx, sizeof(state.ctx));
	state.buf_count = rctx->buf_count;
	memcpy(state.buf, rctx->buf, sizeof(state.buf));

	/* 'out' may not be aligned so memcpy from local variable */
	memcpy(out, &state, sizeof(state));

	return 0;
}

static int ccp_sha_import(struct ahash_request *req, const void *in)
{
	struct ccp_sha_req_ctx *rctx = ahash_request_ctx_dma(req);
	struct ccp_sha_exp_ctx state;

	/* 'in' may not be aligned so memcpy to local variable */
	memcpy(&state, in, sizeof(state));

	memset(rctx, 0, sizeof(*rctx));
	rctx->type = state.type;
	rctx->msg_bits = state.msg_bits;
	rctx->first = state.first;
	memcpy(rctx->ctx, state.ctx, sizeof(rctx->ctx));
	rctx->buf_count = state.buf_count;
	memcpy(rctx->buf, state.buf, sizeof(rctx->buf));

	return 0;
}

static int ccp_sha_setkey(struct crypto_ahash *tfm, const u8 *key,
			  unsigned int key_len)
{
	struct ccp_ctx *ctx = crypto_ahash_ctx_dma(tfm);
	struct crypto_shash *shash = ctx->u.sha.hmac_tfm;
	unsigned int block_size = crypto_shash_blocksize(shash);
	unsigned int digest_size = crypto_shash_digestsize(shash);
	int i, ret;

	/* Set to zero until complete */
	ctx->u.sha.key_len = 0;

	/* Clear key area to provide zero padding for keys smaller
	 * than the block size
	 */
	memset(ctx->u.sha.key, 0, sizeof(ctx->u.sha.key));

	if (key_len > block_size) {
		/* Must hash the input key */
		ret = crypto_shash_tfm_digest(shash, key, key_len,
					      ctx->u.sha.key);
		if (ret)
			return -EINVAL;

		key_len = digest_size;
	} else {
		memcpy(ctx->u.sha.key, key, key_len);
	}

	for (i = 0; i < block_size; i++) {
		ctx->u.sha.ipad[i] = ctx->u.sha.key[i] ^ HMAC_IPAD_VALUE;
		ctx->u.sha.opad[i] = ctx->u.sha.key[i] ^ HMAC_OPAD_VALUE;
	}

	sg_init_one(&ctx->u.sha.opad_sg, ctx->u.sha.opad, block_size);
	ctx->u.sha.opad_count = block_size;

	ctx->u.sha.key_len = key_len;

	return 0;
}

static int ccp_sha_cra_init(struct crypto_tfm *tfm)
{
	struct crypto_ahash *ahash = __crypto_ahash_cast(tfm);
	struct ccp_ctx *ctx = crypto_ahash_ctx_dma(ahash);

	ctx->complete = ccp_sha_complete;
	ctx->u.sha.key_len = 0;

	crypto_ahash_set_reqsize_dma(ahash, sizeof(struct ccp_sha_req_ctx));

	return 0;
}

static void ccp_sha_cra_exit(struct crypto_tfm *tfm)
{
}

static int ccp_hmac_sha_cra_init(struct crypto_tfm *tfm)
{
	struct ccp_ctx *ctx = crypto_tfm_ctx_dma(tfm);
	struct ccp_crypto_ahash_alg *alg = ccp_crypto_ahash_alg(tfm);
	struct crypto_shash *hmac_tfm;

	hmac_tfm = crypto_alloc_shash(alg->child_alg, 0, 0);
	if (IS_ERR(hmac_tfm)) {
		pr_warn("could not load driver %s need for HMAC support\n",
			alg->child_alg);
		return PTR_ERR(hmac_tfm);
	}

	ctx->u.sha.hmac_tfm = hmac_tfm;

	return ccp_sha_cra_init(tfm);
}

static void ccp_hmac_sha_cra_exit(struct crypto_tfm *tfm)
{
	struct ccp_ctx *ctx = crypto_tfm_ctx_dma(tfm);

	if (ctx->u.sha.hmac_tfm)
		crypto_free_shash(ctx->u.sha.hmac_tfm);

	ccp_sha_cra_exit(tfm);
}

struct ccp_sha_def {
	unsigned int version;
	const char *name;
	const char *drv_name;
	enum ccp_sha_type type;
	u32 digest_size;
	u32 block_size;
};

static struct ccp_sha_def sha_algs[] = {
	{
		.version	= CCP_VERSION(3, 0),
		.name		= "sha1",
		.drv_name	= "sha1-ccp",
		.type		= CCP_SHA_TYPE_1,
		.digest_size	= SHA1_DIGEST_SIZE,
		.block_size	= SHA1_BLOCK_SIZE,
	},
	{
		.version	= CCP_VERSION(3, 0),
		.name		= "sha224",
		.drv_name	= "sha224-ccp",
		.type		= CCP_SHA_TYPE_224,
		.digest_size	= SHA224_DIGEST_SIZE,
		.block_size	= SHA224_BLOCK_SIZE,
	},
	{
		.version	= CCP_VERSION(3, 0),
		.name		= "sha256",
		.drv_name	= "sha256-ccp",
		.type		= CCP_SHA_TYPE_256,
		.digest_size	= SHA256_DIGEST_SIZE,
		.block_size	= SHA256_BLOCK_SIZE,
	},
	{
		.version	= CCP_VERSION(5, 0),
		.name		= "sha384",
		.drv_name	= "sha384-ccp",
		.type		= CCP_SHA_TYPE_384,
		.digest_size	= SHA384_DIGEST_SIZE,
		.block_size	= SHA384_BLOCK_SIZE,
	},
	{
		.version	= CCP_VERSION(5, 0),
		.name		= "sha512",
		.drv_name	= "sha512-ccp",
		.type		= CCP_SHA_TYPE_512,
		.digest_size	= SHA512_DIGEST_SIZE,
		.block_size	= SHA512_BLOCK_SIZE,
	},
};

static int ccp_register_hmac_alg(struct list_head *head,
				 const struct ccp_sha_def *def,
				 const struct ccp_crypto_ahash_alg *base_alg)
{
	struct ccp_crypto_ahash_alg *ccp_alg;
	struct ahash_alg *alg;
	struct hash_alg_common *halg;
	struct crypto_alg *base;
	int ret;

	ccp_alg = kzalloc(sizeof(*ccp_alg), GFP_KERNEL);
	if (!ccp_alg)
		return -ENOMEM;

	/* Copy the base algorithm and only change what's necessary */
	*ccp_alg = *base_alg;
	INIT_LIST_HEAD(&ccp_alg->entry);

	strscpy(ccp_alg->child_alg, def->name, CRYPTO_MAX_ALG_NAME);

	alg = &ccp_alg->alg;
	alg->setkey = ccp_sha_setkey;

	halg = &alg->halg;

	base = &halg->base;
	snprintf(base->cra_name, CRYPTO_MAX_ALG_NAME, "hmac(%s)", def->name);
	snprintf(base->cra_driver_name, CRYPTO_MAX_ALG_NAME, "hmac-%s",
		 def->drv_name);
	base->cra_init = ccp_hmac_sha_cra_init;
	base->cra_exit = ccp_hmac_sha_cra_exit;

	ret = crypto_register_ahash(alg);
	if (ret) {
		pr_err("%s ahash algorithm registration error (%d)\n",
		       base->cra_name, ret);
		kfree(ccp_alg);
		return ret;
	}

	list_add(&ccp_alg->entry, head);

	return ret;
}

static int ccp_register_sha_alg(struct list_head *head,
				const struct ccp_sha_def *def)
{
	struct ccp_crypto_ahash_alg *ccp_alg;
	struct ahash_alg *alg;
	struct hash_alg_common *halg;
	struct crypto_alg *base;
	int ret;

	ccp_alg = kzalloc(sizeof(*ccp_alg), GFP_KERNEL);
	if (!ccp_alg)
		return -ENOMEM;

	INIT_LIST_HEAD(&ccp_alg->entry);

	ccp_alg->type = def->type;

	alg = &ccp_alg->alg;
	alg->init = ccp_sha_init;
	alg->update = ccp_sha_update;
	alg->final = ccp_sha_final;
	alg->finup = ccp_sha_finup;
	alg->digest = ccp_sha_digest;
	alg->export = ccp_sha_export;
	alg->import = ccp_sha_import;

	halg = &alg->halg;
	halg->digestsize = def->digest_size;
	halg->statesize = sizeof(struct ccp_sha_exp_ctx);

	base = &halg->base;
	snprintf(base->cra_name, CRYPTO_MAX_ALG_NAME, "%s", def->name);
	snprintf(base->cra_driver_name, CRYPTO_MAX_ALG_NAME, "%s",
		 def->drv_name);
	base->cra_flags = CRYPTO_ALG_ASYNC |
			  CRYPTO_ALG_ALLOCATES_MEMORY |
			  CRYPTO_ALG_KERN_DRIVER_ONLY |
			  CRYPTO_ALG_NEED_FALLBACK;
	base->cra_blocksize = def->block_size;
	base->cra_ctxsize = sizeof(struct ccp_ctx) + crypto_dma_padding();
	base->cra_priority = CCP_CRA_PRIORITY;
	base->cra_init = ccp_sha_cra_init;
	base->cra_exit = ccp_sha_cra_exit;
	base->cra_module = THIS_MODULE;

	ret = crypto_register_ahash(alg);
	if (ret) {
		pr_err("%s ahash algorithm registration error (%d)\n",
		       base->cra_name, ret);
		kfree(ccp_alg);
		return ret;
	}

	list_add(&ccp_alg->entry, head);

	ret = ccp_register_hmac_alg(head, def, ccp_alg);

	return ret;
}

int ccp_register_sha_algs(struct list_head *head)
{
	int i, ret;
	unsigned int ccpversion = ccp_version();

	for (i = 0; i < ARRAY_SIZE(sha_algs); i++) {
		if (sha_algs[i].version > ccpversion)
			continue;
		ret = ccp_register_sha_alg(head, &sha_algs[i]);
		if (ret)
			return ret;
	}

	return 0;
}
