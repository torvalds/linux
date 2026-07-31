// SPDX-License-Identifier: GPL-2.0
/*
 *  thermal_hwmon.c - Generic Thermal Management hwmon support.
 *
 *  Code based on Intel thermal_core.c. Copyrights of the original code:
 *  Copyright (C) 2008 Intel Corp
 *  Copyright (C) 2008 Zhang Rui <rui.zhang@intel.com>
 *  Copyright (C) 2008 Sujith Thomas <sujith.thomas@intel.com>
 *
 *  Copyright (C) 2013 Texas Instruments
 *  Copyright (C) 2013 Eduardo Valentin <eduardo.valentin@ti.com>
 */
#include <linux/err.h>
#include <linux/export.h>
#include <linux/hwmon.h>
#include <linux/slab.h>
#include <linux/thermal.h>

#include "thermal_hwmon.h"
#include "thermal_core.h"

/*
 * Needs to be large enough to hold a thermal zone type string followed by an
 * underline character and a 32-bit integer in decimal representation.
 */
#define THERMAL_HWMON_NAME_LENGTH (THERMAL_NAME_LENGTH + 11)

struct thermal_hwmon_attr {
	struct device_attribute attr;
};

/* one temperature input for each thermal zone */
struct thermal_hwmon_temp {
	struct thermal_zone_device *tz;
	struct thermal_hwmon_attr temp_input;	/* hwmon sys attr */
	struct thermal_hwmon_attr temp_crit;	/* hwmon sys attr */
	bool temp_crit_present;
};

/* hwmon sys I/F */
/* thermal zone devices with the same type share one hwmon device */
struct thermal_hwmon_device {
	char name[THERMAL_HWMON_NAME_LENGTH];
	struct device *device;
	struct list_head node;
	struct thermal_hwmon_temp tz_temp;
};

static LIST_HEAD(thermal_hwmon_list);

static DEFINE_MUTEX(thermal_hwmon_list_lock);

static ssize_t
temp_input_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int temperature;
	int ret;
	struct thermal_hwmon_attr *hwmon_attr
			= container_of(attr, struct thermal_hwmon_attr, attr);
	struct thermal_hwmon_temp *temp
			= container_of(hwmon_attr, struct thermal_hwmon_temp,
				       temp_input);
	struct thermal_zone_device *tz = temp->tz;

	ret = thermal_zone_get_temp(tz, &temperature);

	if (ret)
		return ret;

	return sysfs_emit(buf, "%d\n", temperature);
}

static ssize_t
temp_crit_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct thermal_hwmon_attr *hwmon_attr
			= container_of(attr, struct thermal_hwmon_attr, attr);
	struct thermal_hwmon_temp *temp
			= container_of(hwmon_attr, struct thermal_hwmon_temp,
				       temp_crit);
	struct thermal_zone_device *tz = temp->tz;
	int temperature;
	int ret;

	guard(thermal_zone)(tz);

	ret = tz->ops.get_crit_temp(tz, &temperature);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%d\n", temperature);
}

static bool thermal_zone_crit_temp_valid(struct thermal_zone_device *tz)
{
	int temp;
	return tz->ops.get_crit_temp && !tz->ops.get_crit_temp(tz, &temp);
}

int thermal_add_hwmon_sysfs(struct thermal_zone_device *tz)
{
	struct thermal_hwmon_device *hwmon;
	struct thermal_hwmon_temp *temp;
	int result;

	hwmon = kzalloc_obj(*hwmon);
	if (!hwmon)
		return -ENOMEM;

	/*
	 * Append the thermal zone ID preceded by an underline character to the
	 * type to disambiguate the sensors command output.
	 */
	scnprintf(hwmon->name, THERMAL_HWMON_NAME_LENGTH, "%s_%d", tz->type, tz->id);
	strreplace(hwmon->name, '-', '_');
	hwmon->device = hwmon_device_register_for_thermal(&tz->device,
							  hwmon->name, hwmon);
	if (IS_ERR(hwmon->device)) {
		result = PTR_ERR(hwmon->device);
		goto free_mem;
	}

	temp = &hwmon->tz_temp;

	temp->tz = tz;

	temp->temp_input.attr.attr.name = "temp1_input";
	temp->temp_input.attr.attr.mode = 0444;
	temp->temp_input.attr.show = temp_input_show;
	sysfs_attr_init(&temp->temp_input.attr.attr);
	result = device_create_file(hwmon->device, &temp->temp_input.attr);
	if (result)
		goto unregister_name;

	if (thermal_zone_crit_temp_valid(tz)) {
		temp->temp_crit.attr.attr.name = "temp1_crit";
		temp->temp_crit.attr.attr.mode = 0444;
		temp->temp_crit.attr.show = temp_crit_show;
		sysfs_attr_init(&temp->temp_crit.attr.attr);
		result = device_create_file(hwmon->device,
					    &temp->temp_crit.attr);
		if (result)
			goto unregister_input;

		temp->temp_crit_present = true;
	}

	/* The list is needed for hwmon lookup during removal. */
	mutex_lock(&thermal_hwmon_list_lock);
	list_add_tail(&hwmon->node, &thermal_hwmon_list);
	mutex_unlock(&thermal_hwmon_list_lock);

	return 0;

 unregister_input:
	device_remove_file(hwmon->device, &temp->temp_input.attr);
 unregister_name:
	hwmon_device_unregister(hwmon->device);
 free_mem:
	kfree(hwmon);

	return result;
}
EXPORT_SYMBOL_GPL(thermal_add_hwmon_sysfs);

static struct thermal_hwmon_device *
thermal_hwmon_lookup(const struct thermal_zone_device *tz)
{
	struct thermal_hwmon_device *hwmon;

	list_for_each_entry(hwmon, &thermal_hwmon_list, node) {
		if (hwmon->tz_temp.tz == tz)
			return hwmon;
	}
	return NULL;
}

void thermal_remove_hwmon_sysfs(struct thermal_zone_device *tz)
{
	struct thermal_hwmon_device *hwmon;
	struct thermal_hwmon_temp *temp;

	scoped_guard(mutex, &thermal_hwmon_list_lock) {
		hwmon = thermal_hwmon_lookup(tz);
		if (!hwmon)
			return;

		list_del(&hwmon->node);
	}

	temp = &hwmon->tz_temp;

	device_remove_file(hwmon->device, &temp->temp_input.attr);
	if (temp->temp_crit_present)
		device_remove_file(hwmon->device, &temp->temp_crit.attr);

	hwmon_device_unregister(hwmon->device);
	kfree(hwmon);
}
EXPORT_SYMBOL_GPL(thermal_remove_hwmon_sysfs);

static void devm_thermal_hwmon_release(struct device *dev, void *res)
{
	thermal_remove_hwmon_sysfs(*(struct thermal_zone_device **)res);
}

int devm_thermal_add_hwmon_sysfs(struct device *dev, struct thermal_zone_device *tz)
{
	struct thermal_zone_device **ptr;
	int ret;

	ptr = devres_alloc(devm_thermal_hwmon_release, sizeof(*ptr),
			   GFP_KERNEL);
	if (!ptr) {
		dev_warn(dev, "Failed to allocate device resource data\n");
		return -ENOMEM;
	}

	ret = thermal_add_hwmon_sysfs(tz);
	if (ret) {
		dev_warn(dev, "Failed to add hwmon sysfs attributes\n");
		devres_free(ptr);
		return ret;
	}

	*ptr = tz;
	devres_add(dev, ptr);

	return ret;
}
EXPORT_SYMBOL_GPL(devm_thermal_add_hwmon_sysfs);

MODULE_IMPORT_NS("HWMON_THERMAL");
