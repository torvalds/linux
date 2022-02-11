// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  fan_attr.c - Create extra attributes for ACPI Fan driver
 *
 *  Copyright (C) 2001, 2002 Andy Grover <andrew.grover@intel.com>
 *  Copyright (C) 2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 *  Copyright (C) 2022 Intel Corporation. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/acpi.h>

#include "fan.h"

MODULE_LICENSE("GPL");

static ssize_t show_state(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct acpi_fan_fps *fps = container_of(attr, struct acpi_fan_fps, dev_attr);
	int count;

	if (fps->control == 0xFFFFFFFF || fps->control > 100)
		count = scnprintf(buf, PAGE_SIZE, "not-defined:");
	else
		count = scnprintf(buf, PAGE_SIZE, "%lld:", fps->control);

	if (fps->trip_point == 0xFFFFFFFF || fps->trip_point > 9)
		count += scnprintf(&buf[count], PAGE_SIZE - count, "not-defined:");
	else
		count += scnprintf(&buf[count], PAGE_SIZE - count, "%lld:", fps->trip_point);

	if (fps->speed == 0xFFFFFFFF)
		count += scnprintf(&buf[count], PAGE_SIZE - count, "not-defined:");
	else
		count += scnprintf(&buf[count], PAGE_SIZE - count, "%lld:", fps->speed);

	if (fps->noise_level == 0xFFFFFFFF)
		count += scnprintf(&buf[count], PAGE_SIZE - count, "not-defined:");
	else
		count += scnprintf(&buf[count], PAGE_SIZE - count, "%lld:", fps->noise_level * 100);

	if (fps->power == 0xFFFFFFFF)
		count += scnprintf(&buf[count], PAGE_SIZE - count, "not-defined\n");
	else
		count += scnprintf(&buf[count], PAGE_SIZE - count, "%lld\n", fps->power);

	return count;
}

int acpi_fan_create_attributes(struct acpi_device *device)
{
	struct acpi_fan *fan = acpi_driver_data(device);
	int i, status = 0;

	for (i = 0; i < fan->fps_count; ++i) {
		struct acpi_fan_fps *fps = &fan->fps[i];

		snprintf(fps->name, ACPI_FPS_NAME_LEN, "state%d", i);
		sysfs_attr_init(&fps->dev_attr.attr);
		fps->dev_attr.show = show_state;
		fps->dev_attr.store = NULL;
		fps->dev_attr.attr.name = fps->name;
		fps->dev_attr.attr.mode = 0444;
		status = sysfs_create_file(&device->dev.kobj, &fps->dev_attr.attr);
		if (status) {
			int j;

			for (j = 0; j < i; ++j)
				sysfs_remove_file(&device->dev.kobj, &fan->fps[j].dev_attr.attr);
			break;
		}
	}

	return status;
}

void acpi_fan_delete_attributes(struct acpi_device *device)
{
	struct acpi_fan *fan = acpi_driver_data(device);
	int i;

	for (i = 0; i < fan->fps_count; ++i)
		sysfs_remove_file(&device->dev.kobj, &fan->fps[i].dev_attr.attr);
}
