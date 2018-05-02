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
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
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
#include <linux/hwmon.h>
#include <linux/thermal.h>
#include <linux/slab.h>
#include <linux/err.h>
#include "thermal_hwmon.h"

/* hwmon sys I/F */
/* thermal zone devices with the same type share one hwmon device */
struct thermal_hwmon_device {
	char type[THERMAL_NAME_LENGTH];
	struct device *device;
	int count;
	struct list_head tz_list;
	struct list_head node;
};

struct thermal_hwmon_attr {
	struct device_attribute attr;
	char name[16];
};

/* one temperature input for each thermal zone */
struct thermal_hwmon_temp {
	struct list_head hwmon_node;
	struct thermal_zone_device *tz;
	struct thermal_hwmon_attr temp_input;	/* hwmon sys attr */
	struct thermal_hwmon_attr temp_crit;	/* hwmon sys attr */
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

	return sprintf(buf, "%d\n", temperature);
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

	ret = tz->ops->get_crit_temp(tz, &temperature);
	if (ret)
		return ret;

	return sprintf(buf, "%d\n", temperature);
}


static struct thermal_hwmon_device *
thermal_hwmon_lookup_by_type(const struct thermal_zone_device *tz)
{
	struct thermal_hwmon_device *hwmon;

	mutex_lock(&thermal_hwmon_list_lock);
	list_for_each_entry(hwmon, &thermal_hwmon_list, node)
		if (!strcmp(hwmon->type, tz->type)) {
			mutex_unlock(&thermal_hwmon_list_lock);
			return hwmon;
		}
	mutex_unlock(&thermal_hwmon_list_lock);

	return NULL;
}

/* Find the temperature input matching a given thermal zone */
static struct thermal_hwmon_temp *
thermal_hwmon_lookup_temp(const struct thermal_hwmon_device *hwmon,
			  const struct thermal_zone_device *tz)
{
	struct thermal_hwmon_temp *temp;

	mutex_lock(&thermal_hwmon_list_lock);
	list_for_each_entry(temp, &hwmon->tz_list, hwmon_node)
		if (temp->tz == tz) {
			mutex_unlock(&thermal_hwmon_list_lock);
			return temp;
		}
	mutex_unlock(&thermal_hwmon_list_lock);

	return NULL;
}

static bool thermal_zone_crit_temp_valid(struct thermal_zone_device *tz)
{
	int temp;
	return tz->ops->get_crit_temp && !tz->ops->get_crit_temp(tz, &temp);
}

int thermal_add_hwmon_sysfs(struct thermal_zone_device *tz)
{
	struct thermal_hwmon_device *hwmon;
	struct thermal_hwmon_temp *temp;
	int new_hwmon_device = 1;
	int result;

	hwmon = thermal_hwmon_lookup_by_type(tz);
	if (hwmon) {
		new_hwmon_device = 0;
		goto register_sys_interface;
	}

	hwmon = kzalloc(sizeof(*hwmon), GFP_KERNEL);
	if (!hwmon)
		return -ENOMEM;

	INIT_LIST_HEAD(&hwmon->tz_list);
	strlcpy(hwmon->type, tz->type, THERMAL_NAME_LENGTH);
	hwmon->device = hwmon_device_register_with_info(NULL, hwmon->type,
							hwmon, NULL, NULL);
	if (IS_ERR(hwmon->device)) {
		result = PTR_ERR(hwmon->device);
		goto free_mem;
	}

 register_sys_interface:
	temp = kzalloc(sizeof(*temp), GFP_KERNEL);
	if (!temp) {
		result = -ENOMEM;
		goto unregister_name;
	}

	temp->tz = tz;
	hwmon->count++;

	snprintf(temp->temp_input.name, sizeof(temp->temp_input.name),
		 "temp%d_input", hwmon->count);
	temp->temp_input.attr.attr.name = temp->temp_input.name;
	temp->temp_input.attr.attr.mode = 0444;
	temp->temp_input.attr.show = temp_input_show;
	sysfs_attr_init(&temp->temp_input.attr.attr);
	result = device_create_file(hwmon->device, &temp->temp_input.attr);
	if (result)
		goto free_temp_mem;

	if (thermal_zone_crit_temp_valid(tz)) {
		snprintf(temp->temp_crit.name,
				sizeof(temp->temp_crit.name),
				"temp%d_crit", hwmon->count);
		temp->temp_crit.attr.attr.name = temp->temp_crit.name;
		temp->temp_crit.attr.attr.mode = 0444;
		temp->temp_crit.attr.show = temp_crit_show;
		sysfs_attr_init(&temp->temp_crit.attr.attr);
		result = device_create_file(hwmon->device,
					    &temp->temp_crit.attr);
		if (result)
			goto unregister_input;
	}

	mutex_lock(&thermal_hwmon_list_lock);
	if (new_hwmon_device)
		list_add_tail(&hwmon->node, &thermal_hwmon_list);
	list_add_tail(&temp->hwmon_node, &hwmon->tz_list);
	mutex_unlock(&thermal_hwmon_list_lock);

	return 0;

 unregister_input:
	device_remove_file(hwmon->device, &temp->temp_input.attr);
 free_temp_mem:
	kfree(temp);
 unregister_name:
	if (new_hwmon_device)
		hwmon_device_unregister(hwmon->device);
 free_mem:
	if (new_hwmon_device)
		kfree(hwmon);

	return result;
}
EXPORT_SYMBOL_GPL(thermal_add_hwmon_sysfs);

void thermal_remove_hwmon_sysfs(struct thermal_zone_device *tz)
{
	struct thermal_hwmon_device *hwmon;
	struct thermal_hwmon_temp *temp;

	hwmon = thermal_hwmon_lookup_by_type(tz);
	if (unlikely(!hwmon)) {
		/* Should never happen... */
		dev_dbg(&tz->device, "hwmon device lookup failed!\n");
		return;
	}

	temp = thermal_hwmon_lookup_temp(hwmon, tz);
	if (unlikely(!temp)) {
		/* Should never happen... */
		dev_dbg(&tz->device, "temperature input lookup failed!\n");
		return;
	}

	device_remove_file(hwmon->device, &temp->temp_input.attr);
	if (thermal_zone_crit_temp_valid(tz))
		device_remove_file(hwmon->device, &temp->temp_crit.attr);

	mutex_lock(&thermal_hwmon_list_lock);
	list_del(&temp->hwmon_node);
	kfree(temp);
	if (!list_empty(&hwmon->tz_list)) {
		mutex_unlock(&thermal_hwmon_list_lock);
		return;
	}
	list_del(&hwmon->node);
	mutex_unlock(&thermal_hwmon_list_lock);

	hwmon_device_unregister(hwmon->device);
	kfree(hwmon);
}
EXPORT_SYMBOL_GPL(thermal_remove_hwmon_sysfs);
