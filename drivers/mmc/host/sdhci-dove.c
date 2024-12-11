// SPDX-License-Identifier: GPL-2.0-only
/*
 * sdhci-dove.c Support for SDHCI on Marvell's Dove SoC
 *
 * Author: Saeed Bishara <saeed@marvell.com>
 *	   Mike Rapoport <mike@compulab.co.il>
 * Based on sdhci-cns3xxx.c
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/mmc/host.h>
#include <linux/module.h>
#include <linux/of.h>

#include "sdhci-pltfm.h"

static u16 sdhci_dove_readw(struct sdhci_host *host, int reg)
{
	u16 ret;

	switch (reg) {
	case SDHCI_HOST_VERSION:
	case SDHCI_SLOT_INT_STATUS:
		/* those registers don't exist */
		return 0;
	default:
		ret = readw(host->ioaddr + reg);
	}
	return ret;
}

static u32 sdhci_dove_readl(struct sdhci_host *host, int reg)
{
	u32 ret;

	ret = readl(host->ioaddr + reg);

	switch (reg) {
	case SDHCI_CAPABILITIES:
		/* Mask the support for 3.0V */
		ret &= ~SDHCI_CAN_VDD_300;
		break;
	}
	return ret;
}

static const struct sdhci_ops sdhci_dove_ops = {
	.read_w	= sdhci_dove_readw,
	.read_l	= sdhci_dove_readl,
	.set_clock = sdhci_set_clock,
	.set_bus_width = sdhci_set_bus_width,
	.reset = sdhci_reset,
	.set_uhs_signaling = sdhci_set_uhs_signaling,
};

static const struct sdhci_pltfm_data sdhci_dove_pdata = {
	.ops	= &sdhci_dove_ops,
	.quirks	= SDHCI_QUIRK_NO_SIMULT_VDD_AND_POWER |
		  SDHCI_QUIRK_NO_BUSY_IRQ |
		  SDHCI_QUIRK_BROKEN_TIMEOUT_VAL |
		  SDHCI_QUIRK_FORCE_DMA |
		  SDHCI_QUIRK_NO_HISPD_BIT,
};

static int sdhci_dove_probe(struct platform_device *pdev)
{
	struct sdhci_host *host;
	struct sdhci_pltfm_host *pltfm_host;
	int ret;

	host = sdhci_pltfm_init(pdev, &sdhci_dove_pdata, 0);
	if (IS_ERR(host))
		return PTR_ERR(host);

	pltfm_host = sdhci_priv(host);
	pltfm_host->clk = devm_clk_get_enabled(&pdev->dev, NULL);

	ret = mmc_of_parse(host->mmc);
	if (ret)
		goto err_sdhci_add;

	ret = sdhci_add_host(host);
	if (ret)
		goto err_sdhci_add;

	return 0;

err_sdhci_add:
	sdhci_pltfm_free(pdev);
	return ret;
}

static const struct of_device_id sdhci_dove_of_match_table[] = {
	{ .compatible = "marvell,dove-sdhci", },
	{}
};
MODULE_DEVICE_TABLE(of, sdhci_dove_of_match_table);

static struct platform_driver sdhci_dove_driver = {
	.driver		= {
		.name	= "sdhci-dove",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.pm	= &sdhci_pltfm_pmops,
		.of_match_table = sdhci_dove_of_match_table,
	},
	.probe		= sdhci_dove_probe,
	.remove		= sdhci_pltfm_remove,
};

module_platform_driver(sdhci_dove_driver);

MODULE_DESCRIPTION("SDHCI driver for Dove");
MODULE_AUTHOR("Saeed Bishara <saeed@marvell.com>, "
	      "Mike Rapoport <mike@compulab.co.il>");
MODULE_LICENSE("GPL v2");
