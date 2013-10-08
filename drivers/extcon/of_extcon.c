/*
 * OF helpers for External connector (extcon) framework
 *
 * Copyright (C) 2013 Texas Instruments, Inc.
 * Kishon Vijay Abraham I <kishon@ti.com>
 *
 * Copyright (C) 2013 Samsung Electronics
 * Chanwoo Choi <cw00.choi@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/extcon.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/extcon/of_extcon.h>

/*
 * of_extcon_get_extcon_dev - Get the name of extcon device from devicetree
 * @dev - instance to the given device
 * @index - index into list of extcon_dev
 *
 * return the instance of extcon device
 */
struct extcon_dev *of_extcon_get_extcon_dev(struct device *dev, int index)
{
	struct device_node *node;
	struct extcon_dev *edev;
	struct platform_device *extcon_parent_dev;

	if (!dev->of_node) {
		dev_dbg(dev, "device does not have a device node entry\n");
		return ERR_PTR(-EINVAL);
	}

	node = of_parse_phandle(dev->of_node, "extcon", index);
	if (!node) {
		dev_dbg(dev, "failed to get phandle in %s node\n",
			dev->of_node->full_name);
		return ERR_PTR(-ENODEV);
	}

	extcon_parent_dev = of_find_device_by_node(node);
	if (!extcon_parent_dev) {
		dev_dbg(dev, "unable to find device by node\n");
		return ERR_PTR(-EPROBE_DEFER);
	}

	edev = extcon_get_extcon_dev(dev_name(&extcon_parent_dev->dev));
	if (!edev) {
		dev_dbg(dev, "unable to get extcon device : %s\n",
				dev_name(&extcon_parent_dev->dev));
		return ERR_PTR(-ENODEV);
	}

	return edev;
}
EXPORT_SYMBOL_GPL(of_extcon_get_extcon_dev);
