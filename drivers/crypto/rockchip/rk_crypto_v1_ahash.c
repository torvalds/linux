// SPDX-License-Identifier: GPL-2.0
/*
 * Crypto acceleration support for Rockchip RK3288
 *
 * Copyright (c) 2015, Fuzhou Rockchip Electronics Co., Ltd
 *
 * Author: Zain Wang <zain.wang@rock-chips.com>
 *
 * Some ideas are from marvell/cesa.c and s5p-sss.c driver.
 */
#include "rk_crypto_core.h"
#include "rk_crypto_v1.h"
#include "rk_crypto_v1_reg.h"

/*
 * IC can not process zero message hash,
 * so we put the fixed hash out when met zero message.
 */

static struct rk_alg_ctx *rk_alg_ctx_cast(
	struct rk_crypto_dev *rk_dev)
{
	struct ahash_request *req =
		ahash_request_cast(rk_dev->async_req);

	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct rk_ahash_ctx *ctx = crypto_ahash_ctx(tfm);

	return &ctx->algs_ctx;
}

static int rk_crypto_irq_handle(int irq, void *dev_id)
{
	struct rk_crypto_dev *rk_dev  = platform_get_drvdata(dev_id);
	u32 interrupt_status;

	interrupt_status = CRYPTO_READ(rk_dev, RK_CRYPTO_INTSTS);
	CRYPTO_WRITE(rk_dev, RK_CRYPTO_INTSTS, interrupt_status);

	if (interrupt_status & 0x0a) {
		dev_warn(rk_dev->dev, "DMA Error\n");
		rk_dev->err = -EFAULT;
	}

	return 0;
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

static void rk_ahash_reg_init(struct rk_crypto_dev *rk_dev)
{
	struct ahash_request *req = ahash_request_cast(rk_dev->async_req);
	struct rk_ahash_rctx *rctx = ahash_request_ctx(req);
	struct rk_alg_ctx *alg_ctx = rk_alg_ctx_cast(rk_dev);
	int reg_status = 0;

	reg_status = CRYPTO_READ(rk_dev, RK_CRYPTO_CTRL) |
		     RK_CRYPTO_HASH_FLUSH | _SBF(0xffff, 16);
	CRYPTO_WRITE(rk_dev, RK_CRYPTO_CTRL, reg_status);

	reg_status = CRYPTO_READ(rk_dev, RK_CRYPTO_CTRL);
	reg_status &= (~RK_CRYPTO_HASH_FLUSH);
	reg_status |= _SBF(0xffff, 16);
	CRYPTO_WRITE(rk_dev, RK_CRYPTO_CTRL, reg_status);

	memset_io(rk_dev->reg + RK_CRYPTO_HASH_DOUT_0, 0, 32);

	CRYPTO_WRITE(rk_dev, RK_CRYPTO_INTENA, RK_CRYPTO_HRDMA_ERR_ENA |
					    RK_CRYPTO_HRDMA_DONE_ENA);

	CRYPTO_WRITE(rk_dev, RK_CRYPTO_INTSTS, RK_CRYPTO_HRDMA_ERR_INT |
					    RK_CRYPTO_HRDMA_DONE_INT);

	CRYPTO_WRITE(rk_dev, RK_CRYPTO_HASH_CTRL, rctx->mode |
					       RK_CRYPTO_HASH_SWAP_DO);

	CRYPTO_WRITE(rk_dev, RK_CRYPTO_CONF, RK_CRYPTO_BYTESWAP_HRFIFO |
					  RK_CRYPTO_BYTESWAP_BRFIFO |
					  RK_CRYPTO_BYTESWAP_BTFIFO);

	CRYPTO_WRITE(rk_dev, RK_CRYPTO_HASH_MSG_LEN, alg_ctx->total);
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
	struct rk_crypto_dev *rk_dev = tctx->rk_dev;

	if (!req->nbytes)
		return zero_message_process(req);
	else
		return rk_dev->enqueue(rk_dev, &req->base);
}

static void crypto_ahash_dma_start(struct rk_crypto_dev *rk_dev)
{
	struct rk_alg_ctx *alg_ctx = rk_alg_ctx_cast(rk_dev);

	CRYPTO_WRITE(rk_dev, RK_CRYPTO_HRDMAS, alg_ctx->addr_in);
	CRYPTO_WRITE(rk_dev, RK_CRYPTO_HRDMAL, (alg_ctx->count + 3) / 4);
	CRYPTO_WRITE(rk_dev, RK_CRYPTO_CTRL, RK_CRYPTO_HASH_START |
					  (RK_CRYPTO_HASH_START << 16));
}

static int rk_ahash_set_data_start(struct rk_crypto_dev *rk_dev)
{
	int err;
	struct rk_alg_ctx *alg_ctx = rk_alg_ctx_cast(rk_dev);

	err = rk_dev->load_data(rk_dev, alg_ctx->sg_src, NULL);
	if (!err)
		crypto_ahash_dma_start(rk_dev);
	return err;
}

static int rk_ahash_start(struct rk_crypto_dev *rk_dev)
{
	struct ahash_request *req = ahash_request_cast(rk_dev->async_req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct rk_ahash_rctx *rctx = ahash_request_ctx(req);
	struct rk_alg_ctx *alg_ctx = rk_alg_ctx_cast(rk_dev);

	alg_ctx->total      = req->nbytes;
	alg_ctx->left_bytes = req->nbytes;
	alg_ctx->sg_src     = req->src;
	alg_ctx->req_src    = req->src;
	alg_ctx->src_nents  = sg_nents_for_len(req->src, req->nbytes);

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
		return -EINVAL;
	}

	rk_ahash_reg_init(rk_dev);
	return rk_ahash_set_data_start(rk_dev);
}

static int rk_ahash_crypto_rx(struct rk_crypto_dev *rk_dev)
{
	int err = 0;
	struct ahash_request *req = ahash_request_cast(rk_dev->async_req);
	struct rk_alg_ctx *alg_ctx = rk_alg_ctx_cast(rk_dev);
	struct crypto_ahash *tfm;

	CRYPTO_TRACE("left_bytes = %u\n", alg_ctx->left_bytes);

	err = rk_dev->unload_data(rk_dev);
	if (err)
		goto out_rx;

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
		err = rk_ahash_set_data_start(rk_dev);
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
		while (!CRYPTO_READ(rk_dev, RK_CRYPTO_HASH_STS))
			udelay(10);

		tfm = crypto_ahash_reqtfm(req);
		memcpy_fromio(req->result, rk_dev->reg + RK_CRYPTO_HASH_DOUT_0,
			      crypto_ahash_digestsize(tfm));
	}

out_rx:
	return err;
}

static int rk_cra_hash_init(struct crypto_tfm *tfm)
{
	struct rk_ahash_ctx *ctx = crypto_tfm_ctx(tfm);
	struct rk_crypto_algt *algt;
	struct ahash_alg *alg = __crypto_ahash_alg(tfm->__crt_alg);
	const char *alg_name = crypto_tfm_alg_name(tfm);
	struct rk_alg_ctx *alg_ctx = &ctx->algs_ctx;
	struct rk_crypto_dev *rk_dev;

	algt = container_of(alg, struct rk_crypto_algt, alg.hash);
	rk_dev = algt->rk_dev;

	memset(ctx, 0x00, sizeof(*ctx));

	if (!rk_dev->request_crypto)
		return -EFAULT;

	rk_dev->request_crypto(rk_dev, crypto_tfm_alg_name(tfm));

	alg_ctx->align_size     = 4;

	alg_ctx->ops.start      = rk_ahash_start;
	alg_ctx->ops.update     = rk_ahash_crypto_rx;
	alg_ctx->ops.complete   = rk_ahash_crypto_complete;
	alg_ctx->ops.irq_handle = rk_crypto_irq_handle;

	ctx->rk_dev = rk_dev;

	/* for fallback */
	ctx->fallback_tfm = crypto_alloc_ahash(alg_name, 0,
					       CRYPTO_ALG_NEED_FALLBACK);
	if (IS_ERR(ctx->fallback_tfm)) {
		dev_err(rk_dev->dev, "Could not load fallback driver.\n");
		return PTR_ERR(ctx->fallback_tfm);
	}
	crypto_ahash_set_reqsize(__crypto_ahash_cast(tfm),
				 sizeof(struct rk_ahash_rctx) +
				 crypto_ahash_reqsize(ctx->fallback_tfm));

	algt->alg.hash.halg.statesize = crypto_ahash_statesize(ctx->fallback_tfm);

	return 0;
}

static void rk_cra_hash_exit(struct crypto_tfm *tfm)
{
	struct rk_ahash_ctx *ctx = crypto_tfm_ctx(tfm);

	if (ctx->fallback_tfm)
		crypto_free_ahash(ctx->fallback_tfm);

	ctx->rk_dev->release_crypto(ctx->rk_dev, crypto_tfm_alg_name(tfm));
}

struct rk_crypto_algt rk_v1_ahash_sha1   = RK_HASH_ALGO_INIT(SHA1, sha1);
struct rk_crypto_algt rk_v1_ahash_sha256 = RK_HASH_ALGO_INIT(SHA256, sha256);
struct rk_crypto_algt rk_v1_ahash_md5    = RK_HASH_ALGO_INIT(MD5, md5);

