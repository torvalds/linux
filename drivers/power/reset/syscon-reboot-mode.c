/*
 * Copyright (c) 2016, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include "reboot-mode.h"

static struct regmap *map;
static u32 offset;
static u32 mask = 0xffffffff;

static int syscon_reboot_mode_write(int magic)
{
	regmap_update_bits(map, offset, mask, magic);

	return 0;
}

static int syscon_reboot_mode_probe(struct platform_device *pdev)
{
	int ret;

	map = syscon_node_to_regmap(pdev->dev.parent->of_node);
	if (IS_ERR(map))
		return PTR_ERR(map);
	if (of_property_read_u32(pdev->dev.of_node, "offset", &offset))
		return -EINVAL;
	of_property_read_u32(pdev->dev.of_node, "mask", &mask);
	ret = reboot_mode_register(&pdev->dev, syscon_reboot_mode_write);
	if (ret)
		dev_err(&pdev->dev, "can't register reboot mode\n");

	return ret;
}

static const struct of_device_id syscon_reboot_mode_of_match[] = {
	{ .compatible = "syscon-reboot-mode" },
	{}
};

static struct platform_driver syscon_reboot_mode_driver = {
	.probe = syscon_reboot_mode_probe,
	.driver = {
		.name = "syscon-reboot-mode",
		.of_match_table = syscon_reboot_mode_of_match,
	},
};
module_platform_driver(syscon_reboot_mode_driver);

MODULE_AUTHOR("Andy Yan <andy.yan@rock-chips.com");
MODULE_DESCRIPTION("SYSCON reboot mode driver");
MODULE_LICENSE("GPL v2");
