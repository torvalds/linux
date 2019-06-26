// SPDX-License-Identifier: GPL-2.0
/*
 * Platform UFS Host driver for Cadence controller
 *
 * Copyright (C) 2018 Cadence Design Systems, Inc.
 *
 * Authors:
 *	Jan Kotas <jank@cadence.com>
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/time.h>

#include "ufshcd-pltfrm.h"

#define CDNS_UFS_REG_HCLKDIV 0xFC

/**
 * Sets HCLKDIV register value based on the core_clk
 * @hba: host controller instance
 *
 * Return zero for success and non-zero for failure
 */
static int cdns_ufs_set_hclkdiv(struct ufs_hba *hba)
{
	struct ufs_clk_info *clki;
	struct list_head *head = &hba->clk_list_head;
	unsigned long core_clk_rate = 0;
	u32 core_clk_div = 0;

	if (list_empty(head))
		return 0;

	list_for_each_entry(clki, head, list) {
		if (IS_ERR_OR_NULL(clki->clk))
			continue;
		if (!strcmp(clki->name, "core_clk"))
			core_clk_rate = clk_get_rate(clki->clk);
	}

	if (!core_clk_rate) {
		dev_err(hba->dev, "%s: unable to find core_clk rate\n",
			__func__);
		return -EINVAL;
	}

	core_clk_div = core_clk_rate / USEC_PER_SEC;

	ufshcd_writel(hba, core_clk_div, CDNS_UFS_REG_HCLKDIV);
	/**
	 * Make sure the register was updated,
	 * UniPro layer will not work with an incorrect value.
	 */
	mb();

	return 0;
}

/**
 * Sets clocks used by the controller
 * @hba: host controller instance
 * @on: if true, enable clocks, otherwise disable
 * @status: notify stage (pre, post change)
 *
 * Return zero for success and non-zero for failure
 */
static int cdns_ufs_setup_clocks(struct ufs_hba *hba, bool on,
				 enum ufs_notify_change_status status)
{
	if ((!on) || (status == PRE_CHANGE))
		return 0;

	return cdns_ufs_set_hclkdiv(hba);
}

static struct ufs_hba_variant_ops cdns_pltfm_hba_vops = {
	.name = "cdns-ufs-pltfm",
	.setup_clocks = cdns_ufs_setup_clocks,
};

/**
 * cdns_ufs_pltfrm_probe - probe routine of the driver
 * @pdev: pointer to platform device handle
 *
 * Return zero for success and non-zero for failure
 */
static int cdns_ufs_pltfrm_probe(struct platform_device *pdev)
{
	int err;
	struct device *dev = &pdev->dev;

	/* Perform generic probe */
	err = ufshcd_pltfrm_init(pdev, &cdns_pltfm_hba_vops);
	if (err)
		dev_err(dev, "ufshcd_pltfrm_init() failed %d\n", err);

	return err;
}

/**
 * cdns_ufs_pltfrm_remove - removes the ufs driver
 * @pdev: pointer to platform device handle
 *
 * Always returns 0
 */
static int cdns_ufs_pltfrm_remove(struct platform_device *pdev)
{
	struct ufs_hba *hba =  platform_get_drvdata(pdev);

	ufshcd_remove(hba);
	return 0;
}

static const struct of_device_id cdns_ufs_of_match[] = {
	{ .compatible = "cdns,ufshc" },
	{},
};

MODULE_DEVICE_TABLE(of, cdns_ufs_of_match);

static const struct dev_pm_ops cdns_ufs_dev_pm_ops = {
	.suspend         = ufshcd_pltfrm_suspend,
	.resume          = ufshcd_pltfrm_resume,
	.runtime_suspend = ufshcd_pltfrm_runtime_suspend,
	.runtime_resume  = ufshcd_pltfrm_runtime_resume,
	.runtime_idle    = ufshcd_pltfrm_runtime_idle,
};

static struct platform_driver cdns_ufs_pltfrm_driver = {
	.probe	= cdns_ufs_pltfrm_probe,
	.remove	= cdns_ufs_pltfrm_remove,
	.driver	= {
		.name   = "cdns-ufshcd",
		.pm     = &cdns_ufs_dev_pm_ops,
		.of_match_table = cdns_ufs_of_match,
	},
};

module_platform_driver(cdns_ufs_pltfrm_driver);

MODULE_AUTHOR("Jan Kotas <jank@cadence.com>");
MODULE_DESCRIPTION("Cadence UFS host controller platform driver");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(UFSHCD_DRIVER_VERSION);
