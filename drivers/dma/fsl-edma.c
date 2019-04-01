/*
 * drivers/dma/fsl-edma.c
 *
 * Copyright 2013-2014 Freescale Semiconductor, Inc.
 *
 * Driver for the Freescale eDMA engine with flexible channel multiplexing
 * capability for DMA request sources. The eDMA block can be found on some
 * Vybrid and Layerscape SoCs.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_dma.h>

#include "fsl-edma-common.h"

static irqreturn_t fsl_edma_tx_handler(int irq, void *dev_id)
{
	struct fsl_edma_engine *fsl_edma = dev_id;
	unsigned int intr, ch;
	struct edma_regs *regs = &fsl_edma->regs;
	struct fsl_edma_chan *fsl_chan;

	intr = edma_readl(fsl_edma, regs->intl);
	if (!intr)
		return IRQ_NONE;

	for (ch = 0; ch < fsl_edma->n_chans; ch++) {
		if (intr & (0x1 << ch)) {
			edma_writeb(fsl_edma, EDMA_CINT_CINT(ch), regs->cint);

			fsl_chan = &fsl_edma->chans[ch];

			spin_lock(&fsl_chan->vchan.lock);
			if (!fsl_chan->edesc->iscyclic) {
				list_del(&fsl_chan->edesc->vdesc.node);
				vchan_cookie_complete(&fsl_chan->edesc->vdesc);
				fsl_chan->edesc = NULL;
				fsl_chan->status = DMA_COMPLETE;
				fsl_chan->idle = true;
			} else {
				vchan_cyclic_callback(&fsl_chan->edesc->vdesc);
			}

			if (!fsl_chan->edesc)
				fsl_edma_xfer_desc(fsl_chan);

			spin_unlock(&fsl_chan->vchan.lock);
		}
	}
	return IRQ_HANDLED;
}

static irqreturn_t fsl_edma_err_handler(int irq, void *dev_id)
{
	struct fsl_edma_engine *fsl_edma = dev_id;
	unsigned int err, ch;
	struct edma_regs *regs = &fsl_edma->regs;

	err = edma_readl(fsl_edma, regs->errl);
	if (!err)
		return IRQ_NONE;

	for (ch = 0; ch < fsl_edma->n_chans; ch++) {
		if (err & (0x1 << ch)) {
			fsl_edma_disable_request(&fsl_edma->chans[ch]);
			edma_writeb(fsl_edma, EDMA_CERR_CERR(ch), regs->cerr);
			fsl_edma->chans[ch].status = DMA_ERROR;
			fsl_edma->chans[ch].idle = true;
		}
	}
	return IRQ_HANDLED;
}

static irqreturn_t fsl_edma_irq_handler(int irq, void *dev_id)
{
	if (fsl_edma_tx_handler(irq, dev_id) == IRQ_HANDLED)
		return IRQ_HANDLED;

	return fsl_edma_err_handler(irq, dev_id);
}

static struct dma_chan *fsl_edma_xlate(struct of_phandle_args *dma_spec,
		struct of_dma *ofdma)
{
	struct fsl_edma_engine *fsl_edma = ofdma->of_dma_data;
	struct dma_chan *chan, *_chan;
	struct fsl_edma_chan *fsl_chan;
	unsigned long chans_per_mux = fsl_edma->n_chans / DMAMUX_NR;

	if (dma_spec->args_count != 2)
		return NULL;

	mutex_lock(&fsl_edma->fsl_edma_mutex);
	list_for_each_entry_safe(chan, _chan, &fsl_edma->dma_dev.channels, device_node) {
		if (chan->client_count)
			continue;
		if ((chan->chan_id / chans_per_mux) == dma_spec->args[0]) {
			chan = dma_get_slave_channel(chan);
			if (chan) {
				chan->device->privatecnt++;
				fsl_chan = to_fsl_edma_chan(chan);
				fsl_chan->slave_id = dma_spec->args[1];
				fsl_edma_chan_mux(fsl_chan, fsl_chan->slave_id,
						true);
				mutex_unlock(&fsl_edma->fsl_edma_mutex);
				return chan;
			}
		}
	}
	mutex_unlock(&fsl_edma->fsl_edma_mutex);
	return NULL;
}

static int
fsl_edma_irq_init(struct platform_device *pdev, struct fsl_edma_engine *fsl_edma)
{
	int ret;

	fsl_edma->txirq = platform_get_irq_byname(pdev, "edma-tx");
	if (fsl_edma->txirq < 0) {
		dev_err(&pdev->dev, "Can't get edma-tx irq.\n");
		return fsl_edma->txirq;
	}

	fsl_edma->errirq = platform_get_irq_byname(pdev, "edma-err");
	if (fsl_edma->errirq < 0) {
		dev_err(&pdev->dev, "Can't get edma-err irq.\n");
		return fsl_edma->errirq;
	}

	if (fsl_edma->txirq == fsl_edma->errirq) {
		ret = devm_request_irq(&pdev->dev, fsl_edma->txirq,
				fsl_edma_irq_handler, 0, "eDMA", fsl_edma);
		if (ret) {
			dev_err(&pdev->dev, "Can't register eDMA IRQ.\n");
			 return  ret;
		}
	} else {
		ret = devm_request_irq(&pdev->dev, fsl_edma->txirq,
				fsl_edma_tx_handler, 0, "eDMA tx", fsl_edma);
		if (ret) {
			dev_err(&pdev->dev, "Can't register eDMA tx IRQ.\n");
			return  ret;
		}

		ret = devm_request_irq(&pdev->dev, fsl_edma->errirq,
				fsl_edma_err_handler, 0, "eDMA err", fsl_edma);
		if (ret) {
			dev_err(&pdev->dev, "Can't register eDMA err IRQ.\n");
			return  ret;
		}
	}

	return 0;
}

static void fsl_edma_irq_exit(
		struct platform_device *pdev, struct fsl_edma_engine *fsl_edma)
{
	if (fsl_edma->txirq == fsl_edma->errirq) {
		devm_free_irq(&pdev->dev, fsl_edma->txirq, fsl_edma);
	} else {
		devm_free_irq(&pdev->dev, fsl_edma->txirq, fsl_edma);
		devm_free_irq(&pdev->dev, fsl_edma->errirq, fsl_edma);
	}
}

static void fsl_disable_clocks(struct fsl_edma_engine *fsl_edma, int nr_clocks)
{
	int i;

	for (i = 0; i < nr_clocks; i++)
		clk_disable_unprepare(fsl_edma->muxclk[i]);
}

static int fsl_edma_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct fsl_edma_engine *fsl_edma;
	struct fsl_edma_chan *fsl_chan;
	struct edma_regs *regs;
	struct resource *res;
	int len, chans;
	int ret, i;

	ret = of_property_read_u32(np, "dma-channels", &chans);
	if (ret) {
		dev_err(&pdev->dev, "Can't get dma-channels.\n");
		return ret;
	}

	len = sizeof(*fsl_edma) + sizeof(*fsl_chan) * chans;
	fsl_edma = devm_kzalloc(&pdev->dev, len, GFP_KERNEL);
	if (!fsl_edma)
		return -ENOMEM;

	fsl_edma->version = v1;
	fsl_edma->n_chans = chans;
	mutex_init(&fsl_edma->fsl_edma_mutex);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	fsl_edma->membase = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(fsl_edma->membase))
		return PTR_ERR(fsl_edma->membase);

	fsl_edma_setup_regs(fsl_edma);
	regs = &fsl_edma->regs;

	for (i = 0; i < DMAMUX_NR; i++) {
		char clkname[32];

		res = platform_get_resource(pdev, IORESOURCE_MEM, 1 + i);
		fsl_edma->muxbase[i] = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(fsl_edma->muxbase[i])) {
			/* on error: disable all previously enabled clks */
			fsl_disable_clocks(fsl_edma, i);
			return PTR_ERR(fsl_edma->muxbase[i]);
		}

		sprintf(clkname, "dmamux%d", i);
		fsl_edma->muxclk[i] = devm_clk_get(&pdev->dev, clkname);
		if (IS_ERR(fsl_edma->muxclk[i])) {
			dev_err(&pdev->dev, "Missing DMAMUX block clock.\n");
			/* on error: disable all previously enabled clks */
			fsl_disable_clocks(fsl_edma, i);
			return PTR_ERR(fsl_edma->muxclk[i]);
		}

		ret = clk_prepare_enable(fsl_edma->muxclk[i]);
		if (ret)
			/* on error: disable all previously enabled clks */
			fsl_disable_clocks(fsl_edma, i);

	}

	fsl_edma->big_endian = of_property_read_bool(np, "big-endian");

	INIT_LIST_HEAD(&fsl_edma->dma_dev.channels);
	for (i = 0; i < fsl_edma->n_chans; i++) {
		struct fsl_edma_chan *fsl_chan = &fsl_edma->chans[i];

		fsl_chan->edma = fsl_edma;
		fsl_chan->pm_state = RUNNING;
		fsl_chan->slave_id = 0;
		fsl_chan->idle = true;
		fsl_chan->dma_dir = DMA_NONE;
		fsl_chan->vchan.desc_free = fsl_edma_free_desc;
		vchan_init(&fsl_chan->vchan, &fsl_edma->dma_dev);

		edma_writew(fsl_edma, 0x0, &regs->tcd[i].csr);
		fsl_edma_chan_mux(fsl_chan, 0, false);
	}

	edma_writel(fsl_edma, ~0, regs->intl);
	ret = fsl_edma_irq_init(pdev, fsl_edma);
	if (ret)
		return ret;

	dma_cap_set(DMA_PRIVATE, fsl_edma->dma_dev.cap_mask);
	dma_cap_set(DMA_SLAVE, fsl_edma->dma_dev.cap_mask);
	dma_cap_set(DMA_CYCLIC, fsl_edma->dma_dev.cap_mask);

	fsl_edma->dma_dev.dev = &pdev->dev;
	fsl_edma->dma_dev.device_alloc_chan_resources
		= fsl_edma_alloc_chan_resources;
	fsl_edma->dma_dev.device_free_chan_resources
		= fsl_edma_free_chan_resources;
	fsl_edma->dma_dev.device_tx_status = fsl_edma_tx_status;
	fsl_edma->dma_dev.device_prep_slave_sg = fsl_edma_prep_slave_sg;
	fsl_edma->dma_dev.device_prep_dma_cyclic = fsl_edma_prep_dma_cyclic;
	fsl_edma->dma_dev.device_config = fsl_edma_slave_config;
	fsl_edma->dma_dev.device_pause = fsl_edma_pause;
	fsl_edma->dma_dev.device_resume = fsl_edma_resume;
	fsl_edma->dma_dev.device_terminate_all = fsl_edma_terminate_all;
	fsl_edma->dma_dev.device_issue_pending = fsl_edma_issue_pending;

	fsl_edma->dma_dev.src_addr_widths = FSL_EDMA_BUSWIDTHS;
	fsl_edma->dma_dev.dst_addr_widths = FSL_EDMA_BUSWIDTHS;
	fsl_edma->dma_dev.directions = BIT(DMA_DEV_TO_MEM) | BIT(DMA_MEM_TO_DEV);

	platform_set_drvdata(pdev, fsl_edma);

	ret = dma_async_device_register(&fsl_edma->dma_dev);
	if (ret) {
		dev_err(&pdev->dev,
			"Can't register Freescale eDMA engine. (%d)\n", ret);
		fsl_disable_clocks(fsl_edma, DMAMUX_NR);
		return ret;
	}

	ret = of_dma_controller_register(np, fsl_edma_xlate, fsl_edma);
	if (ret) {
		dev_err(&pdev->dev,
			"Can't register Freescale eDMA of_dma. (%d)\n", ret);
		dma_async_device_unregister(&fsl_edma->dma_dev);
		fsl_disable_clocks(fsl_edma, DMAMUX_NR);
		return ret;
	}

	/* enable round robin arbitration */
	edma_writel(fsl_edma, EDMA_CR_ERGA | EDMA_CR_ERCA, regs->cr);

	return 0;
}

static int fsl_edma_remove(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct fsl_edma_engine *fsl_edma = platform_get_drvdata(pdev);

	fsl_edma_irq_exit(pdev, fsl_edma);
	fsl_edma_cleanup_vchan(&fsl_edma->dma_dev);
	of_dma_controller_free(np);
	dma_async_device_unregister(&fsl_edma->dma_dev);
	fsl_disable_clocks(fsl_edma, DMAMUX_NR);

	return 0;
}

static int fsl_edma_suspend_late(struct device *dev)
{
	struct fsl_edma_engine *fsl_edma = dev_get_drvdata(dev);
	struct fsl_edma_chan *fsl_chan;
	unsigned long flags;
	int i;

	for (i = 0; i < fsl_edma->n_chans; i++) {
		fsl_chan = &fsl_edma->chans[i];
		spin_lock_irqsave(&fsl_chan->vchan.lock, flags);
		/* Make sure chan is idle or will force disable. */
		if (unlikely(!fsl_chan->idle)) {
			dev_warn(dev, "WARN: There is non-idle channel.");
			fsl_edma_disable_request(fsl_chan);
			fsl_edma_chan_mux(fsl_chan, 0, false);
		}

		fsl_chan->pm_state = SUSPENDED;
		spin_unlock_irqrestore(&fsl_chan->vchan.lock, flags);
	}

	return 0;
}

static int fsl_edma_resume_early(struct device *dev)
{
	struct fsl_edma_engine *fsl_edma = dev_get_drvdata(dev);
	struct fsl_edma_chan *fsl_chan;
	struct edma_regs *regs = &fsl_edma->regs;
	int i;

	for (i = 0; i < fsl_edma->n_chans; i++) {
		fsl_chan = &fsl_edma->chans[i];
		fsl_chan->pm_state = RUNNING;
		edma_writew(fsl_edma, 0x0, &regs->tcd[i].csr);
		if (fsl_chan->slave_id != 0)
			fsl_edma_chan_mux(fsl_chan, fsl_chan->slave_id, true);
	}

	edma_writel(fsl_edma, EDMA_CR_ERGA | EDMA_CR_ERCA, regs->cr);

	return 0;
}

/*
 * eDMA provides the service to others, so it should be suspend late
 * and resume early. When eDMA suspend, all of the clients should stop
 * the DMA data transmission and let the channel idle.
 */
static const struct dev_pm_ops fsl_edma_pm_ops = {
	.suspend_late   = fsl_edma_suspend_late,
	.resume_early   = fsl_edma_resume_early,
};

static const struct of_device_id fsl_edma_dt_ids[] = {
	{ .compatible = "fsl,vf610-edma", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, fsl_edma_dt_ids);

static struct platform_driver fsl_edma_driver = {
	.driver		= {
		.name	= "fsl-edma",
		.of_match_table = fsl_edma_dt_ids,
		.pm     = &fsl_edma_pm_ops,
	},
	.probe          = fsl_edma_probe,
	.remove		= fsl_edma_remove,
};

static int __init fsl_edma_init(void)
{
	return platform_driver_register(&fsl_edma_driver);
}
subsys_initcall(fsl_edma_init);

static void __exit fsl_edma_exit(void)
{
	platform_driver_unregister(&fsl_edma_driver);
}
module_exit(fsl_edma_exit);

MODULE_ALIAS("platform:fsl-edma");
MODULE_DESCRIPTION("Freescale eDMA engine driver");
MODULE_LICENSE("GPL v2");
