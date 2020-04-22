// SPDX-License-Identifier: GPL-2.0
//
// Copyright (C) 2019 Texas Instruments Incorporated - http://www.ti.com/
// Author: Vignesh Raghavendra <vigneshr@ti.com>

#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mtd/hyperbus.h>
#include <linux/mtd/map.h>
#include <linux/mtd/mtd.h>
#include <linux/of.h>
#include <linux/types.h>

static struct hyperbus_device *map_to_hbdev(struct map_info *map)
{
	return container_of(map, struct hyperbus_device, map);
}

static map_word hyperbus_read16(struct map_info *map, unsigned long addr)
{
	struct hyperbus_device *hbdev = map_to_hbdev(map);
	struct hyperbus_ctlr *ctlr = hbdev->ctlr;
	map_word read_data;

	read_data.x[0] = ctlr->ops->read16(hbdev, addr);

	return read_data;
}

static void hyperbus_write16(struct map_info *map, map_word d,
			     unsigned long addr)
{
	struct hyperbus_device *hbdev = map_to_hbdev(map);
	struct hyperbus_ctlr *ctlr = hbdev->ctlr;

	ctlr->ops->write16(hbdev, addr, d.x[0]);
}

static void hyperbus_copy_from(struct map_info *map, void *to,
			       unsigned long from, ssize_t len)
{
	struct hyperbus_device *hbdev = map_to_hbdev(map);
	struct hyperbus_ctlr *ctlr = hbdev->ctlr;

	ctlr->ops->copy_from(hbdev, to, from, len);
}

static void hyperbus_copy_to(struct map_info *map, unsigned long to,
			     const void *from, ssize_t len)
{
	struct hyperbus_device *hbdev = map_to_hbdev(map);
	struct hyperbus_ctlr *ctlr = hbdev->ctlr;

	ctlr->ops->copy_to(hbdev, to, from, len);
}

int hyperbus_register_device(struct hyperbus_device *hbdev)
{
	const struct hyperbus_ops *ops;
	struct hyperbus_ctlr *ctlr;
	struct device_node *np;
	struct map_info *map;
	struct device *dev;
	int ret;

	if (!hbdev || !hbdev->np || !hbdev->ctlr || !hbdev->ctlr->dev) {
		pr_err("hyperbus: please fill all the necessary fields!\n");
		return -EINVAL;
	}

	np = hbdev->np;
	ctlr = hbdev->ctlr;
	if (!of_device_is_compatible(np, "cypress,hyperflash")) {
		dev_err(ctlr->dev, "\"cypress,hyperflash\" compatible missing\n");
		return -ENODEV;
	}

	hbdev->memtype = HYPERFLASH;

	dev = ctlr->dev;
	map = &hbdev->map;
	map->name = dev_name(dev);
	map->bankwidth = 2;
	map->device_node = np;

	simple_map_init(map);
	ops = ctlr->ops;
	if (ops) {
		if (ops->read16)
			map->read = hyperbus_read16;
		if (ops->write16)
			map->write = hyperbus_write16;
		if (ops->copy_to)
			map->copy_to = hyperbus_copy_to;
		if (ops->copy_from)
			map->copy_from = hyperbus_copy_from;

		if (ops->calibrate && !ctlr->calibrated) {
			ret = ops->calibrate(hbdev);
			if (!ret) {
				dev_err(dev, "Calibration failed\n");
				return -ENODEV;
			}
			ctlr->calibrated = true;
		}
	}

	hbdev->mtd = do_map_probe("cfi_probe", map);
	if (!hbdev->mtd) {
		dev_err(dev, "probing of hyperbus device failed\n");
		return -ENODEV;
	}

	hbdev->mtd->dev.parent = dev;
	mtd_set_of_node(hbdev->mtd, np);

	ret = mtd_device_register(hbdev->mtd, NULL, 0);
	if (ret) {
		dev_err(dev, "failed to register mtd device\n");
		map_destroy(hbdev->mtd);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(hyperbus_register_device);

int hyperbus_unregister_device(struct hyperbus_device *hbdev)
{
	int ret = 0;

	if (hbdev && hbdev->mtd) {
		ret = mtd_device_unregister(hbdev->mtd);
		map_destroy(hbdev->mtd);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(hyperbus_unregister_device);

MODULE_DESCRIPTION("HyperBus Framework");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Vignesh Raghavendra <vigneshr@ti.com>");
