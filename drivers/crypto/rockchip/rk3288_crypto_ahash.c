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
#include <asm/unaligned.h>
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

static void rk_ahash_reg_init(struct ahash_request *req)
{
	struct rk_ahash_rctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct rk_ahash_ctx *tctx = crypto_ahash_ctx(tfm);
	struct rk_crypto_info *dev = tctx->dev;
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

	CRYPTO_WRITE(dev, RK_CRYPTO_HASH_MSG_LEN, req->nbytes);
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

	return crypto_transfer_hash_request_to_engine(dev->engine, req);
}

static void crypto_ahash_dma_start(struct rk_crypto_info *dev, struct scatterlist *sg)
{
	CRYPTO_WRITE(dev, RK_CRYPTO_HRDMAS, sg_dma_address(sg));
	CRYPTO_WRITE(dev, RK_CRYPTO_HRDMAL, sg_dma_len(sg) / 4);
	CRYPTO_WRITE(dev, RK_CRYPTO_CTRL, RK_CRYPTO_HASH_START |
					  (RK_CRYPTO_HASH_START << 16));
}

static int rk_hash_prepare(struct crypto_engine *engine, void *breq)
{
	struct ahash_request *areq = container_of(breq, struct ahash_request, base);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct rk_ahash_rctx *rctx = ahash_request_ctx(areq);
	struct rk_ahash_ctx *tctx = crypto_ahash_ctx(tfm);
	int ret;

	ret = dma_map_sg(tctx->dev->dev, areq->src, sg_nents(areq->src), DMA_TO_DEVICE);
	if (ret <= 0)
		return -EINVAL;

	rctx->nrsg = ret;

	return 0;
}

static int rk_hash_unprepare(struct crypto_engine *engine, void *breq)
{
	struct ahash_request *areq = container_of(breq, struct ahash_request, base);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct rk_ahash_rctx *rctx = ahash_request_ctx(areq);
	struct rk_ahash_ctx *tctx = crypto_ahash_ctx(tfm);

	dma_unmap_sg(tctx->dev->dev, areq->src, rctx->nrsg, DMA_TO_DEVICE);
	return 0;
}

static int rk_hash_run(struct crypto_engine *engine, void *breq)
{
	struct ahash_request *areq = container_of(breq, struct ahash_request, base);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct rk_ahash_rctx *rctx = ahash_request_ctx(areq);
	struct rk_ahash_ctx *tctx = crypto_ahash_ctx(tfm);
	struct scatterlist *sg = areq->src;
	int err = 0;
	int i;
	u32 v;

	rctx->mode = 0;

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
		err =  -EINVAL;
		goto theend;
	}

	rk_ahash_reg_init(areq);

	while (sg) {
		reinit_completion(&tctx->dev->complete);
		tctx->dev->status = 0;
		crypto_ahash_dma_start(tctx->dev, sg);
		wait_for_completion_interruptible_timeout(&tctx->dev->complete,
							  msecs_to_jiffies(2000));
		if (!tctx->dev->status) {
			dev_err(tctx->dev->dev, "DMA timeout\n");
			err = -EFAULT;
			goto theend;
		}
		sg = sg_next(sg);
	}

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
	while (!CRYPTO_READ(tctx->dev, RK_CRYPTO_HASH_STS))
		udelay(10);

	for (i = 0; i < crypto_ahash_digestsize(tfm) / 4; i++) {
		v = readl(tctx->dev->reg + RK_CRYPTO_HASH_DOUT_0 + i * 4);
		put_unaligned_le32(v, areq->result + i * 4);
	}

theend:
	local_bh_disable();
	crypto_finalize_hash_request(engine, breq, err);
	local_bh_enable();

	return 0;
}

static int rk_cra_hash_init(struct crypto_tfm *tfm)
{
	struct rk_ahash_ctx *tctx = crypto_tfm_ctx(tfm);
	struct rk_crypto_tmp *algt;
	struct ahash_alg *alg = __crypto_ahash_alg(tfm->__crt_alg);

	const char *alg_name = crypto_tfm_alg_name(tfm);

	algt = container_of(alg, struct rk_crypto_tmp, alg.hash);

	tctx->dev = algt->dev;

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

	tctx->enginectx.op.do_one_request = rk_hash_run;
	tctx->enginectx.op.prepare_request = rk_hash_prepare;
	tctx->enginectx.op.unprepare_request = rk_hash_unprepare;

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
