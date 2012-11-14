/*
 * ACPI support for platform bus type.
 *
 * Copyright (C) 2012, Intel Corporation
 * Authors: Mika Westerberg <mika.westerberg@linux.intel.com>
 *          Mathias Nyman <mathias.nyman@linux.intel.com>
 *          Rafael J. Wysocki <rafael.j.wysocki@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>

ACPI_MODULE_NAME("platform");

/**
 * acpi_create_platform_device - Create platform device for ACPI device node
 * @adev: ACPI device node to create a platform device for.
 *
 * Check if the given @adev can be represented as a platform device and, if
 * that's the case, create and register a platform device, populate its common
 * resources and returns a pointer to it.  Otherwise, return %NULL.
 *
 * The platform device's name will be taken from the @adev's _HID and _UID.
 */
struct platform_device *acpi_create_platform_device(struct acpi_device *adev)
{
	struct platform_device *pdev = NULL;
	struct acpi_device *acpi_parent;
	struct device *parent = NULL;
	struct resource_list_entry *rentry;
	struct list_head resource_list;
	struct resource *resources;
	int count;

	/* If the ACPI node already has a physical device attached, skip it. */
	if (adev->physical_node_count)
		return NULL;

	INIT_LIST_HEAD(&resource_list);
	count = acpi_dev_get_resources(adev, &resource_list, NULL, NULL);
	if (count <= 0)
		return NULL;

	resources = kmalloc(count * sizeof(struct resource), GFP_KERNEL);
	if (!resources) {
		dev_err(&adev->dev, "No memory for resources\n");
		acpi_dev_free_resource_list(&resource_list);
		return NULL;
	}
	count = 0;
	list_for_each_entry(rentry, &resource_list, node)
		resources[count++] = rentry->res;

	acpi_dev_free_resource_list(&resource_list);

	/*
	 * If the ACPI node has a parent and that parent has a physical device
	 * attached to it, that physical device should be the parent of the
	 * platform device we are about to create.
	 */
	acpi_parent = adev->parent;
	if (acpi_parent) {
		struct acpi_device_physical_node *entry;
		struct list_head *list;

		mutex_lock(&acpi_parent->physical_node_lock);
		list = &acpi_parent->physical_node_list;
		if (!list_empty(list)) {
			entry = list_first_entry(list,
					struct acpi_device_physical_node,
					node);
			parent = entry->dev;
		}
		mutex_unlock(&acpi_parent->physical_node_lock);
	}
	pdev = platform_device_register_resndata(parent, dev_name(&adev->dev),
						 -1, resources, count, NULL, 0);
	if (IS_ERR(pdev)) {
		dev_err(&adev->dev, "platform device creation failed: %ld\n",
			PTR_ERR(pdev));
		pdev = NULL;
	} else {
		dev_dbg(&adev->dev, "created platform device %s\n",
			dev_name(&pdev->dev));
	}

	kfree(resources);
	return pdev;
}

static acpi_status acpi_platform_match(acpi_handle handle, u32 depth,
				       void *data, void **return_value)
{
	struct platform_device *pdev = data;
	struct acpi_device *adev;
	acpi_status status;

	status = acpi_bus_get_device(handle, &adev);
	if (ACPI_FAILURE(status))
		return status;

	/* Skip ACPI devices that have physical device attached */
	if (adev->physical_node_count)
		return AE_OK;

	if (!strcmp(dev_name(&pdev->dev), dev_name(&adev->dev))) {
		*(acpi_handle *)return_value = handle;
		return AE_CTRL_TERMINATE;
	}

	return AE_OK;
}

static int acpi_platform_find_device(struct device *dev, acpi_handle *handle)
{
	struct platform_device *pdev = to_platform_device(dev);
	char *name, *tmp, *hid;

	/*
	 * The platform device is named using the ACPI device name
	 * _HID:INSTANCE so we strip the INSTANCE out in order to find the
	 * correct device using its _HID.
	 */
	name = kstrdup(dev_name(dev), GFP_KERNEL);
	if (!name)
		return -ENOMEM;

	tmp = name;
	hid = strsep(&tmp, ":");
	if (!hid) {
		kfree(name);
		return -ENODEV;
	}

	*handle = NULL;
	acpi_get_devices(hid, acpi_platform_match, pdev, handle);

	kfree(name);
	return *handle ? 0 : -ENODEV;
}

static struct acpi_bus_type acpi_platform_bus = {
	.bus = &platform_bus_type,
	.find_device = acpi_platform_find_device,
};

static int __init acpi_platform_init(void)
{
	return register_acpi_bus_type(&acpi_platform_bus);
}
arch_initcall(acpi_platform_init);
