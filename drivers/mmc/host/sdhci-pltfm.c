/*
 * sdhci-pltfm.c Support for SDHCI platform devices
 * Copyright (c) 2009 Intel Corporation
 *
 * Copyright (c) 2007, 2011 Freescale Semiconductor, Inc.
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

unsigned int sdhci_pltfm_clk_get_max_clock(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);

	return clk_get_rate(pltfm_host->clk);
}
EXPORT_SYMBOL_GPL(sdhci_pltfm_clk_get_max_clock);

static const struct sdhci_ops sdhci_pltfm_ops = {
	.set_clock = sdhci_set_clock,
	.set_bus_width = sdhci_set_bus_width,
	.reset = sdhci_reset,
	.set_uhs_signaling = sdhci_set_uhs_signaling,
};

#ifdef CONFIG_OF
static bool sdhci_of_wp_inverted(struct device_node *np)
{
	if (of_get_property(np, "sdhci,wp-inverted", NULL) ||
	    of_get_property(np, "wp-inverted", NULL))
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
	u32 bus_width;

	if (of_get_property(np, "sdhci,auto-cmd12", NULL))
		host->quirks |= SDHCI_QUIRK_MULTIBLOCK_READ_ACMD12;

	if (of_get_property(np, "sdhci,1-bit-only", NULL) ||
	    (of_property_read_u32(np, "bus-width", &bus_width) == 0 &&
	    bus_width == 1))
		host->quirks |= SDHCI_QUIRK_FORCE_1_BIT_DATA;

	if (sdhci_of_wp_inverted(np))
		host->quirks |= SDHCI_QUIRK_INVERTED_WRITE_PROTECT;

	if (of_get_property(np, "broken-cd", NULL))
		host->quirks |= SDHCI_QUIRK_BROKEN_CARD_DETECTION;

	if (of_get_property(np, "no-1-8-v", NULL))
		host->quirks2 |= SDHCI_QUIRK2_NO_1_8_V;

	if (of_device_is_compatible(np, "fsl,p2020-rev1-esdhc"))
		host->quirks |= SDHCI_QUIRK_BROKEN_DMA;

	if (of_device_is_compatible(np, "fsl,p2020-esdhc") ||
	    of_device_is_compatible(np, "fsl,p1010-esdhc") ||
	    of_device_is_compatible(np, "fsl,t4240-esdhc") ||
	    of_device_is_compatible(np, "fsl,mpc8536-esdhc"))
		host->quirks |= SDHCI_QUIRK_BROKEN_TIMEOUT_VAL;

	of_property_read_u32(np, "clock-frequency", &pltfm_host->clock);

	if (of_find_property(np, "keep-power-in-suspend", NULL))
		host->mmc->pm_caps |= MMC_PM_KEEP_POWER;

	if (of_property_read_bool(np, "wakeup-source") ||
	    of_property_read_bool(np, "enable-sdio-wakeup")) /* legacy */
		host->mmc->pm_caps |= MMC_PM_WAKE_SDIO_IRQ;
}
#else
void sdhci_get_of_property(struct platform_device *pdev) {}
#endif /* CONFIG_OF */
EXPORT_SYMBOL_GPL(sdhci_get_of_property);

struct sdhci_host *sdhci_pltfm_init(struct platform_device *pdev,
				    const struct sdhci_pltfm_data *pdata,
				    size_t priv_size)
{
	struct sdhci_host *host;
	struct resource *iomem;
	void __iomem *ioaddr;
	int irq, ret;

	iomem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	ioaddr = devm_ioremap_resource(&pdev->dev, iomem);
	if (IS_ERR(ioaddr)) {
		ret = PTR_ERR(ioaddr);
		goto err;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "failed to get IRQ number\n");
		ret = irq;
		goto err;
	}

	host = sdhci_alloc_host(&pdev->dev,
		sizeof(struct sdhci_pltfm_host) + priv_size);

	if (IS_ERR(host)) {
		ret = PTR_ERR(host);
		goto err;
	}

	host->ioaddr = ioaddr;
	host->irq = irq;
	host->hw_name = dev_name(&pdev->dev);
	if (pdata && pdata->ops)
		host->ops = pdata->ops;
	else
		host->ops = &sdhci_pltfm_ops;
	if (pdata) {
		host->quirks = pdata->quirks;
		host->quirks2 = pdata->quirks2;
	}

	platform_set_drvdata(pdev, host);

	return host;
err:
	dev_err(&pdev->dev, "%s failed %d\n", __func__, ret);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(sdhci_pltfm_init);

void sdhci_pltfm_free(struct platform_device *pdev)
{
	struct sdhci_host *host = platform_get_drvdata(pdev);

	sdhci_free_host(host);
}
EXPORT_SYMBOL_GPL(sdhci_pltfm_free);

int sdhci_pltfm_register(struct platform_device *pdev,
			const struct sdhci_pltfm_data *pdata,
			size_t priv_size)
{
	struct sdhci_host *host;
	int ret = 0;

	host = sdhci_pltfm_init(pdev, pdata, priv_size);
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
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	int dead = (readl(host->ioaddr + SDHCI_INT_STATUS) == 0xffffffff);

	sdhci_remove_host(host, dead);
	clk_disable_unprepare(pltfm_host->clk);
	sdhci_pltfm_free(pdev);

	return 0;
}
EXPORT_SYMBOL_GPL(sdhci_pltfm_unregister);

#ifdef CONFIG_PM_SLEEP
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
#endif

const struct dev_pm_ops sdhci_pltfm_pmops = {
	SET_SYSTEM_SLEEP_PM_OPS(sdhci_pltfm_suspend, sdhci_pltfm_resume)
};
EXPORT_SYMBOL_GPL(sdhci_pltfm_pmops);

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
