// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2010-2014, The Linux Foundation. All rights reserved.
 */

#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <crypto/internal/hash.h>

#include "common.h"
#include "core.h"
#include "sha.h"

struct qce_sha_saved_state {
	u8 pending_buf[QCE_SHA_MAX_BLOCKSIZE];
	u8 partial_digest[QCE_SHA_MAX_DIGESTSIZE];
	__be32 byte_count[2];
	unsigned int pending_buflen;
	unsigned int flags;
	u64 count;
	bool first_blk;
};

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

	dma_unmap_sg(qce->dev, req->src, rctx->src_nents, DMA_TO_DEVICE);
	dma_unmap_sg(qce->dev, &rctx->result_sg, 1, DMA_FROM_DEVICE);

	memcpy(rctx->digest, result->auth_iv, digestsize);
	if (req->result && rctx->last_blk)
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

	rctx->src_nents = sg_nents_for_len(req->src, req->nbytes);
	if (rctx->src_nents < 0) {
		dev_err(qce->dev, "Invalid numbers of src SG.\n");
		return rctx->src_nents;
	}

	ret = dma_map_sg(qce->dev, req->src, rctx->src_nents, DMA_TO_DEVICE);
	if (!ret)
		return -EIO;

	sg_init_one(&rctx->result_sg, qce->dma.result_buf, QCE_RESULT_BUF_SZ);

	ret = dma_map_sg(qce->dev, &rctx->result_sg, 1, DMA_FROM_DEVICE);
	if (!ret) {
		ret = -EIO;
		goto error_unmap_src;
	}

	ret = qce_dma_prep_sgs(&qce->dma, req->src, rctx->src_nents,
			       &rctx->result_sg, 1, qce_ahash_done, async_req);
	if (ret)
		goto error_unmap_dst;

	qce_dma_issue_pending(&qce->dma);

	ret = qce_start(async_req, tmpl->crypto_alg_type);
	if (ret)
		goto error_terminate;

	return 0;

error_terminate:
	qce_dma_terminate_all(&qce->dma);
error_unmap_dst:
	dma_unmap_sg(qce->dev, &rctx->result_sg, 1, DMA_FROM_DEVICE);
error_unmap_src:
	dma_unmap_sg(qce->dev, req->src, rctx->src_nents, DMA_TO_DEVICE);
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
	struct qce_sha_reqctx *rctx = ahash_request_ctx(req);
	struct qce_sha_saved_state *export_state = out;

	memcpy(export_state->pending_buf, rctx->buf, rctx->buflen);
	memcpy(export_state->partial_digest, rctx->digest, sizeof(rctx->digest));
	export_state->byte_count[0] = rctx->byte_count[0];
	export_state->byte_count[1] = rctx->byte_count[1];
	export_state->pending_buflen = rctx->buflen;
	export_state->count = rctx->count;
	export_state->first_blk = rctx->first_blk;
	export_state->flags = rctx->flags;

	return 0;
}

static int qce_ahash_import(struct ahash_request *req, const void *in)
{
	struct qce_sha_reqctx *rctx = ahash_request_ctx(req);
	const struct qce_sha_saved_state *import_state = in;

	memset(rctx, 0, sizeof(*rctx));
	rctx->count = import_state->count;
	rctx->buflen = import_state->pending_buflen;
	rctx->first_blk = import_state->first_blk;
	rctx->flags = import_state->flags;
	rctx->byte_count[0] = import_state->byte_count[0];
	rctx->byte_count[1] = import_state->byte_count[1];
	memcpy(rctx->buf, import_state->pending_buf, rctx->buflen);
	memcpy(rctx->digest, import_state->partial_digest, sizeof(rctx->digest));

	return 0;
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

	/*
	 * At this point, there is more than one block size of data.  If
	 * the available data to transfer is exactly a multiple of block
	 * size, save the last block to be transferred in qce_ahash_final
	 * (with the last block bit set) if this is indeed the end of data
	 * stream. If not this saved block will be transferred as part of
	 * next update. If this block is not held back and if this is
	 * indeed the end of data stream, the digest obtained will be wrong
	 * since qce_ahash_final will see that rctx->buflen is 0 and return
	 * doing nothing which in turn means that a digest will not be
	 * copied to the destination result buffer.  qce_ahash_final cannot
	 * be made to alter this behavior and allowed to proceed if
	 * rctx->buflen is 0 because the crypto engine BAM does not allow
	 * for zero length transfers.
	 */
	if (!hash_later)
		hash_later = blocksize;

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

	if (rctx->buflen) {
		sg_init_table(rctx->sg, 2);
		sg_set_buf(rctx->sg, rctx->tmpbuf, rctx->buflen);
		sg_chain(rctx->sg, 2, req->src);
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

	if (!rctx->buflen) {
		if (tmpl->hash_zero)
			memcpy(req->result, tmpl->hash_zero,
					tmpl->alg.ahash.halg.digestsize);
		return 0;
	}

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

	if (!rctx->nbytes_orig) {
		if (tmpl->hash_zero)
			memcpy(req->result, tmpl->hash_zero,
					tmpl->alg.ahash.halg.digestsize);
		return 0;
	}

	return qce->async_req_enqueue(tmpl->qce, &req->base);
}

static int qce_ahash_hmac_setkey(struct crypto_ahash *tfm, const u8 *key,
				 unsigned int keylen)
{
	unsigned int digestsize = crypto_ahash_digestsize(tfm);
	struct qce_sha_ctx *ctx = crypto_tfm_ctx(&tfm->base);
	struct crypto_wait wait;
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

	ahash_tfm = crypto_alloc_ahash(alg_name, 0, 0);
	if (IS_ERR(ahash_tfm))
		return PTR_ERR(ahash_tfm);

	req = ahash_request_alloc(ahash_tfm, GFP_KERNEL);
	if (!req) {
		ret = -ENOMEM;
		goto err_free_ahash;
	}

	crypto_init_wait(&wait);
	ahash_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				   crypto_req_done, &wait);
	crypto_ahash_clear_flags(ahash_tfm, ~0);

	buf = kzalloc(keylen + QCE_MAX_ALIGN_SIZE, GFP_KERNEL);
	if (!buf) {
		ret = -ENOMEM;
		goto err_free_req;
	}

	memcpy(buf, key, keylen);
	sg_init_one(&sg, buf, keylen);
	ahash_request_set_crypt(req, &sg, ctx->authkey, keylen);

	ret = crypto_wait_req(crypto_ahash_digest(req), &wait);

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
		.statesize	= sizeof(struct qce_sha_saved_state),
		.std_iv		= std_iv_sha1,
	},
	{
		.flags		= QCE_HASH_SHA256,
		.name		= "sha256",
		.drv_name	= "sha256-qce",
		.digestsize	= SHA256_DIGEST_SIZE,
		.blocksize	= SHA256_BLOCK_SIZE,
		.statesize	= sizeof(struct qce_sha_saved_state),
		.std_iv		= std_iv_sha256,
	},
	{
		.flags		= QCE_HASH_SHA1_HMAC,
		.name		= "hmac(sha1)",
		.drv_name	= "hmac-sha1-qce",
		.digestsize	= SHA1_DIGEST_SIZE,
		.blocksize	= SHA1_BLOCK_SIZE,
		.statesize	= sizeof(struct qce_sha_saved_state),
		.std_iv		= std_iv_sha1,
	},
	{
		.flags		= QCE_HASH_SHA256_HMAC,
		.name		= "hmac(sha256)",
		.drv_name	= "hmac-sha256-qce",
		.digestsize	= SHA256_DIGEST_SIZE,
		.blocksize	= SHA256_BLOCK_SIZE,
		.statesize	= sizeof(struct qce_sha_saved_state),
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

	if (IS_SHA1(def->flags))
		tmpl->hash_zero = sha1_zero_message_hash;
	else if (IS_SHA256(def->flags))
		tmpl->hash_zero = sha256_zero_message_hash;

	base = &alg->halg.base;
	base->cra_blocksize = def->blocksize;
	base->cra_priority = 300;
	base->cra_flags = CRYPTO_ALG_ASYNC | CRYPTO_ALG_KERN_DRIVER_ONLY;
	base->cra_ctxsize = sizeof(struct qce_sha_ctx);
	base->cra_alignmask = 0;
	base->cra_module = THIS_MODULE;
	base->cra_init = qce_ahash_cra_init;

	snprintf(base->cra_name, CRYPTO_MAX_ALG_NAME, "%s", def->name);
	snprintf(base->cra_driver_name, CRYPTO_MAX_ALG_NAME, "%s",
		 def->drv_name);

	INIT_LIST_HEAD(&tmpl->entry);
	tmpl->crypto_alg_type = CRYPTO_ALG_TYPE_AHASH;
	tmpl->alg_flags = def->flags;
	tmpl->qce = qce;

	ret = crypto_register_ahash(alg);
	if (ret) {
		dev_err(qce->dev, "%s registration failed\n", base->cra_name);
		kfree(tmpl);
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
