/*
 * linux/drivers/mmc/host/tmio_mmc.c
 *
 * Copyright (C) 2007 Ian Molton
 * Copyright (C) 2004 Ian Molton
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Driver for the MMC / SD / SDIO cell found in:
 *
 * TC6393XB TC6391XB TC6387XB T7L66XB ASIC3
 */

#include <linux/device.h>
#include <linux/mfd/core.h>
#include <linux/mfd/tmio.h>
#include <linux/mmc/host.h>
#include <linux/module.h>
#include <linux/pagemap.h>
#include <linux/scatterlist.h>

#include "tmio_mmc.h"

#ifdef CONFIG_PM
static int tmio_mmc_suspend(struct platform_device *dev, pm_message_t state)
{
	const struct mfd_cell *cell = mfd_get_cell(dev);
	int ret;

	ret = tmio_mmc_host_suspend(&dev->dev);

	/* Tell MFD core it can disable us now.*/
	if (!ret && cell->disable)
		cell->disable(dev);

	return ret;
}

static int tmio_mmc_resume(struct platform_device *dev)
{
	const struct mfd_cell *cell = mfd_get_cell(dev);
	int ret = 0;

	/* Tell the MFD core we are ready to be enabled */
	if (cell->resume)
		ret = cell->resume(dev);

	if (!ret)
		ret = tmio_mmc_host_resume(&dev->dev);

	return ret;
}
#else
#define tmio_mmc_suspend NULL
#define tmio_mmc_resume NULL
#endif

static int __devinit tmio_mmc_probe(struct platform_device *pdev)
{
	const struct mfd_cell *cell = mfd_get_cell(pdev);
	struct tmio_mmc_data *pdata;
	struct tmio_mmc_host *host;
	int ret = -EINVAL, irq;

	if (pdev->num_resources != 2)
		goto out;

	pdata = pdev->dev.platform_data;
	if (!pdata || !pdata->hclk)
		goto out;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = irq;
		goto out;
	}

	/* Tell the MFD core we are ready to be enabled */
	if (cell->enable) {
		ret = cell->enable(pdev);
		if (ret)
			goto out;
	}

	ret = tmio_mmc_host_probe(&host, pdev, pdata);
	if (ret)
		goto cell_disable;

	ret = request_irq(irq, tmio_mmc_irq, IRQF_TRIGGER_FALLING,
				dev_name(&pdev->dev), host);
	if (ret)
		goto host_remove;

	pr_info("%s at 0x%08lx irq %d\n", mmc_hostname(host->mmc),
		(unsigned long)host->ctl, irq);

	return 0;

host_remove:
	tmio_mmc_host_remove(host);
cell_disable:
	if (cell->disable)
		cell->disable(pdev);
out:
	return ret;
}

static int __devexit tmio_mmc_remove(struct platform_device *pdev)
{
	const struct mfd_cell *cell = mfd_get_cell(pdev);
	struct mmc_host *mmc = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);

	if (mmc) {
		struct tmio_mmc_host *host = mmc_priv(mmc);
		free_irq(platform_get_irq(pdev, 0), host);
		tmio_mmc_host_remove(host);
		if (cell->disable)
			cell->disable(pdev);
	}

	return 0;
}

/* ------------------- device registration ----------------------- */

static struct platform_driver tmio_mmc_driver = {
	.driver = {
		.name = "tmio-mmc",
		.owner = THIS_MODULE,
	},
	.probe = tmio_mmc_probe,
	.remove = __devexit_p(tmio_mmc_remove),
	.suspend = tmio_mmc_suspend,
	.resume = tmio_mmc_resume,
};

module_platform_driver(tmio_mmc_driver);

MODULE_DESCRIPTION("Toshiba TMIO SD/MMC driver");
MODULE_AUTHOR("Ian Molton <spyro@f2s.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:tmio-mmc");
