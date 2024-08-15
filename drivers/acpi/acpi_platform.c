// SPDX-License-Identifier: GPL-2.0-only
/*
 * ACPI support for platform bus type.
 *
 * Copyright (C) 2012, Intel Corporation
 * Authors: Mika Westerberg <mika.westerberg@linux.intel.com>
 *          Mathias Nyman <mathias.nyman@linux.intel.com>
 *          Rafael J. Wysocki <rafael.j.wysocki@intel.com>
 */

#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/pci.h>
#include <linux/platform_device.h>

#include "internal.h"

static const struct acpi_device_id forbidden_id_list[] = {
	{"ACPI0009", 0},	/* IOxAPIC */
	{"ACPI000A", 0},	/* IOAPIC */
	{"PNP0000",  0},	/* PIC */
	{"PNP0100",  0},	/* Timer */
	{"PNP0200",  0},	/* AT DMA Controller */
	{"SMB0001",  0},	/* ACPI SMBUS virtual device */
	{ }
};

static struct platform_device *acpi_platform_device_find_by_companion(struct acpi_device *adev)
{
	struct device *dev;

	dev = bus_find_device_by_acpi_dev(&platform_bus_type, adev);
	return dev ? to_platform_device(dev) : NULL;
}

static int acpi_platform_device_remove_notify(struct notifier_block *nb,
					      unsigned long value, void *arg)
{
	struct acpi_device *adev = arg;
	struct platform_device *pdev;

	switch (value) {
	case ACPI_RECONFIG_DEVICE_ADD:
		/* Nothing to do here */
		break;
	case ACPI_RECONFIG_DEVICE_REMOVE:
		if (!acpi_device_enumerated(adev))
			break;

		pdev = acpi_platform_device_find_by_companion(adev);
		if (!pdev)
			break;

		platform_device_unregister(pdev);
		put_device(&pdev->dev);
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block acpi_platform_notifier = {
	.notifier_call = acpi_platform_device_remove_notify,
};

static void acpi_platform_fill_resource(struct acpi_device *adev,
	const struct resource *src, struct resource *dest)
{
	struct device *parent;

	*dest = *src;

	/*
	 * If the device has parent we need to take its resources into
	 * account as well because this device might consume part of those.
	 */
	parent = acpi_get_first_physical_node(acpi_dev_parent(adev));
	if (parent && dev_is_pci(parent))
		dest->parent = pci_find_resource(to_pci_dev(parent), dest);
}

/**
 * acpi_create_platform_device - Create platform device for ACPI device node
 * @adev: ACPI device node to create a platform device for.
 * @properties: Optional collection of build-in properties.
 *
 * Check if the given @adev can be represented as a platform device and, if
 * that's the case, create and register a platform device, populate its common
 * resources and returns a pointer to it.  Otherwise, return %NULL.
 *
 * Name of the platform device will be the same as @adev's.
 */
struct platform_device *acpi_create_platform_device(struct acpi_device *adev,
						    const struct property_entry *properties)
{
	struct acpi_device *parent = acpi_dev_parent(adev);
	struct platform_device *pdev = NULL;
	struct platform_device_info pdevinfo;
	struct resource_entry *rentry;
	struct list_head resource_list;
	struct resource *resources = NULL;
	int count;

	/* If the ACPI node already has a physical device attached, skip it. */
	if (adev->physical_node_count)
		return NULL;

	if (!acpi_match_device_ids(adev, forbidden_id_list))
		return ERR_PTR(-EINVAL);

	INIT_LIST_HEAD(&resource_list);
	count = acpi_dev_get_resources(adev, &resource_list, NULL, NULL);
	if (count < 0)
		return NULL;
	if (count > 0) {
		resources = kcalloc(count, sizeof(*resources), GFP_KERNEL);
		if (!resources) {
			acpi_dev_free_resource_list(&resource_list);
			return ERR_PTR(-ENOMEM);
		}
		count = 0;
		list_for_each_entry(rentry, &resource_list, node)
			acpi_platform_fill_resource(adev, rentry->res,
						    &resources[count++]);

		acpi_dev_free_resource_list(&resource_list);
	}

	memset(&pdevinfo, 0, sizeof(pdevinfo));
	/*
	 * If the ACPI node has a parent and that parent has a physical device
	 * attached to it, that physical device should be the parent of the
	 * platform device we are about to create.
	 */
	pdevinfo.parent = parent ? acpi_get_first_physical_node(parent) : NULL;
	pdevinfo.name = dev_name(&adev->dev);
	pdevinfo.id = PLATFORM_DEVID_NONE;
	pdevinfo.res = resources;
	pdevinfo.num_res = count;
	pdevinfo.fwnode = acpi_fwnode_handle(adev);
	pdevinfo.properties = properties;

	if (acpi_dma_supported(adev))
		pdevinfo.dma_mask = DMA_BIT_MASK(32);
	else
		pdevinfo.dma_mask = 0;

	pdev = platform_device_register_full(&pdevinfo);
	if (IS_ERR(pdev))
		dev_err(&adev->dev, "platform device creation failed: %ld\n",
			PTR_ERR(pdev));
	else {
		set_dev_node(&pdev->dev, acpi_get_node(adev->handle));
		dev_dbg(&adev->dev, "created platform device %s\n",
			dev_name(&pdev->dev));
	}

	kfree(resources);

	return pdev;
}
EXPORT_SYMBOL_GPL(acpi_create_platform_device);

void __init acpi_platform_init(void)
{
	acpi_reconfig_notifier_register(&acpi_platform_notifier);
}
