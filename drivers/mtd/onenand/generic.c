/*
 *  linux/drivers/mtd/onenand/generic.c
 *
 *  Copyright (c) 2005 Samsung Electronics
 *  Kyungmin Park <kyungmin.park@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Overview:
 *   This is a device driver for the OneNAND flash for generic boards.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/onenand.h>
#include <linux/mtd/partitions.h>
#include <linux/io.h>

/*
 * Note: Driver name and platform data format have been updated!
 *
 * This version of the driver is named "onenand-flash" and takes struct
 * onenand_platform_data as platform data. The old ARM-specific version
 * with the name "onenand" used to take struct flash_platform_data.
 */
#define DRIVER_NAME	"onenand-flash"

struct onenand_info {
	struct mtd_info		mtd;
	struct onenand_chip	onenand;
};

static int generic_onenand_probe(struct platform_device *pdev)
{
	struct onenand_info *info;
	struct onenand_platform_data *pdata = dev_get_platdata(&pdev->dev);
	struct resource *res = pdev->resource;
	unsigned long size = resource_size(res);
	int err;

	info = kzalloc(sizeof(struct onenand_info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	if (!request_mem_region(res->start, size, dev_name(&pdev->dev))) {
		err = -EBUSY;
		goto out_free_info;
	}

	info->onenand.base = ioremap(res->start, size);
	if (!info->onenand.base) {
		err = -ENOMEM;
		goto out_release_mem_region;
	}

	info->onenand.mmcontrol = pdata ? pdata->mmcontrol : NULL;
	info->onenand.irq = platform_get_irq(pdev, 0);

	info->mtd.dev.parent = &pdev->dev;
	info->mtd.priv = &info->onenand;

	if (onenand_scan(&info->mtd, 1)) {
		err = -ENXIO;
		goto out_iounmap;
	}

	err = mtd_device_parse_register(&info->mtd, NULL, NULL,
					pdata ? pdata->parts : NULL,
					pdata ? pdata->nr_parts : 0);

	platform_set_drvdata(pdev, info);

	return 0;

out_iounmap:
	iounmap(info->onenand.base);
out_release_mem_region:
	release_mem_region(res->start, size);
out_free_info:
	kfree(info);

	return err;
}

static int generic_onenand_remove(struct platform_device *pdev)
{
	struct onenand_info *info = platform_get_drvdata(pdev);
	struct resource *res = pdev->resource;
	unsigned long size = resource_size(res);

	if (info) {
		onenand_release(&info->mtd);
		release_mem_region(res->start, size);
		iounmap(info->onenand.base);
		kfree(info);
	}

	return 0;
}

static struct platform_driver generic_onenand_driver = {
	.driver = {
		.name		= DRIVER_NAME,
	},
	.probe		= generic_onenand_probe,
	.remove		= generic_onenand_remove,
};

module_platform_driver(generic_onenand_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kyungmin Park <kyungmin.park@samsung.com>");
MODULE_DESCRIPTION("Glue layer for OneNAND flash on generic boards");
MODULE_ALIAS("platform:" DRIVER_NAME);
