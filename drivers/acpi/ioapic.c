/*
 * IOAPIC/IOxAPIC/IOSAPIC driver
 *
 * Copyright (C) 2009 Fujitsu Limited.
 * (c) Copyright 2009 Hewlett-Packard Development Company, L.P.
 *
 * Copyright (C) 2014 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Based on original drivers/pci/ioapic.c
 *	Yinghai Lu <yinghai@kernel.org>
 *	Jiang Liu <jiang.liu@intel.com>
 */

/*
 * This driver manages I/O APICs added by hotplug after boot.
 * We try to claim all I/O APIC devices, but those present at boot were
 * registered when we parsed the ACPI MADT.
 */

#define pr_fmt(fmt) "ACPI : IOAPIC: " fmt

#include <linux/slab.h>
#include <linux/acpi.h>
#include <linux/pci.h>
#include <acpi/acpi.h>

struct acpi_pci_ioapic {
	acpi_handle	root_handle;
	acpi_handle	handle;
	u32		gsi_base;
	struct resource	res;
	struct pci_dev	*pdev;
	struct list_head list;
};

static LIST_HEAD(ioapic_list);
static DEFINE_MUTEX(ioapic_list_lock);

static acpi_status setup_res(struct acpi_resource *acpi_res, void *data)
{
	struct resource *res = data;
	struct resource_win win;

	res->flags = 0;
	if (acpi_dev_filter_resource_type(acpi_res, IORESOURCE_MEM) == 0)
		return AE_OK;

	if (!acpi_dev_resource_memory(acpi_res, res)) {
		if (acpi_dev_resource_address_space(acpi_res, &win) ||
		    acpi_dev_resource_ext_address_space(acpi_res, &win))
			*res = win.res;
	}
	if ((res->flags & IORESOURCE_PREFETCH) ||
	    (res->flags & IORESOURCE_DISABLED))
		res->flags = 0;

	return AE_CTRL_TERMINATE;
}

static bool acpi_is_ioapic(acpi_handle handle, char **type)
{
	acpi_status status;
	struct acpi_device_info *info;
	char *hid = NULL;
	bool match = false;

	if (!acpi_has_method(handle, "_GSB"))
		return false;

	status = acpi_get_object_info(handle, &info);
	if (ACPI_SUCCESS(status)) {
		if (info->valid & ACPI_VALID_HID)
			hid = info->hardware_id.string;
		if (hid) {
			if (strcmp(hid, "ACPI0009") == 0) {
				*type = "IOxAPIC";
				match = true;
			} else if (strcmp(hid, "ACPI000A") == 0) {
				*type = "IOAPIC";
				match = true;
			}
		}
		kfree(info);
	}

	return match;
}

static acpi_status handle_ioapic_add(acpi_handle handle, u32 lvl,
				     void *context, void **rv)
{
	acpi_status status;
	unsigned long long gsi_base;
	struct acpi_pci_ioapic *ioapic;
	struct pci_dev *dev = NULL;
	struct resource *res = NULL;
	char *type = NULL;

	if (!acpi_is_ioapic(handle, &type))
		return AE_OK;

	mutex_lock(&ioapic_list_lock);
	list_for_each_entry(ioapic, &ioapic_list, list)
		if (ioapic->handle == handle) {
			mutex_unlock(&ioapic_list_lock);
			return AE_OK;
		}

	status = acpi_evaluate_integer(handle, "_GSB", NULL, &gsi_base);
	if (ACPI_FAILURE(status)) {
		acpi_handle_warn(handle, "failed to evaluate _GSB method\n");
		goto exit;
	}

	ioapic = kzalloc(sizeof(*ioapic), GFP_KERNEL);
	if (!ioapic) {
		pr_err("cannot allocate memory for new IOAPIC\n");
		goto exit;
	} else {
		ioapic->root_handle = (acpi_handle)context;
		ioapic->handle = handle;
		ioapic->gsi_base = (u32)gsi_base;
		INIT_LIST_HEAD(&ioapic->list);
	}

	if (acpi_ioapic_registered(handle, (u32)gsi_base))
		goto done;

	dev = acpi_get_pci_dev(handle);
	if (dev && pci_resource_len(dev, 0)) {
		if (pci_enable_device(dev) < 0)
			goto exit_put;
		pci_set_master(dev);
		if (pci_request_region(dev, 0, type))
			goto exit_disable;
		res = &dev->resource[0];
		ioapic->pdev = dev;
	} else {
		pci_dev_put(dev);
		dev = NULL;

		res = &ioapic->res;
		acpi_walk_resources(handle, METHOD_NAME__CRS, setup_res, res);
		if (res->flags == 0) {
			acpi_handle_warn(handle, "failed to get resource\n");
			goto exit_free;
		} else if (request_resource(&iomem_resource, res)) {
			acpi_handle_warn(handle, "failed to insert resource\n");
			goto exit_free;
		}
	}

	if (acpi_register_ioapic(handle, res->start, (u32)gsi_base)) {
		acpi_handle_warn(handle, "failed to register IOAPIC\n");
		goto exit_release;
	}
done:
	list_add(&ioapic->list, &ioapic_list);
	mutex_unlock(&ioapic_list_lock);

	if (dev)
		dev_info(&dev->dev, "%s at %pR, GSI %u\n",
			 type, res, (u32)gsi_base);
	else
		acpi_handle_info(handle, "%s at %pR, GSI %u\n",
				 type, res, (u32)gsi_base);

	return AE_OK;

exit_release:
	if (dev)
		pci_release_region(dev, 0);
	else
		release_resource(res);
exit_disable:
	if (dev)
		pci_disable_device(dev);
exit_put:
	pci_dev_put(dev);
exit_free:
	kfree(ioapic);
exit:
	mutex_unlock(&ioapic_list_lock);
	*(acpi_status *)rv = AE_ERROR;
	return AE_OK;
}

int acpi_ioapic_add(struct acpi_pci_root *root)
{
	acpi_status status, retval = AE_OK;

	status = acpi_walk_namespace(ACPI_TYPE_DEVICE, root->device->handle,
				     UINT_MAX, handle_ioapic_add, NULL,
				     root->device->handle, (void **)&retval);

	return ACPI_SUCCESS(status) && ACPI_SUCCESS(retval) ? 0 : -ENODEV;
}

int acpi_ioapic_remove(struct acpi_pci_root *root)
{
	int retval = 0;
	struct acpi_pci_ioapic *ioapic, *tmp;

	mutex_lock(&ioapic_list_lock);
	list_for_each_entry_safe(ioapic, tmp, &ioapic_list, list) {
		if (root->device->handle != ioapic->root_handle)
			continue;

		if (acpi_unregister_ioapic(ioapic->handle, ioapic->gsi_base))
			retval = -EBUSY;

		if (ioapic->pdev) {
			pci_release_region(ioapic->pdev, 0);
			pci_disable_device(ioapic->pdev);
			pci_dev_put(ioapic->pdev);
		} else if (ioapic->res.flags && ioapic->res.parent) {
			release_resource(&ioapic->res);
		}
		list_del(&ioapic->list);
		kfree(ioapic);
	}
	mutex_unlock(&ioapic_list_lock);

	return retval;
}
