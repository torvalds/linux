/*
 * Toshiba TC6387XB support
 * Copyright (c) 2005 Ian Molton
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This file contains TC6387XB base support.
 *
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/mfd/core.h>
#include <linux/mfd/tmio.h>
#include <linux/mfd/tc6387xb.h>
#include <linux/slab.h>

enum {
	TC6387XB_CELL_MMC,
};

struct tc6387xb {
	void __iomem *scr;
	struct clk *clk32k;
	struct resource rscr;
};

static struct resource tc6387xb_mmc_resources[] = {
	{
		.start = 0x800,
		.end   = 0x9ff,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = 0,
		.end   = 0,
		.flags = IORESOURCE_IRQ,
	},
};

/*--------------------------------------------------------------------------*/

#ifdef CONFIG_PM
static int tc6387xb_suspend(struct platform_device *dev, pm_message_t state)
{
	struct tc6387xb *tc6387xb = platform_get_drvdata(dev);
	struct tc6387xb_platform_data *pdata = dev_get_platdata(&dev->dev);

	if (pdata && pdata->suspend)
		pdata->suspend(dev);
	clk_disable_unprepare(tc6387xb->clk32k);

	return 0;
}

static int tc6387xb_resume(struct platform_device *dev)
{
	struct tc6387xb *tc6387xb = platform_get_drvdata(dev);
	struct tc6387xb_platform_data *pdata = dev_get_platdata(&dev->dev);

	clk_prepare_enable(tc6387xb->clk32k);
	if (pdata && pdata->resume)
		pdata->resume(dev);

	tmio_core_mmc_resume(tc6387xb->scr + 0x200, 0,
		tc6387xb_mmc_resources[0].start & 0xfffe);

	return 0;
}
#else
#define tc6387xb_suspend  NULL
#define tc6387xb_resume   NULL
#endif

/*--------------------------------------------------------------------------*/

static void tc6387xb_mmc_pwr(struct platform_device *mmc, int state)
{
	struct tc6387xb *tc6387xb = dev_get_drvdata(mmc->dev.parent);

	tmio_core_mmc_pwr(tc6387xb->scr + 0x200, 0, state);
}

static void tc6387xb_mmc_clk_div(struct platform_device *mmc, int state)
{
	struct tc6387xb *tc6387xb = dev_get_drvdata(mmc->dev.parent);

	tmio_core_mmc_clk_div(tc6387xb->scr + 0x200, 0, state);
}


static int tc6387xb_mmc_enable(struct platform_device *mmc)
{
	struct tc6387xb *tc6387xb = dev_get_drvdata(mmc->dev.parent);

	clk_prepare_enable(tc6387xb->clk32k);

	tmio_core_mmc_enable(tc6387xb->scr + 0x200, 0,
		tc6387xb_mmc_resources[0].start & 0xfffe);

	return 0;
}

static int tc6387xb_mmc_disable(struct platform_device *mmc)
{
	struct tc6387xb *tc6387xb = dev_get_drvdata(mmc->dev.parent);

	clk_disable_unprepare(tc6387xb->clk32k);

	return 0;
}

static struct tmio_mmc_data tc6387xb_mmc_data = {
	.hclk = 24000000,
	.set_pwr = tc6387xb_mmc_pwr,
	.set_clk_div = tc6387xb_mmc_clk_div,
};

/*--------------------------------------------------------------------------*/

static const struct mfd_cell tc6387xb_cells[] = {
	[TC6387XB_CELL_MMC] = {
		.name = "tmio-mmc",
		.enable = tc6387xb_mmc_enable,
		.disable = tc6387xb_mmc_disable,
		.platform_data = &tc6387xb_mmc_data,
		.pdata_size    = sizeof(tc6387xb_mmc_data),
		.num_resources = ARRAY_SIZE(tc6387xb_mmc_resources),
		.resources = tc6387xb_mmc_resources,
	},
};

static int tc6387xb_probe(struct platform_device *dev)
{
	struct tc6387xb_platform_data *pdata = dev_get_platdata(&dev->dev);
	struct resource *iomem, *rscr;
	struct clk *clk32k;
	struct tc6387xb *tc6387xb;
	int irq, ret;

	iomem = platform_get_resource(dev, IORESOURCE_MEM, 0);
	if (!iomem)
		return -EINVAL;

	tc6387xb = kzalloc(sizeof(*tc6387xb), GFP_KERNEL);
	if (!tc6387xb)
		return -ENOMEM;

	ret  = platform_get_irq(dev, 0);
	if (ret >= 0)
		irq = ret;
	else
		goto err_no_irq;

	clk32k = clk_get(&dev->dev, "CLK_CK32K");
	if (IS_ERR(clk32k)) {
		ret = PTR_ERR(clk32k);
		goto err_no_clk;
	}

	rscr = &tc6387xb->rscr;
	rscr->name = "tc6387xb-core";
	rscr->start = iomem->start;
	rscr->end = iomem->start + 0xff;
	rscr->flags = IORESOURCE_MEM;

	ret = request_resource(iomem, rscr);
	if (ret)
		goto err_resource;

	tc6387xb->scr = ioremap(rscr->start, resource_size(rscr));
	if (!tc6387xb->scr) {
		ret = -ENOMEM;
		goto err_ioremap;
	}

	tc6387xb->clk32k = clk32k;
	platform_set_drvdata(dev, tc6387xb);

	if (pdata && pdata->enable)
		pdata->enable(dev);

	dev_info(&dev->dev, "Toshiba tc6387xb initialised\n");

	ret = mfd_add_devices(&dev->dev, dev->id, tc6387xb_cells,
			      ARRAY_SIZE(tc6387xb_cells), iomem, irq, NULL);

	if (!ret)
		return 0;

	iounmap(tc6387xb->scr);
err_ioremap:
	release_resource(&tc6387xb->rscr);
err_resource:
	clk_put(clk32k);
err_no_clk:
err_no_irq:
	kfree(tc6387xb);
	return ret;
}

static int tc6387xb_remove(struct platform_device *dev)
{
	struct tc6387xb *tc6387xb = platform_get_drvdata(dev);

	mfd_remove_devices(&dev->dev);
	iounmap(tc6387xb->scr);
	release_resource(&tc6387xb->rscr);
	clk_disable_unprepare(tc6387xb->clk32k);
	clk_put(tc6387xb->clk32k);
	kfree(tc6387xb);

	return 0;
}


static struct platform_driver tc6387xb_platform_driver = {
	.driver = {
		.name		= "tc6387xb",
	},
	.probe		= tc6387xb_probe,
	.remove		= tc6387xb_remove,
	.suspend        = tc6387xb_suspend,
	.resume         = tc6387xb_resume,
};

module_platform_driver(tc6387xb_platform_driver);

MODULE_DESCRIPTION("Toshiba TC6387XB core driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Ian Molton");
MODULE_ALIAS("platform:tc6387xb");

