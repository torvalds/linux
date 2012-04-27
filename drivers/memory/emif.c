/*
 * EMIF driver
 *
 * Copyright (C) 2012 Texas Instruments, Inc.
 *
 * Aneesh V <aneesh@ti.com>
 * Santosh Shilimkar <santosh.shilimkar@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/reboot.h>
#include <linux/platform_data/emif_plat.h>
#include <linux/io.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/module.h>
#include <linux/list.h>
#include <memory/jedec_ddr.h>
#include "emif.h"

/**
 * struct emif_data - Per device static data for driver's use
 * @duplicate:			Whether the DDR devices attached to this EMIF
 *				instance are exactly same as that on EMIF1. In
 *				this case we can save some memory and processing
 * @temperature_level:		Maximum temperature of LPDDR2 devices attached
 *				to this EMIF - read from MR4 register. If there
 *				are two devices attached to this EMIF, this
 *				value is the maximum of the two temperature
 *				levels.
 * @node:			node in the device list
 * @base:			base address of memory-mapped IO registers.
 * @dev:			device pointer.
 * @plat_data:			Pointer to saved platform data.
 */
struct emif_data {
	u8				duplicate;
	u8				temperature_level;
	struct list_head		node;
	void __iomem			*base;
	struct device			*dev;
	struct emif_platform_data	*plat_data;
};

static struct emif_data *emif1;
static LIST_HEAD(device_list);

static void get_default_timings(struct emif_data *emif)
{
	struct emif_platform_data *pd = emif->plat_data;

	pd->timings		= lpddr2_jedec_timings;
	pd->timings_arr_size	= ARRAY_SIZE(lpddr2_jedec_timings);

	dev_warn(emif->dev, "%s: using default timings\n", __func__);
}

static int is_dev_data_valid(u32 type, u32 density, u32 io_width, u32 phy_type,
		u32 ip_rev, struct device *dev)
{
	int valid;

	valid = (type == DDR_TYPE_LPDDR2_S4 ||
			type == DDR_TYPE_LPDDR2_S2)
		&& (density >= DDR_DENSITY_64Mb
			&& density <= DDR_DENSITY_8Gb)
		&& (io_width >= DDR_IO_WIDTH_8
			&& io_width <= DDR_IO_WIDTH_32);

	/* Combinations of EMIF and PHY revisions that we support today */
	switch (ip_rev) {
	case EMIF_4D:
		valid = valid && (phy_type == EMIF_PHY_TYPE_ATTILAPHY);
		break;
	case EMIF_4D5:
		valid = valid && (phy_type == EMIF_PHY_TYPE_INTELLIPHY);
		break;
	default:
		valid = 0;
	}

	if (!valid)
		dev_err(dev, "%s: invalid DDR details\n", __func__);
	return valid;
}

static int is_custom_config_valid(struct emif_custom_configs *cust_cfgs,
		struct device *dev)
{
	int valid = 1;

	if ((cust_cfgs->mask & EMIF_CUSTOM_CONFIG_LPMODE) &&
		(cust_cfgs->lpmode != EMIF_LP_MODE_DISABLE))
		valid = cust_cfgs->lpmode_freq_threshold &&
			cust_cfgs->lpmode_timeout_performance &&
			cust_cfgs->lpmode_timeout_power;

	if (cust_cfgs->mask & EMIF_CUSTOM_CONFIG_TEMP_ALERT_POLL_INTERVAL)
		valid = valid && cust_cfgs->temp_alert_poll_interval_ms;

	if (!valid)
		dev_warn(dev, "%s: invalid custom configs\n", __func__);

	return valid;
}

static struct emif_data *__init_or_module get_device_details(
		struct platform_device *pdev)
{
	u32				size;
	struct emif_data		*emif = NULL;
	struct ddr_device_info		*dev_info;
	struct emif_custom_configs	*cust_cfgs;
	struct emif_platform_data	*pd;
	struct device			*dev;
	void				*temp;

	pd = pdev->dev.platform_data;
	dev = &pdev->dev;

	if (!(pd && pd->device_info && is_dev_data_valid(pd->device_info->type,
			pd->device_info->density, pd->device_info->io_width,
			pd->phy_type, pd->ip_rev, dev))) {
		dev_err(dev, "%s: invalid device data\n", __func__);
		goto error;
	}

	emif	= devm_kzalloc(dev, sizeof(*emif), GFP_KERNEL);
	temp	= devm_kzalloc(dev, sizeof(*pd), GFP_KERNEL);
	dev_info = devm_kzalloc(dev, sizeof(*dev_info), GFP_KERNEL);

	if (!emif || !pd || !dev_info) {
		dev_err(dev, "%s:%d: allocation error\n", __func__, __LINE__);
		goto error;
	}

	memcpy(temp, pd, sizeof(*pd));
	pd = temp;
	memcpy(dev_info, pd->device_info, sizeof(*dev_info));

	pd->device_info		= dev_info;
	emif->plat_data		= pd;
	emif->dev		= dev;
	emif->temperature_level	= SDRAM_TEMP_NOMINAL;

	/*
	 * For EMIF instances other than EMIF1 see if the devices connected
	 * are exactly same as on EMIF1(which is typically the case). If so,
	 * mark it as a duplicate of EMIF1 and skip copying timings data.
	 * This will save some memory and some computation later.
	 */
	emif->duplicate = emif1 && (memcmp(dev_info,
		emif1->plat_data->device_info,
		sizeof(struct ddr_device_info)) == 0);

	if (emif->duplicate) {
		pd->timings = NULL;
		pd->min_tck = NULL;
		goto out;
	} else if (emif1) {
		dev_warn(emif->dev, "%s: Non-symmetric DDR geometry\n",
			__func__);
	}

	/*
	 * Copy custom configs - ignore allocation error, if any, as
	 * custom_configs is not very critical
	 */
	cust_cfgs = pd->custom_configs;
	if (cust_cfgs && is_custom_config_valid(cust_cfgs, dev)) {
		temp = devm_kzalloc(dev, sizeof(*cust_cfgs), GFP_KERNEL);
		if (temp)
			memcpy(temp, cust_cfgs, sizeof(*cust_cfgs));
		else
			dev_warn(dev, "%s:%d: allocation error\n", __func__,
				__LINE__);
		pd->custom_configs = temp;
	}

	/*
	 * Copy timings and min-tck values from platform data. If it is not
	 * available or if memory allocation fails, use JEDEC defaults
	 */
	size = sizeof(struct lpddr2_timings) * pd->timings_arr_size;
	if (pd->timings) {
		temp = devm_kzalloc(dev, size, GFP_KERNEL);
		if (temp) {
			memcpy(temp, pd->timings, sizeof(*pd->timings));
			pd->timings = temp;
		} else {
			dev_warn(dev, "%s:%d: allocation error\n", __func__,
				__LINE__);
			get_default_timings(emif);
		}
	} else {
		get_default_timings(emif);
	}

	if (pd->min_tck) {
		temp = devm_kzalloc(dev, sizeof(*pd->min_tck), GFP_KERNEL);
		if (temp) {
			memcpy(temp, pd->min_tck, sizeof(*pd->min_tck));
			pd->min_tck = temp;
		} else {
			dev_warn(dev, "%s:%d: allocation error\n", __func__,
				__LINE__);
			pd->min_tck = &lpddr2_jedec_min_tck;
		}
	} else {
		pd->min_tck = &lpddr2_jedec_min_tck;
	}

out:
	return emif;

error:
	return NULL;
}

static int __init_or_module emif_probe(struct platform_device *pdev)
{
	struct emif_data	*emif;
	struct resource		*res;

	emif = get_device_details(pdev);
	if (!emif) {
		pr_err("%s: error getting device data\n", __func__);
		goto error;
	}

	if (!emif1)
		emif1 = emif;

	list_add(&emif->node, &device_list);

	/* Save pointers to each other in emif and device structures */
	emif->dev = &pdev->dev;
	platform_set_drvdata(pdev, emif);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(emif->dev, "%s: error getting memory resource\n",
			__func__);
		goto error;
	}

	emif->base = devm_request_and_ioremap(emif->dev, res);
	if (!emif->base) {
		dev_err(emif->dev, "%s: devm_request_and_ioremap() failed\n",
			__func__);
		goto error;
	}

	dev_info(&pdev->dev, "%s: device configured with addr = %p\n",
		__func__, emif->base);

	return 0;
error:
	return -ENODEV;
}

static struct platform_driver emif_driver = {
	.driver = {
		.name = "emif",
	},
};

static int __init_or_module emif_register(void)
{
	return platform_driver_probe(&emif_driver, emif_probe);
}

static void __exit emif_unregister(void)
{
	platform_driver_unregister(&emif_driver);
}

module_init(emif_register);
module_exit(emif_unregister);
MODULE_DESCRIPTION("TI EMIF SDRAM Controller Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:emif");
MODULE_AUTHOR("Texas Instruments Inc");
