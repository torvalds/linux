/*
 * coreboot_table-of.c
 *
 * Coreboot table access through open firmware.
 *
 * Copyright 2017 Google Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License v2.0 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/device.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>

#include "coreboot_table.h"

static int coreboot_table_of_probe(struct platform_device *pdev)
{
	struct device_node *fw_dn = pdev->dev.of_node;
	void __iomem *ptr;

	ptr = of_iomap(fw_dn, 0);
	if (!ptr)
		return -ENOMEM;

	return coreboot_table_init(&pdev->dev, ptr);
}

static int coreboot_table_of_remove(struct platform_device *pdev)
{
	return coreboot_table_exit();
}

static const struct of_device_id coreboot_of_match[] = {
	{ .compatible = "coreboot" },
	{}
};
MODULE_DEVICE_TABLE(of, coreboot_of_match);

static struct platform_driver coreboot_table_of_driver = {
	.probe = coreboot_table_of_probe,
	.remove = coreboot_table_of_remove,
	.driver = {
		.name = "coreboot_table_of",
		.of_match_table = coreboot_of_match,
	},
};
module_platform_driver(coreboot_table_of_driver);

MODULE_AUTHOR("Google, Inc.");
MODULE_LICENSE("GPL");
