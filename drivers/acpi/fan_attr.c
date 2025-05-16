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
		count += sysfs_emit_at(buf, count, "not-defined:");
	else
		count += sysfs_emit_at(buf, count, "%lld:", fps->trip_point);

	if (fps->speed == 0xFFFFFFFF)
		count += sysfs_emit_at(buf, count, "not-defined:");
	else
		count += sysfs_emit_at(buf, count, "%lld:", fps->speed);

	if (fps->noise_level == 0xFFFFFFFF)
		count += sysfs_emit_at(buf, count, "not-defined:");
	else
		count += sysfs_emit_at(buf, count, "%lld:", fps->noise_level * 100);

	if (fps->power == 0xFFFFFFFF)
		count += sysfs_emit_at(buf, count, "not-defined\n");
	else
		count += sysfs_emit_at(buf, count, "%lld\n", fps->power);

	return count;
}

static ssize_t show_fan_speed(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct acpi_device *acpi_dev = container_of(dev, struct acpi_device, dev);
	struct acpi_fan_fst fst;
	int status;

	status = acpi_fan_get_fst(acpi_dev, &fst);
	if (status)
		return status;

	return sprintf(buf, "%lld\n", fst.speed);
}

static ssize_t show_fine_grain_control(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct acpi_device *acpi_dev = container_of(dev, struct acpi_device, dev);
	struct acpi_fan *fan = acpi_driver_data(acpi_dev);

	return sprintf(buf, "%d\n", fan->fif.fine_grain_ctrl);
}

int acpi_fan_create_attributes(struct acpi_device *device)
{
	struct acpi_fan *fan = acpi_driver_data(device);
	int i, status;

	/* _FST is present if we are here */
	sysfs_attr_init(&fan->fst_speed.attr);
	fan->fst_speed.show = show_fan_speed;
	fan->fst_speed.store = NULL;
	fan->fst_speed.attr.name = "fan_speed_rpm";
	fan->fst_speed.attr.mode = 0444;
	status = sysfs_create_file(&device->dev.kobj, &fan->fst_speed.attr);
	if (status)
		return status;

	if (!fan->acpi4)
		return 0;

	sysfs_attr_init(&fan->fine_grain_control.attr);
	fan->fine_grain_control.show = show_fine_grain_control;
	fan->fine_grain_control.store = NULL;
	fan->fine_grain_control.attr.name = "fine_grain_control";
	fan->fine_grain_control.attr.mode = 0444;
	status = sysfs_create_file(&device->dev.kobj, &fan->fine_grain_control.attr);
	if (status)
		goto rem_fst_attr;

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
			goto rem_fine_grain_attr;
		}
	}

	return 0;

rem_fine_grain_attr:
	sysfs_remove_file(&device->dev.kobj, &fan->fine_grain_control.attr);

rem_fst_attr:
	sysfs_remove_file(&device->dev.kobj, &fan->fst_speed.attr);

	return status;
}

void acpi_fan_delete_attributes(struct acpi_device *device)
{
	struct acpi_fan *fan = acpi_driver_data(device);
	int i;

	sysfs_remove_file(&device->dev.kobj, &fan->fst_speed.attr);

	if (!fan->acpi4)
		return;

	for (i = 0; i < fan->fps_count; ++i)
		sysfs_remove_file(&device->dev.kobj, &fan->fps[i].dev_attr.attr);

	sysfs_remove_file(&device->dev.kobj, &fan->fine_grain_control.attr);
}
