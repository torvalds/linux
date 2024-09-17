// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * container.c  - ACPI Generic Container Driver
 *
 * Copyright (C) 2004 Anil S Keshavamurthy (anil.s.keshavamurthy@intel.com)
 * Copyright (C) 2004 Keiichiro Tokunaga (tokunaga.keiich@jp.fujitsu.com)
 * Copyright (C) 2004 Motoyuki Ito (motoyuki@soft.fujitsu.com)
 * Copyright (C) 2004 FUJITSU LIMITED
 * Copyright (C) 2004, 2013 Intel Corp.
 * Author: Rafael J. Wysocki <rafael.j.wysocki@intel.com>
 */
#include <linux/acpi.h>
#include <linux/container.h>

#include "internal.h"

static const struct acpi_device_id container_device_ids[] = {
	{"ACPI0004", 0},
	{"PNP0A05", 0},
	{"PNP0A06", 0},
	{"", 0},
};

#ifdef CONFIG_ACPI_CONTAINER

static int check_offline(struct acpi_device *adev, void *not_used)
{
	if (acpi_scan_is_offline(adev, false))
		return 0;

	return -EBUSY;
}

static int acpi_container_offline(struct container_dev *cdev)
{
	/* Check all of the dependent devices' physical companions. */
	return acpi_dev_for_each_child(ACPI_COMPANION(&cdev->dev), check_offline, NULL);
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

static void container_device_online(struct acpi_device *adev)
{
	struct device *dev = acpi_driver_data(adev);

	kobject_uevent(&dev->kobj, KOBJ_ONLINE);
}

static struct acpi_scan_handler container_handler = {
	.ids = container_device_ids,
	.attach = container_device_attach,
	.detach = container_device_detach,
	.hotplug = {
		.enabled = true,
		.demand_offline = true,
		.notify_online = container_device_online,
	},
};

void __init acpi_container_init(void)
{
	acpi_scan_add_handler(&container_handler);
}

#else

static struct acpi_scan_handler container_handler = {
	.ids = container_device_ids,
};

void __init acpi_container_init(void)
{
	acpi_scan_add_handler_with_hotplug(&container_handler, "container");
}

#endif /* CONFIG_ACPI_CONTAINER */
