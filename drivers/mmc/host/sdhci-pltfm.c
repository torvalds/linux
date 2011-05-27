/*
 * sdhci-pltfm.c Support for SDHCI platform devices
 * Copyright (c) 2009 Intel Corporation
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

#include "sdhci.h"
#include "sdhci-pltfm.h"

static struct sdhci_ops sdhci_pltfm_ops = {
};

struct sdhci_host *sdhci_pltfm_init(struct platform_device *pdev,
				    struct sdhci_pltfm_data *pdata)
{
	struct sdhci_host *host;
	struct sdhci_pltfm_host *pltfm_host;
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
	if (pdev->dev.parent != &platform_bus)
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

void sdhci_pltfm_free(struct platform_device *pdev)
{
	struct sdhci_host *host = platform_get_drvdata(pdev);
	struct resource *iomem = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	iounmap(host->ioaddr);
	release_mem_region(iomem->start, resource_size(iomem));
	sdhci_free_host(host);
	platform_set_drvdata(pdev, NULL);
}

int sdhci_pltfm_register(struct platform_device *pdev,
			 struct sdhci_pltfm_data *pdata)
{
	struct sdhci_host *host;
	int ret = 0;

	host = sdhci_pltfm_init(pdev, pdata);
	if (IS_ERR(host))
		return PTR_ERR(host);

	ret = sdhci_add_host(host);
	if (ret)
		sdhci_pltfm_free(pdev);

	return ret;
}

int sdhci_pltfm_unregister(struct platform_device *pdev)
{
	struct sdhci_host *host = platform_get_drvdata(pdev);
	int dead = (readl(host->ioaddr + SDHCI_INT_STATUS) == 0xffffffff);

	sdhci_remove_host(host, dead);
	sdhci_pltfm_free(pdev);

	return 0;
}

#ifdef CONFIG_PM
int sdhci_pltfm_suspend(struct platform_device *dev, pm_message_t state)
{
	struct sdhci_host *host = platform_get_drvdata(dev);

	return sdhci_suspend_host(host, state);
}

int sdhci_pltfm_resume(struct platform_device *dev)
{
	struct sdhci_host *host = platform_get_drvdata(dev);

	return sdhci_resume_host(host);
}
#endif	/* CONFIG_PM */
