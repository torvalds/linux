/*
 * sdhci-brcmstb.c Support for SDHCI on Broadcom BRCMSTB SoC's
 *
 * Copyright (C) 2015 Broadcom Corporation
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
 */

#include <linux/io.h>
#include <linux/mmc/host.h>
#include <linux/module.h>
#include <linux/of.h>

#include "sdhci-pltfm.h"

#ifdef CONFIG_PM_SLEEP

static int sdhci_brcmstb_suspend(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	int res;

	if (host->tuning_mode != SDHCI_TUNING_MODE_3)
		mmc_retune_needed(host->mmc);

	res = sdhci_suspend_host(host);
	if (res)
		return res;
	clk_disable_unprepare(pltfm_host->clk);
	return res;
}

static int sdhci_brcmstb_resume(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	int err;

	err = clk_prepare_enable(pltfm_host->clk);
	if (err)
		return err;
	return sdhci_resume_host(host);
}

#endif /* CONFIG_PM_SLEEP */

static SIMPLE_DEV_PM_OPS(sdhci_brcmstb_pmops, sdhci_brcmstb_suspend,
			sdhci_brcmstb_resume);

static const struct sdhci_ops sdhci_brcmstb_ops = {
	.set_clock = sdhci_set_clock,
	.set_bus_width = sdhci_set_bus_width,
	.reset = sdhci_reset,
	.set_uhs_signaling = sdhci_set_uhs_signaling,
};

static struct sdhci_pltfm_data sdhci_brcmstb_pdata = {
	.ops = &sdhci_brcmstb_ops,
};

static int sdhci_brcmstb_probe(struct platform_device *pdev)
{
	struct sdhci_host *host;
	struct sdhci_pltfm_host *pltfm_host;
	struct clk *clk;
	int res;

	clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "Clock not found in Device Tree\n");
		clk = NULL;
	}
	res = clk_prepare_enable(clk);
	if (res)
		return res;

	host = sdhci_pltfm_init(pdev, &sdhci_brcmstb_pdata, 0);
	if (IS_ERR(host)) {
		res = PTR_ERR(host);
		goto err_clk;
	}

	/* Enable MMC_CAP2_HC_ERASE_SZ for better max discard calculations */
	host->mmc->caps2 |= MMC_CAP2_HC_ERASE_SZ;

	sdhci_get_of_property(pdev);
	mmc_of_parse(host->mmc);

	/*
	 * Supply the existing CAPS, but clear the UHS modes. This
	 * will allow these modes to be specified by device tree
	 * properties through mmc_of_parse().
	 */
	host->caps = sdhci_readl(host, SDHCI_CAPABILITIES);
	if (of_device_is_compatible(pdev->dev.of_node, "brcm,bcm7425-sdhci"))
		host->caps &= ~SDHCI_CAN_64BIT;
	host->caps1 = sdhci_readl(host, SDHCI_CAPABILITIES_1);
	host->caps1 &= ~(SDHCI_SUPPORT_SDR50 | SDHCI_SUPPORT_SDR104 |
			SDHCI_SUPPORT_DDR50);
	host->quirks |= SDHCI_QUIRK_MISSING_CAPS |
		SDHCI_QUIRK_BROKEN_TIMEOUT_VAL;

	res = sdhci_add_host(host);
	if (res)
		goto err;

	pltfm_host = sdhci_priv(host);
	pltfm_host->clk = clk;
	return res;

err:
	sdhci_pltfm_free(pdev);
err_clk:
	clk_disable_unprepare(clk);
	return res;
}

static const struct of_device_id sdhci_brcm_of_match[] = {
	{ .compatible = "brcm,bcm7425-sdhci" },
	{ .compatible = "brcm,bcm7445-sdhci" },
	{},
};
MODULE_DEVICE_TABLE(of, sdhci_brcm_of_match);

static struct platform_driver sdhci_brcmstb_driver = {
	.driver		= {
		.name	= "sdhci-brcmstb",
		.pm	= &sdhci_brcmstb_pmops,
		.of_match_table = of_match_ptr(sdhci_brcm_of_match),
	},
	.probe		= sdhci_brcmstb_probe,
	.remove		= sdhci_pltfm_unregister,
};

module_platform_driver(sdhci_brcmstb_driver);

MODULE_DESCRIPTION("SDHCI driver for Broadcom BRCMSTB SoCs");
MODULE_AUTHOR("Broadcom");
MODULE_LICENSE("GPL v2");
