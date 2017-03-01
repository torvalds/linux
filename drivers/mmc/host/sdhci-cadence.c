/*
 * Copyright (C) 2016 Socionext Inc.
 *   Author: Masahiro Yamada <yamada.masahiro@socionext.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/bitops.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>

#include "sdhci-pltfm.h"

/* HRS - Host Register Set (specific to Cadence) */
#define SDHCI_CDNS_HRS04		0x10		/* PHY access port */
#define   SDHCI_CDNS_HRS04_ACK			BIT(26)
#define   SDHCI_CDNS_HRS04_RD			BIT(25)
#define   SDHCI_CDNS_HRS04_WR			BIT(24)
#define   SDHCI_CDNS_HRS04_RDATA_SHIFT		16
#define   SDHCI_CDNS_HRS04_WDATA_SHIFT		8
#define   SDHCI_CDNS_HRS04_ADDR_SHIFT		0

#define SDHCI_CDNS_HRS06		0x18		/* eMMC control */
#define   SDHCI_CDNS_HRS06_TUNE_UP		BIT(15)
#define   SDHCI_CDNS_HRS06_TUNE_SHIFT		8
#define   SDHCI_CDNS_HRS06_TUNE_MASK		0x3f
#define   SDHCI_CDNS_HRS06_MODE_MASK		0x7
#define   SDHCI_CDNS_HRS06_MODE_SD		0x0
#define   SDHCI_CDNS_HRS06_MODE_MMC_SDR		0x2
#define   SDHCI_CDNS_HRS06_MODE_MMC_DDR		0x3
#define   SDHCI_CDNS_HRS06_MODE_MMC_HS200	0x4
#define   SDHCI_CDNS_HRS06_MODE_MMC_HS400	0x5

/* SRS - Slot Register Set (SDHCI-compatible) */
#define SDHCI_CDNS_SRS_BASE		0x200

/* PHY */
#define SDHCI_CDNS_PHY_DLY_SD_HS	0x00
#define SDHCI_CDNS_PHY_DLY_SD_DEFAULT	0x01
#define SDHCI_CDNS_PHY_DLY_UHS_SDR12	0x02
#define SDHCI_CDNS_PHY_DLY_UHS_SDR25	0x03
#define SDHCI_CDNS_PHY_DLY_UHS_SDR50	0x04
#define SDHCI_CDNS_PHY_DLY_UHS_DDR50	0x05
#define SDHCI_CDNS_PHY_DLY_EMMC_LEGACY	0x06
#define SDHCI_CDNS_PHY_DLY_EMMC_SDR	0x07
#define SDHCI_CDNS_PHY_DLY_EMMC_DDR	0x08

/*
 * The tuned val register is 6 bit-wide, but not the whole of the range is
 * available.  The range 0-42 seems to be available (then 43 wraps around to 0)
 * but I am not quite sure if it is official.  Use only 0 to 39 for safety.
 */
#define SDHCI_CDNS_MAX_TUNING_LOOP	40

struct sdhci_cdns_priv {
	void __iomem *hrs_addr;
};

static void sdhci_cdns_write_phy_reg(struct sdhci_cdns_priv *priv,
				     u8 addr, u8 data)
{
	void __iomem *reg = priv->hrs_addr + SDHCI_CDNS_HRS04;
	u32 tmp;

	tmp = (data << SDHCI_CDNS_HRS04_WDATA_SHIFT) |
	      (addr << SDHCI_CDNS_HRS04_ADDR_SHIFT);
	writel(tmp, reg);

	tmp |= SDHCI_CDNS_HRS04_WR;
	writel(tmp, reg);

	tmp &= ~SDHCI_CDNS_HRS04_WR;
	writel(tmp, reg);
}

static void sdhci_cdns_phy_init(struct sdhci_cdns_priv *priv)
{
	sdhci_cdns_write_phy_reg(priv, SDHCI_CDNS_PHY_DLY_SD_HS, 4);
	sdhci_cdns_write_phy_reg(priv, SDHCI_CDNS_PHY_DLY_SD_DEFAULT, 4);
	sdhci_cdns_write_phy_reg(priv, SDHCI_CDNS_PHY_DLY_EMMC_LEGACY, 9);
	sdhci_cdns_write_phy_reg(priv, SDHCI_CDNS_PHY_DLY_EMMC_SDR, 2);
	sdhci_cdns_write_phy_reg(priv, SDHCI_CDNS_PHY_DLY_EMMC_DDR, 3);
}

static inline void *sdhci_cdns_priv(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);

	return sdhci_pltfm_priv(pltfm_host);
}

static unsigned int sdhci_cdns_get_timeout_clock(struct sdhci_host *host)
{
	/*
	 * Cadence's spec says the Timeout Clock Frequency is the same as the
	 * Base Clock Frequency.  Divide it by 1000 to return a value in kHz.
	 */
	return host->max_clk / 1000;
}

static void sdhci_cdns_set_uhs_signaling(struct sdhci_host *host,
					 unsigned int timing)
{
	struct sdhci_cdns_priv *priv = sdhci_cdns_priv(host);
	u32 mode, tmp;

	switch (timing) {
	case MMC_TIMING_MMC_HS:
		mode = SDHCI_CDNS_HRS06_MODE_MMC_SDR;
		break;
	case MMC_TIMING_MMC_DDR52:
		mode = SDHCI_CDNS_HRS06_MODE_MMC_DDR;
		break;
	case MMC_TIMING_MMC_HS200:
		mode = SDHCI_CDNS_HRS06_MODE_MMC_HS200;
		break;
	case MMC_TIMING_MMC_HS400:
		mode = SDHCI_CDNS_HRS06_MODE_MMC_HS400;
		break;
	default:
		mode = SDHCI_CDNS_HRS06_MODE_SD;
		break;
	}

	/* The speed mode for eMMC is selected by HRS06 register */
	tmp = readl(priv->hrs_addr + SDHCI_CDNS_HRS06);
	tmp &= ~SDHCI_CDNS_HRS06_MODE_MASK;
	tmp |= mode;
	writel(tmp, priv->hrs_addr + SDHCI_CDNS_HRS06);

	/* For SD, fall back to the default handler */
	if (mode == SDHCI_CDNS_HRS06_MODE_SD)
		sdhci_set_uhs_signaling(host, timing);
}

static const struct sdhci_ops sdhci_cdns_ops = {
	.set_clock = sdhci_set_clock,
	.get_timeout_clock = sdhci_cdns_get_timeout_clock,
	.set_bus_width = sdhci_set_bus_width,
	.reset = sdhci_reset,
	.set_uhs_signaling = sdhci_cdns_set_uhs_signaling,
};

static const struct sdhci_pltfm_data sdhci_cdns_pltfm_data = {
	.ops = &sdhci_cdns_ops,
};

static int sdhci_cdns_set_tune_val(struct sdhci_host *host, unsigned int val)
{
	struct sdhci_cdns_priv *priv = sdhci_cdns_priv(host);
	void __iomem *reg = priv->hrs_addr + SDHCI_CDNS_HRS06;
	u32 tmp;

	if (WARN_ON(val > SDHCI_CDNS_HRS06_TUNE_MASK))
		return -EINVAL;

	tmp = readl(reg);
	tmp &= ~(SDHCI_CDNS_HRS06_TUNE_MASK << SDHCI_CDNS_HRS06_TUNE_SHIFT);
	tmp |= val << SDHCI_CDNS_HRS06_TUNE_SHIFT;
	tmp |= SDHCI_CDNS_HRS06_TUNE_UP;
	writel(tmp, reg);

	return readl_poll_timeout(reg, tmp, !(tmp & SDHCI_CDNS_HRS06_TUNE_UP),
				  0, 1);
}

static int sdhci_cdns_execute_tuning(struct mmc_host *mmc, u32 opcode)
{
	struct sdhci_host *host = mmc_priv(mmc);
	int cur_streak = 0;
	int max_streak = 0;
	int end_of_streak = 0;
	int i;

	/*
	 * This handler only implements the eMMC tuning that is specific to
	 * this controller.  Fall back to the standard method for SD timing.
	 */
	if (host->timing != MMC_TIMING_MMC_HS200)
		return sdhci_execute_tuning(mmc, opcode);

	if (WARN_ON(opcode != MMC_SEND_TUNING_BLOCK_HS200))
		return -EINVAL;

	for (i = 0; i < SDHCI_CDNS_MAX_TUNING_LOOP; i++) {
		if (sdhci_cdns_set_tune_val(host, i) ||
		    mmc_send_tuning(host->mmc, opcode, NULL)) { /* bad */
			cur_streak = 0;
		} else { /* good */
			cur_streak++;
			if (cur_streak > max_streak) {
				max_streak = cur_streak;
				end_of_streak = i;
			}
		}
	}

	if (!max_streak) {
		dev_err(mmc_dev(host->mmc), "no tuning point found\n");
		return -EIO;
	}

	return sdhci_cdns_set_tune_val(host, end_of_streak - max_streak / 2);
}

static int sdhci_cdns_probe(struct platform_device *pdev)
{
	struct sdhci_host *host;
	struct sdhci_pltfm_host *pltfm_host;
	struct sdhci_cdns_priv *priv;
	struct clk *clk;
	int ret;

	clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	ret = clk_prepare_enable(clk);
	if (ret)
		return ret;

	host = sdhci_pltfm_init(pdev, &sdhci_cdns_pltfm_data, sizeof(*priv));
	if (IS_ERR(host)) {
		ret = PTR_ERR(host);
		goto disable_clk;
	}

	pltfm_host = sdhci_priv(host);
	pltfm_host->clk = clk;

	priv = sdhci_cdns_priv(host);
	priv->hrs_addr = host->ioaddr;
	host->ioaddr += SDHCI_CDNS_SRS_BASE;
	host->mmc_host_ops.execute_tuning = sdhci_cdns_execute_tuning;

	ret = mmc_of_parse(host->mmc);
	if (ret)
		goto free;

	sdhci_cdns_phy_init(priv);

	ret = sdhci_add_host(host);
	if (ret)
		goto free;

	return 0;
free:
	sdhci_pltfm_free(pdev);
disable_clk:
	clk_disable_unprepare(clk);

	return ret;
}

static const struct of_device_id sdhci_cdns_match[] = {
	{ .compatible = "socionext,uniphier-sd4hc" },
	{ .compatible = "cdns,sd4hc" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sdhci_cdns_match);

static struct platform_driver sdhci_cdns_driver = {
	.driver = {
		.name = "sdhci-cdns",
		.pm = &sdhci_pltfm_pmops,
		.of_match_table = sdhci_cdns_match,
	},
	.probe = sdhci_cdns_probe,
	.remove = sdhci_pltfm_unregister,
};
module_platform_driver(sdhci_cdns_driver);

MODULE_AUTHOR("Masahiro Yamada <yamada.masahiro@socionext.com>");
MODULE_DESCRIPTION("Cadence SD/SDIO/eMMC Host Controller Driver");
MODULE_LICENSE("GPL");
