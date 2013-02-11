/*
 * Intel Low Power Subsystem clocks.
 *
 * Copyright (C) 2013, Intel Corporation
 * Authors: Mika Westerberg <mika.westerberg@linux.intel.com>
 *	    Heikki Krogerus <heikki.krogerus@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/acpi.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>

static int clk_lpss_is_mmio_resource(struct acpi_resource *res, void *data)
{
	struct resource r;
	return !acpi_dev_resource_memory(res, &r);
}

static acpi_status clk_lpss_find_mmio(acpi_handle handle, u32 level,
				      void *data, void **retval)
{
	struct resource_list_entry *rentry;
	struct list_head resource_list;
	struct acpi_device *adev;
	const char *uid = data;
	int ret;

	if (acpi_bus_get_device(handle, &adev))
		return AE_OK;

	if (uid) {
		if (!adev->pnp.unique_id)
			return AE_OK;
		if (strcmp(uid, adev->pnp.unique_id))
			return AE_OK;
	}

	INIT_LIST_HEAD(&resource_list);
	ret = acpi_dev_get_resources(adev, &resource_list,
				     clk_lpss_is_mmio_resource, NULL);
	if (ret < 0)
		return AE_NO_MEMORY;

	list_for_each_entry(rentry, &resource_list, node)
		if (resource_type(&rentry->res) == IORESOURCE_MEM) {
			*(struct resource *)retval = rentry->res;
			break;
		}

	acpi_dev_free_resource_list(&resource_list);
	return AE_OK;
}

/**
 * clk_register_lpss_gate - register LPSS clock gate
 * @name: name of this clock gate
 * @parent_name: parent clock name
 * @hid: ACPI _HID of the device
 * @uid: ACPI _UID of the device (optional)
 * @offset: LPSS PRV_CLOCK_PARAMS offset
 *
 * Creates and registers LPSS clock gate.
 */
struct clk *clk_register_lpss_gate(const char *name, const char *parent_name,
				   const char *hid, const char *uid,
				   unsigned offset)
{
	struct resource res = { };
	void __iomem *mmio_base;
	acpi_status status;
	struct clk *clk;

	/*
	 * First try to look the device and its mmio resource from the
	 * ACPI namespace.
	 */
	status = acpi_get_devices(hid, clk_lpss_find_mmio, (void *)uid,
				  (void **)&res);
	if (ACPI_FAILURE(status) || !res.start)
		return ERR_PTR(-ENODEV);

	mmio_base = ioremap(res.start, resource_size(&res));
	if (!mmio_base)
		return ERR_PTR(-ENOMEM);

	clk = clk_register_gate(NULL, name, parent_name, 0, mmio_base + offset,
				0, 0, NULL);
	if (IS_ERR(clk))
		iounmap(mmio_base);

	return clk;
}
