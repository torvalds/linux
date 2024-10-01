// SPDX-License-Identifier: GPL-2.0-only
/*
 * Synopsys G210 Test Chip driver
 *
 * Copyright (C) 2015-2016 Synopsys, Inc. (www.synopsys.com)
 *
 * Authors: Joao Pinto <jpinto@synopsys.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/delay.h>
#include <linux/pm_runtime.h>

#include "ufshcd-pltfrm.h"
#include "ufshcd-dwc.h"
#include "tc-dwc-g210.h"

/*
 * UFS DWC specific variant operations
 */
static struct ufs_hba_variant_ops tc_dwc_g210_20bit_pltfm_hba_vops = {
	.name                   = "tc-dwc-g210-pltfm",
	.link_startup_notify	= ufshcd_dwc_link_startup_notify,
	.phy_initialization = tc_dwc_g210_config_20_bit,
};

static struct ufs_hba_variant_ops tc_dwc_g210_40bit_pltfm_hba_vops = {
	.name                   = "tc-dwc-g210-pltfm",
	.link_startup_notify	= ufshcd_dwc_link_startup_notify,
	.phy_initialization = tc_dwc_g210_config_40_bit,
};

static const struct of_device_id tc_dwc_g210_pltfm_match[] = {
	{
		.compatible = "snps,g210-tc-6.00-20bit",
		.data = &tc_dwc_g210_20bit_pltfm_hba_vops,
	},
	{
		.compatible = "snps,g210-tc-6.00-40bit",
		.data = &tc_dwc_g210_40bit_pltfm_hba_vops,
	},
	{ },
};
MODULE_DEVICE_TABLE(of, tc_dwc_g210_pltfm_match);

/**
 * tc_dwc_g210_pltfm_probe()
 * @pdev: pointer to platform device structure
 *
 */
static int tc_dwc_g210_pltfm_probe(struct platform_device *pdev)
{
	int err;
	const struct of_device_id *of_id;
	struct ufs_hba_variant_ops *vops;
	struct device *dev = &pdev->dev;

	of_id = of_match_node(tc_dwc_g210_pltfm_match, dev->of_node);
	vops = (struct ufs_hba_variant_ops *)of_id->data;

	/* Perform generic probe */
	err = ufshcd_pltfrm_init(pdev, vops);
	if (err)
		dev_err(dev, "ufshcd_pltfrm_init() failed %d\n", err);

	return err;
}

/**
 * tc_dwc_g210_pltfm_remove()
 * @pdev: pointer to platform device structure
 *
 */
static void tc_dwc_g210_pltfm_remove(struct platform_device *pdev)
{
	struct ufs_hba *hba =  platform_get_drvdata(pdev);

	pm_runtime_get_sync(&(pdev)->dev);
	ufshcd_remove(hba);
}

static const struct dev_pm_ops tc_dwc_g210_pltfm_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(ufshcd_system_suspend, ufshcd_system_resume)
	SET_RUNTIME_PM_OPS(ufshcd_runtime_suspend, ufshcd_runtime_resume, NULL)
};

static struct platform_driver tc_dwc_g210_pltfm_driver = {
	.probe		= tc_dwc_g210_pltfm_probe,
	.remove_new	= tc_dwc_g210_pltfm_remove,
	.driver		= {
		.name	= "tc-dwc-g210-pltfm",
		.pm	= &tc_dwc_g210_pltfm_pm_ops,
		.of_match_table	= of_match_ptr(tc_dwc_g210_pltfm_match),
	},
};

module_platform_driver(tc_dwc_g210_pltfm_driver);

MODULE_ALIAS("platform:tc-dwc-g210-pltfm");
MODULE_DESCRIPTION("Synopsys Test Chip G210 platform glue driver");
MODULE_AUTHOR("Joao Pinto <Joao.Pinto@synopsys.com>");
MODULE_LICENSE("Dual BSD/GPL");
