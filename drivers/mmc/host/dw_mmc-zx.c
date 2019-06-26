// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ZX Specific Extensions for Synopsys DW Multimedia Card Interface driver
 *
 * Copyright (C) 2016, Linaro Ltd.
 * Copyright (C) 2016, ZTE Corp.
 */

#include <linux/clk.h>
#include <linux/mfd/syscon.h>
#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include "dw_mmc.h"
#include "dw_mmc-pltfm.h"
#include "dw_mmc-zx.h"

struct dw_mci_zx_priv_data {
	struct regmap	*sysc_base;
};

enum delay_type {
	DELAY_TYPE_READ,	/* read dqs delay */
	DELAY_TYPE_CLK,		/* clk sample delay */
};

static int dw_mci_zx_emmc_set_delay(struct dw_mci *host, unsigned int delay,
				    enum delay_type dflag)
{
	struct dw_mci_zx_priv_data *priv = host->priv;
	struct regmap *sysc_base = priv->sysc_base;
	unsigned int clksel;
	unsigned int loop = 1000;
	int ret;

	if (!sysc_base)
		return -EINVAL;

	ret = regmap_update_bits(sysc_base, LB_AON_EMMC_CFG_REG0,
				 PARA_HALF_CLK_MODE | PARA_DLL_BYPASS_MODE |
				 PARA_PHASE_DET_SEL_MASK |
				 PARA_DLL_LOCK_NUM_MASK |
				 DLL_REG_SET | PARA_DLL_START_MASK,
				 PARA_DLL_START(4) | PARA_DLL_LOCK_NUM(4));
	if (ret)
		return ret;

	ret = regmap_read(sysc_base, LB_AON_EMMC_CFG_REG1, &clksel);
	if (ret)
		return ret;

	if (dflag == DELAY_TYPE_CLK) {
		clksel &= ~CLK_SAMP_DELAY_MASK;
		clksel |= CLK_SAMP_DELAY(delay);
	} else {
		clksel &= ~READ_DQS_DELAY_MASK;
		clksel |= READ_DQS_DELAY(delay);
	}

	regmap_write(sysc_base, LB_AON_EMMC_CFG_REG1, clksel);
	regmap_update_bits(sysc_base, LB_AON_EMMC_CFG_REG0,
			   PARA_DLL_START_MASK | PARA_DLL_LOCK_NUM_MASK |
			   DLL_REG_SET,
			   PARA_DLL_START(4) | PARA_DLL_LOCK_NUM(4) |
			   DLL_REG_SET);

	do {
		ret = regmap_read(sysc_base, LB_AON_EMMC_CFG_REG2, &clksel);
		if (ret)
			return ret;

	} while (--loop && !(clksel & ZX_DLL_LOCKED));

	if (!loop) {
		dev_err(host->dev, "Error: %s dll lock fail\n", __func__);
		return -EIO;
	}

	return 0;
}

static int dw_mci_zx_emmc_execute_tuning(struct dw_mci_slot *slot, u32 opcode)
{
	struct dw_mci *host = slot->host;
	struct mmc_host *mmc = slot->mmc;
	int ret, len = 0, start = 0, end = 0, delay, best = 0;

	for (delay = 1; delay < 128; delay++) {
		ret = dw_mci_zx_emmc_set_delay(host, delay, DELAY_TYPE_CLK);
		if (!ret && mmc_send_tuning(mmc, opcode, NULL)) {
			if (start >= 0) {
				end = delay - 1;
				/* check and update longest good range */
				if ((end - start) > len) {
					best = (start + end) >> 1;
					len = end - start;
				}
			}
			start = -1;
			end = 0;
			continue;
		}
		if (start < 0)
			start = delay;
	}

	if (start >= 0) {
		end = delay - 1;
		if ((end - start) > len) {
			best = (start + end) >> 1;
			len = end - start;
		}
	}
	if (best < 0)
		return -EIO;

	dev_info(host->dev, "%s best range: start %d end %d\n", __func__,
		 start, end);
	return dw_mci_zx_emmc_set_delay(host, best, DELAY_TYPE_CLK);
}

static int dw_mci_zx_prepare_hs400_tuning(struct dw_mci *host,
					  struct mmc_ios *ios)
{
	int ret;

	/* config phase shift as 90 degree */
	ret = dw_mci_zx_emmc_set_delay(host, 32, DELAY_TYPE_READ);
	if (ret < 0)
		return -EIO;

	return 0;
}

static int dw_mci_zx_execute_tuning(struct dw_mci_slot *slot, u32 opcode)
{
	struct dw_mci *host = slot->host;

	if (host->verid == 0x290a) /* only for emmc */
		return dw_mci_zx_emmc_execute_tuning(slot, opcode);
	/* TODO: Add 0x210a dedicated tuning for sd/sdio */

	return 0;
}

static int dw_mci_zx_parse_dt(struct dw_mci *host)
{
	struct device_node *np = host->dev->of_node;
	struct device_node *node;
	struct dw_mci_zx_priv_data *priv;
	struct regmap *sysc_base;
	int ret;

	/* syscon is needed only by emmc */
	node = of_parse_phandle(np, "zte,aon-syscon", 0);
	if (node) {
		sysc_base = syscon_node_to_regmap(node);
		of_node_put(node);

		if (IS_ERR(sysc_base)) {
			ret = PTR_ERR(sysc_base);
			if (ret != -EPROBE_DEFER)
				dev_err(host->dev, "Can't get syscon: %d\n",
					ret);
			return ret;
		}
	} else {
		return 0;
	}

	priv = devm_kzalloc(host->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	priv->sysc_base = sysc_base;
	host->priv = priv;

	return 0;
}

static unsigned long zx_dwmmc_caps[3] = {
	MMC_CAP_CMD23,
	MMC_CAP_CMD23,
	MMC_CAP_CMD23,
};

static const struct dw_mci_drv_data zx_drv_data = {
	.caps			= zx_dwmmc_caps,
	.num_caps		= ARRAY_SIZE(zx_dwmmc_caps),
	.execute_tuning		= dw_mci_zx_execute_tuning,
	.prepare_hs400_tuning	= dw_mci_zx_prepare_hs400_tuning,
	.parse_dt               = dw_mci_zx_parse_dt,
};

static const struct of_device_id dw_mci_zx_match[] = {
	{ .compatible = "zte,zx296718-dw-mshc", .data = &zx_drv_data},
	{},
};
MODULE_DEVICE_TABLE(of, dw_mci_zx_match);

static int dw_mci_zx_probe(struct platform_device *pdev)
{
	const struct dw_mci_drv_data *drv_data;
	const struct of_device_id *match;

	match = of_match_node(dw_mci_zx_match, pdev->dev.of_node);
	drv_data = match->data;

	return dw_mci_pltfm_register(pdev, drv_data);
}

static const struct dev_pm_ops dw_mci_zx_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(dw_mci_runtime_suspend,
			   dw_mci_runtime_resume,
			   NULL)
};

static struct platform_driver dw_mci_zx_pltfm_driver = {
	.probe		= dw_mci_zx_probe,
	.remove		= dw_mci_pltfm_remove,
	.driver		= {
		.name		= "dwmmc_zx",
		.of_match_table	= dw_mci_zx_match,
		.pm		= &dw_mci_zx_dev_pm_ops,
	},
};

module_platform_driver(dw_mci_zx_pltfm_driver);

MODULE_DESCRIPTION("ZTE emmc/sd driver");
MODULE_LICENSE("GPL v2");
