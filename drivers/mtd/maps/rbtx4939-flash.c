// SPDX-License-Identifier: GPL-2.0-only
/*
 * rbtx4939-flash (based on physmap.c)
 *
 * This is a simplified physmap driver with map_init callback function.
 *
 * Copyright (C) 2009 Atsushi Nemoto <anemo@mba.ocn.ne.jp>
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>
#include <asm/txx9/rbtx4939.h>

struct rbtx4939_flash_info {
	struct mtd_info *mtd;
	struct map_info map;
};

static int rbtx4939_flash_remove(struct platform_device *dev)
{
	struct rbtx4939_flash_info *info;

	info = platform_get_drvdata(dev);
	if (!info)
		return 0;

	if (info->mtd) {
		mtd_device_unregister(info->mtd);
		map_destroy(info->mtd);
	}
	return 0;
}

static const char * const rom_probe_types[] = {
	"cfi_probe", "jedec_probe", NULL };

static int rbtx4939_flash_probe(struct platform_device *dev)
{
	struct rbtx4939_flash_data *pdata;
	struct rbtx4939_flash_info *info;
	struct resource *res;
	const char * const *probe_type;
	int err = 0;
	unsigned long size;

	pdata = dev_get_platdata(&dev->dev);
	if (!pdata)
		return -ENODEV;

	res = platform_get_resource(dev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;
	info = devm_kzalloc(&dev->dev, sizeof(struct rbtx4939_flash_info),
			    GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	platform_set_drvdata(dev, info);

	size = resource_size(res);
	pr_notice("rbtx4939 platform flash device: %pR\n", res);

	if (!devm_request_mem_region(&dev->dev, res->start, size,
				     dev_name(&dev->dev)))
		return -EBUSY;

	info->map.name = dev_name(&dev->dev);
	info->map.phys = res->start;
	info->map.size = size;
	info->map.bankwidth = pdata->width;

	info->map.virt = devm_ioremap(&dev->dev, info->map.phys, size);
	if (!info->map.virt)
		return -EBUSY;

	if (pdata->map_init)
		(*pdata->map_init)(&info->map);
	else
		simple_map_init(&info->map);

	probe_type = rom_probe_types;
	for (; !info->mtd && *probe_type; probe_type++)
		info->mtd = do_map_probe(*probe_type, &info->map);
	if (!info->mtd) {
		dev_err(&dev->dev, "map_probe failed\n");
		err = -ENXIO;
		goto err_out;
	}
	info->mtd->dev.parent = &dev->dev;
	err = mtd_device_register(info->mtd, pdata->parts, pdata->nr_parts);

	if (err)
		goto err_out;
	return 0;

err_out:
	rbtx4939_flash_remove(dev);
	return err;
}

#ifdef CONFIG_PM
static void rbtx4939_flash_shutdown(struct platform_device *dev)
{
	struct rbtx4939_flash_info *info = platform_get_drvdata(dev);

	if (mtd_suspend(info->mtd) == 0)
		mtd_resume(info->mtd);
}
#else
#define rbtx4939_flash_shutdown NULL
#endif

static struct platform_driver rbtx4939_flash_driver = {
	.probe		= rbtx4939_flash_probe,
	.remove		= rbtx4939_flash_remove,
	.shutdown	= rbtx4939_flash_shutdown,
	.driver		= {
		.name	= "rbtx4939-flash",
	},
};

module_platform_driver(rbtx4939_flash_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("RBTX4939 MTD map driver");
MODULE_ALIAS("platform:rbtx4939-flash");
