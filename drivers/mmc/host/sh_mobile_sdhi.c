/*
 * SuperH Mobile SDHI
 *
 * Copyright (C) 2009 Magnus Damm
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Based on "Compaq ASIC3 support":
 *
 * Copyright 2001 Compaq Computer Corporation.
 * Copyright 2004-2005 Phil Blundell
 * Copyright 2007-2008 OpenedHand Ltd.
 *
 * Authors: Phil Blundell <pb@handhelds.org>,
 *	    Samuel Ortiz <sameo@openedhand.com>
 *
 */

#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sh_mobile_sdhi.h>
#include <linux/mfd/tmio.h>
#include <linux/sh_dma.h>

#include "tmio_mmc.h"

struct sh_mobile_sdhi {
	struct clk *clk;
	struct tmio_mmc_data mmc_data;
	struct sh_dmae_slave param_tx;
	struct sh_dmae_slave param_rx;
	struct tmio_mmc_dma dma_priv;
};

static void sh_mobile_sdhi_set_pwr(struct platform_device *pdev, int state)
{
	struct sh_mobile_sdhi_info *p = pdev->dev.platform_data;

	if (p && p->set_pwr)
		p->set_pwr(pdev, state);
}

static int sh_mobile_sdhi_get_cd(struct platform_device *pdev)
{
	struct sh_mobile_sdhi_info *p = pdev->dev.platform_data;

	if (p && p->get_cd)
		return p->get_cd(pdev);
	else
		return -ENOSYS;
}

static int __devinit sh_mobile_sdhi_probe(struct platform_device *pdev)
{
	struct sh_mobile_sdhi *priv;
	struct tmio_mmc_data *mmc_data;
	struct sh_mobile_sdhi_info *p = pdev->dev.platform_data;
	struct tmio_mmc_host *host;
	char clk_name[8];
	int i, irq, ret;

	priv = kzalloc(sizeof(struct sh_mobile_sdhi), GFP_KERNEL);
	if (priv == NULL) {
		dev_err(&pdev->dev, "kzalloc failed\n");
		return -ENOMEM;
	}

	mmc_data = &priv->mmc_data;
	p->pdata = mmc_data;

	snprintf(clk_name, sizeof(clk_name), "sdhi%d", pdev->id);
	priv->clk = clk_get(&pdev->dev, clk_name);
	if (IS_ERR(priv->clk)) {
		dev_err(&pdev->dev, "cannot get clock \"%s\"\n", clk_name);
		ret = PTR_ERR(priv->clk);
		goto eclkget;
	}

	clk_enable(priv->clk);

	mmc_data->hclk = clk_get_rate(priv->clk);
	mmc_data->set_pwr = sh_mobile_sdhi_set_pwr;
	mmc_data->get_cd = sh_mobile_sdhi_get_cd;
	mmc_data->capabilities = MMC_CAP_MMC_HIGHSPEED;
	if (p) {
		mmc_data->flags = p->tmio_flags;
		mmc_data->ocr_mask = p->tmio_ocr_mask;
		mmc_data->capabilities |= p->tmio_caps;

		if (p->dma_slave_tx > 0 && p->dma_slave_rx > 0) {
			priv->param_tx.slave_id = p->dma_slave_tx;
			priv->param_rx.slave_id = p->dma_slave_rx;
			priv->dma_priv.chan_priv_tx = &priv->param_tx;
			priv->dma_priv.chan_priv_rx = &priv->param_rx;
			priv->dma_priv.alignment_shift = 1; /* 2-byte alignment */
			mmc_data->dma = &priv->dma_priv;
		}
	}

	/*
	 * All SDHI blocks support 2-byte and larger block sizes in 4-bit
	 * bus width mode.
	 */
	mmc_data->flags |= TMIO_MMC_BLKSZ_2BYTES;

	/*
	 * All SDHI blocks support SDIO IRQ signalling.
	 */
	mmc_data->flags |= TMIO_MMC_SDIO_IRQ;

	ret = tmio_mmc_host_probe(&host, pdev, mmc_data);
	if (ret < 0)
		goto eprobe;

	for (i = 0; i < 3; i++) {
		irq = platform_get_irq(pdev, i);
		if (irq < 0) {
			if (i) {
				continue;
			} else {
				ret = irq;
				goto eirq;
			}
		}
		ret = request_irq(irq, tmio_mmc_irq, 0,
				  dev_name(&pdev->dev), host);
		if (ret) {
			while (i--) {
				irq = platform_get_irq(pdev, i);
				if (irq >= 0)
					free_irq(irq, host);
			}
			goto eirq;
		}
	}
	dev_info(&pdev->dev, "%s base at 0x%08lx clock rate %u MHz\n",
		 mmc_hostname(host->mmc), (unsigned long)
		 (platform_get_resource(pdev,IORESOURCE_MEM, 0)->start),
		 mmc_data->hclk / 1000000);

	return ret;

eirq:
	tmio_mmc_host_remove(host);
eprobe:
	clk_disable(priv->clk);
	clk_put(priv->clk);
eclkget:
	kfree(priv);
	return ret;
}

static int sh_mobile_sdhi_remove(struct platform_device *pdev)
{
	struct mmc_host *mmc = platform_get_drvdata(pdev);
	struct tmio_mmc_host *host = mmc_priv(mmc);
	struct sh_mobile_sdhi *priv = container_of(host->pdata, struct sh_mobile_sdhi, mmc_data);
	struct sh_mobile_sdhi_info *p = pdev->dev.platform_data;
	int i, irq;

	p->pdata = NULL;

	tmio_mmc_host_remove(host);

	for (i = 0; i < 3; i++) {
		irq = platform_get_irq(pdev, i);
		if (irq >= 0)
			free_irq(irq, host);
	}

	clk_disable(priv->clk);
	clk_put(priv->clk);
	kfree(priv);

	return 0;
}

static const struct dev_pm_ops tmio_mmc_dev_pm_ops = {
	.suspend = tmio_mmc_host_suspend,
	.resume = tmio_mmc_host_resume,
	.runtime_suspend = tmio_mmc_host_runtime_suspend,
	.runtime_resume = tmio_mmc_host_runtime_resume,
};

static struct platform_driver sh_mobile_sdhi_driver = {
	.driver		= {
		.name	= "sh_mobile_sdhi",
		.owner	= THIS_MODULE,
		.pm	= &tmio_mmc_dev_pm_ops,
	},
	.probe		= sh_mobile_sdhi_probe,
	.remove		= __devexit_p(sh_mobile_sdhi_remove),
};

static int __init sh_mobile_sdhi_init(void)
{
	return platform_driver_register(&sh_mobile_sdhi_driver);
}

static void __exit sh_mobile_sdhi_exit(void)
{
	platform_driver_unregister(&sh_mobile_sdhi_driver);
}

module_init(sh_mobile_sdhi_init);
module_exit(sh_mobile_sdhi_exit);

MODULE_DESCRIPTION("SuperH Mobile SDHI driver");
MODULE_AUTHOR("Magnus Damm");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:sh_mobile_sdhi");
