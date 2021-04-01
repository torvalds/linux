// SPDX-License-Identifier: GPL-2.0-only
/*
 * Crypto acceleration support for Rockchip RK3288
 *
 * Copyright (c) 2015, Fuzhou Rockchip Electronics Co., Ltd
 *
 * Author: Zain Wang <zain.wang@rock-chips.com>
 *
 * Some ideas are from marvell/cesa.c and s5p-sss.c driver.
 */
#include <linux/device.h>
#include "rk3288_crypto.h"

/*
 * IC can not process zero message hash,
 * so we put the fixed hash out when met zero message.
 */

static bool rk_ahash_need_fallback(struct ahash_request *req)
{
	struct scatterlist *sg;

	sg = req->src;
	while (sg) {
		if (!IS_ALIGNED(sg->offset, sizeof(u32))) {
			return true;
		}
		if (sg->length % 4) {
			return true;
		}
		sg = sg_next(sg);
	}
	return false;
}

static int rk_ahash_digest_fb(struct ahash_request *areq)
{
	struct rk_ahash_rctx *rctx = ahash_request_ctx(areq);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct rk_ahash_ctx *tfmctx = crypto_ahash_ctx(tfm);

	ahash_request_set_tfm(&rctx->fallback_req, tfmctx->fallback_tfm);
	rctx->fallback_req.base.flags = areq->base.flags &
					CRYPTO_TFM_REQ_MAY_SLEEP;

	rctx->fallback_req.nbytes = areq->nbytes;
	rctx->fallback_req.src = areq->src;
	rctx->fallback_req.result = areq->result;

	return crypto_ahash_digest(&rctx->fallback_req);
}

static int zero_message_process(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	int rk_digest_size = crypto_ahash_digestsize(tfm);

	switch (rk_digest_size) {
	case SHA1_DIGEST_SIZE:
		memcpy(req->result, sha1_zero_message_hash, rk_digest_size);
		break;
	case SHA256_DIGEST_SIZE:
		memcpy(req->result, sha256_zero_message_hash, rk_digest_size);
		break;
	case MD5_DIGEST_SIZE:
		memcpy(req->result, md5_zero_message_hash, rk_digest_size);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void rk_ahash_crypto_complete(struct crypto_async_request *base, int err)
{
	if (base->complete)
		base->complete(base, err);
}

static void rk_ahash_reg_init(struct rk_crypto_info *dev)
{
	struct ahash_request *req = ahash_request_cast(dev->async_req);
	struct rk_ahash_rctx *rctx = ahash_request_ctx(req);
	int reg_status;

	reg_status = CRYPTO_READ(dev, RK_CRYPTO_CTRL) |
		     RK_CRYPTO_HASH_FLUSH | _SBF(0xffff, 16);
	CRYPTO_WRITE(dev, RK_CRYPTO_CTRL, reg_status);

	reg_status = CRYPTO_READ(dev, RK_CRYPTO_CTRL);
	reg_status &= (~RK_CRYPTO_HASH_FLUSH);
	reg_status |= _SBF(0xffff, 16);
	CRYPTO_WRITE(dev, RK_CRYPTO_CTRL, reg_status);

	memset_io(dev->reg + RK_CRYPTO_HASH_DOUT_0, 0, 32);

	CRYPTO_WRITE(dev, RK_CRYPTO_INTENA, RK_CRYPTO_HRDMA_ERR_ENA |
					    RK_CRYPTO_HRDMA_DONE_ENA);

	CRYPTO_WRITE(dev, RK_CRYPTO_INTSTS, RK_CRYPTO_HRDMA_ERR_INT |
					    RK_CRYPTO_HRDMA_DONE_INT);

	CRYPTO_WRITE(dev, RK_CRYPTO_HASH_CTRL, rctx->mode |
					       RK_CRYPTO_HASH_SWAP_DO);

	CRYPTO_WRITE(dev, RK_CRYPTO_CONF, RK_CRYPTO_BYTESWAP_HRFIFO |
					  RK_CRYPTO_BYTESWAP_BRFIFO |
					  RK_CRYPTO_BYTESWAP_BTFIFO);

	CRYPTO_WRITE(dev, RK_CRYPTO_HASH_MSG_LEN, dev->total);
}

static int rk_ahash_init(struct ahash_request *req)
{
	struct rk_ahash_rctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct rk_ahash_ctx *ctx = crypto_ahash_ctx(tfm);

	ahash_request_set_tfm(&rctx->fallback_req, ctx->fallback_tfm);
	rctx->fallback_req.base.flags = req->base.flags &
					CRYPTO_TFM_REQ_MAY_SLEEP;

	return crypto_ahash_init(&rctx->fallback_req);
}

static int rk_ahash_update(struct ahash_request *req)
{
	struct rk_ahash_rctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct rk_ahash_ctx *ctx = crypto_ahash_ctx(tfm);

	ahash_request_set_tfm(&rctx->fallback_req, ctx->fallback_tfm);
	rctx->fallback_req.base.flags = req->base.flags &
					CRYPTO_TFM_REQ_MAY_SLEEP;
	rctx->fallback_req.nbytes = req->nbytes;
	rctx->fallback_req.src = req->src;

	return crypto_ahash_update(&rctx->fallback_req);
}

static int rk_ahash_final(struct ahash_request *req)
{
	struct rk_ahash_rctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct rk_ahash_ctx *ctx = crypto_ahash_ctx(tfm);

	ahash_request_set_tfm(&rctx->fallback_req, ctx->fallback_tfm);
	rctx->fallback_req.base.flags = req->base.flags &
					CRYPTO_TFM_REQ_MAY_SLEEP;
	rctx->fallback_req.result = req->result;

	return crypto_ahash_final(&rctx->fallback_req);
}

static int rk_ahash_finup(struct ahash_request *req)
{
	struct rk_ahash_rctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct rk_ahash_ctx *ctx = crypto_ahash_ctx(tfm);

	ahash_request_set_tfm(&rctx->fallback_req, ctx->fallback_tfm);
	rctx->fallback_req.base.flags = req->base.flags &
					CRYPTO_TFM_REQ_MAY_SLEEP;

	rctx->fallback_req.nbytes = req->nbytes;
	rctx->fallback_req.src = req->src;
	rctx->fallback_req.result = req->result;

	return crypto_ahash_finup(&rctx->fallback_req);
}

static int rk_ahash_import(struct ahash_request *req, const void *in)
{
	struct rk_ahash_rctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct rk_ahash_ctx *ctx = crypto_ahash_ctx(tfm);

	ahash_request_set_tfm(&rctx->fallback_req, ctx->fallback_tfm);
	rctx->fallback_req.base.flags = req->base.flags &
					CRYPTO_TFM_REQ_MAY_SLEEP;

	return crypto_ahash_import(&rctx->fallback_req, in);
}

static int rk_ahash_export(struct ahash_request *req, void *out)
{
	struct rk_ahash_rctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct rk_ahash_ctx *ctx = crypto_ahash_ctx(tfm);

	ahash_request_set_tfm(&rctx->fallback_req, ctx->fallback_tfm);
	rctx->fallback_req.base.flags = req->base.flags &
					CRYPTO_TFM_REQ_MAY_SLEEP;

	return crypto_ahash_export(&rctx->fallback_req, out);
}

static int rk_ahash_digest(struct ahash_request *req)
{
	struct rk_ahash_ctx *tctx = crypto_tfm_ctx(req->base.tfm);
	struct rk_crypto_info *dev = tctx->dev;

	if (rk_ahash_need_fallback(req))
		return rk_ahash_digest_fb(req);

	if (!req->nbytes)
		return zero_message_process(req);
	else
		return dev->enqueue(dev, &req->base);
}

static void crypto_ahash_dma_start(struct rk_crypto_info *dev)
{
	CRYPTO_WRITE(dev, RK_CRYPTO_HRDMAS, dev->addr_in);
	CRYPTO_WRITE(dev, RK_CRYPTO_HRDMAL, (dev->count + 3) / 4);
	CRYPTO_WRITE(dev, RK_CRYPTO_CTRL, RK_CRYPTO_HASH_START |
					  (RK_CRYPTO_HASH_START << 16));
}

static int rk_ahash_set_data_start(struct rk_crypto_info *dev)
{
	int err;

	err = dev->load_data(dev, dev->sg_src, NULL);
	if (!err)
		crypto_ahash_dma_start(dev);
	return err;
}

static int rk_ahash_start(struct rk_crypto_info *dev)
{
	struct ahash_request *req = ahash_request_cast(dev->async_req);
	struct crypto_ahash *tfm;
	struct rk_ahash_rctx *rctx;

	dev->total = req->nbytes;
	dev->left_bytes = req->nbytes;
	dev->sg_dst = NULL;
	dev->sg_src = req->src;
	dev->first = req->src;
	dev->src_nents = sg_nents(req->src);
	rctx = ahash_request_ctx(req);
	rctx->mode = 0;

	tfm = crypto_ahash_reqtfm(req);
	switch (crypto_ahash_digestsize(tfm)) {
	case SHA1_DIGEST_SIZE:
		rctx->mode = RK_CRYPTO_HASH_SHA1;
		break;
	case SHA256_DIGEST_SIZE:
		rctx->mode = RK_CRYPTO_HASH_SHA256;
		break;
	case MD5_DIGEST_SIZE:
		rctx->mode = RK_CRYPTO_HASH_MD5;
		break;
	default:
		return -EINVAL;
	}

	rk_ahash_reg_init(dev);
	return rk_ahash_set_data_start(dev);
}

static int rk_ahash_crypto_rx(struct rk_crypto_info *dev)
{
	int err = 0;
	struct ahash_request *req = ahash_request_cast(dev->async_req);
	struct crypto_ahash *tfm;

	dev->unload_data(dev);
	if (dev->left_bytes) {
		if (sg_is_last(dev->sg_src)) {
			dev_warn(dev->dev, "[%s:%d], Lack of data\n",
					__func__, __LINE__);
			err = -ENOMEM;
			goto out_rx;
		}
		dev->sg_src = sg_next(dev->sg_src);
		err = rk_ahash_set_data_start(dev);
	} else {
		/*
		 * it will take some time to process date after last dma
		 * transmission.
		 *
		 * waiting time is relative with the last date len,
		 * so cannot set a fixed time here.
		 * 10us makes system not call here frequently wasting
		 * efficiency, and make it response quickly when dma
		 * complete.
		 */
		while (!CRYPTO_READ(dev, RK_CRYPTO_HASH_STS))
			udelay(10);

		tfm = crypto_ahash_reqtfm(req);
		memcpy_fromio(req->result, dev->reg + RK_CRYPTO_HASH_DOUT_0,
			      crypto_ahash_digestsize(tfm));
		dev->complete(dev->async_req, 0);
		tasklet_schedule(&dev->queue_task);
	}

out_rx:
	return err;
}

static int rk_cra_hash_init(struct crypto_tfm *tfm)
{
	struct rk_ahash_ctx *tctx = crypto_tfm_ctx(tfm);
	struct rk_crypto_tmp *algt;
	struct ahash_alg *alg = __crypto_ahash_alg(tfm->__crt_alg);

	const char *alg_name = crypto_tfm_alg_name(tfm);

	algt = container_of(alg, struct rk_crypto_tmp, alg.hash);

	tctx->dev = algt->dev;
	tctx->dev->start = rk_ahash_start;
	tctx->dev->update = rk_ahash_crypto_rx;
	tctx->dev->complete = rk_ahash_crypto_complete;

	/* for fallback */
	tctx->fallback_tfm = crypto_alloc_ahash(alg_name, 0,
					       CRYPTO_ALG_NEED_FALLBACK);
	if (IS_ERR(tctx->fallback_tfm)) {
		dev_err(tctx->dev->dev, "Could not load fallback driver.\n");
		return PTR_ERR(tctx->fallback_tfm);
	}
	crypto_ahash_set_reqsize(__crypto_ahash_cast(tfm),
				 sizeof(struct rk_ahash_rctx) +
				 crypto_ahash_reqsize(tctx->fallback_tfm));

	return 0;
}

static void rk_cra_hash_exit(struct crypto_tfm *tfm)
{
	struct rk_ahash_ctx *tctx = crypto_tfm_ctx(tfm);

	crypto_free_ahash(tctx->fallback_tfm);
}

struct rk_crypto_tmp rk_ahash_sha1 = {
	.type = ALG_TYPE_HASH,
	.alg.hash = {
		.init = rk_ahash_init,
		.update = rk_ahash_update,
		.final = rk_ahash_final,
		.finup = rk_ahash_finup,
		.export = rk_ahash_export,
		.import = rk_ahash_import,
		.digest = rk_ahash_digest,
		.halg = {
			 .digestsize = SHA1_DIGEST_SIZE,
			 .statesize = sizeof(struct sha1_state),
			 .base = {
				  .cra_name = "sha1",
				  .cra_driver_name = "rk-sha1",
				  .cra_priority = 300,
				  .cra_flags = CRYPTO_ALG_ASYNC |
					       CRYPTO_ALG_NEED_FALLBACK,
				  .cra_blocksize = SHA1_BLOCK_SIZE,
				  .cra_ctxsize = sizeof(struct rk_ahash_ctx),
				  .cra_alignmask = 3,
				  .cra_init = rk_cra_hash_init,
				  .cra_exit = rk_cra_hash_exit,
				  .cra_module = THIS_MODULE,
				  }
			 }
	}
};

struct rk_crypto_tmp rk_ahash_sha256 = {
	.type = ALG_TYPE_HASH,
	.alg.hash = {
		.init = rk_ahash_init,
		.update = rk_ahash_update,
		.final = rk_ahash_final,
		.finup = rk_ahash_finup,
		.export = rk_ahash_export,
		.import = rk_ahash_import,
		.digest = rk_ahash_digest,
		.halg = {
			 .digestsize = SHA256_DIGEST_SIZE,
			 .statesize = sizeof(struct sha256_state),
			 .base = {
				  .cra_name = "sha256",
				  .cra_driver_name = "rk-sha256",
				  .cra_priority = 300,
				  .cra_flags = CRYPTO_ALG_ASYNC |
					       CRYPTO_ALG_NEED_FALLBACK,
				  .cra_blocksize = SHA256_BLOCK_SIZE,
				  .cra_ctxsize = sizeof(struct rk_ahash_ctx),
				  .cra_alignmask = 3,
				  .cra_init = rk_cra_hash_init,
				  .cra_exit = rk_cra_hash_exit,
				  .cra_module = THIS_MODULE,
				  }
			 }
	}
};

struct rk_crypto_tmp rk_ahash_md5 = {
	.type = ALG_TYPE_HASH,
	.alg.hash = {
		.init = rk_ahash_init,
		.update = rk_ahash_update,
		.final = rk_ahash_final,
		.finup = rk_ahash_finup,
		.export = rk_ahash_export,
		.import = rk_ahash_import,
		.digest = rk_ahash_digest,
		.halg = {
			 .digestsize = MD5_DIGEST_SIZE,
			 .statesize = sizeof(struct md5_state),
			 .base = {
				  .cra_name = "md5",
				  .cra_driver_name = "rk-md5",
				  .cra_priority = 300,
				  .cra_flags = CRYPTO_ALG_ASYNC |
					       CRYPTO_ALG_NEED_FALLBACK,
				  .cra_blocksize = SHA1_BLOCK_SIZE,
				  .cra_ctxsize = sizeof(struct rk_ahash_ctx),
				  .cra_alignmask = 3,
				  .cra_init = rk_cra_hash_init,
				  .cra_exit = rk_cra_hash_exit,
				  .cra_module = THIS_MODULE,
				  }
			}
	}
};
