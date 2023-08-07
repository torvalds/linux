// SPDX-License-Identifier: GPL-2.0
/*
 * sun8i-ss-prng.c - hardware cryptographic offloader for
 * Allwinner A80/A83T SoC
 *
 * Copyright (C) 2015-2020 Corentin Labbe <clabbe@baylibre.com>
 *
 * This file handle the PRNG found in the SS
 *
 * You could find a link for the datasheet in Documentation/arch/arm/sunxi.rst
 */
#include "sun8i-ss.h"
#include <linux/dma-mapping.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/pm_runtime.h>
#include <crypto/internal/rng.h>

int sun8i_ss_prng_seed(struct crypto_rng *tfm, const u8 *seed,
		       unsigned int slen)
{
	struct sun8i_ss_rng_tfm_ctx *ctx = crypto_rng_ctx(tfm);

	if (ctx->seed && ctx->slen != slen) {
		kfree_sensitive(ctx->seed);
		ctx->slen = 0;
		ctx->seed = NULL;
	}
	if (!ctx->seed)
		ctx->seed = kmalloc(slen, GFP_KERNEL);
	if (!ctx->seed)
		return -ENOMEM;

	memcpy(ctx->seed, seed, slen);
	ctx->slen = slen;

	return 0;
}

int sun8i_ss_prng_init(struct crypto_tfm *tfm)
{
	struct sun8i_ss_rng_tfm_ctx *ctx = crypto_tfm_ctx(tfm);

	memset(ctx, 0, sizeof(struct sun8i_ss_rng_tfm_ctx));
	return 0;
}

void sun8i_ss_prng_exit(struct crypto_tfm *tfm)
{
	struct sun8i_ss_rng_tfm_ctx *ctx = crypto_tfm_ctx(tfm);

	kfree_sensitive(ctx->seed);
	ctx->seed = NULL;
	ctx->slen = 0;
}

int sun8i_ss_prng_generate(struct crypto_rng *tfm, const u8 *src,
			   unsigned int slen, u8 *dst, unsigned int dlen)
{
	struct sun8i_ss_rng_tfm_ctx *ctx = crypto_rng_ctx(tfm);
	struct rng_alg *alg = crypto_rng_alg(tfm);
	struct sun8i_ss_alg_template *algt;
	unsigned int todo_with_padding;
	struct sun8i_ss_dev *ss;
	dma_addr_t dma_iv, dma_dst;
	unsigned int todo;
	int err = 0;
	int flow;
	void *d;
	u32 v;

	algt = container_of(alg, struct sun8i_ss_alg_template, alg.rng);
	ss = algt->ss;

	if (ctx->slen == 0) {
		dev_err(ss->dev, "The PRNG is not seeded\n");
		return -EINVAL;
	}

	/* The SS does not give an updated seed, so we need to get a new one.
	 * So we will ask for an extra PRNG_SEED_SIZE data.
	 * We want dlen + seedsize rounded up to a multiple of PRNG_DATA_SIZE
	 */
	todo = dlen + PRNG_SEED_SIZE + PRNG_DATA_SIZE;
	todo -= todo % PRNG_DATA_SIZE;

	todo_with_padding = ALIGN(todo, dma_get_cache_alignment());
	if (todo_with_padding < todo || todo < dlen)
		return -EOVERFLOW;

	d = kzalloc(todo_with_padding, GFP_KERNEL);
	if (!d)
		return -ENOMEM;

	flow = sun8i_ss_get_engine_number(ss);

#ifdef CONFIG_CRYPTO_DEV_SUN8I_SS_DEBUG
	algt->stat_req++;
	algt->stat_bytes += todo;
#endif

	v = SS_ALG_PRNG | SS_PRNG_CONTINUE | SS_START;
	if (flow)
		v |= SS_FLOW1;
	else
		v |= SS_FLOW0;

	dma_iv = dma_map_single(ss->dev, ctx->seed, ctx->slen, DMA_TO_DEVICE);
	if (dma_mapping_error(ss->dev, dma_iv)) {
		dev_err(ss->dev, "Cannot DMA MAP IV\n");
		err = -EFAULT;
		goto err_free;
	}

	dma_dst = dma_map_single(ss->dev, d, todo, DMA_FROM_DEVICE);
	if (dma_mapping_error(ss->dev, dma_dst)) {
		dev_err(ss->dev, "Cannot DMA MAP DST\n");
		err = -EFAULT;
		goto err_iv;
	}

	err = pm_runtime_resume_and_get(ss->dev);
	if (err < 0)
		goto err_pm;
	err = 0;

	mutex_lock(&ss->mlock);
	writel(dma_iv, ss->base + SS_IV_ADR_REG);
	/* the PRNG act badly (failing rngtest) without SS_KEY_ADR_REG set */
	writel(dma_iv, ss->base + SS_KEY_ADR_REG);
	writel(dma_dst, ss->base + SS_DST_ADR_REG);
	writel(todo / 4, ss->base + SS_LEN_ADR_REG);

	reinit_completion(&ss->flows[flow].complete);
	ss->flows[flow].status = 0;
	/* Be sure all data is written before enabling the task */
	wmb();

	writel(v, ss->base + SS_CTL_REG);

	wait_for_completion_interruptible_timeout(&ss->flows[flow].complete,
						  msecs_to_jiffies(todo));
	if (ss->flows[flow].status == 0) {
		dev_err(ss->dev, "DMA timeout for PRNG (size=%u)\n", todo);
		err = -EFAULT;
	}
	/* Since cipher and hash use the linux/cryptoengine and that we have
	 * a cryptoengine per flow, we are sure that they will issue only one
	 * request per flow.
	 * Since the cryptoengine wait for completion before submitting a new
	 * one, the mlock could be left just after the final writel.
	 * But cryptoengine cannot handle crypto_rng, so we need to be sure
	 * nothing will use our flow.
	 * The easiest way is to grab mlock until the hardware end our requests.
	 * We could have used a per flow lock, but this would increase
	 * complexity.
	 * The drawback is that no request could be handled for the other flow.
	 */
	mutex_unlock(&ss->mlock);

	pm_runtime_put(ss->dev);

err_pm:
	dma_unmap_single(ss->dev, dma_dst, todo, DMA_FROM_DEVICE);
err_iv:
	dma_unmap_single(ss->dev, dma_iv, ctx->slen, DMA_TO_DEVICE);

	if (!err) {
		memcpy(dst, d, dlen);
		/* Update seed */
		memcpy(ctx->seed, d + dlen, ctx->slen);
	}
err_free:
	kfree_sensitive(d);

	return err;
}
