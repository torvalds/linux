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

struct resource_info {
	struct device *dev;
	struct resource *res;
	size_t n, cur;
};

static acpi_status acpi_platform_count_resources(struct acpi_resource *res,
						 void *data)
{
	struct acpi_resource_extended_irq *acpi_xirq;
	struct acpi_resource_irq *acpi_irq;
	struct resource_info *ri = data;

	switch (res->type) {
	case ACPI_RESOURCE_TYPE_IRQ:
		acpi_irq = &res->data.irq;
		ri->n += acpi_irq->interrupt_count;
		break;
	case ACPI_RESOURCE_TYPE_EXTENDED_IRQ:
		acpi_xirq = &res->data.extended_irq;
		ri->n += acpi_xirq->interrupt_count;
		break;
	default:
		ri->n++;
	}

	return AE_OK;
}

static acpi_status acpi_platform_add_resources(struct acpi_resource *res,
					       void *data)
{
	struct resource_info *ri = data;
	struct resource *r;

	r = ri->res + ri->cur;
	if (acpi_dev_resource_memory(res, r)
	    || acpi_dev_resource_io(res, r)
	    || acpi_dev_resource_address_space(res, r)
	    || acpi_dev_resource_ext_address_space(res, r)) {
		ri->cur++;
		return AE_OK;
	}
	if (acpi_dev_resource_interrupt(res, 0, r)) {
		int i;

		r++;
		for (i = 1; acpi_dev_resource_interrupt(res, i, r); i++)
			r++;

		ri->cur += i;
	}
	return AE_OK;
}

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
	struct resource_info ri;
	acpi_status status;

	/* If the ACPI node already has a physical device attached, skip it. */
	if (adev->physical_node_count)
		return NULL;

	memset(&ri, 0, sizeof(ri));
	/* First, count the resources. */
	status = acpi_walk_resources(adev->handle, METHOD_NAME__CRS,
				     acpi_platform_count_resources, &ri);
	if (ACPI_FAILURE(status) || !ri.n)
		return NULL;

	/* Next, allocate memory for all the resources and populate it. */
	ri.dev = &adev->dev;
	ri.res = kzalloc(ri.n * sizeof(struct resource), GFP_KERNEL);
	if (!ri.res) {
		dev_err(&adev->dev,
			"failed to allocate memory for resources\n");
		return NULL;
	}

	status = acpi_walk_resources(adev->handle, METHOD_NAME__CRS,
				     acpi_platform_add_resources, &ri);
	if (ACPI_FAILURE(status)) {
		dev_err(&adev->dev, "failed to walk resources\n");
		goto out;
	}

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
						 -1, ri.res, ri.cur, NULL, 0);
	if (IS_ERR(pdev)) {
		dev_err(&adev->dev, "platform device creation failed: %ld\n",
			PTR_ERR(pdev));
		pdev = NULL;
	} else {
		dev_dbg(&adev->dev, "created platform device %s\n",
			dev_name(&pdev->dev));
	}

 out:
	kfree(ri.res);
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
