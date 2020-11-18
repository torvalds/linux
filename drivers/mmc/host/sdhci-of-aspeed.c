// SPDX-License-Identifier: GPL-2.0-or-later
/* Copyright (C) 2019 ASPEED Technology Inc. */
/* Copyright (C) 2019 IBM Corp. */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/mmc/host.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>

#include "sdhci-pltfm.h"

#define ASPEED_SDC_INFO		0x00
#define   ASPEED_SDC_S1MMC8	BIT(25)
#define   ASPEED_SDC_S0MMC8	BIT(24)

struct aspeed_sdc {
	struct clk *clk;
	struct resource *res;

	spinlock_t lock;
	void __iomem *regs;
};

struct aspeed_sdhci {
	struct aspeed_sdc *parent;
	u32 width_mask;
};

static void aspeed_sdc_configure_8bit_mode(struct aspeed_sdc *sdc,
					   struct aspeed_sdhci *sdhci,
					   bool bus8)
{
	u32 info;

	/* Set/clear 8 bit mode */
	spin_lock(&sdc->lock);
	info = readl(sdc->regs + ASPEED_SDC_INFO);
	if (bus8)
		info |= sdhci->width_mask;
	else
		info &= ~sdhci->width_mask;
	writel(info, sdc->regs + ASPEED_SDC_INFO);
	spin_unlock(&sdc->lock);
}

static void aspeed_sdhci_set_clock(struct sdhci_host *host, unsigned int clock)
{
	struct sdhci_pltfm_host *pltfm_host;
	unsigned long parent;
	int div;
	u16 clk;

	pltfm_host = sdhci_priv(host);
	parent = clk_get_rate(pltfm_host->clk);

	sdhci_writew(host, 0, SDHCI_CLOCK_CONTROL);

	if (clock == 0)
		return;

	if (WARN_ON(clock > host->max_clk))
		clock = host->max_clk;

	for (div = 2; div < 256; div *= 2) {
		if ((parent / div) <= clock)
			break;
	}
	div >>= 1;

	clk = div << SDHCI_DIVIDER_SHIFT;

	sdhci_enable_clk(host, clk);
}

static unsigned int aspeed_sdhci_get_max_clock(struct sdhci_host *host)
{
	if (host->mmc->f_max)
		return host->mmc->f_max;

	return sdhci_pltfm_clk_get_max_clock(host);
}

static void aspeed_sdhci_set_bus_width(struct sdhci_host *host, int width)
{
	struct sdhci_pltfm_host *pltfm_priv;
	struct aspeed_sdhci *aspeed_sdhci;
	struct aspeed_sdc *aspeed_sdc;
	u8 ctrl;

	pltfm_priv = sdhci_priv(host);
	aspeed_sdhci = sdhci_pltfm_priv(pltfm_priv);
	aspeed_sdc = aspeed_sdhci->parent;

	/* Set/clear 8-bit mode */
	aspeed_sdc_configure_8bit_mode(aspeed_sdc, aspeed_sdhci,
				       width == MMC_BUS_WIDTH_8);

	/* Set/clear 1 or 4 bit mode */
	ctrl = sdhci_readb(host, SDHCI_HOST_CONTROL);
	if (width == MMC_BUS_WIDTH_4)
		ctrl |= SDHCI_CTRL_4BITBUS;
	else
		ctrl &= ~SDHCI_CTRL_4BITBUS;
	sdhci_writeb(host, ctrl, SDHCI_HOST_CONTROL);
}

static u32 aspeed_sdhci_readl(struct sdhci_host *host, int reg)
{
	u32 val = readl(host->ioaddr + reg);

	if (unlikely(reg == SDHCI_PRESENT_STATE) &&
	    (host->mmc->caps2 & MMC_CAP2_CD_ACTIVE_HIGH))
		val ^= SDHCI_CARD_PRESENT;

	return val;
}

static const struct sdhci_ops aspeed_sdhci_ops = {
	.read_l = aspeed_sdhci_readl,
	.set_clock = aspeed_sdhci_set_clock,
	.get_max_clock = aspeed_sdhci_get_max_clock,
	.set_bus_width = aspeed_sdhci_set_bus_width,
	.get_timeout_clock = sdhci_pltfm_clk_get_max_clock,
	.reset = sdhci_reset,
	.set_uhs_signaling = sdhci_set_uhs_signaling,
};

static const struct sdhci_pltfm_data aspeed_sdhci_pdata = {
	.ops = &aspeed_sdhci_ops,
	.quirks = SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN,
};

static inline int aspeed_sdhci_calculate_slot(struct aspeed_sdhci *dev,
					      struct resource *res)
{
	resource_size_t delta;

	if (!res || resource_type(res) != IORESOURCE_MEM)
		return -EINVAL;

	if (res->start < dev->parent->res->start)
		return -EINVAL;

	delta = res->start - dev->parent->res->start;
	if (delta & (0x100 - 1))
		return -EINVAL;

	return (delta / 0x100) - 1;
}

static int aspeed_sdhci_probe(struct platform_device *pdev)
{
	struct sdhci_pltfm_host *pltfm_host;
	struct aspeed_sdhci *dev;
	struct sdhci_host *host;
	struct resource *res;
	int slot;
	int ret;

	host = sdhci_pltfm_init(pdev, &aspeed_sdhci_pdata, sizeof(*dev));
	if (IS_ERR(host))
		return PTR_ERR(host);

	pltfm_host = sdhci_priv(host);
	dev = sdhci_pltfm_priv(pltfm_host);
	dev->parent = dev_get_drvdata(pdev->dev.parent);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	slot = aspeed_sdhci_calculate_slot(dev, res);

	if (slot < 0)
		return slot;
	else if (slot >= 2)
		return -EINVAL;

	dev_info(&pdev->dev, "Configuring for slot %d\n", slot);
	dev->width_mask = !slot ? ASPEED_SDC_S0MMC8 : ASPEED_SDC_S1MMC8;

	sdhci_get_of_property(pdev);

	pltfm_host->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(pltfm_host->clk))
		return PTR_ERR(pltfm_host->clk);

	ret = clk_prepare_enable(pltfm_host->clk);
	if (ret) {
		dev_err(&pdev->dev, "Unable to enable SDIO clock\n");
		goto err_pltfm_free;
	}

	ret = mmc_of_parse(host->mmc);
	if (ret)
		goto err_sdhci_add;

	ret = sdhci_add_host(host);
	if (ret)
		goto err_sdhci_add;

	return 0;

err_sdhci_add:
	clk_disable_unprepare(pltfm_host->clk);
err_pltfm_free:
	sdhci_pltfm_free(pdev);
	return ret;
}

static int aspeed_sdhci_remove(struct platform_device *pdev)
{
	struct sdhci_pltfm_host *pltfm_host;
	struct sdhci_host *host;
	int dead = 0;

	host = platform_get_drvdata(pdev);
	pltfm_host = sdhci_priv(host);

	sdhci_remove_host(host, dead);

	clk_disable_unprepare(pltfm_host->clk);

	sdhci_pltfm_free(pdev);

	return 0;
}

static const struct of_device_id aspeed_sdhci_of_match[] = {
	{ .compatible = "aspeed,ast2400-sdhci", },
	{ .compatible = "aspeed,ast2500-sdhci", },
	{ .compatible = "aspeed,ast2600-sdhci", },
	{ }
};

static struct platform_driver aspeed_sdhci_driver = {
	.driver		= {
		.name	= "sdhci-aspeed",
		.of_match_table = aspeed_sdhci_of_match,
	},
	.probe		= aspeed_sdhci_probe,
	.remove		= aspeed_sdhci_remove,
};

static int aspeed_sdc_probe(struct platform_device *pdev)

{
	struct device_node *parent, *child;
	struct aspeed_sdc *sdc;
	int ret;

	sdc = devm_kzalloc(&pdev->dev, sizeof(*sdc), GFP_KERNEL);
	if (!sdc)
		return -ENOMEM;

	spin_lock_init(&sdc->lock);

	sdc->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(sdc->clk))
		return PTR_ERR(sdc->clk);

	ret = clk_prepare_enable(sdc->clk);
	if (ret) {
		dev_err(&pdev->dev, "Unable to enable SDCLK\n");
		return ret;
	}

	sdc->res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	sdc->regs = devm_ioremap_resource(&pdev->dev, sdc->res);
	if (IS_ERR(sdc->regs)) {
		ret = PTR_ERR(sdc->regs);
		goto err_clk;
	}

	dev_set_drvdata(&pdev->dev, sdc);

	parent = pdev->dev.of_node;
	for_each_available_child_of_node(parent, child) {
		struct platform_device *cpdev;

		cpdev = of_platform_device_create(child, NULL, &pdev->dev);
		if (!cpdev) {
			of_node_put(child);
			ret = -ENODEV;
			goto err_clk;
		}
	}

	return 0;

err_clk:
	clk_disable_unprepare(sdc->clk);
	return ret;
}

static int aspeed_sdc_remove(struct platform_device *pdev)
{
	struct aspeed_sdc *sdc = dev_get_drvdata(&pdev->dev);

	clk_disable_unprepare(sdc->clk);

	return 0;
}

static const struct of_device_id aspeed_sdc_of_match[] = {
	{ .compatible = "aspeed,ast2400-sd-controller", },
	{ .compatible = "aspeed,ast2500-sd-controller", },
	{ .compatible = "aspeed,ast2600-sd-controller", },
	{ }
};

MODULE_DEVICE_TABLE(of, aspeed_sdc_of_match);

static struct platform_driver aspeed_sdc_driver = {
	.driver		= {
		.name	= "sd-controller-aspeed",
		.pm	= &sdhci_pltfm_pmops,
		.of_match_table = aspeed_sdc_of_match,
	},
	.probe		= aspeed_sdc_probe,
	.remove		= aspeed_sdc_remove,
};

static int __init aspeed_sdc_init(void)
{
	int rc;

	rc = platform_driver_register(&aspeed_sdhci_driver);
	if (rc < 0)
		return rc;

	rc = platform_driver_register(&aspeed_sdc_driver);
	if (rc < 0)
		platform_driver_unregister(&aspeed_sdhci_driver);

	return rc;
}
module_init(aspeed_sdc_init);

static void __exit aspeed_sdc_exit(void)
{
	platform_driver_unregister(&aspeed_sdc_driver);
	platform_driver_unregister(&aspeed_sdhci_driver);
}
module_exit(aspeed_sdc_exit);

MODULE_DESCRIPTION("Driver for the ASPEED SD/SDIO/SDHCI Controllers");
MODULE_AUTHOR("Ryan Chen <ryan_chen@aspeedtech.com>");
MODULE_AUTHOR("Andrew Jeffery <andrew@aj.id.au>");
MODULE_LICENSE("GPL");
