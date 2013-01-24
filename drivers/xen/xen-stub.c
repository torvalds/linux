/*
 * xen-stub.c - stub drivers to reserve space for Xen
 *
 * Copyright (C) 2012 Intel Corporation
 *    Author: Liu Jinsong <jinsong.liu@intel.com>
 *    Author: Jiang Yunhong <yunhong.jiang@intel.com>
 *
 * Copyright (C) 2012 Oracle Inc
 *    Author: Konrad Rzeszutek Wilk <konrad.wilk@oracle.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/export.h>
#include <linux/types.h>
#include <linux/acpi.h>
#include <acpi/acpi_drivers.h>
#include <xen/acpi.h>

#ifdef CONFIG_ACPI

/*--------------------------------------------
	stub driver for Xen memory hotplug
--------------------------------------------*/

static const struct acpi_device_id memory_device_ids[] = {
	{ACPI_MEMORY_DEVICE_HID, 0},
	{"", 0},
};

static struct acpi_driver xen_stub_memory_device_driver = {
	/* same name as native memory driver to block native loaded */
	.name = "acpi_memhotplug",
	.class = ACPI_MEMORY_DEVICE_CLASS,
	.ids = memory_device_ids,
};

int xen_stub_memory_device_init(void)
{
	if (!xen_initial_domain())
		return -ENODEV;

	/* just reserve space for Xen, block native driver loaded */
	return acpi_bus_register_driver(&xen_stub_memory_device_driver);
}
EXPORT_SYMBOL_GPL(xen_stub_memory_device_init);
subsys_initcall(xen_stub_memory_device_init);

void xen_stub_memory_device_exit(void)
{
	acpi_bus_unregister_driver(&xen_stub_memory_device_driver);
}
EXPORT_SYMBOL_GPL(xen_stub_memory_device_exit);


/*--------------------------------------------
	stub driver for Xen cpu hotplug
--------------------------------------------*/

static const struct acpi_device_id processor_device_ids[] = {
	{ACPI_PROCESSOR_OBJECT_HID, 0},
	{ACPI_PROCESSOR_DEVICE_HID, 0},
	{"", 0},
};

static struct acpi_driver xen_stub_processor_driver = {
	/* same name as native processor driver to block native loaded */
	.name = "processor",
	.class = ACPI_PROCESSOR_CLASS,
	.ids = processor_device_ids,
};

int xen_stub_processor_init(void)
{
	if (!xen_initial_domain())
		return -ENODEV;

	/* just reserve space for Xen, block native driver loaded */
	return acpi_bus_register_driver(&xen_stub_processor_driver);
}
EXPORT_SYMBOL_GPL(xen_stub_processor_init);
subsys_initcall(xen_stub_processor_init);

void xen_stub_processor_exit(void)
{
	acpi_bus_unregister_driver(&xen_stub_processor_driver);
}
EXPORT_SYMBOL_GPL(xen_stub_processor_exit);

#endif
