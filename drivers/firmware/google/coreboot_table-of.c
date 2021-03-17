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
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#include "coreboot_table.h"

static int coreboot_table_of_probe(struct platform_device *pdev)
{
	struct device_node *fw_dn = pdev->dev.of_node;
	void __iomem *ptr;

	ptr = of_iomap(fw_dn, 0);
	of_node_put(fw_dn);
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
	{},
};

static struct platform_driver coreboot_table_of_driver = {
	.probe = coreboot_table_of_probe,
	.remove = coreboot_table_of_remove,
	.driver = {
		.name = "coreboot_table_of",
		.of_match_table = coreboot_of_match,
	},
};

static int __init platform_coreboot_table_of_init(void)
{
	struct platform_device *pdev;
	struct device_node *of_node;

	/* Limit device creation to the presence of /firmware/coreboot node */
	of_node = of_find_node_by_path("/firmware/coreboot");
	if (!of_node)
		return -ENODEV;

	if (!of_match_node(coreboot_of_match, of_node))
		return -ENODEV;

	pdev = of_platform_device_create(of_node, "coreboot_table_of", NULL);
	if (!pdev)
		return -ENODEV;

	return platform_driver_register(&coreboot_table_of_driver);
}

module_init(platform_coreboot_table_of_init);

MODULE_AUTHOR("Google, Inc.");
MODULE_LICENSE("GPL");
