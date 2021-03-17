// SPDX-License-Identifier: GPL-2.0
/*
 * sun8i-ce-prng.c - hardware cryptographic offloader for
 * Allwinner H3/A64/H5/H2+/H6/R40 SoC
 *
 * Copyright (C) 2015-2020 Corentin Labbe <clabbe@baylibre.com>
 *
 * This file handle the PRNG
 *
 * You could find a link for the datasheet in Documentation/arm/sunxi.rst
 */
#include "sun8i-ce.h"
#include <linux/dma-mapping.h>
#include <linux/pm_runtime.h>
#include <crypto/internal/rng.h>

int sun8i_ce_prng_init(struct crypto_tfm *tfm)
{
	struct sun8i_ce_rng_tfm_ctx *ctx = crypto_tfm_ctx(tfm);

	memset(ctx, 0, sizeof(struct sun8i_ce_rng_tfm_ctx));
	return 0;
}

void sun8i_ce_prng_exit(struct crypto_tfm *tfm)
{
	struct sun8i_ce_rng_tfm_ctx *ctx = crypto_tfm_ctx(tfm);

	memzero_explicit(ctx->seed, ctx->slen);
	kfree(ctx->seed);
	ctx->seed = NULL;
	ctx->slen = 0;
}

int sun8i_ce_prng_seed(struct crypto_rng *tfm, const u8 *seed,
		       unsigned int slen)
{
	struct sun8i_ce_rng_tfm_ctx *ctx = crypto_rng_ctx(tfm);

	if (ctx->seed && ctx->slen != slen) {
		memzero_explicit(ctx->seed, ctx->slen);
		kfree(ctx->seed);
		ctx->slen = 0;
		ctx->seed = NULL;
	}
	if (!ctx->seed)
		ctx->seed = kmalloc(slen, GFP_KERNEL | GFP_DMA);
	if (!ctx->seed)
		return -ENOMEM;

	memcpy(ctx->seed, seed, slen);
	ctx->slen = slen;

	return 0;
}

int sun8i_ce_prng_generate(struct crypto_rng *tfm, const u8 *src,
			   unsigned int slen, u8 *dst, unsigned int dlen)
{
	struct sun8i_ce_rng_tfm_ctx *ctx = crypto_rng_ctx(tfm);
	struct rng_alg *alg = crypto_rng_alg(tfm);
	struct sun8i_ce_alg_template *algt;
	struct sun8i_ce_dev *ce;
	dma_addr_t dma_iv, dma_dst;
	int err = 0;
	int flow = 3;
	unsigned int todo;
	struct sun8i_ce_flow *chan;
	struct ce_task *cet;
	u32 common, sym;
	void *d;

	algt = container_of(alg, struct sun8i_ce_alg_template, alg.rng);
	ce = algt->ce;

	if (ctx->slen == 0) {
		dev_err(ce->dev, "not seeded\n");
		return -EINVAL;
	}

	/* we want dlen + seedsize rounded up to a multiple of PRNG_DATA_SIZE */
	todo = dlen + ctx->slen + PRNG_DATA_SIZE * 2;
	todo -= todo % PRNG_DATA_SIZE;

	d = kzalloc(todo, GFP_KERNEL | GFP_DMA);
	if (!d) {
		err = -ENOMEM;
		goto err_mem;
	}

	dev_dbg(ce->dev, "%s PRNG slen=%u dlen=%u todo=%u multi=%u\n", __func__,
		slen, dlen, todo, todo / PRNG_DATA_SIZE);

#ifdef CONFIG_CRYPTO_DEV_SUN8I_CE_DEBUG
	algt->stat_req++;
	algt->stat_bytes += todo;
#endif

	dma_iv = dma_map_single(ce->dev, ctx->seed, ctx->slen, DMA_TO_DEVICE);
	if (dma_mapping_error(ce->dev, dma_iv)) {
		dev_err(ce->dev, "Cannot DMA MAP IV\n");
		goto err_iv;
	}

	dma_dst = dma_map_single(ce->dev, d, todo, DMA_FROM_DEVICE);
	if (dma_mapping_error(ce->dev, dma_dst)) {
		dev_err(ce->dev, "Cannot DMA MAP DST\n");
		err = -EFAULT;
		goto err_dst;
	}

	err = pm_runtime_get_sync(ce->dev);
	if (err < 0) {
		pm_runtime_put_noidle(ce->dev);
		goto err_pm;
	}

	mutex_lock(&ce->rnglock);
	chan = &ce->chanlist[flow];

	cet = &chan->tl[0];
	memset(cet, 0, sizeof(struct ce_task));

	cet->t_id = cpu_to_le32(flow);
	common = ce->variant->prng | CE_COMM_INT;
	cet->t_common_ctl = cpu_to_le32(common);

	/* recent CE (H6) need length in bytes, in word otherwise */
	if (ce->variant->prng_t_dlen_in_bytes)
		cet->t_dlen = cpu_to_le32(todo);
	else
		cet->t_dlen = cpu_to_le32(todo / 4);

	sym = PRNG_LD;
	cet->t_sym_ctl = cpu_to_le32(sym);
	cet->t_asym_ctl = 0;

	cet->t_key = cpu_to_le32(dma_iv);
	cet->t_iv = cpu_to_le32(dma_iv);

	cet->t_dst[0].addr = cpu_to_le32(dma_dst);
	cet->t_dst[0].len = cpu_to_le32(todo / 4);
	ce->chanlist[flow].timeout = 2000;

	err = sun8i_ce_run_task(ce, 3, "PRNG");
	mutex_unlock(&ce->rnglock);

	pm_runtime_put(ce->dev);

err_pm:
	dma_unmap_single(ce->dev, dma_dst, todo, DMA_FROM_DEVICE);
err_dst:
	dma_unmap_single(ce->dev, dma_iv, ctx->slen, DMA_TO_DEVICE);

	if (!err) {
		memcpy(dst, d, dlen);
		memcpy(ctx->seed, d + dlen, ctx->slen);
	}
	memzero_explicit(d, todo);
err_iv:
	kfree(d);
err_mem:
	return err;
}
