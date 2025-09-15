// SPDX-License-Identifier: GPL-2.0+
/*
 * NPCM SDHC MMC host controller driver.
 *
 * Copyright (c) 2023 Nuvoton Technology corporation.
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>

#include "sdhci-pltfm.h"

static const struct sdhci_pltfm_data npcm7xx_sdhci_pdata = {
	.quirks  = SDHCI_QUIRK_DELAY_AFTER_POWER,
	.quirks2 = SDHCI_QUIRK2_STOP_WITH_TC |
		   SDHCI_QUIRK2_NO_1_8_V,
};

static const struct sdhci_pltfm_data npcm8xx_sdhci_pdata = {
	.quirks  = SDHCI_QUIRK_DELAY_AFTER_POWER,
	.quirks2 = SDHCI_QUIRK2_STOP_WITH_TC,
};

static int npcm_sdhci_probe(struct platform_device *pdev)
{
	const struct sdhci_pltfm_data *data;
	struct sdhci_pltfm_host *pltfm_host;
	struct device *dev = &pdev->dev;
	struct sdhci_host *host;
	u32 caps;
	int ret;

	data = of_device_get_match_data(dev);
	if (!data)
		return -EINVAL;

	host = sdhci_pltfm_init(pdev, data, 0);
	if (IS_ERR(host))
		return PTR_ERR(host);

	pltfm_host = sdhci_priv(host);

	pltfm_host->clk = devm_clk_get_optional_enabled(dev, NULL);
	if (IS_ERR(pltfm_host->clk)) {
		return PTR_ERR(pltfm_host->clk);
	}

	caps = sdhci_readl(host, SDHCI_CAPABILITIES);
	if (caps & SDHCI_CAN_DO_8BIT)
		host->mmc->caps |= MMC_CAP_8_BIT_DATA;

	ret = mmc_of_parse(host->mmc);
	if (ret)
		return ret;

	return sdhci_add_host(host);
}

static const struct of_device_id npcm_sdhci_of_match[] = {
	{ .compatible = "nuvoton,npcm750-sdhci", .data = &npcm7xx_sdhci_pdata },
	{ .compatible = "nuvoton,npcm845-sdhci", .data = &npcm8xx_sdhci_pdata },
	{ }
};
MODULE_DEVICE_TABLE(of, npcm_sdhci_of_match);

static struct platform_driver npcm_sdhci_driver = {
	.driver = {
		.name	= "npcm-sdhci",
		.of_match_table = npcm_sdhci_of_match,
		.pm	= &sdhci_pltfm_pmops,
	},
	.probe		= npcm_sdhci_probe,
	.remove		= sdhci_pltfm_remove,
};
module_platform_driver(npcm_sdhci_driver);

MODULE_DESCRIPTION("NPCM Secure Digital Host Controller Interface driver");
MODULE_AUTHOR("Tomer Maimon <tomer.maimon@nuvoton.com>");
MODULE_LICENSE("GPL");
