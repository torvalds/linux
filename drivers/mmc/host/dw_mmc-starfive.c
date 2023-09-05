// SPDX-License-Identifier: GPL-2.0
/*
 * StarFive Designware Mobile Storage Host Controller Driver
 *
 * Copyright (c) 2022 StarFive Technology Co., Ltd.
 */

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

struct starfive_priv {
	struct device *dev;
	struct regmap *reg_syscon;
	u32 syscon_offset;
	u32 syscon_shift;
	u32 syscon_mask;
};

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

static int dw_mci_starfive_execute_tuning(struct dw_mci_slot *slot,
					     u32 opcode)
{
	static const int grade  = MAX_DELAY_CHAIN;
	struct dw_mci *host = slot->host;
	struct starfive_priv *priv = host->priv;
	int rise_point = -1, fall_point = -1;
	int err, prev_err = 0;
	int i;
	bool found = 0;
	u32 regval;

	/*
	 * Use grade as the max delay chain, and use the rise_point and
	 * fall_point to ensure the best sampling point of a data input
	 * signals.
	 */
	for (i = 0; i < grade; i++) {
		regval = i << priv->syscon_shift;
		err = regmap_update_bits(priv->reg_syscon, priv->syscon_offset,
						priv->syscon_mask, regval);
		if (err)
			return err;
		mci_writel(host, RINTSTS, ALL_INT_CLR);

		err = mmc_send_tuning(slot->mmc, opcode, NULL);
		if (!err)
			found = 1;

		if (i > 0) {
			if (err && !prev_err)
				fall_point = i - 1;
			if (!err && prev_err)
				rise_point = i;
		}

		if (rise_point != -1 && fall_point != -1)
			goto tuning_out;

		prev_err = err;
		err = 0;
	}

tuning_out:
	if (found) {
		if (rise_point == -1)
			rise_point = 0;
		if (fall_point == -1)
			fall_point = grade - 1;
		if (fall_point < rise_point) {
			if ((rise_point + fall_point) >
			    (grade - 1))
				i = fall_point / 2;
			else
				i = (rise_point + grade - 1) / 2;
		} else {
			i = (rise_point + fall_point) / 2;
		}

		regval = i << priv->syscon_shift;
		err = regmap_update_bits(priv->reg_syscon, priv->syscon_offset,
						priv->syscon_mask, regval);
		if (err)
			return err;
		mci_writel(host, RINTSTS, ALL_INT_CLR);

		dev_info(host->dev, "Found valid delay chain! use it [delay=%d]\n", i);
	} else {
		dev_err(host->dev, "No valid delay chain! use default\n");
		err = -EINVAL;
	}

	mci_writel(host, RINTSTS, ALL_INT_CLR);
	return err;
}

static int dw_mci_starfive_parse_dt(struct dw_mci *host)
{
	struct of_phandle_args args;
	struct starfive_priv *priv;
	int ret;

	priv = devm_kzalloc(host->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	ret = of_parse_phandle_with_fixed_args(host->dev->of_node,
						"starfive,sysreg", 3, 0, &args);
	if (ret) {
		dev_err(host->dev, "Failed to parse starfive,sysreg\n");
		return -EINVAL;
	}

	priv->reg_syscon = syscon_node_to_regmap(args.np);
	of_node_put(args.np);
	if (IS_ERR(priv->reg_syscon))
		return PTR_ERR(priv->reg_syscon);

	priv->syscon_offset = args.args[0];
	priv->syscon_shift  = args.args[1];
	priv->syscon_mask   = args.args[2];

	host->priv = priv;

	return 0;
}

static const struct dw_mci_drv_data starfive_data = {
	.common_caps		= MMC_CAP_CMD23,
	.set_ios		= dw_mci_starfive_set_ios,
	.parse_dt		= dw_mci_starfive_parse_dt,
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
