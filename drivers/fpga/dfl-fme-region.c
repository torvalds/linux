// SPDX-License-Identifier: GPL-2.0
/*
 * FPGA Region Driver for FPGA Management Engine (FME)
 *
 * Copyright (C) 2017-2018 Intel Corporation, Inc.
 *
 * Authors:
 *   Wu Hao <hao.wu@intel.com>
 *   Joseph Grecco <joe.grecco@intel.com>
 *   Enno Luebbers <enno.luebbers@intel.com>
 *   Tim Whisonant <tim.whisonant@intel.com>
 *   Ananda Ravuri <ananda.ravuri@intel.com>
 *   Henry Mitchel <henry.mitchel@intel.com>
 */

#include <linux/module.h>
#include <linux/fpga/fpga-mgr.h>
#include <linux/fpga/fpga-region.h>

#include "dfl-fme-pr.h"

static int fme_region_get_bridges(struct fpga_region *region)
{
	struct dfl_fme_region_pdata *pdata = region->priv;
	struct device *dev = &pdata->br->dev;

	return fpga_bridge_get_to_list(dev, region->info, &region->bridge_list);
}

static int fme_region_probe(struct platform_device *pdev)
{
	struct dfl_fme_region_pdata *pdata = dev_get_platdata(&pdev->dev);
	struct fpga_region_info info = { 0 };
	struct device *dev = &pdev->dev;
	struct fpga_region *region;
	struct fpga_manager *mgr;
	int ret;

	mgr = fpga_mgr_get(&pdata->mgr->dev);
	if (IS_ERR(mgr))
		return -EPROBE_DEFER;

	info.mgr = mgr;
	info.compat_id = mgr->compat_id;
	info.get_bridges = fme_region_get_bridges;
	info.priv = pdata;
	region = fpga_region_register_full(dev, &info);
	if (IS_ERR(region)) {
		ret = PTR_ERR(region);
		goto eprobe_mgr_put;
	}

	platform_set_drvdata(pdev, region);

	dev_dbg(dev, "DFL FME FPGA Region probed\n");

	return 0;

eprobe_mgr_put:
	fpga_mgr_put(mgr);
	return ret;
}

static void fme_region_remove(struct platform_device *pdev)
{
	struct fpga_region *region = platform_get_drvdata(pdev);
	struct fpga_manager *mgr = region->mgr;

	fpga_region_unregister(region);
	fpga_mgr_put(mgr);
}

static struct platform_driver fme_region_driver = {
	.driver = {
		.name = DFL_FPGA_FME_REGION,
	},
	.probe = fme_region_probe,
	.remove = fme_region_remove,
};

module_platform_driver(fme_region_driver);

MODULE_DESCRIPTION("FPGA Region for DFL FPGA Management Engine");
MODULE_AUTHOR("Intel Corporation");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:dfl-fme-region");
