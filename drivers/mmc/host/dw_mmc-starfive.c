// SPDX-License-Identifier: GPL-2.0
/*
 * StarFive Designware Mobile Storage Host Controller Driver
 *
 * Copyright (c) 2022 StarFive Technology Co., Ltd.
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/mfd/syscon.h>
#include <linux/mmc/host.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include "dw_mmc.h"
#include "dw_mmc-pltfm.h"

#define ALL_INT_CLR		0x1ffff
#define MAX_DELAY_CHAIN		32

#define STARFIVE_SMPL_PHASE     GENMASK(20, 16)

static void dw_mci_starfive_set_ios(struct dw_mci *host, struct mmc_ios *ios)
{
	int ret;
	unsigned int clock;

	if (ios->timing == MMC_TIMING_MMC_DDR52 || ios->timing == MMC_TIMING_UHS_DDR50) {
		clock = (ios->clock > 50000000 && ios->clock <= 52000000) ? 100000000 : ios->clock;
		ret = clk_set_rate(host->ciu_clk, clock);
		if (ret)
			dev_dbg(host->dev, "Use an external frequency divider %uHz\n", ios->clock);
		host->bus_hz = clk_get_rate(host->ciu_clk);
	} else {
		dev_dbg(host->dev, "Using the internal divider\n");
	}
}

static void dw_mci_starfive_set_sample_phase(struct dw_mci *host, u32 smpl_phase)
{
	/* change driver phase and sample phase */
	u32 reg_value = mci_readl(host, UHS_REG_EXT);

	/* In UHS_REG_EXT, only 5 bits valid in DRV_PHASE and SMPL_PHASE */
	reg_value &= ~STARFIVE_SMPL_PHASE;
	reg_value |= FIELD_PREP(STARFIVE_SMPL_PHASE, smpl_phase);
	mci_writel(host, UHS_REG_EXT, reg_value);

	/* We should delay 1ms wait for timing setting finished. */
	mdelay(1);
}

static int dw_mci_starfive_execute_tuning(struct dw_mci_slot *slot,
					     u32 opcode)
{
	static const int grade  = MAX_DELAY_CHAIN;
	struct dw_mci *host = slot->host;
	int smpl_phase, smpl_raise = -1, smpl_fall = -1;
	int ret;

	for (smpl_phase = 0; smpl_phase < grade; smpl_phase++) {
		dw_mci_starfive_set_sample_phase(host, smpl_phase);
		mci_writel(host, RINTSTS, ALL_INT_CLR);

		ret = mmc_send_tuning(slot->mmc, opcode, NULL);

		if (!ret && smpl_raise < 0) {
			smpl_raise = smpl_phase;
		} else if (ret && smpl_raise >= 0) {
			smpl_fall = smpl_phase - 1;
			break;
		}
	}

	if (smpl_phase >= grade)
		smpl_fall = grade - 1;

	if (smpl_raise < 0) {
		smpl_phase = 0;
		dev_err(host->dev, "No valid delay chain! use default\n");
		ret = -EINVAL;
		goto out;
	}

	smpl_phase = (smpl_raise + smpl_fall) / 2;
	dev_dbg(host->dev, "Found valid delay chain! use it [delay=%d]\n", smpl_phase);
	ret = 0;

out:
	dw_mci_starfive_set_sample_phase(host, smpl_phase);
	mci_writel(host, RINTSTS, ALL_INT_CLR);
	return ret;
}

static const struct dw_mci_drv_data starfive_data = {
	.common_caps		= MMC_CAP_CMD23,
	.set_ios		= dw_mci_starfive_set_ios,
	.execute_tuning		= dw_mci_starfive_execute_tuning,
};

static const struct of_device_id dw_mci_starfive_match[] = {
	{ .compatible = "starfive,jh7110-mmc",
		.data = &starfive_data },
	{},
};
MODULE_DEVICE_TABLE(of, dw_mci_starfive_match);

static int dw_mci_starfive_probe(struct platform_device *pdev)
{
	return dw_mci_pltfm_register(pdev, &starfive_data);
}

static struct platform_driver dw_mci_starfive_driver = {
	.probe = dw_mci_starfive_probe,
	.remove_new = dw_mci_pltfm_remove,
	.driver = {
		.name = "dwmmc_starfive",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table = dw_mci_starfive_match,
	},
};
module_platform_driver(dw_mci_starfive_driver);

MODULE_DESCRIPTION("StarFive JH7110 Specific DW-MSHC Driver Extension");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:dwmmc_starfive");
