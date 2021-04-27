// SPDX-License-Identifier: GPL-2.0
/*
 * Hash acceleration support for Rockchip Crypto v2
 *
 * Copyright (c) 2020, Rockchip Electronics Co., Ltd
 *
 * Author: Lin Jinhan <troy.lin@rock-chips.com>
 *
 * Some ideas are from marvell/cesa.c and s5p-sss.c driver.
 */

#include <linux/slab.h>
#include <linux/iopoll.h>

#include "rk_crypto_core.h"
#include "rk_crypto_v2.h"
#include "rk_crypto_v2_reg.h"

#define RK_HASH_CTX_MAGIC	0x1A1A1A1A
#define RK_POLL_PERIOD_US	100
#define RK_POLL_TIMEOUT_US	50000
#define HASH_MAX_SIZE		PAGE_SIZE

static const u8 null_hash_md5_value[] = {
	0xd4, 0x1d, 0x8c, 0xd9, 0x8f, 0x00, 0xb2, 0x04,
	0xe9, 0x80, 0x09, 0x98, 0xec, 0xf8, 0x42, 0x7e
};

static const u8 null_hash_sha1_value[] = {
	0xda, 0x39, 0xa3, 0xee, 0x5e, 0x6b, 0x4b, 0x0d,
	0x32, 0x55, 0xbf, 0xef, 0x95, 0x60, 0x18, 0x90,
	0xaf, 0xd8, 0x07, 0x09
};

static const u8 null_hash_sha256_value[] = {
	0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14,
	0x9a, 0xfb, 0xf4, 0xc8, 0x99, 0x6f, 0xb9, 0x24,
	0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c,
	0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55
};

static const u8 null_hash_sha512_value[] = {
	0xcf, 0x83, 0xe1, 0x35, 0x7e, 0xef, 0xb8, 0xbd,
	0xf1, 0x54, 0x28, 0x50, 0xd6, 0x6d, 0x80, 0x07,
	0xd6, 0x20, 0xe4, 0x05, 0x0b, 0x57, 0x15, 0xdc,
	0x83, 0xf4, 0xa9, 0x21, 0xd3, 0x6c, 0xe9, 0xce,
	0x47, 0xd0, 0xd1, 0x3c, 0x5d, 0x85, 0xf2, 0xb0,
	0xff, 0x83, 0x18, 0xd2, 0x87, 0x7e, 0xec, 0x2f,
	0x63, 0xb9, 0x31, 0xbd, 0x47, 0x41, 0x7a, 0x81,
	0xa5, 0x38, 0x32, 0x7a, 0xf9, 0x27, 0xda, 0x3e
};

static const u8 null_hash_sm3_value[] = {
	0x1a, 0xb2, 0x1d, 0x83, 0x55, 0xcf, 0xa1, 0x7f,
	0x8e, 0x61, 0x19, 0x48, 0x31, 0xe8, 0x1a, 0x8f,
	0x22, 0xbe, 0xc8, 0xc7, 0x28, 0xfe, 0xfb, 0x74,
	0x7e, 0xd0, 0x35, 0xeb, 0x50, 0x82, 0xaa, 0x2b
};

static const u32 hash_algo2bc[] = {
	[HASH_ALGO_MD5]    = CRYPTO_MD5,
	[HASH_ALGO_SHA1]   = CRYPTO_SHA1,
	[HASH_ALGO_SHA256] = CRYPTO_SHA256,
	[HASH_ALGO_SHA512] = CRYPTO_SHA512,
	[HASH_ALGO_SM3]    = CRYPTO_SM3,
};

const char *hash_algo2name[] = {
	[HASH_ALGO_MD5]    = "md5",
	[HASH_ALGO_SHA1]   = "sha1",
	[HASH_ALGO_SHA256] = "sha256",
	[HASH_ALGO_SHA512] = "sha512",
	[HASH_ALGO_SM3]    = "sm3",
};

static struct rk_alg_ctx *rk_alg_ctx_cast(
	struct rk_crypto_dev *rk_dev)
{
	struct ahash_request *req =
		ahash_request_cast(rk_dev->async_req);

	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct rk_ahash_ctx *ctx = crypto_ahash_ctx(tfm);

	return &ctx->algs_ctx;
}

static inline void word2byte_be(u32 word, u8 *ch)
{
	ch[0] = (word >> 24) & 0xff;
	ch[1] = (word >> 16) & 0xff;
	ch[2] = (word >> 8) & 0xff;
	ch[3] = (word >> 0) & 0xff;
}

static inline u32 byte2word_be(const u8 *ch)
{
	return (*ch << 24) + (*(ch + 1) << 16) +
		    (*(ch + 2) << 8) + *(ch + 3);
}

static struct rk_crypto_algt *rk_ahash_get_algt(struct crypto_ahash *tfm)
{
	struct ahash_alg *alg = __crypto_ahash_alg(tfm->base.__crt_alg);

	return container_of(alg, struct rk_crypto_algt, alg.hash);
}

static int zero_message_process(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	int rk_digest_size = crypto_ahash_digestsize(tfm);
	struct rk_crypto_algt *algt = rk_ahash_get_algt(tfm);

	switch (algt->algo) {
	case HASH_ALGO_MD5:
		memcpy(req->result, null_hash_md5_value, rk_digest_size);
		break;
	case HASH_ALGO_SHA1:
		memcpy(req->result, null_hash_sha1_value, rk_digest_size);
		break;
	case HASH_ALGO_SHA256:
		memcpy(req->result, null_hash_sha256_value, rk_digest_size);
		break;
	case HASH_ALGO_SHA512:
		memcpy(req->result, null_hash_sha512_value, rk_digest_size);
		break;
	case HASH_ALGO_SM3:
		memcpy(req->result, null_hash_sm3_value, rk_digest_size);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int rk_crypto_irq_handle(int irq, void *dev_id)
{
	struct rk_crypto_dev *rk_dev  = platform_get_drvdata(dev_id);
	u32 interrupt_status;
	struct rk_hw_crypto_v2_info *hw_info =
			(struct rk_hw_crypto_v2_info *)rk_dev->hw_info;
	struct rk_alg_ctx *alg_ctx = rk_alg_ctx_cast(rk_dev);

	interrupt_status = CRYPTO_READ(rk_dev, CRYPTO_DMA_INT_ST);
	CRYPTO_WRITE(rk_dev, CRYPTO_DMA_INT_ST, interrupt_status);

	interrupt_status &= CRYPTO_LOCKSTEP_MASK;

	if (interrupt_status != CRYPTO_SRC_ITEM_DONE_INT_ST) {
		dev_err(rk_dev->dev, "DMA desc = %p\n", hw_info->desc);
		dev_err(rk_dev->dev, "DMA addr_in = %08x\n",
			(u32)alg_ctx->addr_in);
		dev_err(rk_dev->dev, "DMA addr_out = %08x\n",
			(u32)alg_ctx->addr_out);
		dev_err(rk_dev->dev, "DMA count = %08x\n", alg_ctx->count);
		dev_err(rk_dev->dev, "DMA desc_dma = %08x\n",
			(u32)hw_info->desc_dma);
		dev_err(rk_dev->dev, "DMA Error status = %08x\n",
			interrupt_status);
		dev_err(rk_dev->dev, "DMA CRYPTO_DMA_LLI_ADDR status = %08x\n",
			CRYPTO_READ(rk_dev, CRYPTO_DMA_LLI_ADDR));
		dev_err(rk_dev->dev, "DMA CRYPTO_DMA_ST status = %08x\n",
			CRYPTO_READ(rk_dev, CRYPTO_DMA_ST));
		dev_err(rk_dev->dev, "DMA CRYPTO_DMA_STATE status = %08x\n",
			CRYPTO_READ(rk_dev, CRYPTO_DMA_STATE));
		dev_err(rk_dev->dev, "DMA CRYPTO_DMA_LLI_RADDR status = %08x\n",
			CRYPTO_READ(rk_dev, CRYPTO_DMA_LLI_RADDR));
		dev_err(rk_dev->dev, "DMA CRYPTO_DMA_SRC_RADDR status = %08x\n",
			CRYPTO_READ(rk_dev, CRYPTO_DMA_SRC_RADDR));
		dev_err(rk_dev->dev, "DMA CRYPTO_DMA_DST_RADDR status = %08x\n",
			CRYPTO_READ(rk_dev, CRYPTO_DMA_DST_RADDR));
		rk_dev->err = -EFAULT;
	}

	return 0;
}

static void rk_ahash_crypto_complete(struct crypto_async_request *base, int err)
{
	if (base->complete)
		base->complete(base, err);
}

static inline void clear_hash_out_reg(struct rk_crypto_dev *rk_dev)
{
	int i;

	/*clear out register*/
	for (i = 0; i < 16; i++)
		CRYPTO_WRITE(rk_dev, CRYPTO_HASH_DOUT_0 + 4 * i, 0);
}

static void write_key_reg(struct rk_crypto_dev *rk_dev, const u8 *key,
			  u32 key_len)
{
	u32 i;
	u32 chn_base_addr;

	chn_base_addr = CRYPTO_CH0_KEY_0;

	for (i = 0; i < key_len / 4; i++, chn_base_addr += 4)
		CRYPTO_WRITE(rk_dev, chn_base_addr,
			     byte2word_be(key + i * 4));
}

static int rk_ahash_init(struct ahash_request *req)
{
	struct rk_ahash_rctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct rk_ahash_ctx *ctx = crypto_ahash_ctx(tfm);

	CRYPTO_TRACE();

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

	CRYPTO_TRACE();

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

	CRYPTO_TRACE();

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

	CRYPTO_TRACE();

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

	CRYPTO_TRACE();

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

	CRYPTO_TRACE();

	ahash_request_set_tfm(&rctx->fallback_req, ctx->fallback_tfm);
	rctx->fallback_req.base.flags = req->base.flags &
					CRYPTO_TFM_REQ_MAY_SLEEP;

	return crypto_ahash_export(&rctx->fallback_req, out);
}

static int rk_ahash_digest(struct ahash_request *req)
{
	struct rk_ahash_ctx *tctx = crypto_tfm_ctx(req->base.tfm);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct rk_crypto_algt *algt = rk_ahash_get_algt(tfm);
	struct rk_crypto_dev *rk_dev = tctx->rk_dev;

	CRYPTO_TRACE("calc data %u bytes.", req->nbytes);

	if (!req->nbytes)
		return IS_TYPE_HMAC(algt->type) ?
		       crypto_ahash_digest(req) :
		       zero_message_process(req);
	else
		return rk_dev->enqueue(rk_dev, &req->base);
}

static int rk_ahash_calc_digest(const char *alg_name, const u8 *key, u32 keylen,
				u8 *digest, u32 digest_len)
{
	struct crypto_ahash *ahash_tfm;
	struct ahash_request *req;
	struct crypto_wait wait;
	struct scatterlist sg;
	int ret;

	CRYPTO_TRACE();

	ahash_tfm = crypto_alloc_ahash(alg_name, 0, CRYPTO_ALG_NEED_FALLBACK);
	if (IS_ERR(ahash_tfm))
		return PTR_ERR(ahash_tfm);

	req = ahash_request_alloc(ahash_tfm, GFP_KERNEL);
	if (!req) {
		crypto_free_ahash(ahash_tfm);
		return -ENOMEM;
	}

	init_completion(&wait.completion);

	crypto_ahash_clear_flags(ahash_tfm, ~0);

	sg_init_one(&sg, key, keylen);
	ahash_request_set_crypt(req, &sg, digest, keylen);

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

static int rk_ahash_hmac_setkey(struct crypto_ahash *tfm, const u8 *key,
				 unsigned int keylen)
{
	unsigned int blocksize = crypto_tfm_alg_blocksize(crypto_ahash_tfm(tfm));
	unsigned int digestsize = crypto_ahash_digestsize(tfm);
	struct rk_crypto_algt *algt = rk_ahash_get_algt(tfm);
	struct rk_ahash_ctx *ctx = crypto_ahash_ctx(tfm);
	const char *alg_name;
	int ret;

	CRYPTO_MSG();

	crypto_ahash_clear_flags(ctx->fallback_tfm, ~0);
	ret = crypto_ahash_setkey(ctx->fallback_tfm, key, keylen);
	if (ret) {
		CRYPTO_MSG("setkey failed, ret = %d\n", ret);
		goto exit;
	}

	memset(ctx->authkey, 0, sizeof(ctx->authkey));

	if (keylen <= blocksize) {
		memcpy(ctx->authkey, key, keylen);
		goto exit;
	}

	if (algt->algo >= ARRAY_SIZE(hash_algo2name)) {
		CRYPTO_MSG("hash algo %d invalid\n", algt->algo);
		return -EINVAL;
	}

	alg_name = hash_algo2name[algt->algo];

	CRYPTO_TRACE("calc key digest %s", alg_name);

	ret = rk_ahash_calc_digest(alg_name, key, keylen, ctx->authkey, digestsize);
	if (ret) {
		CRYPTO_MSG("rk_ahash_calc_digest error ret = %d\n", ret);
		goto exit;
	}

exit:
	if (ret)
		crypto_ahash_set_flags(tfm, CRYPTO_TFM_RES_BAD_KEY_LEN);
	return ret;
}

static void rk_ahash_dma_start(struct rk_crypto_dev *rk_dev)
{
	struct rk_hw_crypto_v2_info *hw_info =
			(struct rk_hw_crypto_v2_info *)rk_dev->hw_info;
	struct rk_alg_ctx *alg_ctx = rk_alg_ctx_cast(rk_dev);

	CRYPTO_TRACE();

	memset(hw_info->desc, 0x00, sizeof(*hw_info->desc));

	hw_info->desc->src_addr = alg_ctx->addr_in;
	hw_info->desc->src_len  = alg_ctx->count;
	hw_info->desc->next_addr = 0;
	hw_info->desc->dma_ctrl = 0x00000401;
	hw_info->desc->user_define = 0x7;

	dma_wmb();

	CRYPTO_WRITE(rk_dev, CRYPTO_DMA_LLI_ADDR, hw_info->desc_dma);
	CRYPTO_WRITE(rk_dev, CRYPTO_HASH_CTL,
		     (CRYPTO_HASH_ENABLE <<
		      CRYPTO_WRITE_MASK_SHIFT) |
		      CRYPTO_HASH_ENABLE);

	CRYPTO_WRITE(rk_dev, CRYPTO_DMA_CTL, 0x00010001);/* start */
}

static int rk_ahash_set_data_start(struct rk_crypto_dev *rk_dev)
{
	int err;
	struct rk_alg_ctx *alg_ctx = rk_alg_ctx_cast(rk_dev);

	CRYPTO_TRACE();

	err = rk_dev->load_data(rk_dev, alg_ctx->sg_src, alg_ctx->sg_dst);
	if (!err)
		rk_ahash_dma_start(rk_dev);
	return err;
}

static int rk_ahash_start(struct rk_crypto_dev *rk_dev)
{
	struct ahash_request *req = ahash_request_cast(rk_dev->async_req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct rk_crypto_algt *algt = rk_ahash_get_algt(tfm);
	struct rk_ahash_ctx *ctx = crypto_ahash_ctx(tfm);
	struct rk_alg_ctx *alg_ctx = rk_alg_ctx_cast(rk_dev);
	u32 reg_ctrl = 0;

	CRYPTO_TRACE();

	alg_ctx->total      = req->nbytes;
	alg_ctx->left_bytes = req->nbytes;
	alg_ctx->sg_src     = req->src;
	alg_ctx->req_src    = req->src;
	alg_ctx->src_nents  = sg_nents_for_len(req->src, req->nbytes);

	if (algt->algo >= ARRAY_SIZE(hash_algo2bc))
		goto exit;

	reg_ctrl |= hash_algo2bc[algt->algo];
	clear_hash_out_reg(rk_dev);

	reg_ctrl |= CRYPTO_HW_PAD_ENABLE;

	if (IS_TYPE_HMAC(algt->type)) {
		CRYPTO_TRACE("this is hmac");
		reg_ctrl |= CRYPTO_HMAC_ENABLE;
		write_key_reg(rk_dev, ctx->authkey, sizeof(ctx->authkey));
	}

	CRYPTO_WRITE(rk_dev, CRYPTO_HASH_CTL, reg_ctrl | CRYPTO_WRITE_MASK_ALL);

	CRYPTO_WRITE(rk_dev, CRYPTO_FIFO_CTL, 0x00030003);
	CRYPTO_WRITE(rk_dev, CRYPTO_DMA_INT_EN, 0x7f);

	return rk_ahash_set_data_start(rk_dev);
exit:
	CRYPTO_WRITE(rk_dev, CRYPTO_HASH_CTL, CRYPTO_WRITE_MASK_ALL | 0);
	return -1;
}

static int rk_ahash_get_result(struct rk_crypto_dev *rk_dev,
			       uint8_t *data, uint32_t data_len)
{
	int ret;
	u32 i, offset;
	u32 reg_ctrl = 0;

	ret = readl_poll_timeout_atomic(rk_dev->reg + CRYPTO_HASH_VALID,
					reg_ctrl,
					reg_ctrl & CRYPTO_HASH_IS_VALID,
					RK_POLL_PERIOD_US,
					RK_POLL_TIMEOUT_US);
	if (ret)
		goto exit;

	offset = CRYPTO_HASH_DOUT_0;
	for (i = 0; i < data_len / 4; i++, offset += 4)
		word2byte_be(CRYPTO_READ(rk_dev, offset),
			  data + i * 4);

	if (data_len % 4) {
		uint8_t tmp_buf[4];

		word2byte_be(CRYPTO_READ(rk_dev, offset), tmp_buf);
		memcpy(data + i * 4, tmp_buf, data_len % 4);
	}

	CRYPTO_WRITE(rk_dev, CRYPTO_HASH_VALID, CRYPTO_HASH_IS_VALID);

exit:
	return ret;
}

static int rk_ahash_crypto_rx(struct rk_crypto_dev *rk_dev)
{
	int err = 0;
	struct ahash_request *req = ahash_request_cast(rk_dev->async_req);
	struct rk_alg_ctx *alg_ctx = rk_alg_ctx_cast(rk_dev);
	struct crypto_ahash *tfm;

	CRYPTO_TRACE("left bytes = %u", alg_ctx->left_bytes);

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
		 */

		tfm = crypto_ahash_reqtfm(req);

		err = rk_ahash_get_result(rk_dev, req->result,
					  crypto_ahash_digestsize(tfm));

		alg_ctx->ops.complete(rk_dev->async_req, err);
		tasklet_schedule(&rk_dev->queue_task);
	}

out_rx:
	return err;

}

static int rk_cra_hash_init(struct crypto_tfm *tfm)
{
	struct rk_crypto_algt *algt =
		rk_ahash_get_algt(__crypto_ahash_cast(tfm));
	const char *alg_name = crypto_tfm_alg_name(tfm);
	struct rk_ahash_ctx *ctx = crypto_tfm_ctx(tfm);
	struct rk_crypto_dev *rk_dev = algt->rk_dev;
	struct rk_alg_ctx *alg_ctx = &ctx->algs_ctx;

	CRYPTO_TRACE();

	memset(ctx, 0x00, sizeof(*ctx));

	if (!rk_dev->request_crypto)
		return -EFAULT;

	rk_dev->request_crypto(rk_dev, alg_name);

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

	CRYPTO_TRACE();

	if (ctx->fallback_tfm)
		crypto_free_ahash(ctx->fallback_tfm);

	ctx->rk_dev->release_crypto(ctx->rk_dev, crypto_tfm_alg_name(tfm));
}

struct rk_crypto_algt rk_v2_ahash_md5    = RK_HASH_ALGO_INIT(MD5, md5);
struct rk_crypto_algt rk_v2_ahash_sha1   = RK_HASH_ALGO_INIT(SHA1, sha1);
struct rk_crypto_algt rk_v2_ahash_sha256 = RK_HASH_ALGO_INIT(SHA256, sha256);
struct rk_crypto_algt rk_v2_ahash_sha512 = RK_HASH_ALGO_INIT(SHA512, sha512);
struct rk_crypto_algt rk_v2_ahash_sm3    = RK_HASH_ALGO_INIT(SM3, sm3);

struct rk_crypto_algt rk_v2_hmac_md5     = RK_HMAC_ALGO_INIT(MD5, md5);
struct rk_crypto_algt rk_v2_hmac_sha1    = RK_HMAC_ALGO_INIT(SHA1, sha1);
struct rk_crypto_algt rk_v2_hmac_sha256  = RK_HMAC_ALGO_INIT(SHA256, sha256);
struct rk_crypto_algt rk_v2_hmac_sha512  = RK_HMAC_ALGO_INIT(SHA512, sha512);
struct rk_crypto_algt rk_v2_hmac_sm3     = RK_HMAC_ALGO_INIT(SM3, sm3);

