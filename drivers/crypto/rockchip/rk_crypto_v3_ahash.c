// SPDX-License-Identifier: GPL-2.0
/*
 * Hash acceleration support for Rockchip Crypto v3
 *
 * Copyright (c) 2022, Rockchip Electronics Co., Ltd
 *
 * Author: Lin Jinhan <troy.lin@rock-chips.com>
 *
 */

#include <linux/slab.h>
#include <linux/iopoll.h>

#include "rk_crypto_core.h"
#include "rk_crypto_v3.h"
#include "rk_crypto_v3_reg.h"
#include "rk_crypto_ahash_utils.h"
#include "rk_crypto_utils.h"

#define RK_HASH_CTX_MAGIC	0x1A1A1A1A
#define RK_POLL_PERIOD_US	100
#define RK_POLL_TIMEOUT_US	50000

struct rk_ahash_expt_ctx {
	struct rk_ahash_ctx	ctx;
	u8			lastc[RK_DMA_ALIGNMENT];
};

struct rk_hash_mid_data {
	u32 valid_flag;
	u32 hash_ctl;
	u32 data[CRYPTO_HASH_MID_WORD_SIZE];
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
	ret = read_poll_timeout_atomic(CRYPTO_READ, tmp, !tmp, 0, pool_timeout_us,
				       false, rk_dev, CRYPTO_RST_CTL);
	if (ret)
		dev_err(rk_dev->dev, "cipher reset pool timeout %ums.",
			pool_timeout_us);

	CRYPTO_WRITE(rk_dev, CRYPTO_HASH_CTL, 0xffff0000);
}

static int rk_hash_mid_data_store(struct rk_crypto_dev *rk_dev, struct rk_hash_mid_data *mid_data)
{
	int ret;
	uint32_t reg_ctrl;

	CRYPTO_TRACE();

	ret = read_poll_timeout_atomic(CRYPTO_READ,
					reg_ctrl,
					reg_ctrl & CRYPTO_HASH_MID_IS_VALID,
					0,
					RK_POLL_TIMEOUT_US,
					false, rk_dev, CRYPTO_MID_VALID);

	CRYPTO_WRITE(rk_dev, CRYPTO_MID_VALID_SWITCH,
		     CRYPTO_MID_VALID_ENABLE << CRYPTO_WRITE_MASK_SHIFT);
	if (ret) {
		CRYPTO_TRACE("CRYPTO_MID_VALID timeout.");
		goto exit;
	}

	CRYPTO_WRITE(rk_dev, CRYPTO_MID_VALID,
		     CRYPTO_HASH_MID_IS_VALID |
		     CRYPTO_HASH_MID_IS_VALID << CRYPTO_WRITE_MASK_SHIFT);

	rk_crypto_read_regs(rk_dev, CRYPTO_HASH_MID_DATA_0,
			    (u8 *)mid_data->data, sizeof(mid_data->data));

	mid_data->hash_ctl   = CRYPTO_READ(rk_dev, CRYPTO_HASH_CTL);
	mid_data->valid_flag = 1;

	CRYPTO_WRITE(rk_dev, CRYPTO_HASH_CTL, 0 | CRYPTO_WRITE_MASK_ALL);

exit:
	return ret;
}

static int rk_hash_mid_data_restore(struct rk_crypto_dev *rk_dev, struct rk_hash_mid_data *mid_data)
{
	CRYPTO_TRACE();

	CRYPTO_WRITE(rk_dev, CRYPTO_MID_VALID_SWITCH,
		     CRYPTO_MID_VALID_ENABLE | CRYPTO_MID_VALID_ENABLE << CRYPTO_WRITE_MASK_SHIFT);

	CRYPTO_WRITE(rk_dev, CRYPTO_MID_VALID,
		     CRYPTO_HASH_MID_IS_VALID |
		     CRYPTO_HASH_MID_IS_VALID << CRYPTO_WRITE_MASK_SHIFT);

	if (!mid_data->valid_flag) {
		CRYPTO_TRACE("clear mid data");
		rk_crypto_clear_regs(rk_dev, CRYPTO_HASH_MID_DATA_0, ARRAY_SIZE(mid_data->data));
		return 0;
	}

	rk_crypto_write_regs(rk_dev, CRYPTO_HASH_MID_DATA_0,
			     (u8 *)mid_data->data, sizeof(mid_data->data));

	CRYPTO_WRITE(rk_dev, CRYPTO_HASH_CTL, mid_data->hash_ctl | CRYPTO_WRITE_MASK_ALL);

	return 0;
}

static int rk_crypto_irq_handle(int irq, void *dev_id)
{
	struct rk_crypto_dev *rk_dev  = platform_get_drvdata(dev_id);
	u32 interrupt_status;
	struct rk_hw_crypto_v3_info *hw_info =
			(struct rk_hw_crypto_v3_info *)rk_dev->hw_info;
	struct rk_alg_ctx *alg_ctx = rk_ahash_alg_ctx(rk_dev);

	/* disable crypto irq */
	CRYPTO_WRITE(rk_dev, CRYPTO_DMA_INT_EN, 0);

	interrupt_status = CRYPTO_READ(rk_dev, CRYPTO_DMA_INT_ST);
	CRYPTO_WRITE(rk_dev, CRYPTO_DMA_INT_ST, interrupt_status);

	interrupt_status &= CRYPTO_LOCKSTEP_MASK;

	if (interrupt_status != CRYPTO_SRC_ITEM_DONE_INT_ST) {
		dev_err(rk_dev->dev, "DMA desc = %p\n", hw_info->hw_desc.lli_head);
		dev_err(rk_dev->dev, "DMA addr_in = %08x\n",
			(u32)alg_ctx->addr_in);
		dev_err(rk_dev->dev, "DMA addr_out = %08x\n",
			(u32)alg_ctx->addr_out);
		dev_err(rk_dev->dev, "DMA count = %08x\n", alg_ctx->count);
		dev_err(rk_dev->dev, "DMA desc_dma = %08x\n",
			(u32)hw_info->hw_desc.lli_head_dma);
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
	struct rk_alg_ctx *alg_ctx = rk_ahash_alg_ctx(ctx->rk_dev);

	struct rk_hw_crypto_v3_info *hw_info = ctx->rk_dev->hw_info;
	struct crypto_lli_desc *lli_desc = hw_info->hw_desc.lli_head;

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

	if (alg_ctx->total)
		rk_hash_mid_data_store(ctx->rk_dev, (struct rk_hash_mid_data *)ctx->priv);

	if (base->complete)
		base->complete(base, err);
}

static inline void clear_hash_out_reg(struct rk_crypto_dev *rk_dev)
{
	rk_crypto_clear_regs(rk_dev, CRYPTO_HASH_DOUT_0, 16);
}

static int write_key_reg(struct rk_crypto_dev *rk_dev, const u8 *key,
			  u32 key_len)
{
	rk_crypto_write_regs(rk_dev, CRYPTO_CH0_KEY_0, key, key_len);

	return 0;
}

static int rk_hw_hash_init(struct rk_crypto_dev *rk_dev, u32 algo, u32 type)
{
	u32 reg_ctrl = 0;
	struct rk_ahash_ctx *ctx = rk_ahash_ctx_cast(rk_dev);
	struct rk_hash_mid_data *mid_data = (struct rk_hash_mid_data *)ctx->priv;

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

	memset(mid_data, 0x00, sizeof(*mid_data));

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

static int rk_ahash_dma_start(struct rk_crypto_dev *rk_dev, uint32_t flag)
{
	struct rk_hw_crypto_v3_info *hw_info =
			(struct rk_hw_crypto_v3_info *)rk_dev->hw_info;
	struct rk_alg_ctx *alg_ctx = rk_ahash_alg_ctx(rk_dev);
	struct rk_ahash_ctx *ctx = rk_ahash_ctx_cast(rk_dev);
	struct crypto_lli_desc *lli_head, *lli_tail;
	u32 dma_ctl = CRYPTO_DMA_RESTART;
	bool is_final = flag & RK_FLAG_FINAL;
	int ret;

	CRYPTO_TRACE("ctx->calc_cnt = %u, count %u Byte, is_final = %d",
		     ctx->calc_cnt, alg_ctx->count, is_final);

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

	if (alg_ctx->total == alg_ctx->left_bytes + alg_ctx->count)
		rk_hash_mid_data_restore(rk_dev, (struct rk_hash_mid_data *)ctx->priv);

	if (alg_ctx->aligned)
		ret = rk_crypto_hw_desc_init(&hw_info->hw_desc,
					     alg_ctx->sg_src, NULL, alg_ctx->count);
	else
		ret = rk_crypto_hw_desc_init(&hw_info->hw_desc,
					     &alg_ctx->sg_tmp, NULL, alg_ctx->count);
	if (ret)
		return ret;

	lli_head = hw_info->hw_desc.lli_head;
	lli_tail = hw_info->hw_desc.lli_tail;

	lli_tail->dma_ctrl  = is_final ? LLI_DMA_CTRL_LAST : LLI_DMA_CTRL_PAUSE;
	lli_tail->dma_ctrl |= LLI_DMA_CTRL_SRC_DONE;

	if (ctx->calc_cnt == 0) {
		dma_ctl = CRYPTO_DMA_START;

		lli_head->user_define |= LLI_USER_CIPHER_START;
		lli_head->user_define |= LLI_USER_STRING_START;

		CRYPTO_WRITE(rk_dev, CRYPTO_DMA_LLI_ADDR, hw_info->hw_desc.lli_head_dma);
		CRYPTO_WRITE(rk_dev, CRYPTO_HASH_CTL,
			     (CRYPTO_HASH_ENABLE << CRYPTO_WRITE_MASK_SHIFT) |
			     CRYPTO_HASH_ENABLE);
	}

	if (is_final && alg_ctx->left_bytes == 0)
		lli_tail->user_define |= LLI_USER_STRING_LAST;

	CRYPTO_TRACE("dma_ctrl = %08x, user_define = %08x, len = %u",
		     lli_head->dma_ctrl, lli_head->user_define, alg_ctx->count);

	rk_crypto_dump_hw_desc(&hw_info->hw_desc);

	dma_wmb();

	/* enable crypto irq */
	CRYPTO_WRITE(rk_dev, CRYPTO_DMA_INT_EN, 0x7f);

	CRYPTO_WRITE(rk_dev, CRYPTO_DMA_CTL, dma_ctl | dma_ctl << CRYPTO_WRITE_MASK_SHIFT);

	return 0;
}

static int rk_ahash_get_result(struct rk_crypto_dev *rk_dev,
			       uint8_t *data, uint32_t data_len)
{
	int ret = 0;
	u32 reg_ctrl = 0;
	struct rk_ahash_ctx *ctx = rk_ahash_ctx_cast(rk_dev);

	memset(ctx->priv, 0x00, sizeof(struct rk_hash_mid_data));

	ret = read_poll_timeout_atomic(CRYPTO_READ, reg_ctrl,
				       reg_ctrl & CRYPTO_HASH_IS_VALID,
				       RK_POLL_PERIOD_US,
				       RK_POLL_TIMEOUT_US, false,
				       rk_dev, CRYPTO_HASH_VALID);
	if (ret)
		goto exit;

	rk_crypto_read_regs(rk_dev, CRYPTO_HASH_DOUT_0, data, data_len);

	CRYPTO_WRITE(rk_dev, CRYPTO_HASH_VALID, CRYPTO_HASH_IS_VALID);

exit:
	clean_hash_setting(rk_dev);

	return ret;
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

	alg_ctx->ops.hw_write_key  = write_key_reg;
	alg_ctx->ops.hw_init       = rk_hw_hash_init;
	alg_ctx->ops.hw_dma_start  = rk_ahash_dma_start;
	alg_ctx->ops.hw_get_result = rk_ahash_get_result;

	ctx->rk_dev   = rk_dev;
	ctx->hash_tmp = (u8 *)get_zeroed_page(GFP_KERNEL | GFP_DMA32);
	if (!ctx->hash_tmp) {
		dev_err(rk_dev->dev, "Can't get zeroed page for hash tmp.\n");
		return -ENOMEM;
	}

	ctx->priv = kmalloc(sizeof(struct rk_hash_mid_data), GFP_KERNEL);
	if (!ctx->priv) {
		free_page((unsigned long)ctx->hash_tmp);
		return -ENOMEM;
	}

	memset(ctx->priv, 0x00, sizeof(struct rk_hash_mid_data));

	rk_dev->request_crypto(rk_dev, alg_name);

	crypto_ahash_set_reqsize(__crypto_ahash_cast(tfm), sizeof(struct rk_ahash_rctx));

	algt->alg.hash.halg.statesize = sizeof(struct rk_ahash_expt_ctx);

	return 0;
}

static void rk_cra_hash_exit(struct crypto_tfm *tfm)
{
	struct rk_ahash_ctx *ctx = crypto_tfm_ctx(tfm);

	CRYPTO_TRACE();

	if (ctx->hash_tmp)
		free_page((unsigned long)ctx->hash_tmp);

	kfree(ctx->priv);

	ctx->rk_dev->release_crypto(ctx->rk_dev, crypto_tfm_alg_name(tfm));
}

struct rk_crypto_algt rk_v3_ahash_md5    = RK_HASH_ALGO_INIT(MD5, md5);
struct rk_crypto_algt rk_v3_ahash_sha1   = RK_HASH_ALGO_INIT(SHA1, sha1);
struct rk_crypto_algt rk_v3_ahash_sha224 = RK_HASH_ALGO_INIT(SHA224, sha224);
struct rk_crypto_algt rk_v3_ahash_sha256 = RK_HASH_ALGO_INIT(SHA256, sha256);
struct rk_crypto_algt rk_v3_ahash_sha384 = RK_HASH_ALGO_INIT(SHA384, sha384);
struct rk_crypto_algt rk_v3_ahash_sha512 = RK_HASH_ALGO_INIT(SHA512, sha512);
struct rk_crypto_algt rk_v3_ahash_sm3    = RK_HASH_ALGO_INIT(SM3, sm3);

struct rk_crypto_algt rk_v3_hmac_md5     = RK_HMAC_ALGO_INIT(MD5, md5);
struct rk_crypto_algt rk_v3_hmac_sha1    = RK_HMAC_ALGO_INIT(SHA1, sha1);
struct rk_crypto_algt rk_v3_hmac_sha256  = RK_HMAC_ALGO_INIT(SHA256, sha256);
struct rk_crypto_algt rk_v3_hmac_sha512  = RK_HMAC_ALGO_INIT(SHA512, sha512);
struct rk_crypto_algt rk_v3_hmac_sm3     = RK_HMAC_ALGO_INIT(SM3, sm3);

