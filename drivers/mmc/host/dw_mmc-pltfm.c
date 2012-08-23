/*
 * Synopsys DesignWare Multimedia Card Interface driver
 *
 * Copyright (C) 2009 NXP Semiconductors
 * Copyright (C) 2009, 2010 Imagination Technologies Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/dw_mmc.h>
#include "dw_mmc.h"

static int __devinit dw_mci_pltfm_probe(struct platform_device *pdev)
{
	struct dw_mci *host;
	struct resource	*regs;
	int ret;

	host = kzalloc(sizeof(struct dw_mci), GFP_KERNEL);
	if (!host)
		return -ENOMEM;

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!regs) {
		ret = -ENXIO;
		goto err_free;
	}

	host->irq = platform_get_irq(pdev, 0);
	if (host->irq < 0) {
		ret = host->irq;
		goto err_free;
	}

	host->dev = pdev->dev;
	host->irq_flags = 0;
	host->pdata = pdev->dev.platform_data;
	ret = -ENOMEM;
	host->regs = ioremap(regs->start, resource_size(regs));
	if (!host->regs)
		goto err_free;
	platform_set_drvdata(pdev, host);
	ret = dw_mci_probe(host);
	if (ret)
		goto err_out;
	return ret;
err_out:
	iounmap(host->regs);
err_free:
	kfree(host);
	return ret;
}

static int __devexit dw_mci_pltfm_remove(struct platform_device *pdev)
{
	struct dw_mci *host = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);
	dw_mci_remove(host);
	iounmap(host->regs);
	kfree(host);
	return 0;
}

#ifdef CONFIG_PM_SLEEP
/*
 * TODO: we should probably disable the clock to the card in the suspend path.
 */
static int dw_mci_pltfm_suspend(struct device *dev)
{
	int ret;
	struct dw_mci *host = dev_get_drvdata(dev);

	ret = dw_mci_suspend(host);
	if (ret)
		return ret;

	return 0;
}

static int dw_mci_pltfm_resume(struct device *dev)
{
	int ret;
	struct dw_mci *host = dev_get_drvdata(dev);

	ret = dw_mci_resume(host);
	if (ret)
		return ret;

	return 0;
}
#else
#define dw_mci_pltfm_suspend	NULL
#define dw_mci_pltfm_resume	NULL
#endif /* CONFIG_PM_SLEEP */

static SIMPLE_DEV_PM_OPS(dw_mci_pltfm_pmops, dw_mci_pltfm_suspend, dw_mci_pltfm_resume);

static struct platform_driver dw_mci_pltfm_driver = {
	.remove		= __exit_p(dw_mci_pltfm_remove),
	.driver		= {
		.name		= "dw_mmc",
		.pm		= &dw_mci_pltfm_pmops,
	},
};

static int __init dw_mci_init(void)
{
	return platform_driver_probe(&dw_mci_pltfm_driver, dw_mci_pltfm_probe);
}

static void __exit dw_mci_exit(void)
{
	platform_driver_unregister(&dw_mci_pltfm_driver);
}

module_init(dw_mci_init);
module_exit(dw_mci_exit);

MODULE_DESCRIPTION("DW Multimedia Card Interface driver");
MODULE_AUTHOR("NXP Semiconductor VietNam");
MODULE_AUTHOR("Imagination Technologies Ltd");
MODULE_LICENSE("GPL v2");
