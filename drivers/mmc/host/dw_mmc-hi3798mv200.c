// SPDX-License-Identifier: GPL-2.0-only
/*
 * Modified from dw_mmc-hi3798cv200.c
 *
 * Copyright (c) 2024 Yang Xiwen <forbidden405@outlook.com>
 * Copyright (c) 2018 HiSilicon Technologies Co., Ltd.
 */

#include <linux/clk.h>
#include <linux/mfd/syscon.h>
#include <linux/mmc/host.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include "dw_mmc.h"
#include "dw_mmc-pltfm.h"

#define SDMMC_TUNING_CTRL	0x118
#define SDMMC_TUNING_FIND_EDGE	BIT(5)

#define ALL_INT_CLR		0x1ffff

/* DLL ctrl reg */
#define SAP_DLL_CTRL_DLLMODE	BIT(16)

struct dw_mci_hi3798mv200_priv {
	struct clk *sample_clk;
	struct clk *drive_clk;
	struct regmap *crg_reg;
	u32 sap_dll_offset;
	struct mmc_clk_phase_map phase_map;
};

static void dw_mci_hi3798mv200_set_ios(struct dw_mci *host, struct mmc_ios *ios)
{
	struct dw_mci_hi3798mv200_priv *priv = host->priv;
	struct mmc_clk_phase phase = priv->phase_map.phase[ios->timing];
	u32 val;

	val = mci_readl(host, ENABLE_SHIFT);
	if (ios->timing == MMC_TIMING_MMC_DDR52
	    || ios->timing == MMC_TIMING_UHS_DDR50)
		val |= SDMMC_ENABLE_PHASE;
	else
		val &= ~SDMMC_ENABLE_PHASE;
	mci_writel(host, ENABLE_SHIFT, val);

	val = mci_readl(host, DDR_REG);
	if (ios->timing == MMC_TIMING_MMC_HS400)
		val |= SDMMC_DDR_HS400;
	else
		val &= ~SDMMC_DDR_HS400;
	mci_writel(host, DDR_REG, val);

	if (clk_set_rate(host->ciu_clk, ios->clock))
		dev_warn(host->dev, "Failed to set rate to %u\n", ios->clock);
	else
		/*
		 * CLK_MUX_ROUND_NEAREST is enabled for this clock
		 * The actual clock rate is not what we set, but a rounded value
		 * so we should get the rate once again
		 */
		host->bus_hz = clk_get_rate(host->ciu_clk);

	if (phase.valid) {
		clk_set_phase(priv->drive_clk, phase.out_deg);
		clk_set_phase(priv->sample_clk, phase.in_deg);
	} else {
		dev_warn(host->dev,
			 "The phase entry for timing mode %d is missing in device tree.\n",
			 ios->timing);
	}
}

static inline int dw_mci_hi3798mv200_enable_tuning(struct dw_mci_slot *slot)
{
	struct dw_mci_hi3798mv200_priv *priv = slot->host->priv;

	return regmap_clear_bits(priv->crg_reg, priv->sap_dll_offset, SAP_DLL_CTRL_DLLMODE);
}

static inline int dw_mci_hi3798mv200_disable_tuning(struct dw_mci_slot *slot)
{
	struct dw_mci_hi3798mv200_priv *priv = slot->host->priv;

	return regmap_set_bits(priv->crg_reg, priv->sap_dll_offset, SAP_DLL_CTRL_DLLMODE);
}

static int dw_mci_hi3798mv200_execute_tuning_mix_mode(struct dw_mci_slot *slot,
					     u32 opcode)
{
	static const int degrees[] = { 0, 45, 90, 135, 180, 225, 270, 315 };
	struct dw_mci *host = slot->host;
	struct dw_mci_hi3798mv200_priv *priv = host->priv;
	int raise_point = -1, fall_point = -1, mid;
	int err, prev_err = -1;
	int found = 0;
	int regval;
	int i;
	int ret;

	ret = dw_mci_hi3798mv200_enable_tuning(slot);
	if (ret < 0)
		return ret;

	for (i = 0; i < ARRAY_SIZE(degrees); i++) {
		clk_set_phase(priv->sample_clk, degrees[i]);
		mci_writel(host, RINTSTS, ALL_INT_CLR);

		/*
		 * HiSilicon implemented a tuning mechanism.
		 * It needs special interaction with the DLL.
		 *
		 * Treat edge(flip) found as an error too.
		 */
		err = mmc_send_tuning(slot->mmc, opcode, NULL);
		regval = mci_readl(host, TUNING_CTRL);
		if (err || (regval & SDMMC_TUNING_FIND_EDGE))
			err = 1;
		else
			found = 1;

		if (i > 0) {
			if (err && !prev_err)
				fall_point = i - 1;
			if (!err && prev_err)
				raise_point = i;
		}

		if (raise_point != -1 && fall_point != -1)
			goto tuning_out;

		prev_err = err;
	}

tuning_out:
	ret = dw_mci_hi3798mv200_disable_tuning(slot);
	if (ret < 0)
		return ret;

	if (found) {
		if (raise_point == -1)
			raise_point = 0;
		if (fall_point == -1)
			fall_point = ARRAY_SIZE(degrees) - 1;
		if (fall_point < raise_point) {
			if ((raise_point + fall_point) >
			    (ARRAY_SIZE(degrees) - 1))
				mid = fall_point / 2;
			else
				mid = (raise_point + ARRAY_SIZE(degrees) - 1) / 2;
		} else {
			mid = (raise_point + fall_point) / 2;
		}

		/*
		 * We don't care what timing we are tuning for,
		 * simply use the same phase for all timing needs tuning.
		 */
		priv->phase_map.phase[MMC_TIMING_MMC_HS200].in_deg = degrees[mid];
		priv->phase_map.phase[MMC_TIMING_MMC_HS400].in_deg = degrees[mid];
		priv->phase_map.phase[MMC_TIMING_UHS_SDR104].in_deg = degrees[mid];

		clk_set_phase(priv->sample_clk, degrees[mid]);
		dev_dbg(host->dev, "Tuning clk_sample[%d, %d], set[%d]\n",
			raise_point, fall_point, degrees[mid]);
		ret = 0;
	} else {
		dev_err(host->dev, "No valid clk_sample shift!\n");
		ret = -EINVAL;
	}

	mci_writel(host, RINTSTS, ALL_INT_CLR);

	return ret;
}

static int dw_mci_hi3798mv200_init(struct dw_mci *host)
{
	struct dw_mci_hi3798mv200_priv *priv;
	struct device_node *np = host->dev->of_node;
	int ret;

	priv = devm_kzalloc(host->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	mmc_of_parse_clk_phase(host->dev, &priv->phase_map);

	priv->sample_clk = devm_clk_get_enabled(host->dev, "ciu-sample");
	if (IS_ERR(priv->sample_clk))
		return dev_err_probe(host->dev, PTR_ERR(priv->sample_clk),
				     "failed to get enabled ciu-sample clock\n");

	priv->drive_clk = devm_clk_get_enabled(host->dev, "ciu-drive");
	if (IS_ERR(priv->drive_clk))
		return dev_err_probe(host->dev, PTR_ERR(priv->drive_clk),
				     "failed to get enabled ciu-drive clock\n");

	priv->crg_reg = syscon_regmap_lookup_by_phandle(np, "hisilicon,sap-dll-reg");
	if (IS_ERR(priv->crg_reg))
		return dev_err_probe(host->dev, PTR_ERR(priv->crg_reg),
				     "failed to get CRG reg\n");

	ret = of_property_read_u32_index(np, "hisilicon,sap-dll-reg", 1, &priv->sap_dll_offset);
	if (ret)
		return dev_err_probe(host->dev, ret, "failed to get sample DLL register offset\n");

	host->priv = priv;
	return 0;
}

static const struct dw_mci_drv_data hi3798mv200_data = {
	.common_caps = MMC_CAP_CMD23,
	.init = dw_mci_hi3798mv200_init,
	.set_ios = dw_mci_hi3798mv200_set_ios,
	.execute_tuning = dw_mci_hi3798mv200_execute_tuning_mix_mode,
};

static const struct of_device_id dw_mci_hi3798mv200_match[] = {
	{ .compatible = "hisilicon,hi3798mv200-dw-mshc" },
	{},
};

static int dw_mci_hi3798mv200_probe(struct platform_device *pdev)
{
	return dw_mci_pltfm_register(pdev, &hi3798mv200_data);
}

static void dw_mci_hi3798mv200_remove(struct platform_device *pdev)
{
	dw_mci_pltfm_remove(pdev);
}

MODULE_DEVICE_TABLE(of, dw_mci_hi3798mv200_match);
static struct platform_driver dw_mci_hi3798mv200_driver = {
	.probe = dw_mci_hi3798mv200_probe,
	.remove_new = dw_mci_hi3798mv200_remove,
	.driver = {
		.name = "dwmmc_hi3798mv200",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table = dw_mci_hi3798mv200_match,
	},
};
module_platform_driver(dw_mci_hi3798mv200_driver);

MODULE_DESCRIPTION("HiSilicon Hi3798MV200 Specific DW-MSHC Driver Extension");
MODULE_LICENSE("GPL");
