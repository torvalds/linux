/*
 * sdhci-pltfm.c Support for SDHCI platform devices
 * Copyright (c) 2009 Intel Corporation
 *
 * Copyright (c) 2007 Freescale Semiconductor, Inc.
 * Copyright (c) 2009 MontaVista Software, Inc.
 *
 * Authors: Xiaobo Xie <X.Xie@freescale.com>
 *	    Anton Vorontsov <avorontsov@ru.mvista.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* Supports:
 * SDHCI platform devices
 *
 * Inspired by sdhci-pci.c, by Pierre Ossman
 */

#include <linux/err.h>
#include <linux/module.h>
#include <linux/of.h>
#ifdef CONFIG_PPC
#include <asm/machdep.h>
#endif
#include "sdhci-pltfm.h"

static struct sdhci_ops sdhci_pltfm_ops = {
};

#ifdef CONFIG_OF
static bool sdhci_of_wp_inverted(struct device_node *np)
{
	if (of_get_property(np, "sdhci,wp-inverted", NULL))
		return true;

	/* Old device trees don't have the wp-inverted property. */
#ifdef CONFIG_PPC
	return machine_is(mpc837x_rdb) || machine_is(mpc837x_mds);
#else
	return false;
#endif /* CONFIG_PPC */
}

void sdhci_get_of_property(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct sdhci_host *host = platform_get_drvdata(pdev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	const __be32 *clk;
	int size;

	if (of_device_is_available(np)) {
		if (of_get_property(np, "sdhci,auto-cmd12", NULL))
			host->quirks |= SDHCI_QUIRK_MULTIBLOCK_READ_ACMD12;

		if (of_get_property(np, "sdhci,1-bit-only", NULL))
			host->quirks |= SDHCI_QUIRK_FORCE_1_BIT_DATA;

		if (sdhci_of_wp_inverted(np))
			host->quirks |= SDHCI_QUIRK_INVERTED_WRITE_PROTECT;

		clk = of_get_property(np, "clock-frequency", &size);
		if (clk && size == sizeof(*clk) && *clk)
			pltfm_host->clock = be32_to_cpup(clk);
	}
}
#else
void sdhci_get_of_property(struct platform_device *pdev) {}
#endif /* CONFIG_OF */
EXPORT_SYMBOL_GPL(sdhci_get_of_property);

struct sdhci_host *sdhci_pltfm_init(struct platform_device *pdev,
				    struct sdhci_pltfm_data *pdata)
{
	struct sdhci_host *host;
	struct sdhci_pltfm_host *pltfm_host;
	struct device_node *np = pdev->dev.of_node;
	struct resource *iomem;
	int ret;

	iomem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!iomem) {
		ret = -ENOMEM;
		goto err;
	}

	if (resource_size(iomem) < 0x100)
		dev_err(&pdev->dev, "Invalid iomem size!\n");

	/* Some PCI-based MFD need the parent here */
	if (pdev->dev.parent != &platform_bus && !np)
		host = sdhci_alloc_host(pdev->dev.parent, sizeof(*pltfm_host));
	else
		host = sdhci_alloc_host(&pdev->dev, sizeof(*pltfm_host));

	if (IS_ERR(host)) {
		ret = PTR_ERR(host);
		goto err;
	}

	pltfm_host = sdhci_priv(host);

	host->hw_name = dev_name(&pdev->dev);
	if (pdata && pdata->ops)
		host->ops = pdata->ops;
	else
		host->ops = &sdhci_pltfm_ops;
	if (pdata)
		host->quirks = pdata->quirks;
	host->irq = platform_get_irq(pdev, 0);

	if (!request_mem_region(iomem->start, resource_size(iomem),
		mmc_hostname(host->mmc))) {
		dev_err(&pdev->dev, "cannot request region\n");
		ret = -EBUSY;
		goto err_request;
	}

	host->ioaddr = ioremap(iomem->start, resource_size(iomem));
	if (!host->ioaddr) {
		dev_err(&pdev->dev, "failed to remap registers\n");
		ret = -ENOMEM;
		goto err_remap;
	}

	platform_set_drvdata(pdev, host);

	return host;

err_remap:
	release_mem_region(iomem->start, resource_size(iomem));
err_request:
	sdhci_free_host(host);
err:
	dev_err(&pdev->dev, "%s failed %d\n", __func__, ret);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(sdhci_pltfm_init);

void sdhci_pltfm_free(struct platform_device *pdev)
{
	struct sdhci_host *host = platform_get_drvdata(pdev);
	struct resource *iomem = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	iounmap(host->ioaddr);
	release_mem_region(iomem->start, resource_size(iomem));
	sdhci_free_host(host);
	platform_set_drvdata(pdev, NULL);
}
EXPORT_SYMBOL_GPL(sdhci_pltfm_free);

int sdhci_pltfm_register(struct platform_device *pdev,
			 struct sdhci_pltfm_data *pdata)
{
	struct sdhci_host *host;
	int ret = 0;

	host = sdhci_pltfm_init(pdev, pdata);
	if (IS_ERR(host))
		return PTR_ERR(host);

	sdhci_get_of_property(pdev);

	ret = sdhci_add_host(host);
	if (ret)
		sdhci_pltfm_free(pdev);

	return ret;
}
EXPORT_SYMBOL_GPL(sdhci_pltfm_register);

int sdhci_pltfm_unregister(struct platform_device *pdev)
{
	struct sdhci_host *host = platform_get_drvdata(pdev);
	int dead = (readl(host->ioaddr + SDHCI_INT_STATUS) == 0xffffffff);

	sdhci_remove_host(host, dead);
	sdhci_pltfm_free(pdev);

	return 0;
}
EXPORT_SYMBOL_GPL(sdhci_pltfm_unregister);

#ifdef CONFIG_PM
static int sdhci_pltfm_suspend(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);

	return sdhci_suspend_host(host);
}

static int sdhci_pltfm_resume(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);

	return sdhci_resume_host(host);
}

const struct dev_pm_ops sdhci_pltfm_pmops = {
	.suspend	= sdhci_pltfm_suspend,
	.resume		= sdhci_pltfm_resume,
};
EXPORT_SYMBOL_GPL(sdhci_pltfm_pmops);
#endif	/* CONFIG_PM */

static int __init sdhci_pltfm_drv_init(void)
{
	pr_info("sdhci-pltfm: SDHCI platform and OF driver helper\n");

	return 0;
}
module_init(sdhci_pltfm_drv_init);

static void __exit sdhci_pltfm_drv_exit(void)
{
}
module_exit(sdhci_pltfm_drv_exit);

MODULE_DESCRIPTION("SDHCI platform and OF driver helper");
MODULE_AUTHOR("Intel Corporation");
MODULE_LICENSE("GPL v2");
