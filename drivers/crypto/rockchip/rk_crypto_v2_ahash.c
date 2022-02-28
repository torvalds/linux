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

struct rk_ahash_expt_ctx {
	struct rk_ahash_ctx	ctx;
	u8			lastc[RK_DMA_ALIGNMENT];
};

static const u32 hash_algo2bc[] = {
	[HASH_ALGO_MD5]    = CRYPTO_MD5,
	[HASH_ALGO_SHA1]   = CRYPTO_SHA1,
	[HASH_ALGO_SHA224] = CRYPTO_SHA224,
	[HASH_ALGO_SHA256] = CRYPTO_SHA256,
	[HASH_ALGO_SHA384] = CRYPTO_SHA384,
	[HASH_ALGO_SHA512] = CRYPTO_SHA512,
	[HASH_ALGO_SM3]    = CRYPTO_SM3,
};

const char *hash_algo2name[] = {
	[HASH_ALGO_MD5]    = "md5",
	[HASH_ALGO_SHA1]   = "sha1",
	[HASH_ALGO_SHA224] = "sha224",
	[HASH_ALGO_SHA256] = "sha256",
	[HASH_ALGO_SHA384] = "sha384",
	[HASH_ALGO_SHA512] = "sha512",
	[HASH_ALGO_SM3]    = "sm3",
};

static struct rk_ahash_ctx *rk_ahash_ctx_cast(
	struct rk_crypto_dev *rk_dev)
{
	struct ahash_request *req =
		ahash_request_cast(rk_dev->async_req);

	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);

	return crypto_ahash_ctx(tfm);
}

static struct rk_alg_ctx *rk_alg_ctx_cast(
	struct rk_crypto_dev *rk_dev)
{
	return &(rk_ahash_ctx_cast(rk_dev))->algs_ctx;
}

static void rk_alg_ctx_clear(struct rk_alg_ctx *alg_ctx)
{
	alg_ctx->total	    = 0;
	alg_ctx->left_bytes = 0;
	alg_ctx->count      = 0;
	alg_ctx->sg_src     = 0;
	alg_ctx->req_src    = 0;
	alg_ctx->src_nents  = 0;
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

static void rk_hash_reset(struct rk_crypto_dev *rk_dev)
{
	int ret;
	u32 tmp = 0, tmp_mask = 0;
	unsigned int pool_timeout_us = 1000;

	CRYPTO_WRITE(rk_dev, CRYPTO_DMA_INT_EN, 0x00);

	tmp = CRYPTO_SW_CC_RESET;
	tmp_mask = tmp << CRYPTO_WRITE_MASK_SHIFT;

	CRYPTO_WRITE(rk_dev, CRYPTO_RST_CTL, tmp | tmp_mask);

	/* This is usually done in 20 clock cycles */
	ret = readl_poll_timeout_atomic(rk_dev->reg + CRYPTO_RST_CTL,
					tmp, !tmp, 0, pool_timeout_us);
	if (ret)
		dev_err(rk_dev->dev, "cipher reset pool timeout %ums.",
			pool_timeout_us);

	CRYPTO_WRITE(rk_dev, CRYPTO_HASH_CTL, 0xffff0000);
}

static int rk_crypto_irq_handle(int irq, void *dev_id)
{
	struct rk_crypto_dev *rk_dev  = platform_get_drvdata(dev_id);
	u32 interrupt_status;
	struct rk_hw_crypto_v2_info *hw_info =
			(struct rk_hw_crypto_v2_info *)rk_dev->hw_info;
	struct rk_alg_ctx *alg_ctx = rk_alg_ctx_cast(rk_dev);

	/* disable crypto irq */
	CRYPTO_WRITE(rk_dev, CRYPTO_DMA_INT_EN, 0);

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
	struct ahash_request *req = ahash_request_cast(base);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct rk_ahash_ctx *ctx = crypto_ahash_ctx(tfm);
	struct rk_alg_ctx *alg_ctx = rk_alg_ctx_cast(ctx->rk_dev);

	struct rk_hw_crypto_v2_info *hw_info = ctx->rk_dev->hw_info;
	struct crypto_lli_desc *lli_desc = hw_info->desc;

	if (err) {
		rk_hash_reset(ctx->rk_dev);
		pr_err("aligned = %u, align_size = %u\n",
		       alg_ctx->aligned, alg_ctx->align_size);
		pr_err("total = %u, left = %u, count = %u\n",
		       alg_ctx->total, alg_ctx->left_bytes, alg_ctx->count);
		pr_err("lli->src     = %08x\n", lli_desc->src_addr);
		pr_err("lli->src_len = %08x\n", lli_desc->src_len);
		pr_err("lli->dst     = %08x\n", lli_desc->dst_addr);
		pr_err("lli->dst_len = %08x\n", lli_desc->dst_len);
		pr_err("lli->dma_ctl = %08x\n", lli_desc->dma_ctrl);
		pr_err("lli->usr_def = %08x\n", lli_desc->user_define);
		pr_err("lli->next    = %08x\n\n\n", lli_desc->next_addr);
	}

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

static int rk_hw_hash_init(struct rk_crypto_dev *rk_dev, u32 algo, u32 type)
{
	u32 reg_ctrl = 0;

	if (algo >= ARRAY_SIZE(hash_algo2bc))
		goto exit;

	rk_hash_reset(rk_dev);

	clear_hash_out_reg(rk_dev);

	reg_ctrl = hash_algo2bc[algo] | CRYPTO_HW_PAD_ENABLE;

	if (IS_TYPE_HMAC(type)) {
		CRYPTO_TRACE("this is hmac");
		reg_ctrl |= CRYPTO_HMAC_ENABLE;
	}

	CRYPTO_WRITE(rk_dev, CRYPTO_HASH_CTL, reg_ctrl | CRYPTO_WRITE_MASK_ALL);
	CRYPTO_WRITE(rk_dev, CRYPTO_FIFO_CTL, 0x00030003);

	return 0;
exit:
	CRYPTO_WRITE(rk_dev, CRYPTO_HASH_CTL, 0 | CRYPTO_WRITE_MASK_ALL);

	return -EINVAL;
}

static void clean_hash_setting(struct rk_crypto_dev *rk_dev)
{
	CRYPTO_WRITE(rk_dev, CRYPTO_DMA_INT_EN, 0);
	CRYPTO_WRITE(rk_dev, CRYPTO_HASH_CTL, 0 | CRYPTO_WRITE_MASK_ALL);
}

static int rk_ahash_init(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct rk_crypto_algt *algt = rk_ahash_get_algt(tfm);
	struct rk_ahash_rctx *rctx = ahash_request_ctx(req);
	struct rk_ahash_ctx *ctx = crypto_ahash_ctx(tfm);

	CRYPTO_TRACE();

	memset(rctx, 0x00, sizeof(*rctx));

	return rk_hw_hash_init(ctx->rk_dev, algt->algo, algt->type);
}

static int rk_ahash_update(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct rk_ahash_ctx *ctx = crypto_ahash_ctx(tfm);
	struct rk_ahash_rctx *rctx = ahash_request_ctx(req);
	struct rk_crypto_dev *rk_dev = ctx->rk_dev;

	CRYPTO_TRACE("nbytes = %u", req->nbytes);

	memset(rctx, 0x00, sizeof(*rctx));

	return rk_dev->enqueue(rk_dev, &req->base);
}

static int rk_ahash_final(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct rk_ahash_ctx *ctx = crypto_ahash_ctx(tfm);
	struct rk_ahash_rctx *rctx = ahash_request_ctx(req);
	struct rk_crypto_dev *rk_dev = ctx->rk_dev;

	CRYPTO_TRACE();

	memset(rctx, 0x00, sizeof(*rctx));

	rctx->is_final = true;

	return rk_dev->enqueue(rk_dev, &req->base);
}

static int rk_ahash_finup(struct ahash_request *req)
{
	int ret = 0;
	crypto_completion_t complete_bak;
	void *data_bak;

	DECLARE_CRYPTO_WAIT(wait);

	/* update not trigger user complete */
	complete_bak = req->base.complete;
	data_bak     = req->base.data;

	req->base.complete = crypto_req_done;
	req->base.data     = &wait;

	ret = crypto_wait_req(rk_ahash_update(req), &wait);
	if (ret) {
		CRYPTO_MSG("rk_ahash_update failed, ret = %d", ret);
		goto exit;
	}

	/* final will trigger user complete */
	req->base.complete = complete_bak;
	req->base.data     = data_bak;

	ret = rk_ahash_final(req);

exit:
	return ret;
}

static int rk_ahash_import(struct ahash_request *req, const void *in)
{
	struct rk_ahash_expt_ctx state;

	/* 'in' may not be aligned so memcpy to local variable */
	memcpy(&state, in, sizeof(state));

	///TODO:  deal with import

	return 0;
}

static int rk_ahash_export(struct ahash_request *req, void *out)
{
	struct rk_ahash_expt_ctx state;

	/* Don't let anything leak to 'out' */
	memset(&state, 0, sizeof(state));

	///TODO:  deal with import

	memcpy(out, &state, sizeof(state));

	return 0;
}

static int rk_ahash_digest(struct ahash_request *req)
{
	CRYPTO_TRACE("calc data %u bytes.", req->nbytes);

	return rk_ahash_init(req) ?: rk_ahash_finup(req);
}

static int rk_ahash_fallback_digest(const char *alg_name, bool is_hmac,
				    const u8 *key, u32 key_len,
				    const u8 *msg, u32 msg_len,
				    u8 *digest, u32 digest_len)
{
	struct crypto_ahash *ahash_tfm;
	struct ahash_request *req;
	struct crypto_wait wait;
	struct scatterlist sg;
	int ret;

	CRYPTO_TRACE("%s, is_hmac = %d, key_len = %u, msg_len = %u, digest_len = %u",
		     alg_name, is_hmac, key_len, msg_len, digest_len);

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

static int rk_ahash_hmac_setkey(struct crypto_ahash *tfm, const u8 *key,
				unsigned int keylen)
{
	unsigned int blocksize = crypto_tfm_alg_blocksize(crypto_ahash_tfm(tfm));
	unsigned int digestsize = crypto_ahash_digestsize(tfm);
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
				       ctx->authkey, digestsize);
	if (ret) {
		CRYPTO_MSG("rk_ahash_fallback_digest error ret = %d\n", ret);
		goto exit;
	}

	ctx->authkey_len = digestsize;
exit:
	if (ret == 0)
		write_key_reg(ctx->rk_dev, ctx->authkey, sizeof(ctx->authkey));

	return ret;
}

static int rk_ahash_dma_start(struct rk_crypto_dev *rk_dev, bool is_final)
{
	struct rk_hw_crypto_v2_info *hw_info =
			(struct rk_hw_crypto_v2_info *)rk_dev->hw_info;
	struct rk_alg_ctx *alg_ctx = rk_alg_ctx_cast(rk_dev);
	u32 dma_ctl = hw_info->is_started ? CRYPTO_DMA_RESTART : CRYPTO_DMA_START;

	CRYPTO_TRACE("count %u Byte, is_started = %d, is_final = %d",
		     alg_ctx->count, hw_info->is_started, is_final);

	if (alg_ctx->count % RK_DMA_ALIGNMENT && !is_final) {
		dev_err(rk_dev->dev, "count = %u is not aligned with [%u]\n",
			alg_ctx->count, RK_DMA_ALIGNMENT);
		return -EINVAL;
	}

	if (alg_ctx->count == 0) {
		/* do nothing */
		CRYPTO_TRACE("empty calc");
		return 0;
	}

	memset(hw_info->desc, 0x00, sizeof(*hw_info->desc));

	hw_info->desc->src_addr  = alg_ctx->addr_in;
	hw_info->desc->src_len   = alg_ctx->count;
	hw_info->desc->next_addr = hw_info->desc_dma;

	hw_info->desc->dma_ctrl  = is_final ? LLI_DMA_CTRL_LAST : LLI_DMA_CTRL_PAUSE;
	hw_info->desc->dma_ctrl |= LLI_DMA_CTRL_SRC_DONE;

	if (!hw_info->is_started) {
		hw_info->is_started = true;
		hw_info->desc->user_define |= LLI_USER_CIPHER_START;
		hw_info->desc->user_define |= LLI_USER_STRING_START;

		CRYPTO_WRITE(rk_dev, CRYPTO_DMA_LLI_ADDR, hw_info->desc_dma);
		CRYPTO_WRITE(rk_dev, CRYPTO_HASH_CTL,
			     (CRYPTO_HASH_ENABLE << CRYPTO_WRITE_MASK_SHIFT) |
			     CRYPTO_HASH_ENABLE);

		hw_info->hash_calc_cnt = 0;
	}

	if (is_final && alg_ctx->left_bytes == 0) {
		hw_info->desc->user_define |= LLI_USER_STRING_LAST;
		hw_info->is_started = false;
	}

	CRYPTO_TRACE("dma_ctrl = %08x, user_define = %08x, len = %u",
		     hw_info->desc->dma_ctrl, hw_info->desc->user_define, alg_ctx->count);

	hw_info->hash_calc_cnt += alg_ctx->count;

	dma_wmb();

	/* enable crypto irq */
	CRYPTO_WRITE(rk_dev, CRYPTO_DMA_INT_EN, 0x7f);

	CRYPTO_WRITE(rk_dev, CRYPTO_DMA_CTL, dma_ctl | dma_ctl << CRYPTO_WRITE_MASK_SHIFT);

	return 0;
}

static int rk_ahash_set_data_start(struct rk_crypto_dev *rk_dev, bool is_final)
{
	int err;
	struct rk_alg_ctx *alg_ctx = rk_alg_ctx_cast(rk_dev);

	CRYPTO_TRACE();

	err = rk_dev->load_data(rk_dev, alg_ctx->sg_src, alg_ctx->sg_dst);
	if (!err)
		err = rk_ahash_dma_start(rk_dev, is_final);

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

static int rk_ahash_start(struct rk_crypto_dev *rk_dev)
{
	struct ahash_request *req = ahash_request_cast(rk_dev->async_req);
	struct rk_alg_ctx *alg_ctx = rk_alg_ctx_cast(rk_dev);
	struct rk_ahash_ctx *ctx = rk_ahash_ctx_cast(rk_dev);
	struct rk_ahash_rctx *rctx = ahash_request_ctx(req);
	struct scatterlist *src_sg;
	unsigned int nbytes;
	int ret = 0;

	CRYPTO_TRACE("origin: old_len = %u, new_len = %u, nbytes = %u, is_final = %d",
		     ctx->hash_tmp_len, ctx->lastc_len, req->nbytes, rctx->is_final);

	/* update 0Byte do nothing */
	if (req->nbytes == 0 && !rctx->is_final)
		goto no_calc;

	if (ctx->lastc_len) {
		/* move lastc saved last time to the head of this calculation */
		memcpy(ctx->hash_tmp + ctx->hash_tmp_len, ctx->lastc, ctx->lastc_len);
		ctx->hash_tmp_len = ctx->hash_tmp_len + ctx->lastc_len;
		ctx->lastc_len = 0;
	}

	CRYPTO_TRACE("hash_tmp_len = %u", ctx->hash_tmp_len);

	/* final request no need to save lastc_new */
	if (!rctx->is_final) {
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
	} else {
		/* final just calc lastc_old */
		nbytes = ctx->hash_tmp_len;
		CRYPTO_TRACE("nbytes = %u", nbytes);
	}

	if (ctx->hash_tmp_len) {
		/* Concatenate old data to the header */
		sg_init_table(ctx->hash_sg, ARRAY_SIZE(ctx->hash_sg));
		sg_set_buf(ctx->hash_sg, ctx->hash_tmp, ctx->hash_tmp_len);
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

	if (nbytes)
		ret = rk_ahash_set_data_start(rk_dev, rctx->is_final);

exit:
	return ret;
no_calc:
	CRYPTO_TRACE("no calc");
	rk_alg_ctx_clear(alg_ctx);

	return 0;
}

static int rk_ahash_get_zero_result(struct rk_crypto_dev *rk_dev,
				    uint8_t *data, uint32_t data_len)
{

	struct ahash_request *req =
		ahash_request_cast(rk_dev->async_req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct rk_crypto_algt *algt = rk_ahash_get_algt(tfm);
	struct rk_ahash_ctx *ctx = crypto_ahash_ctx(tfm);

	return rk_ahash_fallback_digest(algt->name, algt->type == ALG_TYPE_HMAC,
					ctx->authkey, ctx->authkey_len,
					NULL, 0,
					data, data_len);

}
static int rk_ahash_get_result(struct rk_crypto_dev *rk_dev,
			       uint8_t *data, uint32_t data_len)
{
	int ret = 0;
	u32 i, offset;
	u32 reg_ctrl = 0;
	struct rk_hw_crypto_v2_info *hw_info =
			(struct rk_hw_crypto_v2_info *)rk_dev->hw_info;

	hw_info->is_started = false;

	/* use fallback hash */
	if (hw_info->hash_calc_cnt == 0) {
		CRYPTO_TRACE("use fallback hash");
		ret = rk_ahash_get_zero_result(rk_dev, data, data_len);
		goto exit;
	}

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
	hw_info->hash_calc_cnt = 0;
	return ret;
}

static int rk_ahash_crypto_rx(struct rk_crypto_dev *rk_dev)
{
	int err = 0;
	struct ahash_request *req = ahash_request_cast(rk_dev->async_req);
	struct rk_alg_ctx *alg_ctx = rk_alg_ctx_cast(rk_dev);
	struct rk_ahash_rctx *rctx = ahash_request_ctx(req);

	CRYPTO_TRACE("left bytes = %u, is_final = %d", alg_ctx->left_bytes, rctx->is_final);

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
		err = rk_ahash_set_data_start(rk_dev, rctx->is_final);
	} else {
		/*
		 * it will take some time to process date after last dma
		 * transmission.
		 */
		struct crypto_ahash *tfm;

		if (!rctx->is_final)
			goto out_rx;

		if (!req->result) {
			err = -EINVAL;
			goto out_rx;
		}

		tfm = crypto_ahash_reqtfm(req);

		err = rk_ahash_get_result(rk_dev, req->result,
					  crypto_ahash_digestsize(tfm));
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

	alg_ctx->align_size     = RK_DMA_ALIGNMENT;

	alg_ctx->ops.start      = rk_ahash_start;
	alg_ctx->ops.update     = rk_ahash_crypto_rx;
	alg_ctx->ops.complete   = rk_ahash_crypto_complete;
	alg_ctx->ops.irq_handle = rk_crypto_irq_handle;

	ctx->rk_dev   = rk_dev;
	ctx->hash_tmp = (u8 *)get_zeroed_page(GFP_KERNEL | GFP_DMA32);
	if (!ctx->hash_tmp) {
		dev_err(rk_dev->dev, "Can't get zeroed page for hash tmp.\n");
		return -ENOMEM;
	}

	rk_dev->request_crypto(rk_dev, alg_name);

	crypto_ahash_set_reqsize(__crypto_ahash_cast(tfm), sizeof(struct rk_ahash_rctx));

	algt->alg.hash.halg.statesize = sizeof(struct rk_ahash_expt_ctx);

	return 0;
}

static void rk_cra_hash_exit(struct crypto_tfm *tfm)
{
	struct rk_ahash_ctx *ctx = crypto_tfm_ctx(tfm);

	CRYPTO_TRACE();

	clean_hash_setting(ctx->rk_dev);

	if (ctx->hash_tmp)
		free_page((unsigned long)ctx->hash_tmp);

	ctx->rk_dev->release_crypto(ctx->rk_dev, crypto_tfm_alg_name(tfm));
}

struct rk_crypto_algt rk_v2_ahash_md5    = RK_HASH_ALGO_INIT(MD5, md5);
struct rk_crypto_algt rk_v2_ahash_sha1   = RK_HASH_ALGO_INIT(SHA1, sha1);
struct rk_crypto_algt rk_v2_ahash_sha224 = RK_HASH_ALGO_INIT(SHA224, sha224);
struct rk_crypto_algt rk_v2_ahash_sha256 = RK_HASH_ALGO_INIT(SHA256, sha256);
struct rk_crypto_algt rk_v2_ahash_sha384 = RK_HASH_ALGO_INIT(SHA384, sha384);
struct rk_crypto_algt rk_v2_ahash_sha512 = RK_HASH_ALGO_INIT(SHA512, sha512);
struct rk_crypto_algt rk_v2_ahash_sm3    = RK_HASH_ALGO_INIT(SM3, sm3);

struct rk_crypto_algt rk_v2_hmac_md5     = RK_HMAC_ALGO_INIT(MD5, md5);
struct rk_crypto_algt rk_v2_hmac_sha1    = RK_HMAC_ALGO_INIT(SHA1, sha1);
struct rk_crypto_algt rk_v2_hmac_sha256  = RK_HMAC_ALGO_INIT(SHA256, sha256);
struct rk_crypto_algt rk_v2_hmac_sha512  = RK_HMAC_ALGO_INIT(SHA512, sha512);
struct rk_crypto_algt rk_v2_hmac_sm3     = RK_HMAC_ALGO_INIT(SM3, sm3);

