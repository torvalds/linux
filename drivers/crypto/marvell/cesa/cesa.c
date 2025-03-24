// SPDX-License-Identifier: GPL-2.0-only
/*
 * Support for Marvell's Cryptographic Engine and Security Accelerator (CESA)
 * that can be found on the following platform: Orion, Kirkwood, Armada. This
 * driver supports the TDMA engine on platforms on which it is available.
 *
 * Author: Boris Brezillon <boris.brezillon@free-electrons.com>
 * Author: Arnaud Ebalard <arno@natisbad.org>
 *
 * This work is based on an initial version written by
 * Sebastian Andrzej Siewior < sebastian at breakpoint dot cc >
 */

#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/genalloc.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kthread.h>
#include <linux/mbus.h>
#include <linux/platform_device.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_irq.h>

#include "cesa.h"

/* Limit of the crypto queue before reaching the backlog */
#define CESA_CRYPTO_DEFAULT_MAX_QLEN 128

struct mv_cesa_dev *cesa_dev;

struct crypto_async_request *
mv_cesa_dequeue_req_locked(struct mv_cesa_engine *engine,
			   struct crypto_async_request **backlog)
{
	struct crypto_async_request *req;

	*backlog = crypto_get_backlog(&engine->queue);
	req = crypto_dequeue_request(&engine->queue);

	if (!req)
		return NULL;

	return req;
}

static void mv_cesa_rearm_engine(struct mv_cesa_engine *engine)
{
	struct crypto_async_request *req = NULL, *backlog = NULL;
	struct mv_cesa_ctx *ctx;


	spin_lock_bh(&engine->lock);
	if (!engine->req) {
		req = mv_cesa_dequeue_req_locked(engine, &backlog);
		engine->req = req;
	}
	spin_unlock_bh(&engine->lock);

	if (!req)
		return;

	if (backlog)
		crypto_request_complete(backlog, -EINPROGRESS);

	ctx = crypto_tfm_ctx(req->tfm);
	ctx->ops->step(req);
}

static int mv_cesa_std_process(struct mv_cesa_engine *engine, u32 status)
{
	struct crypto_async_request *req;
	struct mv_cesa_ctx *ctx;
	int res;

	req = engine->req;
	ctx = crypto_tfm_ctx(req->tfm);
	res = ctx->ops->process(req, status);

	if (res == 0) {
		ctx->ops->complete(req);
		mv_cesa_engine_enqueue_complete_request(engine, req);
	} else if (res == -EINPROGRESS) {
		ctx->ops->step(req);
	}

	return res;
}

static int mv_cesa_int_process(struct mv_cesa_engine *engine, u32 status)
{
	if (engine->chain.first && engine->chain.last)
		return mv_cesa_tdma_process(engine, status);

	return mv_cesa_std_process(engine, status);
}

static inline void
mv_cesa_complete_req(struct mv_cesa_ctx *ctx, struct crypto_async_request *req,
		     int res)
{
	ctx->ops->cleanup(req);
	local_bh_disable();
	crypto_request_complete(req, res);
	local_bh_enable();
}

static irqreturn_t mv_cesa_int(int irq, void *priv)
{
	struct mv_cesa_engine *engine = priv;
	struct crypto_async_request *req;
	struct mv_cesa_ctx *ctx;
	u32 status, mask;
	irqreturn_t ret = IRQ_NONE;

	while (true) {
		int res;

		mask = mv_cesa_get_int_mask(engine);
		status = readl(engine->regs + CESA_SA_INT_STATUS);

		if (!(status & mask))
			break;

		/*
		 * TODO: avoid clearing the FPGA_INT_STATUS if this not
		 * relevant on some platforms.
		 */
		writel(~status, engine->regs + CESA_SA_FPGA_INT_STATUS);
		writel(~status, engine->regs + CESA_SA_INT_STATUS);

		/* Process fetched requests */
		res = mv_cesa_int_process(engine, status & mask);
		ret = IRQ_HANDLED;

		spin_lock_bh(&engine->lock);
		req = engine->req;
		if (res != -EINPROGRESS)
			engine->req = NULL;
		spin_unlock_bh(&engine->lock);

		ctx = crypto_tfm_ctx(req->tfm);

		if (res && res != -EINPROGRESS)
			mv_cesa_complete_req(ctx, req, res);

		/* Launch the next pending request */
		mv_cesa_rearm_engine(engine);

		/* Iterate over the complete queue */
		while (true) {
			req = mv_cesa_engine_dequeue_complete_request(engine);
			if (!req)
				break;

			ctx = crypto_tfm_ctx(req->tfm);
			mv_cesa_complete_req(ctx, req, 0);
		}
	}

	return ret;
}

int mv_cesa_queue_req(struct crypto_async_request *req,
		      struct mv_cesa_req *creq)
{
	int ret;
	struct mv_cesa_engine *engine = creq->engine;

	spin_lock_bh(&engine->lock);
	ret = crypto_enqueue_request(&engine->queue, req);
	if ((mv_cesa_req_get_type(creq) == CESA_DMA_REQ) &&
	    (ret == -EINPROGRESS || ret == -EBUSY))
		mv_cesa_tdma_chain(engine, creq);
	spin_unlock_bh(&engine->lock);

	if (ret != -EINPROGRESS)
		return ret;

	mv_cesa_rearm_engine(engine);

	return -EINPROGRESS;
}

static int mv_cesa_add_algs(struct mv_cesa_dev *cesa)
{
	int ret;
	int i, j;

	for (i = 0; i < cesa->caps->ncipher_algs; i++) {
		ret = crypto_register_skcipher(cesa->caps->cipher_algs[i]);
		if (ret)
			goto err_unregister_crypto;
	}

	for (i = 0; i < cesa->caps->nahash_algs; i++) {
		ret = crypto_register_ahash(cesa->caps->ahash_algs[i]);
		if (ret)
			goto err_unregister_ahash;
	}

	return 0;

err_unregister_ahash:
	for (j = 0; j < i; j++)
		crypto_unregister_ahash(cesa->caps->ahash_algs[j]);
	i = cesa->caps->ncipher_algs;

err_unregister_crypto:
	for (j = 0; j < i; j++)
		crypto_unregister_skcipher(cesa->caps->cipher_algs[j]);

	return ret;
}

static void mv_cesa_remove_algs(struct mv_cesa_dev *cesa)
{
	int i;

	for (i = 0; i < cesa->caps->nahash_algs; i++)
		crypto_unregister_ahash(cesa->caps->ahash_algs[i]);

	for (i = 0; i < cesa->caps->ncipher_algs; i++)
		crypto_unregister_skcipher(cesa->caps->cipher_algs[i]);
}

static struct skcipher_alg *orion_cipher_algs[] = {
	&mv_cesa_ecb_des_alg,
	&mv_cesa_cbc_des_alg,
	&mv_cesa_ecb_des3_ede_alg,
	&mv_cesa_cbc_des3_ede_alg,
	&mv_cesa_ecb_aes_alg,
	&mv_cesa_cbc_aes_alg,
};

static struct ahash_alg *orion_ahash_algs[] = {
	&mv_md5_alg,
	&mv_sha1_alg,
	&mv_ahmac_md5_alg,
	&mv_ahmac_sha1_alg,
};

static struct skcipher_alg *armada_370_cipher_algs[] = {
	&mv_cesa_ecb_des_alg,
	&mv_cesa_cbc_des_alg,
	&mv_cesa_ecb_des3_ede_alg,
	&mv_cesa_cbc_des3_ede_alg,
	&mv_cesa_ecb_aes_alg,
	&mv_cesa_cbc_aes_alg,
};

static struct ahash_alg *armada_370_ahash_algs[] = {
	&mv_md5_alg,
	&mv_sha1_alg,
	&mv_sha256_alg,
	&mv_ahmac_md5_alg,
	&mv_ahmac_sha1_alg,
	&mv_ahmac_sha256_alg,
};

static const struct mv_cesa_caps orion_caps = {
	.nengines = 1,
	.cipher_algs = orion_cipher_algs,
	.ncipher_algs = ARRAY_SIZE(orion_cipher_algs),
	.ahash_algs = orion_ahash_algs,
	.nahash_algs = ARRAY_SIZE(orion_ahash_algs),
	.has_tdma = false,
};

static const struct mv_cesa_caps kirkwood_caps = {
	.nengines = 1,
	.cipher_algs = orion_cipher_algs,
	.ncipher_algs = ARRAY_SIZE(orion_cipher_algs),
	.ahash_algs = orion_ahash_algs,
	.nahash_algs = ARRAY_SIZE(orion_ahash_algs),
	.has_tdma = true,
};

static const struct mv_cesa_caps armada_370_caps = {
	.nengines = 1,
	.cipher_algs = armada_370_cipher_algs,
	.ncipher_algs = ARRAY_SIZE(armada_370_cipher_algs),
	.ahash_algs = armada_370_ahash_algs,
	.nahash_algs = ARRAY_SIZE(armada_370_ahash_algs),
	.has_tdma = true,
};

static const struct mv_cesa_caps armada_xp_caps = {
	.nengines = 2,
	.cipher_algs = armada_370_cipher_algs,
	.ncipher_algs = ARRAY_SIZE(armada_370_cipher_algs),
	.ahash_algs = armada_370_ahash_algs,
	.nahash_algs = ARRAY_SIZE(armada_370_ahash_algs),
	.has_tdma = true,
};

static const struct of_device_id mv_cesa_of_match_table[] = {
	{ .compatible = "marvell,orion-crypto", .data = &orion_caps },
	{ .compatible = "marvell,kirkwood-crypto", .data = &kirkwood_caps },
	{ .compatible = "marvell,dove-crypto", .data = &kirkwood_caps },
	{ .compatible = "marvell,armada-370-crypto", .data = &armada_370_caps },
	{ .compatible = "marvell,armada-xp-crypto", .data = &armada_xp_caps },
	{ .compatible = "marvell,armada-375-crypto", .data = &armada_xp_caps },
	{ .compatible = "marvell,armada-38x-crypto", .data = &armada_xp_caps },
	{}
};
MODULE_DEVICE_TABLE(of, mv_cesa_of_match_table);

static void
mv_cesa_conf_mbus_windows(struct mv_cesa_engine *engine,
			  const struct mbus_dram_target_info *dram)
{
	void __iomem *iobase = engine->regs;
	int i;

	for (i = 0; i < 4; i++) {
		writel(0, iobase + CESA_TDMA_WINDOW_CTRL(i));
		writel(0, iobase + CESA_TDMA_WINDOW_BASE(i));
	}

	for (i = 0; i < dram->num_cs; i++) {
		const struct mbus_dram_window *cs = dram->cs + i;

		writel(((cs->size - 1) & 0xffff0000) |
		       (cs->mbus_attr << 8) |
		       (dram->mbus_dram_target_id << 4) | 1,
		       iobase + CESA_TDMA_WINDOW_CTRL(i));
		writel(cs->base, iobase + CESA_TDMA_WINDOW_BASE(i));
	}
}

static int mv_cesa_dev_dma_init(struct mv_cesa_dev *cesa)
{
	struct device *dev = cesa->dev;
	struct mv_cesa_dev_dma *dma;

	if (!cesa->caps->has_tdma)
		return 0;

	dma = devm_kzalloc(dev, sizeof(*dma), GFP_KERNEL);
	if (!dma)
		return -ENOMEM;

	dma->tdma_desc_pool = dmam_pool_create("tdma_desc", dev,
					sizeof(struct mv_cesa_tdma_desc),
					16, 0);
	if (!dma->tdma_desc_pool)
		return -ENOMEM;

	dma->op_pool = dmam_pool_create("cesa_op", dev,
					sizeof(struct mv_cesa_op_ctx), 16, 0);
	if (!dma->op_pool)
		return -ENOMEM;

	dma->cache_pool = dmam_pool_create("cesa_cache", dev,
					   CESA_MAX_HASH_BLOCK_SIZE, 1, 0);
	if (!dma->cache_pool)
		return -ENOMEM;

	dma->padding_pool = dmam_pool_create("cesa_padding", dev, 72, 1, 0);
	if (!dma->padding_pool)
		return -ENOMEM;

	cesa->dma = dma;

	return 0;
}

static int mv_cesa_get_sram(struct platform_device *pdev, int idx)
{
	struct mv_cesa_dev *cesa = platform_get_drvdata(pdev);
	struct mv_cesa_engine *engine = &cesa->engines[idx];
	struct resource *res;

	engine->pool = of_gen_pool_get(cesa->dev->of_node,
				       "marvell,crypto-srams", idx);
	if (engine->pool) {
		engine->sram_pool = gen_pool_dma_alloc(engine->pool,
						       cesa->sram_size,
						       &engine->sram_dma);
		if (engine->sram_pool)
			return 0;

		engine->pool = NULL;
		return -ENOMEM;
	}

	engine->sram = devm_platform_get_and_ioremap_resource(pdev, idx, &res);
	if (IS_ERR(engine->sram))
		return PTR_ERR(engine->sram);

	engine->sram_dma = dma_map_resource(cesa->dev, res->start,
					    cesa->sram_size,
					    DMA_BIDIRECTIONAL, 0);
	if (dma_mapping_error(cesa->dev, engine->sram_dma))
		return -ENOMEM;

	return 0;
}

static void mv_cesa_put_sram(struct platform_device *pdev, int idx)
{
	struct mv_cesa_dev *cesa = platform_get_drvdata(pdev);
	struct mv_cesa_engine *engine = &cesa->engines[idx];

	if (engine->pool)
		gen_pool_free(engine->pool, (unsigned long)engine->sram_pool,
			      cesa->sram_size);
	else
		dma_unmap_resource(cesa->dev, engine->sram_dma,
				   cesa->sram_size, DMA_BIDIRECTIONAL, 0);
}

static int mv_cesa_probe(struct platform_device *pdev)
{
	const struct mv_cesa_caps *caps = &orion_caps;
	const struct mbus_dram_target_info *dram;
	const struct of_device_id *match;
	struct device *dev = &pdev->dev;
	struct mv_cesa_dev *cesa;
	struct mv_cesa_engine *engines;
	int irq, ret, i, cpu;
	u32 sram_size;

	if (cesa_dev) {
		dev_err(&pdev->dev, "Only one CESA device authorized\n");
		return -EEXIST;
	}

	if (dev->of_node) {
		match = of_match_node(mv_cesa_of_match_table, dev->of_node);
		if (!match || !match->data)
			return -ENOTSUPP;

		caps = match->data;
	}

	cesa = devm_kzalloc(dev, sizeof(*cesa), GFP_KERNEL);
	if (!cesa)
		return -ENOMEM;

	cesa->caps = caps;
	cesa->dev = dev;

	sram_size = CESA_SA_DEFAULT_SRAM_SIZE;
	of_property_read_u32(cesa->dev->of_node, "marvell,crypto-sram-size",
			     &sram_size);
	if (sram_size < CESA_SA_MIN_SRAM_SIZE)
		sram_size = CESA_SA_MIN_SRAM_SIZE;

	cesa->sram_size = sram_size;
	cesa->engines = devm_kcalloc(dev, caps->nengines, sizeof(*engines),
				     GFP_KERNEL);
	if (!cesa->engines)
		return -ENOMEM;

	spin_lock_init(&cesa->lock);

	cesa->regs = devm_platform_ioremap_resource_byname(pdev, "regs");
	if (IS_ERR(cesa->regs))
		return PTR_ERR(cesa->regs);

	ret = mv_cesa_dev_dma_init(cesa);
	if (ret)
		return ret;

	dram = mv_mbus_dram_info_nooverlap();

	platform_set_drvdata(pdev, cesa);

	for (i = 0; i < caps->nengines; i++) {
		struct mv_cesa_engine *engine = &cesa->engines[i];
		char res_name[16];

		engine->id = i;
		spin_lock_init(&engine->lock);

		ret = mv_cesa_get_sram(pdev, i);
		if (ret)
			goto err_cleanup;

		irq = platform_get_irq(pdev, i);
		if (irq < 0) {
			ret = irq;
			goto err_cleanup;
		}

		engine->irq = irq;

		/*
		 * Not all platforms can gate the CESA clocks: do not complain
		 * if the clock does not exist.
		 */
		snprintf(res_name, sizeof(res_name), "cesa%u", i);
		engine->clk = devm_clk_get_optional_enabled(dev, res_name);
		if (IS_ERR(engine->clk)) {
			engine->clk = devm_clk_get_optional_enabled(dev, NULL);
			if (IS_ERR(engine->clk)) {
				ret = PTR_ERR(engine->clk);
				goto err_cleanup;
			}
		}

		snprintf(res_name, sizeof(res_name), "cesaz%u", i);
		engine->zclk = devm_clk_get_optional_enabled(dev, res_name);
		if (IS_ERR(engine->zclk)) {
			ret = PTR_ERR(engine->zclk);
			goto err_cleanup;
		}

		engine->regs = cesa->regs + CESA_ENGINE_OFF(i);

		if (dram && cesa->caps->has_tdma)
			mv_cesa_conf_mbus_windows(engine, dram);

		writel(0, engine->regs + CESA_SA_INT_STATUS);
		writel(CESA_SA_CFG_STOP_DIG_ERR,
		       engine->regs + CESA_SA_CFG);
		writel(engine->sram_dma & CESA_SA_SRAM_MSK,
		       engine->regs + CESA_SA_DESC_P0);

		ret = devm_request_threaded_irq(dev, irq, NULL, mv_cesa_int,
						IRQF_ONESHOT,
						dev_name(&pdev->dev),
						engine);
		if (ret)
			goto err_cleanup;

		/* Set affinity */
		cpu = cpumask_local_spread(engine->id, NUMA_NO_NODE);
		irq_set_affinity_hint(irq, get_cpu_mask(cpu));

		crypto_init_queue(&engine->queue, CESA_CRYPTO_DEFAULT_MAX_QLEN);
		atomic_set(&engine->load, 0);
		INIT_LIST_HEAD(&engine->complete_queue);
	}

	cesa_dev = cesa;

	ret = mv_cesa_add_algs(cesa);
	if (ret) {
		cesa_dev = NULL;
		goto err_cleanup;
	}

	dev_info(dev, "CESA device successfully registered\n");

	return 0;

err_cleanup:
	for (i = 0; i < caps->nengines; i++)
		mv_cesa_put_sram(pdev, i);

	return ret;
}

static void mv_cesa_remove(struct platform_device *pdev)
{
	struct mv_cesa_dev *cesa = platform_get_drvdata(pdev);
	int i;

	mv_cesa_remove_algs(cesa);

	for (i = 0; i < cesa->caps->nengines; i++)
		mv_cesa_put_sram(pdev, i);
}

static const struct platform_device_id mv_cesa_plat_id_table[] = {
	{ .name = "mv_crypto" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(platform, mv_cesa_plat_id_table);

static struct platform_driver marvell_cesa = {
	.probe		= mv_cesa_probe,
	.remove		= mv_cesa_remove,
	.id_table	= mv_cesa_plat_id_table,
	.driver		= {
		.name	= "marvell-cesa",
		.of_match_table = mv_cesa_of_match_table,
	},
};
module_platform_driver(marvell_cesa);

MODULE_AUTHOR("Boris Brezillon <boris.brezillon@free-electrons.com>");
MODULE_AUTHOR("Arnaud Ebalard <arno@natisbad.org>");
MODULE_DESCRIPTION("Support for Marvell's cryptographic engine");
MODULE_LICENSE("GPL v2");
