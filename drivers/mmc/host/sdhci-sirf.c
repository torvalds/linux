// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * SDHCI support for SiRF primaII and marco SoCs
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/mmc/host.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/mmc/slot-gpio.h>
#include "sdhci-pltfm.h"

#define SDHCI_CLK_DELAY_SETTING 0x4C
#define SDHCI_SIRF_8BITBUS BIT(3)
#define SIRF_TUNING_COUNT 16384

static void sdhci_sirf_set_bus_width(struct sdhci_host *host, int width)
{
	u8 ctrl;

	ctrl = sdhci_readb(host, SDHCI_HOST_CONTROL);
	ctrl &= ~(SDHCI_CTRL_4BITBUS | SDHCI_SIRF_8BITBUS);

	/*
	 * CSR atlas7 and prima2 SD host version is not 3.0
	 * 8bit-width enable bit of CSR SD hosts is 3,
	 * while stardard hosts use bit 5
	 */
	if (width == MMC_BUS_WIDTH_8)
		ctrl |= SDHCI_SIRF_8BITBUS;
	else if (width == MMC_BUS_WIDTH_4)
		ctrl |= SDHCI_CTRL_4BITBUS;

	sdhci_writeb(host, ctrl, SDHCI_HOST_CONTROL);
}

static u32 sdhci_sirf_readl_le(struct sdhci_host *host, int reg)
{
	u32 val = readl(host->ioaddr + reg);

	if (unlikely((reg == SDHCI_CAPABILITIES_1) &&
			(host->mmc->caps & MMC_CAP_UHS_SDR50))) {
		/* fake CAP_1 register */
		val = SDHCI_SUPPORT_DDR50 |
			SDHCI_SUPPORT_SDR50 | SDHCI_USE_SDR50_TUNING;
	}

	if (unlikely(reg == SDHCI_SLOT_INT_STATUS)) {
		u32 prss = val;
		/* fake chips as V3.0 host conreoller */
		prss &= ~(0xFF << 16);
		val = prss | (SDHCI_SPEC_300 << 16);
	}
	return val;
}

static u16 sdhci_sirf_readw_le(struct sdhci_host *host, int reg)
{
	u16 ret = 0;

	ret = readw(host->ioaddr + reg);

	if (unlikely(reg == SDHCI_HOST_VERSION)) {
		ret = readw(host->ioaddr + SDHCI_HOST_VERSION);
		ret |= SDHCI_SPEC_300;
	}

	return ret;
}

static int sdhci_sirf_execute_tuning(struct sdhci_host *host, u32 opcode)
{
	int tuning_seq_cnt = 3;
	int phase;
	u8 tuned_phase_cnt = 0;
	int rc = 0, longest_range = 0;
	int start = -1, end = 0, tuning_value = -1, range = 0;
	u16 clock_setting;
	struct mmc_host *mmc = host->mmc;

	clock_setting = sdhci_readw(host, SDHCI_CLK_DELAY_SETTING);
	clock_setting &= ~0x3fff;

retry:
	phase = 0;
	tuned_phase_cnt = 0;
	do {
		sdhci_writel(host,
			clock_setting | phase,
			SDHCI_CLK_DELAY_SETTING);

		if (!mmc_send_tuning(mmc, opcode, NULL)) {
			/* Tuning is successful at this tuning point */
			tuned_phase_cnt++;
			dev_dbg(mmc_dev(mmc), "%s: Found good phase = %d\n",
				 mmc_hostname(mmc), phase);
			if (start == -1)
				start = phase;
			end = phase;
			range++;
			if (phase == (SIRF_TUNING_COUNT - 1)
				&& range > longest_range)
				tuning_value = (start + end) / 2;
		} else {
			dev_dbg(mmc_dev(mmc), "%s: Found bad phase = %d\n",
				 mmc_hostname(mmc), phase);
			if (range > longest_range) {
				tuning_value = (start + end) / 2;
				longest_range = range;
			}
			start = -1;
			end = range = 0;
		}
	} while (++phase < SIRF_TUNING_COUNT);

	if (tuned_phase_cnt && tuning_value > 0) {
		/*
		 * Finally set the selected phase in delay
		 * line hw block.
		 */
		phase = tuning_value;
		sdhci_writel(host,
			clock_setting | phase,
			SDHCI_CLK_DELAY_SETTING);

		dev_dbg(mmc_dev(mmc), "%s: Setting the tuning phase to %d\n",
			 mmc_hostname(mmc), phase);
	} else {
		if (--tuning_seq_cnt)
			goto retry;
		/* Tuning failed */
		dev_dbg(mmc_dev(mmc), "%s: No tuning point found\n",
		       mmc_hostname(mmc));
		rc = -EIO;
	}

	return rc;
}

static const struct sdhci_ops sdhci_sirf_ops = {
	.read_l = sdhci_sirf_readl_le,
	.read_w = sdhci_sirf_readw_le,
	.platform_execute_tuning = sdhci_sirf_execute_tuning,
	.set_clock = sdhci_set_clock,
	.get_max_clock	= sdhci_pltfm_clk_get_max_clock,
	.set_bus_width = sdhci_sirf_set_bus_width,
	.reset = sdhci_reset,
	.set_uhs_signaling = sdhci_set_uhs_signaling,
};

static const struct sdhci_pltfm_data sdhci_sirf_pdata = {
	.ops = &sdhci_sirf_ops,
	.quirks = SDHCI_QUIRK_BROKEN_TIMEOUT_VAL |
		SDHCI_QUIRK_DATA_TIMEOUT_USES_SDCLK |
		SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN |
		SDHCI_QUIRK_RESET_CMD_DATA_ON_IOS,
	.quirks2 = SDHCI_QUIRK2_PRESET_VALUE_BROKEN,
};

static int sdhci_sirf_probe(struct platform_device *pdev)
{
	struct sdhci_host *host;
	struct sdhci_pltfm_host *pltfm_host;
	struct clk *clk;
	int ret;

	clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "unable to get clock");
		return PTR_ERR(clk);
	}

	host = sdhci_pltfm_init(pdev, &sdhci_sirf_pdata, 0);
	if (IS_ERR(host))
		return PTR_ERR(host);

	pltfm_host = sdhci_priv(host);
	pltfm_host->clk = clk;

	sdhci_get_of_property(pdev);

	ret = clk_prepare_enable(pltfm_host->clk);
	if (ret)
		goto err_clk_prepare;

	ret = sdhci_add_host(host);
	if (ret)
		goto err_sdhci_add;

	/*
	 * We must request the IRQ after sdhci_add_host(), as the tasklet only
	 * gets setup in sdhci_add_host() and we oops.
	 */
	ret = mmc_gpiod_request_cd(host->mmc, "cd", 0, false, 0, NULL);
	if (ret == -EPROBE_DEFER)
		goto err_request_cd;
	if (!ret)
		mmc_gpiod_request_cd_irq(host->mmc);

	return 0;

err_request_cd:
	sdhci_remove_host(host, 0);
err_sdhci_add:
	clk_disable_unprepare(pltfm_host->clk);
err_clk_prepare:
	sdhci_pltfm_free(pdev);
	return ret;
}

static const struct of_device_id sdhci_sirf_of_match[] = {
	{ .compatible = "sirf,prima2-sdhc" },
	{ }
};
MODULE_DEVICE_TABLE(of, sdhci_sirf_of_match);

static struct platform_driver sdhci_sirf_driver = {
	.driver		= {
		.name	= "sdhci-sirf",
		.of_match_table = sdhci_sirf_of_match,
		.pm	= &sdhci_pltfm_pmops,
	},
	.probe		= sdhci_sirf_probe,
	.remove		= sdhci_pltfm_unregister,
};

module_platform_driver(sdhci_sirf_driver);

MODULE_DESCRIPTION("SDHCI driver for SiRFprimaII/SiRFmarco");
MODULE_AUTHOR("Barry Song <21cnbao@gmail.com>");
MODULE_LICENSE("GPL v2");
