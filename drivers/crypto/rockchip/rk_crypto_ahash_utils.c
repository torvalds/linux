// SPDX-License-Identifier: GPL-2.0
/*
 * Rockchip crypto hash uitls
 *
 * Copyright (c) 2022, Rockchip Electronics Co., Ltd
 *
 * Author: Lin Jinhan <troy.lin@rock-chips.com>
 *
 */

#include "rk_crypto_core.h"
#include "rk_crypto_ahash_utils.h"

static const char * const hash_algo2name[] = {
	[HASH_ALGO_MD5]    = "md5",
	[HASH_ALGO_SHA1]   = "sha1",
	[HASH_ALGO_SHA224] = "sha224",
	[HASH_ALGO_SHA256] = "sha256",
	[HASH_ALGO_SHA384] = "sha384",
	[HASH_ALGO_SHA512] = "sha512",
	[HASH_ALGO_SM3]    = "sm3",
};

static void rk_alg_ctx_clear(struct rk_alg_ctx *alg_ctx)
{
	alg_ctx->total	    = 0;
	alg_ctx->left_bytes = 0;
	alg_ctx->count      = 0;
	alg_ctx->sg_src     = 0;
	alg_ctx->req_src    = 0;
	alg_ctx->src_nents  = 0;
}

static void rk_ahash_ctx_clear(struct rk_ahash_ctx *ctx)
{
	rk_alg_ctx_clear(&ctx->algs_ctx);

	memset(ctx->hash_tmp, 0x00, RK_DMA_ALIGNMENT);
	memset(ctx->lastc, 0x00, sizeof(ctx->lastc));

	ctx->hash_tmp_len = 0;
	ctx->calc_cnt     = 0;
	ctx->lastc_len    = 0;
}

struct rk_ahash_ctx *rk_ahash_ctx_cast(struct rk_crypto_dev *rk_dev)
{
	struct ahash_request *req = ahash_request_cast(rk_dev->async_req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);

	return crypto_ahash_ctx(tfm);
}

struct rk_alg_ctx *rk_ahash_alg_ctx(struct rk_crypto_dev *rk_dev)
{
	return &(rk_ahash_ctx_cast(rk_dev))->algs_ctx;
}

struct rk_crypto_algt *rk_ahash_get_algt(struct crypto_ahash *tfm)
{
	struct ahash_alg *alg = __crypto_ahash_alg(tfm->base.__crt_alg);

	return container_of(alg, struct rk_crypto_algt, alg.hash);
}

static int rk_ahash_set_data_start(struct rk_crypto_dev *rk_dev, uint32_t flag)
{
	int err;
	struct rk_alg_ctx *alg_ctx = rk_ahash_alg_ctx(rk_dev);

	CRYPTO_TRACE();

	err = rk_dev->load_data(rk_dev, alg_ctx->sg_src, alg_ctx->sg_dst);
	if (!err)
		err = alg_ctx->ops.hw_dma_start(rk_dev, flag);

	return err;
}

static u32 rk_calc_lastc_new_len(u32 nbytes, u32 old_len)
{
	u32 total_len = nbytes + old_len;

	if (total_len <= RK_DMA_ALIGNMENT)
		return nbytes;

	if (total_len % RK_DMA_ALIGNMENT)
		return total_len % RK_DMA_ALIGNMENT;

	return RK_DMA_ALIGNMENT;
}

static int rk_ahash_fallback_digest(const char *alg_name, bool is_hmac,
				    const u8 *key, u32 key_len,
				    const u8 *msg, u32 msg_len,
				    u8 *digest)
{
	struct crypto_ahash *ahash_tfm;
	struct ahash_request *req;
	struct crypto_wait wait;
	struct scatterlist sg;
	int ret;

	CRYPTO_TRACE("%s, is_hmac = %d, key_len = %u, msg_len = %u",
		     alg_name, is_hmac, key_len, msg_len);

	ahash_tfm = crypto_alloc_ahash(alg_name, 0, CRYPTO_ALG_NEED_FALLBACK);
	if (IS_ERR(ahash_tfm))
		return PTR_ERR(ahash_tfm);

	req = ahash_request_alloc(ahash_tfm, GFP_KERNEL);
	if (!req) {
		crypto_free_ahash(ahash_tfm);
		return -ENOMEM;
	}

	init_completion(&wait.completion);

	ahash_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				   crypto_req_done, &wait);

	crypto_ahash_clear_flags(ahash_tfm, ~0);

	sg_init_one(&sg, msg, msg_len);
	ahash_request_set_crypt(req, &sg, digest, msg_len);

	if (is_hmac)
		crypto_ahash_setkey(ahash_tfm, key, key_len);

	ret = crypto_wait_req(crypto_ahash_digest(req), &wait);
	if (ret) {
		CRYPTO_MSG("digest failed, ret = %d", ret);
		goto exit;
	}

exit:
	ahash_request_free(req);
	crypto_free_ahash(ahash_tfm);

	return ret;
}

static int rk_ahash_get_zero_result(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct rk_crypto_algt *algt = rk_ahash_get_algt(tfm);
	struct rk_ahash_ctx *ctx = crypto_ahash_ctx(tfm);

	return rk_ahash_fallback_digest(crypto_ahash_alg_name(tfm),
					algt->type == ALG_TYPE_HMAC,
					ctx->authkey, ctx->authkey_len,
					NULL, 0, req->result);
}

int rk_ahash_hmac_setkey(struct crypto_ahash *tfm, const u8 *key, unsigned int keylen)
{
	unsigned int blocksize = crypto_tfm_alg_blocksize(crypto_ahash_tfm(tfm));
	struct rk_crypto_algt *algt = rk_ahash_get_algt(tfm);
	struct rk_ahash_ctx *ctx = crypto_ahash_ctx(tfm);
	const char *alg_name;
	int ret = 0;

	CRYPTO_MSG();

	if (algt->algo >= ARRAY_SIZE(hash_algo2name)) {
		CRYPTO_MSG("hash algo %d invalid\n", algt->algo);
		return -EINVAL;
	}

	memset(ctx->authkey, 0, sizeof(ctx->authkey));

	if (keylen <= blocksize) {
		memcpy(ctx->authkey, key, keylen);
		ctx->authkey_len = keylen;
		goto exit;
	}

	alg_name = hash_algo2name[algt->algo];

	CRYPTO_TRACE("calc key digest %s", alg_name);

	ret = rk_ahash_fallback_digest(alg_name, false, NULL, 0, key, keylen,
				       ctx->authkey);
	if (ret) {
		CRYPTO_MSG("rk_ahash_fallback_digest error ret = %d\n", ret);
		goto exit;
	}

	ctx->authkey_len = crypto_ahash_digestsize(tfm);
exit:
	return ret;
}

int rk_ahash_init(struct ahash_request *req)
{
	struct rk_ahash_rctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct rk_ahash_ctx *ctx = crypto_ahash_ctx(tfm);

	CRYPTO_TRACE();

	memset(rctx, 0x00, sizeof(*rctx));
	rk_ahash_ctx_clear(ctx);

	return 0;
}

int rk_ahash_update(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct rk_ahash_ctx *ctx = crypto_ahash_ctx(tfm);
	struct rk_ahash_rctx *rctx = ahash_request_ctx(req);
	struct rk_crypto_dev *rk_dev = ctx->rk_dev;

	CRYPTO_TRACE("nbytes = %u", req->nbytes);

	memset(rctx, 0x00, sizeof(*rctx));

	rctx->flag = RK_FLAG_UPDATE;

	return rk_dev->enqueue(rk_dev, &req->base);
}

int rk_ahash_final(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct rk_ahash_ctx *ctx = crypto_ahash_ctx(tfm);
	struct rk_ahash_rctx *rctx = ahash_request_ctx(req);
	struct rk_crypto_dev *rk_dev = ctx->rk_dev;

	CRYPTO_TRACE();

	memset(rctx, 0x00, sizeof(*rctx));

	rctx->flag = RK_FLAG_FINAL;

	/* use fallback hash */
	if (ctx->calc_cnt == 0 &&
	    ctx->hash_tmp_len == 0 &&
	    ctx->lastc_len == 0) {
		CRYPTO_TRACE("use fallback hash");
		return rk_ahash_get_zero_result(req);
	}

	return rk_dev->enqueue(rk_dev, &req->base);
}

int rk_ahash_finup(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct rk_ahash_ctx *ctx = crypto_ahash_ctx(tfm);
	struct rk_ahash_rctx *rctx = ahash_request_ctx(req);
	struct rk_crypto_dev *rk_dev = ctx->rk_dev;

	CRYPTO_TRACE("nbytes = %u", req->nbytes);

	memset(rctx, 0x00, sizeof(*rctx));

	rctx->flag = RK_FLAG_UPDATE | RK_FLAG_FINAL;

	/* use fallback hash */
	if (req->nbytes == 0 &&
	    ctx->calc_cnt == 0 &&
	    ctx->hash_tmp_len == 0 &&
	    ctx->lastc_len == 0) {
		CRYPTO_TRACE("use fallback hash");
		return rk_ahash_get_zero_result(req);
	}

	return rk_dev->enqueue(rk_dev, &req->base);
}

int rk_ahash_digest(struct ahash_request *req)
{
	CRYPTO_TRACE("calc data %u bytes.", req->nbytes);

	return rk_ahash_init(req) ?: rk_ahash_finup(req);
}

int rk_ahash_start(struct rk_crypto_dev *rk_dev)
{
	struct ahash_request *req = ahash_request_cast(rk_dev->async_req);
	struct rk_alg_ctx *alg_ctx = rk_ahash_alg_ctx(rk_dev);
	struct rk_ahash_ctx *ctx = rk_ahash_ctx_cast(rk_dev);
	struct rk_ahash_rctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct rk_crypto_algt *algt = rk_ahash_get_algt(tfm);
	struct scatterlist *src_sg;
	unsigned long flags;
	unsigned int nbytes;
	int ret = 0;

	CRYPTO_TRACE("origin: old_len = %u, new_len = %u, nbytes = %u, flag = %d",
		     ctx->hash_tmp_len, ctx->lastc_len, req->nbytes, rctx->flag);

	/* update 0Byte do nothing */
	if (req->nbytes == 0 && !(rctx->flag & RK_FLAG_FINAL))
		goto no_calc;

	if (ctx->lastc_len) {
		/* move lastc saved last time to the head of this calculation */
		memcpy(ctx->hash_tmp + ctx->hash_tmp_len, ctx->lastc, ctx->lastc_len);
		ctx->hash_tmp_len = ctx->hash_tmp_len + ctx->lastc_len;
		ctx->lastc_len = 0;
	}

	CRYPTO_TRACE("hash_tmp_len = %u", ctx->hash_tmp_len);

	/* final request no need to save lastc_new */
	if ((rctx->flag & RK_FLAG_UPDATE) && (rctx->flag & RK_FLAG_FINAL)) {
		nbytes = req->nbytes + ctx->hash_tmp_len;

		CRYPTO_TRACE("finup %u bytes", nbytes);
	} else if (rctx->flag & RK_FLAG_UPDATE) {
		ctx->lastc_len = rk_calc_lastc_new_len(req->nbytes, ctx->hash_tmp_len);

		CRYPTO_TRACE("nents = %u, ctx->lastc_len = %u, offset = %u",
			sg_nents_for_len(req->src, req->nbytes), ctx->lastc_len,
			req->nbytes - ctx->lastc_len);

		if (!sg_pcopy_to_buffer(req->src, sg_nents_for_len(req->src, req->nbytes),
			  ctx->lastc, ctx->lastc_len, req->nbytes - ctx->lastc_len)) {
			ret = -EINVAL;
			goto exit;
		}

		nbytes = ctx->hash_tmp_len + req->nbytes - ctx->lastc_len;

		/* not enough data */
		if (nbytes < RK_DMA_ALIGNMENT) {
			CRYPTO_TRACE("nbytes = %u, not enough data", nbytes);
			memcpy(ctx->hash_tmp + ctx->hash_tmp_len,
			       ctx->lastc, ctx->lastc_len);
			ctx->hash_tmp_len = ctx->hash_tmp_len + ctx->lastc_len;
			ctx->lastc_len = 0;
			goto no_calc;
		}

		CRYPTO_TRACE("update nbytes = %u", nbytes);
	} else {
		/* final just calc lastc_old */
		nbytes = ctx->hash_tmp_len;

		CRYPTO_TRACE("final nbytes = %u", nbytes);
	}

	if (ctx->hash_tmp_len) {
		/* Concatenate old data to the header */
		sg_init_table(ctx->hash_sg, ARRAY_SIZE(ctx->hash_sg));
		sg_set_buf(ctx->hash_sg, ctx->hash_tmp, ctx->hash_tmp_len);

		if (rk_crypto_check_dmafd(req->src, sg_nents_for_len(req->src, req->nbytes))) {
			CRYPTO_TRACE("is hash dmafd");
			if (!dma_map_sg(rk_dev->dev, &ctx->hash_sg[0], 1, DMA_TO_DEVICE)) {
				dev_err(rk_dev->dev, "[%s:%d] dma_map_sg(hash_sg)  error\n",
					__func__, __LINE__);
				ret = -ENOMEM;
				goto exit;
			}
			ctx->hash_tmp_mapped = true;
		}

		sg_chain(ctx->hash_sg, ARRAY_SIZE(ctx->hash_sg), req->src);

		src_sg = &ctx->hash_sg[0];
		ctx->hash_tmp_len = 0;
	} else {
		src_sg = req->src;
	}

	alg_ctx->total      = nbytes;
	alg_ctx->left_bytes = nbytes;
	alg_ctx->sg_src     = src_sg;
	alg_ctx->req_src    = src_sg;
	alg_ctx->src_nents  = sg_nents_for_len(src_sg, nbytes);

	CRYPTO_TRACE("adjust: old_len = %u, new_len = %u, nbytes = %u",
		     ctx->hash_tmp_len, ctx->lastc_len, nbytes);

	if (nbytes) {
		spin_lock_irqsave(&rk_dev->lock, flags);
		if (ctx->calc_cnt == 0)
			alg_ctx->ops.hw_init(rk_dev, algt->algo, algt->type);

		/* flush all 64byte key buffer for hmac */
		alg_ctx->ops.hw_write_key(ctx->rk_dev, ctx->authkey, sizeof(ctx->authkey));
		ret = rk_ahash_set_data_start(rk_dev, rctx->flag);
		spin_unlock_irqrestore(&rk_dev->lock, flags);
	}
exit:
	return ret;
no_calc:
	CRYPTO_TRACE("no calc");
	rk_alg_ctx_clear(alg_ctx);

	return 0;
}

int rk_ahash_crypto_rx(struct rk_crypto_dev *rk_dev)
{
	int err = 0;
	struct ahash_request *req = ahash_request_cast(rk_dev->async_req);
	struct rk_alg_ctx *alg_ctx = rk_ahash_alg_ctx(rk_dev);
	struct rk_ahash_rctx *rctx = ahash_request_ctx(req);
	struct rk_ahash_ctx *ctx = rk_ahash_ctx_cast(rk_dev);

	CRYPTO_TRACE("left bytes = %u, flag = %d", alg_ctx->left_bytes, rctx->flag);

	err = rk_dev->unload_data(rk_dev);
	if (err)
		goto out_rx;

	ctx->calc_cnt += alg_ctx->count;

	if (alg_ctx->left_bytes) {
		if (alg_ctx->aligned) {
			if (sg_is_last(alg_ctx->sg_src)) {
				dev_warn(rk_dev->dev, "[%s:%d], Lack of data\n",
					 __func__, __LINE__);
				err = -ENOMEM;
				goto out_rx;
			}
			alg_ctx->sg_src = sg_next(alg_ctx->sg_src);
		}
		err = rk_ahash_set_data_start(rk_dev, rctx->flag);
	} else {
		/*
		 * it will take some time to process date after last dma
		 * transmission.
		 */
		struct crypto_ahash *tfm;

		if (ctx->hash_tmp_mapped)
			dma_unmap_sg(rk_dev->dev, &ctx->hash_sg[0], 1, DMA_TO_DEVICE);

		/* only final will get result */
		if (!(rctx->flag & RK_FLAG_FINAL))
			goto out_rx;

		if (!req->result) {
			err = -EINVAL;
			goto out_rx;
		}

		tfm = crypto_ahash_reqtfm(req);

		err = alg_ctx->ops.hw_get_result(rk_dev, req->result,
						 crypto_ahash_digestsize(tfm));
	}

out_rx:
	return err;
}
