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
#include <linux/platform_device.h>
#include <linux/mmc/host.h>
#include <linux/mfd/core.h>
#include <linux/mfd/tmio.h>
#include <linux/mfd/sh_mobile_sdhi.h>

struct sh_mobile_sdhi {
	struct clk *clk;
	struct tmio_mmc_data mmc_data;
	struct mfd_cell cell_mmc;
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

static int __init sh_mobile_sdhi_probe(struct platform_device *pdev)
{
	struct sh_mobile_sdhi *priv;
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

	snprintf(clk_name, sizeof(clk_name), "sdhi%d", pdev->id);
	priv->clk = clk_get(&pdev->dev, clk_name);
	if (IS_ERR(priv->clk)) {
		dev_err(&pdev->dev, "cannot get clock \"%s\"\n", clk_name);
		ret = PTR_ERR(priv->clk);
		kfree(priv);
		return ret;
	}

	clk_enable(priv->clk);

	priv->mmc_data.hclk = clk_get_rate(priv->clk);
	priv->mmc_data.set_pwr = sh_mobile_sdhi_set_pwr;
	priv->mmc_data.capabilities = MMC_CAP_MMC_HIGHSPEED;

	memcpy(&priv->cell_mmc, &sh_mobile_sdhi_cell, sizeof(priv->cell_mmc));
	priv->cell_mmc.driver_data = &priv->mmc_data;
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
