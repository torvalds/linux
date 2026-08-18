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

/* hwmon sys I/F */
/* thermal zone devices with the same type share one hwmon device */
struct thermal_hwmon_device {
	char name[THERMAL_HWMON_NAME_LENGTH];
	struct device *device;
	struct list_head node;
	struct thermal_zone_device *tz;
};

static LIST_HEAD(thermal_hwmon_list);

static DEFINE_MUTEX(thermal_hwmon_list_lock);

static ssize_t
temp1_input_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct thermal_hwmon_device *hwmon = dev_get_drvdata(dev);
	struct thermal_zone_device *tz = hwmon->tz;
	int temperature;
	int ret;

	ret = thermal_zone_get_temp(tz, &temperature);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%d\n", temperature);
}

static ssize_t
temp1_crit_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct thermal_hwmon_device *hwmon = dev_get_drvdata(dev);
	struct thermal_zone_device *tz = hwmon->tz;
	int temperature;
	int ret;

	guard(thermal_zone)(tz);

	ret = tz->ops.get_crit_temp(tz, &temperature);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%d\n", temperature);
}

static DEVICE_ATTR_RO(temp1_input);
static DEVICE_ATTR_RO(temp1_crit);

static struct attribute *thermal_hwmon_attrs[] = {
	&dev_attr_temp1_input.attr,
	&dev_attr_temp1_crit.attr,
	NULL,
};

static umode_t thermal_hwmon_attr_is_visible(struct kobject *kobj,
					     struct attribute *a, int n)
{
	if (a == &dev_attr_temp1_input.attr)
		return a->mode;

	if (a == &dev_attr_temp1_crit.attr) {
		struct thermal_hwmon_device *hwmon = dev_get_drvdata(kobj_to_dev(kobj));
		struct thermal_zone_device *tz = hwmon->tz;
		int dummy;

		if (tz->ops.get_crit_temp && !tz->ops.get_crit_temp(tz, &dummy))
			return a->mode;
	}

	return 0;
}

static const struct attribute_group thermal_hwmon_group = {
	.attrs	= thermal_hwmon_attrs,
	.is_visible = thermal_hwmon_attr_is_visible,
};

__ATTRIBUTE_GROUPS(thermal_hwmon);

int thermal_add_hwmon_sysfs(struct thermal_zone_device *tz)
{
	struct thermal_hwmon_device *hwmon;

	hwmon = kzalloc_obj(*hwmon);
	if (!hwmon)
		return -ENOMEM;

	hwmon->tz = tz;
	/*
	 * Append the thermal zone ID preceded by an underline character to the
	 * type to disambiguate the sensors command output.
	 */
	scnprintf(hwmon->name, THERMAL_HWMON_NAME_LENGTH, "%s_%d", tz->type, tz->id);
	strreplace(hwmon->name, '-', '_');
	hwmon->device = hwmon_device_register_for_thermal(&tz->device,
							  hwmon->name, hwmon,
							  thermal_hwmon_groups);
	if (IS_ERR(hwmon->device)) {
		int result = PTR_ERR(hwmon->device);

		kfree(hwmon);
		return result;
	}

	/* The list is needed for hwmon lookup during removal. */
	mutex_lock(&thermal_hwmon_list_lock);
	list_add_tail(&hwmon->node, &thermal_hwmon_list);
	mutex_unlock(&thermal_hwmon_list_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(thermal_add_hwmon_sysfs);

static struct thermal_hwmon_device *
thermal_hwmon_lookup(const struct thermal_zone_device *tz)
{
	struct thermal_hwmon_device *hwmon;

	list_for_each_entry(hwmon, &thermal_hwmon_list, node) {
		if (hwmon->tz == tz)
			return hwmon;
	}
	return NULL;
}

void thermal_remove_hwmon_sysfs(struct thermal_zone_device *tz)
{
	struct thermal_hwmon_device *hwmon;

	scoped_guard(mutex, &thermal_hwmon_list_lock) {
		hwmon = thermal_hwmon_lookup(tz);
		if (!hwmon)
			return;

		list_del(&hwmon->node);
	}

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
