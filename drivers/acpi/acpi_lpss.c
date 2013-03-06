/*
 * ACPI support for Intel Lynxpoint LPSS.
 *
 * Copyright (C) 2013, Intel Corporation
 * Authors: Mika Westerberg <mika.westerberg@linux.intel.com>
 *          Rafael J. Wysocki <rafael.j.wysocki@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/acpi.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/platform_data/clk-lpss.h>

#include "internal.h"

ACPI_MODULE_NAME("acpi_lpss");

#define LPSS_CLK_OFFSET 0x800
#define LPSS_CLK_SIZE	0x04

struct lpss_device_desc {
	bool clk_required;
	const char *clk_parent;
};

struct lpss_private_data {
	void __iomem *mmio_base;
	resource_size_t mmio_size;
	struct clk *clk;
	const struct lpss_device_desc *dev_desc;
};

static struct lpss_device_desc lpt_dev_desc = {
	.clk_required = true,
	.clk_parent = "lpss_clk",
};

static const struct acpi_device_id acpi_lpss_device_ids[] = {
	/* Lynxpoint LPSS devices */
	{ "INT33C0", (unsigned long)&lpt_dev_desc },
	{ "INT33C1", (unsigned long)&lpt_dev_desc },
	{ "INT33C2", (unsigned long)&lpt_dev_desc },
	{ "INT33C3", (unsigned long)&lpt_dev_desc },
	{ "INT33C4", (unsigned long)&lpt_dev_desc },
	{ "INT33C5", (unsigned long)&lpt_dev_desc },
	{ "INT33C6", },
	{ "INT33C7", },

	{ }
};

static int is_memory(struct acpi_resource *res, void *not_used)
{
	struct resource r;
	return !acpi_dev_resource_memory(res, &r);
}

/* LPSS main clock device. */
static struct platform_device *lpss_clk_dev;

static inline void lpt_register_clock_device(void)
{
	lpss_clk_dev = platform_device_register_simple("clk-lpt", -1, NULL, 0);
}

static int register_device_clock(struct acpi_device *adev,
				 struct lpss_private_data *pdata)
{
	const struct lpss_device_desc *dev_desc = pdata->dev_desc;

	if (!lpss_clk_dev)
		lpt_register_clock_device();

	if (!dev_desc->clk_parent || !pdata->mmio_base
	    || pdata->mmio_size < LPSS_CLK_OFFSET + LPSS_CLK_SIZE)
		return -ENODATA;

	pdata->clk = clk_register_gate(NULL, dev_name(&adev->dev),
				       dev_desc->clk_parent, 0,
				       pdata->mmio_base + LPSS_CLK_OFFSET,
				       0, 0, NULL);
	if (IS_ERR(pdata->clk))
		return PTR_ERR(pdata->clk);

	clk_register_clkdev(pdata->clk, NULL, dev_name(&adev->dev));
	return 0;
}

static int acpi_lpss_create_device(struct acpi_device *adev,
				   const struct acpi_device_id *id)
{
	struct lpss_device_desc *dev_desc;
	struct lpss_private_data *pdata;
	struct resource_list_entry *rentry;
	struct list_head resource_list;
	int ret;

	dev_desc = (struct lpss_device_desc *)id->driver_data;
	if (!dev_desc)
		return acpi_create_platform_device(adev, id);

	pdata = kzalloc(sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	INIT_LIST_HEAD(&resource_list);
	ret = acpi_dev_get_resources(adev, &resource_list, is_memory, NULL);
	if (ret < 0)
		goto err_out;

	list_for_each_entry(rentry, &resource_list, node)
		if (resource_type(&rentry->res) == IORESOURCE_MEM) {
			pdata->mmio_size = resource_size(&rentry->res);
			pdata->mmio_base = ioremap(rentry->res.start,
						   pdata->mmio_size);
			pdata->dev_desc = dev_desc;
			break;
		}

	acpi_dev_free_resource_list(&resource_list);

	if (dev_desc->clk_required) {
		ret = register_device_clock(adev, pdata);
		if (ret) {
			/*
			 * Skip the device, but don't terminate the namespace
			 * scan.
			 */
			ret = 0;
			goto err_out;
		}
	}

	adev->driver_data = pdata;
	ret = acpi_create_platform_device(adev, id);
	if (ret > 0)
		return ret;

	adev->driver_data = NULL;

 err_out:
	kfree(pdata);
	return ret;
}

static struct acpi_scan_handler lpss_handler = {
	.ids = acpi_lpss_device_ids,
	.attach = acpi_lpss_create_device,
};

void __init acpi_lpss_init(void)
{
	if (!lpt_clk_init())
		acpi_scan_add_handler(&lpss_handler);
}
