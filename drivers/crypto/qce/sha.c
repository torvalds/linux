/*
 * Copyright (c) 2010-2014, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/device.h>
#include <linux/interrupt.h>
#include <crypto/internal/hash.h>

#include "common.h"
#include "core.h"
#include "sha.h"

/* crypto hw padding constant for first operation */
#define SHA_PADDING		64
#define SHA_PADDING_MASK	(SHA_PADDING - 1)

static LIST_HEAD(ahash_algs);

static const u32 std_iv_sha1[SHA256_DIGEST_SIZE / sizeof(u32)] = {
	SHA1_H0, SHA1_H1, SHA1_H2, SHA1_H3, SHA1_H4, 0, 0, 0
};

static const u32 std_iv_sha256[SHA256_DIGEST_SIZE / sizeof(u32)] = {
	SHA256_H0, SHA256_H1, SHA256_H2, SHA256_H3,
	SHA256_H4, SHA256_H5, SHA256_H6, SHA256_H7
};

static void qce_ahash_done(void *data)
{
	struct crypto_async_request *async_req = data;
	struct ahash_request *req = ahash_request_cast(async_req);
	struct crypto_ahash *ahash = crypto_ahash_reqtfm(req);
	struct qce_sha_reqctx *rctx = ahash_request_ctx(req);
	struct qce_alg_template *tmpl = to_ahash_tmpl(async_req->tfm);
	struct qce_device *qce = tmpl->qce;
	struct qce_result_dump *result = qce->dma.result_buf;
	unsigned int digestsize = crypto_ahash_digestsize(ahash);
	int error;
	u32 status;

	error = qce_dma_terminate_all(&qce->dma);
	if (error)
		dev_dbg(qce->dev, "ahash dma termination error (%d)\n", error);

	qce_unmapsg(qce->dev, req->src, rctx->src_nents, DMA_TO_DEVICE,
		    rctx->src_chained);
	qce_unmapsg(qce->dev, &rctx->result_sg, 1, DMA_FROM_DEVICE, 0);

	memcpy(rctx->digest, result->auth_iv, digestsize);
	if (req->result)
		memcpy(req->result, result->auth_iv, digestsize);

	rctx->byte_count[0] = cpu_to_be32(result->auth_byte_count[0]);
	rctx->byte_count[1] = cpu_to_be32(result->auth_byte_count[1]);

	error = qce_check_status(qce, &status);
	if (error < 0)
		dev_dbg(qce->dev, "ahash operation error (%x)\n", status);

	req->src = rctx->src_orig;
	req->nbytes = rctx->nbytes_orig;
	rctx->last_blk = false;
	rctx->first_blk = false;

	qce->async_req_done(tmpl->qce, error);
}

static int qce_ahash_async_req_handle(struct crypto_async_request *async_req)
{
	struct ahash_request *req = ahash_request_cast(async_req);
	struct qce_sha_reqctx *rctx = ahash_request_ctx(req);
	struct qce_sha_ctx *ctx = crypto_tfm_ctx(async_req->tfm);
	struct qce_alg_template *tmpl = to_ahash_tmpl(async_req->tfm);
	struct qce_device *qce = tmpl->qce;
	unsigned long flags = rctx->flags;
	int ret;

	if (IS_SHA_HMAC(flags)) {
		rctx->authkey = ctx->authkey;
		rctx->authklen = QCE_SHA_HMAC_KEY_SIZE;
	} else if (IS_CMAC(flags)) {
		rctx->authkey = ctx->authkey;
		rctx->authklen = AES_KEYSIZE_128;
	}

	rctx->src_nents = qce_countsg(req->src, req->nbytes,
				      &rctx->src_chained);
	ret = qce_mapsg(qce->dev, req->src, rctx->src_nents, DMA_TO_DEVICE,
			rctx->src_chained);
	if (ret < 0)
		return ret;

	sg_init_one(&rctx->result_sg, qce->dma.result_buf, QCE_RESULT_BUF_SZ);

	ret = qce_mapsg(qce->dev, &rctx->result_sg, 1, DMA_FROM_DEVICE, 0);
	if (ret < 0)
		goto error_unmap_src;

	ret = qce_dma_prep_sgs(&qce->dma, req->src, rctx->src_nents,
			       &rctx->result_sg, 1, qce_ahash_done, async_req);
	if (ret)
		goto error_unmap_dst;

	qce_dma_issue_pending(&qce->dma);

	ret = qce_start(async_req, tmpl->crypto_alg_type, 0, 0);
	if (ret)
		goto error_terminate;

	return 0;

error_terminate:
	qce_dma_terminate_all(&qce->dma);
error_unmap_dst:
	qce_unmapsg(qce->dev, &rctx->result_sg, 1, DMA_FROM_DEVICE, 0);
error_unmap_src:
	qce_unmapsg(qce->dev, req->src, rctx->src_nents, DMA_TO_DEVICE,
		    rctx->src_chained);
	return ret;
}

static int qce_ahash_init(struct ahash_request *req)
{
	struct qce_sha_reqctx *rctx = ahash_request_ctx(req);
	struct qce_alg_template *tmpl = to_ahash_tmpl(req->base.tfm);
	const u32 *std_iv = tmpl->std_iv;

	memset(rctx, 0, sizeof(*rctx));
	rctx->first_blk = true;
	rctx->last_blk = false;
	rctx->flags = tmpl->alg_flags;
	memcpy(rctx->digest, std_iv, sizeof(rctx->digest));

	return 0;
}

static int qce_ahash_export(struct ahash_request *req, void *out)
{
	struct crypto_ahash *ahash = crypto_ahash_reqtfm(req);
	struct qce_sha_reqctx *rctx = ahash_request_ctx(req);
	unsigned long flags = rctx->flags;
	unsigned int digestsize = crypto_ahash_digestsize(ahash);
	unsigned int blocksize =
			crypto_tfm_alg_blocksize(crypto_ahash_tfm(ahash));

	if (IS_SHA1(flags) || IS_SHA1_HMAC(flags)) {
		struct sha1_state *out_state = out;

		out_state->count = rctx->count;
		qce_cpu_to_be32p_array((__be32 *)out_state->state,
				       rctx->digest, digestsize);
		memcpy(out_state->buffer, rctx->buf, blocksize);
	} else if (IS_SHA256(flags) || IS_SHA256_HMAC(flags)) {
		struct sha256_state *out_state = out;

		out_state->count = rctx->count;
		qce_cpu_to_be32p_array((__be32 *)out_state->state,
				       rctx->digest, digestsize);
		memcpy(out_state->buf, rctx->buf, blocksize);
	} else {
		return -EINVAL;
	}

	return 0;
}

static int qce_import_common(struct ahash_request *req, u64 in_count,
			     const u32 *state, const u8 *buffer, bool hmac)
{
	struct crypto_ahash *ahash = crypto_ahash_reqtfm(req);
	struct qce_sha_reqctx *rctx = ahash_request_ctx(req);
	unsigned int digestsize = crypto_ahash_digestsize(ahash);
	unsigned int blocksize;
	u64 count = in_count;

	blocksize = crypto_tfm_alg_blocksize(crypto_ahash_tfm(ahash));
	rctx->count = in_count;
	memcpy(rctx->buf, buffer, blocksize);

	if (in_count <= blocksize) {
		rctx->first_blk = 1;
	} else {
		rctx->first_blk = 0;
		/*
		 * For HMAC, there is a hardware padding done when first block
		 * is set. Therefore the byte_count must be incremened by 64
		 * after the first block operation.
		 */
		if (hmac)
			count += SHA_PADDING;
	}

	rctx->byte_count[0] = (__force __be32)(count & ~SHA_PADDING_MASK);
	rctx->byte_count[1] = (__force __be32)(count >> 32);
	qce_cpu_to_be32p_array((__be32 *)rctx->digest, (const u8 *)state,
			       digestsize);
	rctx->buflen = (unsigned int)(in_count & (blocksize - 1));

	return 0;
}

static int qce_ahash_import(struct ahash_request *req, const void *in)
{
	struct qce_sha_reqctx *rctx = ahash_request_ctx(req);
	unsigned long flags = rctx->flags;
	bool hmac = IS_SHA_HMAC(flags);
	int ret = -EINVAL;

	if (IS_SHA1(flags) || IS_SHA1_HMAC(flags)) {
		const struct sha1_state *state = in;

		ret = qce_import_common(req, state->count, state->state,
					state->buffer, hmac);
	} else if (IS_SHA256(flags) || IS_SHA256_HMAC(flags)) {
		const struct sha256_state *state = in;

		ret = qce_import_common(req, state->count, state->state,
					state->buf, hmac);
	}

	return ret;
}

static int qce_ahash_update(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct qce_sha_reqctx *rctx = ahash_request_ctx(req);
	struct qce_alg_template *tmpl = to_ahash_tmpl(req->base.tfm);
	struct qce_device *qce = tmpl->qce;
	struct scatterlist *sg_last, *sg;
	unsigned int total, len;
	unsigned int hash_later;
	unsigned int nbytes;
	unsigned int blocksize;

	blocksize = crypto_tfm_alg_blocksize(crypto_ahash_tfm(tfm));
	rctx->count += req->nbytes;

	/* check for buffer from previous updates and append it */
	total = req->nbytes + rctx->buflen;

	if (total <= blocksize) {
		scatterwalk_map_and_copy(rctx->buf + rctx->buflen, req->src,
					 0, req->nbytes, 0);
		rctx->buflen += req->nbytes;
		return 0;
	}

	/* save the original req structure fields */
	rctx->src_orig = req->src;
	rctx->nbytes_orig = req->nbytes;

	/*
	 * if we have data from previous update copy them on buffer. The old
	 * data will be combined with current request bytes.
	 */
	if (rctx->buflen)
		memcpy(rctx->tmpbuf, rctx->buf, rctx->buflen);

	/* calculate how many bytes will be hashed later */
	hash_later = total % blocksize;
	if (hash_later) {
		unsigned int src_offset = req->nbytes - hash_later;
		scatterwalk_map_and_copy(rctx->buf, req->src, src_offset,
					 hash_later, 0);
	}

	/* here nbytes is multiple of blocksize */
	nbytes = total - hash_later;

	len = rctx->buflen;
	sg = sg_last = req->src;

	while (len < nbytes && sg) {
		if (len + sg_dma_len(sg) > nbytes)
			break;
		len += sg_dma_len(sg);
		sg_last = sg;
		sg = sg_next(sg);
	}

	if (!sg_last)
		return -EINVAL;

	sg_mark_end(sg_last);

	if (rctx->buflen) {
		sg_init_table(rctx->sg, 2);
		sg_set_buf(rctx->sg, rctx->tmpbuf, rctx->buflen);
		scatterwalk_sg_chain(rctx->sg, 2, req->src);
		req->src = rctx->sg;
	}

	req->nbytes = nbytes;
	rctx->buflen = hash_later;

	return qce->async_req_enqueue(tmpl->qce, &req->base);
}

static int qce_ahash_final(struct ahash_request *req)
{
	struct qce_sha_reqctx *rctx = ahash_request_ctx(req);
	struct qce_alg_template *tmpl = to_ahash_tmpl(req->base.tfm);
	struct qce_device *qce = tmpl->qce;

	if (!rctx->buflen)
		return 0;

	rctx->last_blk = true;

	rctx->src_orig = req->src;
	rctx->nbytes_orig = req->nbytes;

	memcpy(rctx->tmpbuf, rctx->buf, rctx->buflen);
	sg_init_one(rctx->sg, rctx->tmpbuf, rctx->buflen);

	req->src = rctx->sg;
	req->nbytes = rctx->buflen;

	return qce->async_req_enqueue(tmpl->qce, &req->base);
}

static int qce_ahash_digest(struct ahash_request *req)
{
	struct qce_sha_reqctx *rctx = ahash_request_ctx(req);
	struct qce_alg_template *tmpl = to_ahash_tmpl(req->base.tfm);
	struct qce_device *qce = tmpl->qce;
	int ret;

	ret = qce_ahash_init(req);
	if (ret)
		return ret;

	rctx->src_orig = req->src;
	rctx->nbytes_orig = req->nbytes;
	rctx->first_blk = true;
	rctx->last_blk = true;

	return qce->async_req_enqueue(tmpl->qce, &req->base);
}

struct qce_ahash_result {
	struct completion completion;
	int error;
};

static void qce_digest_complete(struct crypto_async_request *req, int error)
{
	struct qce_ahash_result *result = req->data;

	if (error == -EINPROGRESS)
		return;

	result->error = error;
	complete(&result->completion);
}

static int qce_ahash_hmac_setkey(struct crypto_ahash *tfm, const u8 *key,
				 unsigned int keylen)
{
	unsigned int digestsize = crypto_ahash_digestsize(tfm);
	struct qce_sha_ctx *ctx = crypto_tfm_ctx(&tfm->base);
	struct qce_ahash_result result;
	struct ahash_request *req;
	struct scatterlist sg;
	unsigned int blocksize;
	struct crypto_ahash *ahash_tfm;
	u8 *buf;
	int ret;
	const char *alg_name;

	blocksize = crypto_tfm_alg_blocksize(crypto_ahash_tfm(tfm));
	memset(ctx->authkey, 0, sizeof(ctx->authkey));

	if (keylen <= blocksize) {
		memcpy(ctx->authkey, key, keylen);
		return 0;
	}

	if (digestsize == SHA1_DIGEST_SIZE)
		alg_name = "sha1-qce";
	else if (digestsize == SHA256_DIGEST_SIZE)
		alg_name = "sha256-qce";
	else
		return -EINVAL;

	ahash_tfm = crypto_alloc_ahash(alg_name, CRYPTO_ALG_TYPE_AHASH,
				       CRYPTO_ALG_TYPE_AHASH_MASK);
	if (IS_ERR(ahash_tfm))
		return PTR_ERR(ahash_tfm);

	req = ahash_request_alloc(ahash_tfm, GFP_KERNEL);
	if (!req) {
		ret = -ENOMEM;
		goto err_free_ahash;
	}

	init_completion(&result.completion);
	ahash_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				   qce_digest_complete, &result);
	crypto_ahash_clear_flags(ahash_tfm, ~0);

	buf = kzalloc(keylen + QCE_MAX_ALIGN_SIZE, GFP_KERNEL);
	if (!buf) {
		ret = -ENOMEM;
		goto err_free_req;
	}

	memcpy(buf, key, keylen);
	sg_init_one(&sg, buf, keylen);
	ahash_request_set_crypt(req, &sg, ctx->authkey, keylen);

	ret = crypto_ahash_digest(req);
	if (ret == -EINPROGRESS || ret == -EBUSY) {
		ret = wait_for_completion_interruptible(&result.completion);
		if (!ret)
			ret = result.error;
	}

	if (ret)
		crypto_ahash_set_flags(tfm, CRYPTO_TFM_RES_BAD_KEY_LEN);

	kfree(buf);
err_free_req:
	ahash_request_free(req);
err_free_ahash:
	crypto_free_ahash(ahash_tfm);
	return ret;
}

static int qce_ahash_cra_init(struct crypto_tfm *tfm)
{
	struct crypto_ahash *ahash = __crypto_ahash_cast(tfm);
	struct qce_sha_ctx *ctx = crypto_tfm_ctx(tfm);

	crypto_ahash_set_reqsize(ahash, sizeof(struct qce_sha_reqctx));
	memset(ctx, 0, sizeof(*ctx));
	return 0;
}

struct qce_ahash_def {
	unsigned long flags;
	const char *name;
	const char *drv_name;
	unsigned int digestsize;
	unsigned int blocksize;
	unsigned int statesize;
	const u32 *std_iv;
};

static const struct qce_ahash_def ahash_def[] = {
	{
		.flags		= QCE_HASH_SHA1,
		.name		= "sha1",
		.drv_name	= "sha1-qce",
		.digestsize	= SHA1_DIGEST_SIZE,
		.blocksize	= SHA1_BLOCK_SIZE,
		.statesize	= sizeof(struct sha1_state),
		.std_iv		= std_iv_sha1,
	},
	{
		.flags		= QCE_HASH_SHA256,
		.name		= "sha256",
		.drv_name	= "sha256-qce",
		.digestsize	= SHA256_DIGEST_SIZE,
		.blocksize	= SHA256_BLOCK_SIZE,
		.statesize	= sizeof(struct sha256_state),
		.std_iv		= std_iv_sha256,
	},
	{
		.flags		= QCE_HASH_SHA1_HMAC,
		.name		= "hmac(sha1)",
		.drv_name	= "hmac-sha1-qce",
		.digestsize	= SHA1_DIGEST_SIZE,
		.blocksize	= SHA1_BLOCK_SIZE,
		.statesize	= sizeof(struct sha1_state),
		.std_iv		= std_iv_sha1,
	},
	{
		.flags		= QCE_HASH_SHA256_HMAC,
		.name		= "hmac(sha256)",
		.drv_name	= "hmac-sha256-qce",
		.digestsize	= SHA256_DIGEST_SIZE,
		.blocksize	= SHA256_BLOCK_SIZE,
		.statesize	= sizeof(struct sha256_state),
		.std_iv		= std_iv_sha256,
	},
};

static int qce_ahash_register_one(const struct qce_ahash_def *def,
				  struct qce_device *qce)
{
	struct qce_alg_template *tmpl;
	struct ahash_alg *alg;
	struct crypto_alg *base;
	int ret;

	tmpl = kzalloc(sizeof(*tmpl), GFP_KERNEL);
	if (!tmpl)
		return -ENOMEM;

	tmpl->std_iv = def->std_iv;

	alg = &tmpl->alg.ahash;
	alg->init = qce_ahash_init;
	alg->update = qce_ahash_update;
	alg->final = qce_ahash_final;
	alg->digest = qce_ahash_digest;
	alg->export = qce_ahash_export;
	alg->import = qce_ahash_import;
	if (IS_SHA_HMAC(def->flags))
		alg->setkey = qce_ahash_hmac_setkey;
	alg->halg.digestsize = def->digestsize;
	alg->halg.statesize = def->statesize;

	base = &alg->halg.base;
	base->cra_blocksize = def->blocksize;
	base->cra_priority = 300;
	base->cra_flags = CRYPTO_ALG_ASYNC;
	base->cra_ctxsize = sizeof(struct qce_sha_ctx);
	base->cra_alignmask = 0;
	base->cra_module = THIS_MODULE;
	base->cra_init = qce_ahash_cra_init;
	INIT_LIST_HEAD(&base->cra_list);

	snprintf(base->cra_name, CRYPTO_MAX_ALG_NAME, "%s", def->name);
	snprintf(base->cra_driver_name, CRYPTO_MAX_ALG_NAME, "%s",
		 def->drv_name);

	INIT_LIST_HEAD(&tmpl->entry);
	tmpl->crypto_alg_type = CRYPTO_ALG_TYPE_AHASH;
	tmpl->alg_flags = def->flags;
	tmpl->qce = qce;

	ret = crypto_register_ahash(alg);
	if (ret) {
		kfree(tmpl);
		dev_err(qce->dev, "%s registration failed\n", base->cra_name);
		return ret;
	}

	list_add_tail(&tmpl->entry, &ahash_algs);
	dev_dbg(qce->dev, "%s is registered\n", base->cra_name);
	return 0;
}

static void qce_ahash_unregister(struct qce_device *qce)
{
	struct qce_alg_template *tmpl, *n;

	list_for_each_entry_safe(tmpl, n, &ahash_algs, entry) {
		crypto_unregister_ahash(&tmpl->alg.ahash);
		list_del(&tmpl->entry);
		kfree(tmpl);
	}
}

static int qce_ahash_register(struct qce_device *qce)
{
	int ret, i;

	for (i = 0; i < ARRAY_SIZE(ahash_def); i++) {
		ret = qce_ahash_register_one(&ahash_def[i], qce);
		if (ret)
			goto err;
	}

	return 0;
err:
	qce_ahash_unregister(qce);
	return ret;
}

const struct qce_algo_ops ahash_ops = {
	.type = CRYPTO_ALG_TYPE_AHASH,
	.register_algs = qce_ahash_register,
	.unregister_algs = qce_ahash_unregister,
	.async_req_handle = qce_ahash_async_req_handle,
};
