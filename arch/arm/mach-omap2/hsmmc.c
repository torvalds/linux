/*
 * linux/arch/arm/mach-omap2/hsmmc.c
 *
 * Copyright (C) 2007-2008 Texas Instruments
 * Copyright (C) 2008 Nokia Corporation
 * Author: Texas Instruments
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/mmc/host.h>
#include <linux/platform_data/gpio-omap.h>
#include <linux/platform_data/hsmmc-omap.h>

#include "soc.h"
#include "omap_device.h"
#include "omap-pm.h"

#include "hsmmc.h"
#include "control.h"

#if IS_ENABLED(CONFIG_MMC_OMAP_HS)

static u16 control_pbias_offset;
static u16 control_devconf1_offset;

#define HSMMC_NAME_LEN	9

static int __init omap_hsmmc_pdata_init(struct omap2_hsmmc_info *c,
					struct omap_hsmmc_platform_data *mmc)
{
	char *hc_name;

	hc_name = kzalloc(sizeof(char) * (HSMMC_NAME_LEN + 1), GFP_KERNEL);
	if (!hc_name) {
		kfree(hc_name);
		return -ENOMEM;
	}

	snprintf(hc_name, (HSMMC_NAME_LEN + 1), "mmc%islot%i", c->mmc, 1);
	mmc->name = hc_name;
	mmc->caps = c->caps;
	mmc->reg_offset = 0;

	return 0;
}

static int omap_hsmmc_done;

void omap_hsmmc_late_init(struct omap2_hsmmc_info *c)
{
	struct platform_device *pdev;
	int res;

	if (omap_hsmmc_done)
		return;

	omap_hsmmc_done = 1;

	for (; c->mmc; c++) {
		pdev = c->pdev;
		if (!pdev)
			continue;
		res = omap_device_register(pdev);
		if (res)
			pr_err("Could not late init MMC\n");
	}
}

#define MAX_OMAP_MMC_HWMOD_NAME_LEN		16

static void __init omap_hsmmc_init_one(struct omap2_hsmmc_info *hsmmcinfo,
					int ctrl_nr)
{
	struct omap_hwmod *oh;
	struct omap_hwmod *ohs[1];
	struct omap_device *od;
	struct platform_device *pdev;
	char oh_name[MAX_OMAP_MMC_HWMOD_NAME_LEN];
	struct omap_hsmmc_platform_data *mmc_data;
	struct omap_hsmmc_dev_attr *mmc_dev_attr;
	char *name;
	int res;

	mmc_data = kzalloc(sizeof(*mmc_data), GFP_KERNEL);
	if (!mmc_data)
		return;

	res = omap_hsmmc_pdata_init(hsmmcinfo, mmc_data);
	if (res < 0)
		goto free_mmc;

	name = "omap_hsmmc";
	res = snprintf(oh_name, MAX_OMAP_MMC_HWMOD_NAME_LEN,
		     "mmc%d", ctrl_nr);
	WARN(res >= MAX_OMAP_MMC_HWMOD_NAME_LEN,
	     "String buffer overflow in MMC%d device setup\n", ctrl_nr);

	oh = omap_hwmod_lookup(oh_name);
	if (!oh) {
		pr_err("Could not look up %s\n", oh_name);
		goto free_name;
	}
	ohs[0] = oh;
	if (oh->dev_attr != NULL) {
		mmc_dev_attr = oh->dev_attr;
		mmc_data->controller_flags = mmc_dev_attr->flags;
	}

	pdev = platform_device_alloc(name, ctrl_nr - 1);
	if (!pdev) {
		pr_err("Could not allocate pdev for %s\n", name);
		goto free_name;
	}
	dev_set_name(&pdev->dev, "%s.%d", pdev->name, pdev->id);

	od = omap_device_alloc(pdev, ohs, 1);
	if (IS_ERR(od)) {
		pr_err("Could not allocate od for %s\n", name);
		goto put_pdev;
	}

	res = platform_device_add_data(pdev, mmc_data,
			      sizeof(struct omap_hsmmc_platform_data));
	if (res) {
		pr_err("Could not add pdata for %s\n", name);
		goto put_pdev;
	}

	hsmmcinfo->pdev = pdev;

	res = omap_device_register(pdev);
	if (res) {
		pr_err("Could not register od for %s\n", name);
		goto free_od;
	}

	goto free_mmc;

free_od:
	omap_device_delete(od);

put_pdev:
	platform_device_put(pdev);

free_name:
	kfree(mmc_data->name);

free_mmc:
	kfree(mmc_data);
}

void __init omap_hsmmc_init(struct omap2_hsmmc_info *controllers)
{
	if (omap_hsmmc_done)
		return;

	omap_hsmmc_done = 1;

	if (cpu_is_omap2430()) {
		control_pbias_offset = OMAP243X_CONTROL_PBIAS_LITE;
		control_devconf1_offset = OMAP243X_CONTROL_DEVCONF1;
	} else {
		control_pbias_offset = OMAP343X_CONTROL_PBIAS_LITE;
		control_devconf1_offset = OMAP343X_CONTROL_DEVCONF1;
	}

	for (; controllers->mmc; controllers++)
		omap_hsmmc_init_one(controllers, controllers->mmc);

}

#endif
