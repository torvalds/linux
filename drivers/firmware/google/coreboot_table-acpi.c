/*
 * coreboot_table-acpi.c
 *
 * Using ACPI to locate Coreboot table and provide coreboot table access.
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

#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include "coreboot_table.h"

static int coreboot_table_acpi_probe(struct platform_device *pdev)
{
	phys_addr_t phyaddr;
	resource_size_t len;
	struct coreboot_table_header __iomem *header = NULL;
	struct resource *res;
	void __iomem *ptr = NULL;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EINVAL;

	len = resource_size(res);
	if (!res->start || !len)
		return -EINVAL;

	phyaddr = res->start;
	header = ioremap_cache(phyaddr, sizeof(*header));
	if (header == NULL)
		return -ENOMEM;

	ptr = ioremap_cache(phyaddr,
			    header->header_bytes + header->table_bytes);
	iounmap(header);
	if (!ptr)
		return -ENOMEM;

	return coreboot_table_init(&pdev->dev, ptr);
}

static int coreboot_table_acpi_remove(struct platform_device *pdev)
{
	return coreboot_table_exit();
}

static const struct acpi_device_id cros_coreboot_acpi_match[] = {
	{ "GOOGCB00", 0 },
	{ "BOOT0000", 0 },
	{ }
};
MODULE_DEVICE_TABLE(acpi, cros_coreboot_acpi_match);

static struct platform_driver coreboot_table_acpi_driver = {
	.probe = coreboot_table_acpi_probe,
	.remove = coreboot_table_acpi_remove,
	.driver = {
		.name = "coreboot_table_acpi",
		.acpi_match_table = ACPI_PTR(cros_coreboot_acpi_match),
	},
};

static int __init coreboot_table_acpi_init(void)
{
	return platform_driver_register(&coreboot_table_acpi_driver);
}

module_init(coreboot_table_acpi_init);

MODULE_AUTHOR("Google, Inc.");
MODULE_LICENSE("GPL");
