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
	struct resource_info *ri = data;

	switch (res->type) {
	case ACPI_RESOURCE_TYPE_FIXED_MEMORY32:
	case ACPI_RESOURCE_TYPE_IRQ:
		ri->n++;
		break;
	case ACPI_RESOURCE_TYPE_EXTENDED_IRQ:
		acpi_xirq = &res->data.extended_irq;
		ri->n += acpi_xirq->interrupt_count;
		break;
	case ACPI_RESOURCE_TYPE_ADDRESS32:
		if (res->data.address32.resource_type == ACPI_IO_RANGE)
			ri->n++;
		break;
	}

	return AE_OK;
}

static acpi_status acpi_platform_add_resources(struct acpi_resource *res,
					       void *data)
{
	struct acpi_resource_fixed_memory32 *acpi_mem;
	struct acpi_resource_address32 *acpi_add32;
	struct acpi_resource_extended_irq *acpi_xirq;
	struct acpi_resource_irq *acpi_irq;
	struct resource_info *ri = data;
	struct resource *r;
	int irq, i;

	switch (res->type) {
	case ACPI_RESOURCE_TYPE_FIXED_MEMORY32:
		acpi_mem = &res->data.fixed_memory32;
		r = &ri->res[ri->cur++];

		r->start = acpi_mem->address;
		r->end = r->start + acpi_mem->address_length - 1;
		r->flags = IORESOURCE_MEM;

		dev_dbg(ri->dev, "Memory32Fixed %pR\n", r);
		break;

	case ACPI_RESOURCE_TYPE_ADDRESS32:
		acpi_add32 = &res->data.address32;

		if (acpi_add32->resource_type == ACPI_IO_RANGE) {
			r = &ri->res[ri->cur++];
			r->start = acpi_add32->minimum;
			r->end = r->start + acpi_add32->address_length - 1;
			r->flags = IORESOURCE_IO;
			dev_dbg(ri->dev, "Address32 %pR\n", r);
		}
		break;

	case ACPI_RESOURCE_TYPE_IRQ:
		acpi_irq = &res->data.irq;
		r = &ri->res[ri->cur++];

		irq = acpi_register_gsi(ri->dev,
					acpi_irq->interrupts[0],
					acpi_irq->triggering,
					acpi_irq->polarity);

		r->start = r->end = irq;
		r->flags = IORESOURCE_IRQ;

		dev_dbg(ri->dev, "IRQ %pR\n", r);
		break;

	case ACPI_RESOURCE_TYPE_EXTENDED_IRQ:
		acpi_xirq = &res->data.extended_irq;

		for (i = 0; i < acpi_xirq->interrupt_count; i++, ri->cur++) {
			r = &ri->res[ri->cur];
			irq = acpi_register_gsi(ri->dev,
						acpi_xirq->interrupts[i],
						acpi_xirq->triggering,
						acpi_xirq->polarity);

			r->start = r->end = irq;
			r->flags = IORESOURCE_IRQ;

			dev_dbg(ri->dev, "Interrupt %pR\n", r);
		}
		break;
	}

	return AE_OK;
}

static acpi_status acpi_platform_get_device_uid(struct acpi_device *adev,
						int *uid)
{
	struct acpi_device_info *info;
	acpi_status status;

	status = acpi_get_object_info(adev->handle, &info);
	if (ACPI_FAILURE(status))
		return status;

	status = AE_NOT_EXIST;
	if ((info->valid & ACPI_VALID_UID) &&
	     !kstrtoint(info->unique_id.string, 0, uid))
		status = AE_OK;

	kfree(info);
	return status;
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
	int devid;

	/* If the ACPI node already has a physical device attached, skip it. */
	if (adev->physical_node_count)
		return NULL;

	/* Use the UID of the device as the new platform device id if found. */
	status = acpi_platform_get_device_uid(adev, &devid);
	if (ACPI_FAILURE(status))
		devid = -1;

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

	if (WARN_ON(ri.n != ri.cur))
		goto out;

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
	pdev = platform_device_register_resndata(parent, acpi_device_hid(adev),
						 devid, ri.res, ri.n, NULL, 0);
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

	if (!strcmp(pdev->name, acpi_device_hid(adev))) {
		int devid;

		/* Check that both name and UID match if it exists */
		status = acpi_platform_get_device_uid(adev, &devid);
		if (ACPI_FAILURE(status))
			devid = -1;

		if (pdev->id != devid)
			return AE_OK;

		*(acpi_handle *)return_value = handle;
		return AE_CTRL_TERMINATE;
	}

	return AE_OK;
}

static int acpi_platform_find_device(struct device *dev, acpi_handle *handle)
{
	struct platform_device *pdev = to_platform_device(dev);

	*handle = NULL;
	acpi_get_devices(pdev->name, acpi_platform_match, pdev, handle);

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
