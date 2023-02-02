// SPDX-License-Identifier: GPL-2.0-only
/*
 * coreboot_table.c
 *
 * Module providing coreboot table access.
 *
 * Copyright 2017 Google Inc.
 * Copyright 2017 Samuel Holland <samuel@sholland.org>
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

static void coreboot_bus_remove(struct device *dev)
{
	struct coreboot_device *device = CB_DEV(dev);
	struct coreboot_driver *driver = CB_DRV(dev->driver);

	if (driver->remove)
		driver->remove(device);
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

static int coreboot_table_populate(struct device *dev, void *ptr)
{
	int i, ret;
	void *ptr_entry;
	struct coreboot_device *device;
	struct coreboot_table_entry *entry;
	struct coreboot_table_header *header = ptr;

	ptr_entry = ptr + header->header_bytes;
	for (i = 0; i < header->table_entries; i++) {
		entry = ptr_entry;

		if (entry->size < sizeof(*entry)) {
			dev_warn(dev, "coreboot table entry too small!\n");
			return -EINVAL;
		}

		device = kzalloc(sizeof(device->dev) + entry->size, GFP_KERNEL);
		if (!device)
			return -ENOMEM;

		device->dev.parent = dev;
		device->dev.bus = &coreboot_bus_type;
		device->dev.release = coreboot_device_release;
		memcpy(device->raw, ptr_entry, entry->size);

		switch (device->entry.tag) {
		case LB_TAG_CBMEM_ENTRY:
			dev_set_name(&device->dev, "cbmem-%08x",
				     device->cbmem_entry.id);
			break;
		default:
			dev_set_name(&device->dev, "coreboot%d", i);
			break;
		}

		ret = device_register(&device->dev);
		if (ret) {
			put_device(&device->dev);
			return ret;
		}

		ptr_entry += entry->size;
	}

	return 0;
}

static int coreboot_table_probe(struct platform_device *pdev)
{
	resource_size_t len;
	struct coreboot_table_header *header;
	struct resource *res;
	struct device *dev = &pdev->dev;
	void *ptr;
	int ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EINVAL;

	len = resource_size(res);
	if (!res->start || !len)
		return -EINVAL;

	/* Check just the header first to make sure things are sane */
	header = memremap(res->start, sizeof(*header), MEMREMAP_WB);
	if (!header)
		return -ENOMEM;

	len = header->header_bytes + header->table_bytes;
	ret = strncmp(header->signature, "LBIO", sizeof(header->signature));
	memunmap(header);
	if (ret) {
		dev_warn(dev, "coreboot table missing or corrupt!\n");
		return -ENODEV;
	}

	ptr = memremap(res->start, len, MEMREMAP_WB);
	if (!ptr)
		return -ENOMEM;

	ret = coreboot_table_populate(dev, ptr);

	memunmap(ptr);

	return ret;
}

static int __cb_dev_unregister(struct device *dev, void *dummy)
{
	device_unregister(dev);
	return 0;
}

static int coreboot_table_remove(struct platform_device *pdev)
{
	bus_for_each_dev(&coreboot_bus_type, NULL, NULL, __cb_dev_unregister);
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

static int __init coreboot_table_driver_init(void)
{
	int ret;

	ret = bus_register(&coreboot_bus_type);
	if (ret)
		return ret;

	ret = platform_driver_register(&coreboot_table_driver);
	if (ret) {
		bus_unregister(&coreboot_bus_type);
		return ret;
	}

	return 0;
}

static void __exit coreboot_table_driver_exit(void)
{
	platform_driver_unregister(&coreboot_table_driver);
	bus_unregister(&coreboot_bus_type);
}

module_init(coreboot_table_driver_init);
module_exit(coreboot_table_driver_exit);

MODULE_AUTHOR("Google, Inc.");
MODULE_LICENSE("GPL");
