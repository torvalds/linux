/*
 * omap iommu: omap device registration
 *
 * Copyright (C) 2008-2009 Nokia Corporation
 *
 * Written by Hiroshi DOYU <Hiroshi.DOYU@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/slab.h>

#include <linux/platform_data/iommu-omap.h>
#include "soc.h"
#include "omap_hwmod.h"
#include "omap_device.h"

static int __init omap_iommu_dev_init(struct omap_hwmod *oh, void *unused)
{
	struct platform_device *pdev;
	struct iommu_platform_data *pdata;
	struct omap_mmu_dev_attr *a = (struct omap_mmu_dev_attr *)oh->dev_attr;
	static int i;

	pdata = kzalloc(sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	pdata->name = oh->name;
	pdata->nr_tlb_entries = a->nr_tlb_entries;

	if (oh->rst_lines_cnt == 1) {
		pdata->reset_name = oh->rst_lines->name;
		pdata->assert_reset = omap_device_assert_hardreset;
		pdata->deassert_reset = omap_device_deassert_hardreset;
	}

	pdev = omap_device_build("omap-iommu", i, oh, pdata, sizeof(*pdata));

	kfree(pdata);

	if (IS_ERR(pdev)) {
		pr_err("%s: device build err: %ld\n", __func__, PTR_ERR(pdev));
		return PTR_ERR(pdev);
	}

	i++;

	return 0;
}

static int __init omap_iommu_init(void)
{
	/* If dtb is there, the devices will be created dynamically */
	if (of_have_populated_dt())
		return -ENODEV;

	return omap_hwmod_for_each_by_class("mmu", omap_iommu_dev_init, NULL);
}
omap_subsys_initcall(omap_iommu_init);
/* must be ready before omap3isp is probed */
