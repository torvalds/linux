// SPDX-License-Identifier: GPL-2.0
/*
 * Cryptographic API.
 *
 * Support for StarFive hardware cryptographic engine.
 * Copyright (c) 2022 StarFive Technology
 *
 */

#include <crypto/engine.h>
#include "jh7110-cryp.h"
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/spinlock.h>

#define DRIVER_NAME             "jh7110-crypto"

struct starfive_dev_list {
	struct list_head        dev_list;
	spinlock_t              lock; /* protect dev_list */
};

static struct starfive_dev_list dev_list = {
	.dev_list = LIST_HEAD_INIT(dev_list.dev_list),
	.lock     = __SPIN_LOCK_UNLOCKED(dev_list.lock),
};

struct starfive_cryp_dev *starfive_cryp_find_dev(struct starfive_cryp_ctx *ctx)
{
	struct starfive_cryp_dev *cryp = NULL, *tmp;

	spin_lock_bh(&dev_list.lock);
	if (!ctx->cryp) {
		list_for_each_entry(tmp, &dev_list.dev_list, list) {
			cryp = tmp;
			break;
		}
		ctx->cryp = cryp;
	} else {
		cryp = ctx->cryp;
	}

	spin_unlock_bh(&dev_list.lock);

	return cryp;
}

static u16 side_chan;
module_param(side_chan, ushort, 0);
MODULE_PARM_DESC(side_chan, "Enable side channel mitigation for AES module.\n"
			    "Enabling this feature will reduce speed performance.\n"
			    " 0 - Disabled\n"
			    " other - Enabled");

static int starfive_dma_init(struct starfive_cryp_dev *cryp)
{
	dma_cap_mask_t mask;

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);

	cryp->tx = dma_request_chan(cryp->dev, "tx");
	if (IS_ERR(cryp->tx))
		return dev_err_probe(cryp->dev, PTR_ERR(cryp->tx),
				     "Error requesting tx dma channel.\n");

	cryp->rx = dma_request_chan(cryp->dev, "rx");
	if (IS_ERR(cryp->rx)) {
		dma_release_channel(cryp->tx);
		return dev_err_probe(cryp->dev, PTR_ERR(cryp->rx),
				     "Error requesting rx dma channel.\n");
	}

	return 0;
}

static void starfive_dma_cleanup(struct starfive_cryp_dev *cryp)
{
	dma_release_channel(cryp->tx);
	dma_release_channel(cryp->rx);
}

static irqreturn_t starfive_cryp_irq(int irq, void *priv)
{
	u32 status;
	u32 mask;
	struct starfive_cryp_dev *cryp = (struct starfive_cryp_dev *)priv;

	mask = readl(cryp->base + STARFIVE_IE_MASK_OFFSET);
	status = readl(cryp->base + STARFIVE_IE_FLAG_OFFSET);
	if (status & STARFIVE_IE_FLAG_AES_DONE) {
		mask |= STARFIVE_IE_MASK_AES_DONE;
		writel(mask, cryp->base + STARFIVE_IE_MASK_OFFSET);
		tasklet_schedule(&cryp->aes_done);
	}

	if (status & STARFIVE_IE_FLAG_HASH_DONE) {
		mask |= STARFIVE_IE_MASK_HASH_DONE;
		writel(mask, cryp->base + STARFIVE_IE_MASK_OFFSET);
		tasklet_schedule(&cryp->hash_done);
	}

	if (status & STARFIVE_IE_FLAG_PKA_DONE) {
		mask |= STARFIVE_IE_MASK_PKA_DONE;
		writel(mask, cryp->base + STARFIVE_IE_MASK_OFFSET);
		complete(&cryp->pka_done);
	}

	return IRQ_HANDLED;
}

static int starfive_cryp_probe(struct platform_device *pdev)
{
	struct starfive_cryp_dev *cryp;
	struct resource *res;
	int irq;
	int ret;

	cryp = devm_kzalloc(&pdev->dev, sizeof(*cryp), GFP_KERNEL);
	if (!cryp)
		return -ENOMEM;

	platform_set_drvdata(pdev, cryp);
	cryp->dev = &pdev->dev;

	cryp->base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(cryp->base))
		return dev_err_probe(&pdev->dev, PTR_ERR(cryp->base),
				     "Error remapping memory for platform device\n");

	tasklet_init(&cryp->aes_done, starfive_aes_done_task, (unsigned long)cryp);
	tasklet_init(&cryp->hash_done, starfive_hash_done_task, (unsigned long)cryp);

	cryp->phys_base = res->start;
	cryp->dma_maxburst = 32;
	cryp->side_chan = side_chan;

	cryp->hclk = devm_clk_get(&pdev->dev, "hclk");
	if (IS_ERR(cryp->hclk))
		return dev_err_probe(&pdev->dev, PTR_ERR(cryp->hclk),
				     "Error getting hardware reference clock\n");

	cryp->ahb = devm_clk_get(&pdev->dev, "ahb");
	if (IS_ERR(cryp->ahb))
		return dev_err_probe(&pdev->dev, PTR_ERR(cryp->ahb),
				     "Error getting ahb reference clock\n");

	cryp->rst = devm_reset_control_get_shared(cryp->dev, NULL);
	if (IS_ERR(cryp->rst))
		return dev_err_probe(&pdev->dev, PTR_ERR(cryp->rst),
				     "Error getting hardware reset line\n");

	init_completion(&cryp->pka_done);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = devm_request_irq(&pdev->dev, irq, starfive_cryp_irq, 0, pdev->name,
			       (void *)cryp);
	if (ret)
		return dev_err_probe(&pdev->dev, irq,
				     "Failed to register interrupt handler\n");

	clk_prepare_enable(cryp->hclk);
	clk_prepare_enable(cryp->ahb);
	reset_control_deassert(cryp->rst);

	spin_lock(&dev_list.lock);
	list_add(&cryp->list, &dev_list.dev_list);
	spin_unlock(&dev_list.lock);

	ret = starfive_dma_init(cryp);
	if (ret) {
		if (ret == -EPROBE_DEFER)
			goto err_probe_defer;
		else
			goto err_dma_init;
	}

	/* Initialize crypto engine */
	cryp->engine = crypto_engine_alloc_init(&pdev->dev, 1);
	if (!cryp->engine) {
		ret = -ENOMEM;
		goto err_engine;
	}

	ret = crypto_engine_start(cryp->engine);
	if (ret)
		goto err_engine_start;

	ret = starfive_aes_register_algs();
	if (ret)
		goto err_algs_aes;

	ret = starfive_hash_register_algs();
	if (ret)
		goto err_algs_hash;

	ret = starfive_rsa_register_algs();
	if (ret)
		goto err_algs_rsa;

	return 0;

err_algs_rsa:
	starfive_hash_unregister_algs();
err_algs_hash:
	starfive_aes_unregister_algs();
err_algs_aes:
	crypto_engine_stop(cryp->engine);
err_engine_start:
	crypto_engine_exit(cryp->engine);
err_engine:
	starfive_dma_cleanup(cryp);
err_dma_init:
	spin_lock(&dev_list.lock);
	list_del(&cryp->list);
	spin_unlock(&dev_list.lock);

	clk_disable_unprepare(cryp->hclk);
	clk_disable_unprepare(cryp->ahb);
	reset_control_assert(cryp->rst);

	tasklet_kill(&cryp->aes_done);
	tasklet_kill(&cryp->hash_done);
err_probe_defer:
	return ret;
}

static void starfive_cryp_remove(struct platform_device *pdev)
{
	struct starfive_cryp_dev *cryp = platform_get_drvdata(pdev);

	starfive_aes_unregister_algs();
	starfive_hash_unregister_algs();
	starfive_rsa_unregister_algs();

	tasklet_kill(&cryp->aes_done);
	tasklet_kill(&cryp->hash_done);

	crypto_engine_stop(cryp->engine);
	crypto_engine_exit(cryp->engine);

	starfive_dma_cleanup(cryp);

	spin_lock(&dev_list.lock);
	list_del(&cryp->list);
	spin_unlock(&dev_list.lock);

	clk_disable_unprepare(cryp->hclk);
	clk_disable_unprepare(cryp->ahb);
	reset_control_assert(cryp->rst);
}

static const struct of_device_id starfive_dt_ids[] __maybe_unused = {
	{ .compatible = "starfive,jh7110-crypto", .data = NULL},
	{},
};
MODULE_DEVICE_TABLE(of, starfive_dt_ids);

static struct platform_driver starfive_cryp_driver = {
	.probe  = starfive_cryp_probe,
	.remove_new = starfive_cryp_remove,
	.driver = {
		.name           = DRIVER_NAME,
		.of_match_table = starfive_dt_ids,
	},
};

module_platform_driver(starfive_cryp_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("StarFive JH7110 Cryptographic Module");
