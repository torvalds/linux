// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Mellanox Technologies.
 */

#include <linux/arm-smccc.h>
#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include "dw_mmc.h"
#include "dw_mmc-pltfm.h"

#define UHS_REG_EXT_SAMPLE_MASK		GENMASK(22, 16)
#define UHS_REG_EXT_DRIVE_MASK		GENMASK(29, 23)
#define BLUEFIELD_UHS_REG_EXT_SAMPLE	2
#define BLUEFIELD_UHS_REG_EXT_DRIVE	4

/* SMC call for RST_N */
#define BLUEFIELD_SMC_SET_EMMC_RST_N	0x82000007

static void dw_mci_bluefield_set_ios(struct dw_mci *host, struct mmc_ios *ios)
{
	u32 reg;

	/* Update the Drive and Sample fields in register UHS_REG_EXT. */
	reg = mci_readl(host, UHS_REG_EXT);
	reg &= ~UHS_REG_EXT_SAMPLE_MASK;
	reg |= FIELD_PREP(UHS_REG_EXT_SAMPLE_MASK,
			  BLUEFIELD_UHS_REG_EXT_SAMPLE);
	reg &= ~UHS_REG_EXT_DRIVE_MASK;
	reg |= FIELD_PREP(UHS_REG_EXT_DRIVE_MASK, BLUEFIELD_UHS_REG_EXT_DRIVE);
	mci_writel(host, UHS_REG_EXT, reg);
}

static void dw_mci_bluefield_hw_reset(struct dw_mci *host)
{
		struct arm_smccc_res res = { 0 };

		arm_smccc_smc(BLUEFIELD_SMC_SET_EMMC_RST_N, 0, 0, 0, 0, 0, 0, 0,
			      &res);

		if (res.a0)
			pr_err("RST_N failed.\n");
}

static const struct dw_mci_drv_data bluefield_drv_data = {
	.set_ios		= dw_mci_bluefield_set_ios,
	.hw_reset		= dw_mci_bluefield_hw_reset
};

static const struct of_device_id dw_mci_bluefield_match[] = {
	{ .compatible = "mellanox,bluefield-dw-mshc",
	  .data = &bluefield_drv_data },
	{},
};
MODULE_DEVICE_TABLE(of, dw_mci_bluefield_match);

static int dw_mci_bluefield_probe(struct platform_device *pdev)
{
	return dw_mci_pltfm_register(pdev, &bluefield_drv_data);
}

static struct platform_driver dw_mci_bluefield_pltfm_driver = {
	.probe		= dw_mci_bluefield_probe,
	.remove		= dw_mci_pltfm_remove,
	.driver		= {
		.name		= "dwmmc_bluefield",
		.probe_type	= PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table	= dw_mci_bluefield_match,
		.pm		= &dw_mci_pltfm_pmops,
	},
};

module_platform_driver(dw_mci_bluefield_pltfm_driver);

MODULE_DESCRIPTION("BlueField DW Multimedia Card driver");
MODULE_AUTHOR("Mellanox Technologies");
MODULE_LICENSE("GPL v2");
