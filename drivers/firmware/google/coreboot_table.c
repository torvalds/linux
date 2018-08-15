/*
 * coreboot_table.c
 *
 * Module providing coreboot table access.
 *
 * Copyright 2017 Google Inc.
 * Copyright 2017 Samuel Holland <samuel@sholland.org>
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
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "coreboot_table.h"

#define CB_DEV(d) container_of(d, struct coreboot_device, dev)
#define CB_DRV(d) container_of(d, struct coreboot_driver, drv)

static struct coreboot_table_header __iomem *ptr_header;

static int coreboot_bus_match(struct device *dev, struct device_driver *drv)
{
	struct coreboot_device *device = CB_DEV(dev);
	struct coreboot_driver *driver = CB_DRV(drv);

	return device->entry.tag == driver->tag;
}

static int coreboot_bus_probe(struct device *dev)
{
	int ret = -ENODEV;
	struct coreboot_device *device = CB_DEV(dev);
	struct coreboot_driver *driver = CB_DRV(dev->driver);

	if (driver->probe)
		ret = driver->probe(device);

	return ret;
}

static int coreboot_bus_remove(struct device *dev)
{
	int ret = 0;
	struct coreboot_device *device = CB_DEV(dev);
	struct coreboot_driver *driver = CB_DRV(dev->driver);

	if (driver->remove)
		ret = driver->remove(device);

	return ret;
}

static struct bus_type coreboot_bus_type = {
	.name		= "coreboot",
	.match		= coreboot_bus_match,
	.probe		= coreboot_bus_probe,
	.remove		= coreboot_bus_remove,
};

static void coreboot_device_release(struct device *dev)
{
	struct coreboot_device *device = CB_DEV(dev);

	kfree(device);
}

int coreboot_driver_register(struct coreboot_driver *driver)
{
	driver->drv.bus = &coreboot_bus_type;

	return driver_register(&driver->drv);
}
EXPORT_SYMBOL(coreboot_driver_register);

void coreboot_driver_unregister(struct coreboot_driver *driver)
{
	driver_unregister(&driver->drv);
}
EXPORT_SYMBOL(coreboot_driver_unregister);

static int coreboot_table_init(struct device *dev, void __iomem *ptr)
{
	int i, ret;
	void *ptr_entry;
	struct coreboot_device *device;
	struct coreboot_table_entry entry;
	struct coreboot_table_header header;

	ptr_header = ptr;
	memcpy_fromio(&header, ptr_header, sizeof(header));

	if (strncmp(header.signature, "LBIO", sizeof(header.signature))) {
		pr_warn("coreboot_table: coreboot table missing or corrupt!\n");
		ret = -ENODEV;
		goto out;
	}

	ret = bus_register(&coreboot_bus_type);
	if (ret)
		goto out;

	ptr_entry = (void *)ptr_header + header.header_bytes;
	for (i = 0; i < header.table_entries; i++) {
		memcpy_fromio(&entry, ptr_entry, sizeof(entry));

		device = kzalloc(sizeof(struct device) + entry.size, GFP_KERNEL);
		if (!device) {
			ret = -ENOMEM;
			break;
		}

		dev_set_name(&device->dev, "coreboot%d", i);
		device->dev.parent = dev;
		device->dev.bus = &coreboot_bus_type;
		device->dev.release = coreboot_device_release;
		memcpy_fromio(&device->entry, ptr_entry, entry.size);

		ret = device_register(&device->dev);
		if (ret) {
			put_device(&device->dev);
			break;
		}

		ptr_entry += entry.size;
	}

	if (ret)
		bus_unregister(&coreboot_bus_type);

out:
	iounmap(ptr);
	return ret;
}

static int coreboot_table_probe(struct platform_device *pdev)
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

static int coreboot_table_remove(struct platform_device *pdev)
{
	if (ptr_header) {
		bus_unregister(&coreboot_bus_type);
		ptr_header = NULL;
	}

	return 0;
}

#ifdef CONFIG_ACPI
static const struct acpi_device_id cros_coreboot_acpi_match[] = {
	{ "GOOGCB00", 0 },
	{ "BOOT0000", 0 },
	{ }
};
MODULE_DEVICE_TABLE(acpi, cros_coreboot_acpi_match);
#endif

#ifdef CONFIG_OF
static const struct of_device_id coreboot_of_match[] = {
	{ .compatible = "coreboot" },
	{}
};
MODULE_DEVICE_TABLE(of, coreboot_of_match);
#endif

static struct platform_driver coreboot_table_driver = {
	.probe = coreboot_table_probe,
	.remove = coreboot_table_remove,
	.driver = {
		.name = "coreboot_table",
		.acpi_match_table = ACPI_PTR(cros_coreboot_acpi_match),
		.of_match_table = of_match_ptr(coreboot_of_match),
	},
};
module_platform_driver(coreboot_table_driver);
MODULE_AUTHOR("Google, Inc.");
MODULE_LICENSE("GPL");
