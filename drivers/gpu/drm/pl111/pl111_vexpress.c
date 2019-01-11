// SPDX-License-Identifier: GPL-2.0
/*
 * Versatile Express PL111 handling
 * Copyright (C) 2018 Linus Walleij
 *
 * This module binds to the "arm,vexpress-muxfpga" device on the
 * Versatile Express configuration bus and sets up which CLCD instance
 * gets muxed out on the DVI bridge.
 */
#include <linux/device.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/vexpress.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include "pl111_drm.h"
#include "pl111_vexpress.h"

#define VEXPRESS_FPGAMUX_MOTHERBOARD		0x00
#define VEXPRESS_FPGAMUX_DAUGHTERBOARD_1	0x01
#define VEXPRESS_FPGAMUX_DAUGHTERBOARD_2	0x02

int pl111_vexpress_clcd_init(struct device *dev,
			     struct pl111_drm_dev_private *priv,
			     struct regmap *map)
{
	struct device_node *root;
	struct device_node *child;
	struct device_node *ct_clcd = NULL;
	bool has_coretile_clcd = false;
	bool has_coretile_hdlcd = false;
	bool mux_motherboard = true;
	u32 val;
	int ret;

	/*
	 * Check if we have a CLCD or HDLCD on the core tile by checking if a
	 * CLCD or HDLCD is available in the root of the device tree.
	 */
	root = of_find_node_by_path("/");
	if (!root)
		return -EINVAL;

	for_each_available_child_of_node(root, child) {
		if (of_device_is_compatible(child, "arm,pl111")) {
			has_coretile_clcd = true;
			ct_clcd = child;
			break;
		}
		if (of_device_is_compatible(child, "arm,hdlcd")) {
			has_coretile_hdlcd = true;
			break;
		}
	}

	/*
	 * If there is a coretile HDLCD and it has a driver,
	 * do not mux the CLCD on the motherboard to the DVI.
	 */
	if (has_coretile_hdlcd && IS_ENABLED(CONFIG_DRM_HDLCD))
		mux_motherboard = false;

	/*
	 * On the Vexpress CA9 we let the CLCD on the coretile
	 * take precedence, so also in this case do not mux the
	 * motherboard to the DVI.
	 */
	if (has_coretile_clcd)
		mux_motherboard = false;

	if (mux_motherboard) {
		dev_info(dev, "DVI muxed to motherboard CLCD\n");
		val = VEXPRESS_FPGAMUX_MOTHERBOARD;
	} else if (ct_clcd == dev->of_node) {
		dev_info(dev,
			 "DVI muxed to daughterboard 1 (core tile) CLCD\n");
		val = VEXPRESS_FPGAMUX_DAUGHTERBOARD_1;
	} else {
		dev_info(dev, "core tile graphics present\n");
		dev_info(dev, "this device will be deactivated\n");
		return -ENODEV;
	}

	ret = regmap_write(map, 0, val);
	if (ret) {
		dev_err(dev, "error setting DVI muxmode\n");
		return -ENODEV;
	}

	return 0;
}

/*
 * This sets up the regmap pointer that will then be retrieved by
 * the detection code in pl111_versatile.c and passed in to the
 * pl111_vexpress_clcd_init() function above.
 */
static int vexpress_muxfpga_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct regmap *map;

	map = devm_regmap_init_vexpress_config(&pdev->dev);
	if (IS_ERR(map))
		return PTR_ERR(map);
	dev_set_drvdata(dev, map);

	return 0;
}

static const struct of_device_id vexpress_muxfpga_match[] = {
	{ .compatible = "arm,vexpress-muxfpga", },
	{}
};

static struct platform_driver vexpress_muxfpga_driver = {
	.driver = {
		.name = "vexpress-muxfpga",
		.of_match_table = of_match_ptr(vexpress_muxfpga_match),
	},
	.probe = vexpress_muxfpga_probe,
};

int vexpress_muxfpga_init(void)
{
	int ret;

	ret = platform_driver_register(&vexpress_muxfpga_driver);
	/* -EBUSY just means this driver is already registered */
	if (ret == -EBUSY)
		ret = 0;
	return ret;
}
