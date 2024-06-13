// SPDX-License-Identifier: GPL-2.0-only
/*
 * AMD Cryptographic Coprocessor (CCP) AES CMAC crypto API support
 *
 * Copyright (C) 2013,2018 Advanced Micro Devices, Inc.
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
#include <crypto/hash.h>
#include <crypto/internal/hash.h>
#include <crypto/scatterwalk.h>

#include "ccp-crypto.h"

static int ccp_aes_cmac_complete(struct crypto_async_request *async_req,
				 int ret)
{
	struct ahash_request *req = ahash_request_cast(async_req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct ccp_aes_cmac_req_ctx *rctx = ahash_request_ctx(req);
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
		memcpy(req->result, rctx->iv, digest_size);

e_free:
	sg_free_table(&rctx->data_sg);

	return ret;
}

static int ccp_do_cmac_update(struct ahash_request *req, unsigned int nbytes,
			      unsigned int final)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct ccp_ctx *ctx = crypto_ahash_ctx(tfm);
	struct ccp_aes_cmac_req_ctx *rctx = ahash_request_ctx(req);
	struct scatterlist *sg, *cmac_key_sg = NULL;
	unsigned int block_size =
		crypto_tfm_alg_blocksize(crypto_ahash_tfm(tfm));
	unsigned int need_pad, sg_count;
	gfp_t gfp;
	u64 len;
	int ret;

	if (!ctx->u.aes.key_len)
		return -EINVAL;

	if (nbytes)
		rctx->null_msg = 0;

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

	if (final && (rctx->null_msg || (len & (block_size - 1))))
		need_pad = 1;
	else
		need_pad = 0;

	sg_init_one(&rctx->iv_sg, rctx->iv, sizeof(rctx->iv));

	/* Build the data scatterlist table - allocate enough entries for all
	 * possible data pieces (buffer, input data, padding)
	 */
	sg_count = (nbytes) ? sg_nents(req->src) + 2 : 2;
	gfp = req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP ?
		GFP_KERNEL : GFP_ATOMIC;
	ret = sg_alloc_table(&rctx->data_sg, sg_count, gfp);
	if (ret)
		return ret;

	sg = NULL;
	if (rctx->buf_count) {
		sg_init_one(&rctx->buf_sg, rctx->buf, rctx->buf_count);
		sg = ccp_crypto_sg_table_add(&rctx->data_sg, &rctx->buf_sg);
		if (!sg) {
			ret = -EINVAL;
			goto e_free;
		}
	}

	if (nbytes) {
		sg = ccp_crypto_sg_table_add(&rctx->data_sg, req->src);
		if (!sg) {
			ret = -EINVAL;
			goto e_free;
		}
	}

	if (need_pad) {
		int pad_length = block_size - (len & (block_size - 1));

		rctx->hash_cnt += pad_length;

		memset(rctx->pad, 0, sizeof(rctx->pad));
		rctx->pad[0] = 0x80;
		sg_init_one(&rctx->pad_sg, rctx->pad, pad_length);
		sg = ccp_crypto_sg_table_add(&rctx->data_sg, &rctx->pad_sg);
		if (!sg) {
			ret = -EINVAL;
			goto e_free;
		}
	}
	if (sg) {
		sg_mark_end(sg);
		sg = rctx->data_sg.sgl;
	}

	/* Initialize the K1/K2 scatterlist */
	if (final)
		cmac_key_sg = (need_pad) ? &ctx->u.aes.k2_sg
					 : &ctx->u.aes.k1_sg;

	memset(&rctx->cmd, 0, sizeof(rctx->cmd));
	INIT_LIST_HEAD(&rctx->cmd.entry);
	rctx->cmd.engine = CCP_ENGINE_AES;
	rctx->cmd.u.aes.type = ctx->u.aes.type;
	rctx->cmd.u.aes.mode = ctx->u.aes.mode;
	rctx->cmd.u.aes.action = CCP_AES_ACTION_ENCRYPT;
	rctx->cmd.u.aes.key = &ctx->u.aes.key_sg;
	rctx->cmd.u.aes.key_len = ctx->u.aes.key_len;
	rctx->cmd.u.aes.iv = &rctx->iv_sg;
	rctx->cmd.u.aes.iv_len = AES_BLOCK_SIZE;
	rctx->cmd.u.aes.src = sg;
	rctx->cmd.u.aes.src_len = rctx->hash_cnt;
	rctx->cmd.u.aes.dst = NULL;
	rctx->cmd.u.aes.cmac_key = cmac_key_sg;
	rctx->cmd.u.aes.cmac_key_len = ctx->u.aes.kn_len;
	rctx->cmd.u.aes.cmac_final = final;

	ret = ccp_crypto_enqueue_request(&req->base, &rctx->cmd);

	return ret;

e_free:
	sg_free_table(&rctx->data_sg);

	return ret;
}

static int ccp_aes_cmac_init(struct ahash_request *req)
{
	struct ccp_aes_cmac_req_ctx *rctx = ahash_request_ctx(req);

	memset(rctx, 0, sizeof(*rctx));

	rctx->null_msg = 1;

	return 0;
}

static int ccp_aes_cmac_update(struct ahash_request *req)
{
	return ccp_do_cmac_update(req, req->nbytes, 0);
}

static int ccp_aes_cmac_final(struct ahash_request *req)
{
	return ccp_do_cmac_update(req, 0, 1);
}

static int ccp_aes_cmac_finup(struct ahash_request *req)
{
	return ccp_do_cmac_update(req, req->nbytes, 1);
}

static int ccp_aes_cmac_digest(struct ahash_request *req)
{
	int ret;

	ret = ccp_aes_cmac_init(req);
	if (ret)
		return ret;

	return ccp_aes_cmac_finup(req);
}

static int ccp_aes_cmac_export(struct ahash_request *req, void *out)
{
	struct ccp_aes_cmac_req_ctx *rctx = ahash_request_ctx(req);
	struct ccp_aes_cmac_exp_ctx state;

	/* Don't let anything leak to 'out' */
	memset(&state, 0, sizeof(state));

	state.null_msg = rctx->null_msg;
	memcpy(state.iv, rctx->iv, sizeof(state.iv));
	state.buf_count = rctx->buf_count;
	memcpy(state.buf, rctx->buf, sizeof(state.buf));

	/* 'out' may not be aligned so memcpy from local variable */
	memcpy(out, &state, sizeof(state));

	return 0;
}

static int ccp_aes_cmac_import(struct ahash_request *req, const void *in)
{
	struct ccp_aes_cmac_req_ctx *rctx = ahash_request_ctx(req);
	struct ccp_aes_cmac_exp_ctx state;

	/* 'in' may not be aligned so memcpy to local variable */
	memcpy(&state, in, sizeof(state));

	memset(rctx, 0, sizeof(*rctx));
	rctx->null_msg = state.null_msg;
	memcpy(rctx->iv, state.iv, sizeof(rctx->iv));
	rctx->buf_count = state.buf_count;
	memcpy(rctx->buf, state.buf, sizeof(rctx->buf));

	return 0;
}

static int ccp_aes_cmac_setkey(struct crypto_ahash *tfm, const u8 *key,
			       unsigned int key_len)
{
	struct ccp_ctx *ctx = crypto_tfm_ctx(crypto_ahash_tfm(tfm));
	struct ccp_crypto_ahash_alg *alg =
		ccp_crypto_ahash_alg(crypto_ahash_tfm(tfm));
	u64 k0_hi, k0_lo, k1_hi, k1_lo, k2_hi, k2_lo;
	u64 rb_hi = 0x00, rb_lo = 0x87;
	struct crypto_aes_ctx aes;
	__be64 *gk;
	int ret;

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

	/* Set to zero until complete */
	ctx->u.aes.key_len = 0;

	/* Set the key for the AES cipher used to generate the keys */
	ret = aes_expandkey(&aes, key, key_len);
	if (ret)
		return ret;

	/* Encrypt a block of zeroes - use key area in context */
	memset(ctx->u.aes.key, 0, sizeof(ctx->u.aes.key));
	aes_encrypt(&aes, ctx->u.aes.key, ctx->u.aes.key);
	memzero_explicit(&aes, sizeof(aes));

	/* Generate K1 and K2 */
	k0_hi = be64_to_cpu(*((__be64 *)ctx->u.aes.key));
	k0_lo = be64_to_cpu(*((__be64 *)ctx->u.aes.key + 1));

	k1_hi = (k0_hi << 1) | (k0_lo >> 63);
	k1_lo = k0_lo << 1;
	if (ctx->u.aes.key[0] & 0x80) {
		k1_hi ^= rb_hi;
		k1_lo ^= rb_lo;
	}
	gk = (__be64 *)ctx->u.aes.k1;
	*gk = cpu_to_be64(k1_hi);
	gk++;
	*gk = cpu_to_be64(k1_lo);

	k2_hi = (k1_hi << 1) | (k1_lo >> 63);
	k2_lo = k1_lo << 1;
	if (ctx->u.aes.k1[0] & 0x80) {
		k2_hi ^= rb_hi;
		k2_lo ^= rb_lo;
	}
	gk = (__be64 *)ctx->u.aes.k2;
	*gk = cpu_to_be64(k2_hi);
	gk++;
	*gk = cpu_to_be64(k2_lo);

	ctx->u.aes.kn_len = sizeof(ctx->u.aes.k1);
	sg_init_one(&ctx->u.aes.k1_sg, ctx->u.aes.k1, sizeof(ctx->u.aes.k1));
	sg_init_one(&ctx->u.aes.k2_sg, ctx->u.aes.k2, sizeof(ctx->u.aes.k2));

	/* Save the supplied key */
	memset(ctx->u.aes.key, 0, sizeof(ctx->u.aes.key));
	memcpy(ctx->u.aes.key, key, key_len);
	ctx->u.aes.key_len = key_len;
	sg_init_one(&ctx->u.aes.key_sg, ctx->u.aes.key, key_len);

	return ret;
}

static int ccp_aes_cmac_cra_init(struct crypto_tfm *tfm)
{
	struct ccp_ctx *ctx = crypto_tfm_ctx(tfm);
	struct crypto_ahash *ahash = __crypto_ahash_cast(tfm);

	ctx->complete = ccp_aes_cmac_complete;
	ctx->u.aes.key_len = 0;

	crypto_ahash_set_reqsize(ahash, sizeof(struct ccp_aes_cmac_req_ctx));

	return 0;
}

int ccp_register_aes_cmac_algs(struct list_head *head)
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
	ccp_alg->mode = CCP_AES_MODE_CMAC;

	alg = &ccp_alg->alg;
	alg->init = ccp_aes_cmac_init;
	alg->update = ccp_aes_cmac_update;
	alg->final = ccp_aes_cmac_final;
	alg->finup = ccp_aes_cmac_finup;
	alg->digest = ccp_aes_cmac_digest;
	alg->export = ccp_aes_cmac_export;
	alg->import = ccp_aes_cmac_import;
	alg->setkey = ccp_aes_cmac_setkey;

	halg = &alg->halg;
	halg->digestsize = AES_BLOCK_SIZE;
	halg->statesize = sizeof(struct ccp_aes_cmac_exp_ctx);

	base = &halg->base;
	snprintf(base->cra_name, CRYPTO_MAX_ALG_NAME, "cmac(aes)");
	snprintf(base->cra_driver_name, CRYPTO_MAX_ALG_NAME, "cmac-aes-ccp");
	base->cra_flags = CRYPTO_ALG_ASYNC |
			  CRYPTO_ALG_ALLOCATES_MEMORY |
			  CRYPTO_ALG_KERN_DRIVER_ONLY |
			  CRYPTO_ALG_NEED_FALLBACK;
	base->cra_blocksize = AES_BLOCK_SIZE;
	base->cra_ctxsize = sizeof(struct ccp_ctx);
	base->cra_priority = CCP_CRA_PRIORITY;
	base->cra_init = ccp_aes_cmac_cra_init;
	base->cra_module = THIS_MODULE;

	ret = crypto_register_ahash(alg);
	if (ret) {
		pr_err("%s ahash algorithm registration error (%d)\n",
		       base->cra_name, ret);
		kfree(ccp_alg);
		return ret;
	}

	list_add(&ccp_alg->entry, head);

	return 0;
}
