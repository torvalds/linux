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
#include <linux/mfd/core.h>
#include <linux/mfd/tmio.h>
#include <linux/mfd/sh_mobile_sdhi.h>
#include <linux/sh_dma.h>

struct sh_mobile_sdhi {
	struct clk *clk;
	struct tmio_mmc_data mmc_data;
	struct mfd_cell cell_mmc;
	struct sh_dmae_slave param_tx;
	struct sh_dmae_slave param_rx;
	struct tmio_mmc_dma dma_priv;
};

static struct resource sh_mobile_sdhi_resources[] = {
	{
		.start = 0x000,
		.end   = 0x1ff,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = 0,
		.end   = 0,
		.flags = IORESOURCE_IRQ,
	},
};

static struct mfd_cell sh_mobile_sdhi_cell = {
	.name          = "tmio-mmc",
	.num_resources = ARRAY_SIZE(sh_mobile_sdhi_resources),
	.resources     = sh_mobile_sdhi_resources,
};

static void sh_mobile_sdhi_set_pwr(struct platform_device *tmio, int state)
{
	struct platform_device *pdev = to_platform_device(tmio->dev.parent);
	struct sh_mobile_sdhi_info *p = pdev->dev.platform_data;

	if (p && p->set_pwr)
		p->set_pwr(pdev, state);
}

static int sh_mobile_sdhi_get_cd(struct platform_device *tmio)
{
	struct platform_device *pdev = to_platform_device(tmio->dev.parent);
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
	struct resource *mem;
	char clk_name[8];
	int ret, irq;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem)
		dev_err(&pdev->dev, "missing MEM resource\n");

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		dev_err(&pdev->dev, "missing IRQ resource\n");

	if (!mem || (irq < 0))
		return -EINVAL;

	priv = kzalloc(sizeof(struct sh_mobile_sdhi), GFP_KERNEL);
	if (priv == NULL) {
		dev_err(&pdev->dev, "kzalloc failed\n");
		return -ENOMEM;
	}

	mmc_data = &priv->mmc_data;

	snprintf(clk_name, sizeof(clk_name), "sdhi%d", pdev->id);
	priv->clk = clk_get(&pdev->dev, clk_name);
	if (IS_ERR(priv->clk)) {
		dev_err(&pdev->dev, "cannot get clock \"%s\"\n", clk_name);
		ret = PTR_ERR(priv->clk);
		kfree(priv);
		return ret;
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

	if (p && p->dma_slave_tx >= 0 && p->dma_slave_rx >= 0) {
		priv->param_tx.slave_id = p->dma_slave_tx;
		priv->param_rx.slave_id = p->dma_slave_rx;
		priv->dma_priv.chan_priv_tx = &priv->param_tx;
		priv->dma_priv.chan_priv_rx = &priv->param_rx;
		priv->dma_priv.alignment_shift = 1; /* 2-byte alignment */
		mmc_data->dma = &priv->dma_priv;
	}

	memcpy(&priv->cell_mmc, &sh_mobile_sdhi_cell, sizeof(priv->cell_mmc));
	priv->cell_mmc.driver_data = mmc_data;
	priv->cell_mmc.platform_data = &priv->cell_mmc;
	priv->cell_mmc.data_size = sizeof(priv->cell_mmc);

	platform_set_drvdata(pdev, priv);

	ret = mfd_add_devices(&pdev->dev, pdev->id,
			      &priv->cell_mmc, 1, mem, irq);
	if (ret) {
		clk_disable(priv->clk);
		clk_put(priv->clk);
		kfree(priv);
	}

	return ret;
}

static int sh_mobile_sdhi_remove(struct platform_device *pdev)
{
	struct sh_mobile_sdhi *priv = platform_get_drvdata(pdev);

	mfd_remove_devices(&pdev->dev);
	clk_disable(priv->clk);
	clk_put(priv->clk);
	kfree(priv);

	return 0;
}

static struct platform_driver sh_mobile_sdhi_driver = {
	.driver		= {
		.name	= "sh_mobile_sdhi",
		.owner	= THIS_MODULE,
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
