/*
 * container.c  - ACPI Generic Container Driver
 *
 * Copyright (C) 2004 Anil S Keshavamurthy (anil.s.keshavamurthy@intel.com)
 * Copyright (C) 2004 Keiichiro Tokunaga (tokunaga.keiich@jp.fujitsu.com)
 * Copyright (C) 2004 Motoyuki Ito (motoyuki@soft.fujitsu.com)
 * Copyright (C) 2004 FUJITSU LIMITED
 * Copyright (C) 2004, 2013 Intel Corp.
 * Author: Rafael J. Wysocki <rafael.j.wysocki@intel.com>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or (at
 *  your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */
#include <linux/acpi.h>
#include <linux/container.h>

#include "internal.h"

#define _COMPONENT			ACPI_CONTAINER_COMPONENT
ACPI_MODULE_NAME("container");

static const struct acpi_device_id container_device_ids[] = {
	{"ACPI0004", 0},
	{"PNP0A05", 0},
	{"PNP0A06", 0},
	{"", 0},
};

static int acpi_container_offline(struct container_dev *cdev)
{
	struct acpi_device *adev = ACPI_COMPANION(&cdev->dev);
	struct acpi_device *child;

	/* Check all of the dependent devices' physical companions. */
	list_for_each_entry(child, &adev->children, node)
		if (!acpi_scan_is_offline(child, false))
			return -EBUSY;

	return 0;
}

static void acpi_container_release(struct device *dev)
{
	kfree(to_container_dev(dev));
}

static int container_device_attach(struct acpi_device *adev,
				   const struct acpi_device_id *not_used)
{
	struct container_dev *cdev;
	struct device *dev;
	int ret;

	if (adev->flags.is_dock_station)
		return 0;

	cdev = kzalloc(sizeof(*cdev), GFP_KERNEL);
	if (!cdev)
		return -ENOMEM;

	cdev->offline = acpi_container_offline;
	dev = &cdev->dev;
	dev->bus = &container_subsys;
	dev_set_name(dev, "%s", dev_name(&adev->dev));
	ACPI_COMPANION_SET(dev, adev);
	dev->release = acpi_container_release;
	ret = device_register(dev);
	if (ret) {
		put_device(dev);
		return ret;
	}
	adev->driver_data = dev;
	return 1;
}

static void container_device_detach(struct acpi_device *adev)
{
	struct device *dev = acpi_driver_data(adev);

	adev->driver_data = NULL;
	if (dev)
		device_unregister(dev);
}

static struct acpi_scan_handler container_handler = {
	.ids = container_device_ids,
	.attach = container_device_attach,
	.detach = container_device_detach,
	.hotplug = {
		.enabled = true,
		.demand_offline = true,
	},
};

void __init acpi_container_init(void)
{
	acpi_scan_add_handler_with_hotplug(&container_handler, "container");
}
