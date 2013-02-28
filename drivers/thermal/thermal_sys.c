/*
 *  thermal.c - Generic Thermal Management Sysfs support.
 *
 *  Copyright (C) 2008 Intel Corp
 *  Copyright (C) 2008 Zhang Rui <rui.zhang@intel.com>
 *  Copyright (C) 2008 Sujith Thomas <sujith.thomas@intel.com>
 *
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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/kdev_t.h>
#include <linux/idr.h>
#include <linux/thermal.h>
#include <linux/spinlock.h>
#include <linux/reboot.h>
#include <net/netlink.h>
#include <net/genetlink.h>

#include "thermal_core.h"

MODULE_AUTHOR("Zhang Rui");
MODULE_DESCRIPTION("Generic thermal management sysfs support");
MODULE_LICENSE("GPL");

static DEFINE_IDR(thermal_tz_idr);
static DEFINE_IDR(thermal_cdev_idr);
static DEFINE_MUTEX(thermal_idr_lock);

static LIST_HEAD(thermal_tz_list);
static LIST_HEAD(thermal_cdev_list);
static LIST_HEAD(thermal_governor_list);

static DEFINE_MUTEX(thermal_list_lock);
static DEFINE_MUTEX(thermal_governor_lock);

static struct thermal_governor *__find_governor(const char *name)
{
	struct thermal_governor *pos;

	list_for_each_entry(pos, &thermal_governor_list, governor_list)
		if (!strnicmp(name, pos->name, THERMAL_NAME_LENGTH))
			return pos;

	return NULL;
}

int thermal_register_governor(struct thermal_governor *governor)
{
	int err;
	const char *name;
	struct thermal_zone_device *pos;

	if (!governor)
		return -EINVAL;

	mutex_lock(&thermal_governor_lock);

	err = -EBUSY;
	if (__find_governor(governor->name) == NULL) {
		err = 0;
		list_add(&governor->governor_list, &thermal_governor_list);
	}

	mutex_lock(&thermal_list_lock);

	list_for_each_entry(pos, &thermal_tz_list, node) {
		if (pos->governor)
			continue;
		if (pos->tzp)
			name = pos->tzp->governor_name;
		else
			name = DEFAULT_THERMAL_GOVERNOR;
		if (!strnicmp(name, governor->name, THERMAL_NAME_LENGTH))
			pos->governor = governor;
	}

	mutex_unlock(&thermal_list_lock);
	mutex_unlock(&thermal_governor_lock);

	return err;
}
EXPORT_SYMBOL_GPL(thermal_register_governor);

void thermal_unregister_governor(struct thermal_governor *governor)
{
	struct thermal_zone_device *pos;

	if (!governor)
		return;

	mutex_lock(&thermal_governor_lock);

	if (__find_governor(governor->name) == NULL)
		goto exit;

	mutex_lock(&thermal_list_lock);

	list_for_each_entry(pos, &thermal_tz_list, node) {
		if (!strnicmp(pos->governor->name, governor->name,
						THERMAL_NAME_LENGTH))
			pos->governor = NULL;
	}

	mutex_unlock(&thermal_list_lock);
	list_del(&governor->governor_list);
exit:
	mutex_unlock(&thermal_governor_lock);
	return;
}
EXPORT_SYMBOL_GPL(thermal_unregister_governor);

static int get_idr(struct idr *idr, struct mutex *lock, int *id)
{
	int ret;

	if (lock)
		mutex_lock(lock);
	ret = idr_alloc(idr, NULL, 0, 0, GFP_KERNEL);
	if (lock)
		mutex_unlock(lock);
	if (unlikely(ret < 0))
		return ret;
	*id = ret;
	return 0;
}

static void release_idr(struct idr *idr, struct mutex *lock, int id)
{
	if (lock)
		mutex_lock(lock);
	idr_remove(idr, id);
	if (lock)
		mutex_unlock(lock);
}

int get_tz_trend(struct thermal_zone_device *tz, int trip)
{
	enum thermal_trend trend;

	if (!tz->ops->get_trend || tz->ops->get_trend(tz, trip, &trend)) {
		if (tz->temperature > tz->last_temperature)
			trend = THERMAL_TREND_RAISING;
		else if (tz->temperature < tz->last_temperature)
			trend = THERMAL_TREND_DROPPING;
		else
			trend = THERMAL_TREND_STABLE;
	}

	return trend;
}
EXPORT_SYMBOL(get_tz_trend);

struct thermal_instance *get_thermal_instance(struct thermal_zone_device *tz,
			struct thermal_cooling_device *cdev, int trip)
{
	struct thermal_instance *pos = NULL;
	struct thermal_instance *target_instance = NULL;

	mutex_lock(&tz->lock);
	mutex_lock(&cdev->lock);

	list_for_each_entry(pos, &tz->thermal_instances, tz_node) {
		if (pos->tz == tz && pos->trip == trip && pos->cdev == cdev) {
			target_instance = pos;
			break;
		}
	}

	mutex_unlock(&cdev->lock);
	mutex_unlock(&tz->lock);

	return target_instance;
}
EXPORT_SYMBOL(get_thermal_instance);

static void print_bind_err_msg(struct thermal_zone_device *tz,
			struct thermal_cooling_device *cdev, int ret)
{
	dev_err(&tz->device, "binding zone %s with cdev %s failed:%d\n",
				tz->type, cdev->type, ret);
}

static void __bind(struct thermal_zone_device *tz, int mask,
			struct thermal_cooling_device *cdev)
{
	int i, ret;

	for (i = 0; i < tz->trips; i++) {
		if (mask & (1 << i)) {
			ret = thermal_zone_bind_cooling_device(tz, i, cdev,
					THERMAL_NO_LIMIT, THERMAL_NO_LIMIT);
			if (ret)
				print_bind_err_msg(tz, cdev, ret);
		}
	}
}

static void __unbind(struct thermal_zone_device *tz, int mask,
			struct thermal_cooling_device *cdev)
{
	int i;

	for (i = 0; i < tz->trips; i++)
		if (mask & (1 << i))
			thermal_zone_unbind_cooling_device(tz, i, cdev);
}

static void bind_cdev(struct thermal_cooling_device *cdev)
{
	int i, ret;
	const struct thermal_zone_params *tzp;
	struct thermal_zone_device *pos = NULL;

	mutex_lock(&thermal_list_lock);

	list_for_each_entry(pos, &thermal_tz_list, node) {
		if (!pos->tzp && !pos->ops->bind)
			continue;

		if (!pos->tzp && pos->ops->bind) {
			ret = pos->ops->bind(pos, cdev);
			if (ret)
				print_bind_err_msg(pos, cdev, ret);
		}

		tzp = pos->tzp;
		if (!tzp || !tzp->tbp)
			continue;

		for (i = 0; i < tzp->num_tbps; i++) {
			if (tzp->tbp[i].cdev || !tzp->tbp[i].match)
				continue;
			if (tzp->tbp[i].match(pos, cdev))
				continue;
			tzp->tbp[i].cdev = cdev;
			__bind(pos, tzp->tbp[i].trip_mask, cdev);
		}
	}

	mutex_unlock(&thermal_list_lock);
}

static void bind_tz(struct thermal_zone_device *tz)
{
	int i, ret;
	struct thermal_cooling_device *pos = NULL;
	const struct thermal_zone_params *tzp = tz->tzp;

	if (!tzp && !tz->ops->bind)
		return;

	mutex_lock(&thermal_list_lock);

	/* If there is no platform data, try to use ops->bind */
	if (!tzp && tz->ops->bind) {
		list_for_each_entry(pos, &thermal_cdev_list, node) {
			ret = tz->ops->bind(tz, pos);
			if (ret)
				print_bind_err_msg(tz, pos, ret);
		}
		goto exit;
	}

	if (!tzp || !tzp->tbp)
		goto exit;

	list_for_each_entry(pos, &thermal_cdev_list, node) {
		for (i = 0; i < tzp->num_tbps; i++) {
			if (tzp->tbp[i].cdev || !tzp->tbp[i].match)
				continue;
			if (tzp->tbp[i].match(tz, pos))
				continue;
			tzp->tbp[i].cdev = pos;
			__bind(tz, tzp->tbp[i].trip_mask, pos);
		}
	}
exit:
	mutex_unlock(&thermal_list_lock);
}

static void thermal_zone_device_set_polling(struct thermal_zone_device *tz,
					    int delay)
{
	if (delay > 1000)
		mod_delayed_work(system_freezable_wq, &tz->poll_queue,
				 round_jiffies(msecs_to_jiffies(delay)));
	else if (delay)
		mod_delayed_work(system_freezable_wq, &tz->poll_queue,
				 msecs_to_jiffies(delay));
	else
		cancel_delayed_work(&tz->poll_queue);
}

static void monitor_thermal_zone(struct thermal_zone_device *tz)
{
	mutex_lock(&tz->lock);

	if (tz->passive)
		thermal_zone_device_set_polling(tz, tz->passive_delay);
	else if (tz->polling_delay)
		thermal_zone_device_set_polling(tz, tz->polling_delay);
	else
		thermal_zone_device_set_polling(tz, 0);

	mutex_unlock(&tz->lock);
}

static void handle_non_critical_trips(struct thermal_zone_device *tz,
			int trip, enum thermal_trip_type trip_type)
{
	if (tz->governor)
		tz->governor->throttle(tz, trip);
}

static void handle_critical_trips(struct thermal_zone_device *tz,
				int trip, enum thermal_trip_type trip_type)
{
	long trip_temp;

	tz->ops->get_trip_temp(tz, trip, &trip_temp);

	/* If we have not crossed the trip_temp, we do not care. */
	if (tz->temperature < trip_temp)
		return;

	if (tz->ops->notify)
		tz->ops->notify(tz, trip, trip_type);

	if (trip_type == THERMAL_TRIP_CRITICAL) {
		pr_emerg("Critical temperature reached(%d C),shutting down\n",
			 tz->temperature / 1000);
		orderly_poweroff(true);
	}
}

static void handle_thermal_trip(struct thermal_zone_device *tz, int trip)
{
	enum thermal_trip_type type;

	tz->ops->get_trip_type(tz, trip, &type);

	if (type == THERMAL_TRIP_CRITICAL || type == THERMAL_TRIP_HOT)
		handle_critical_trips(tz, trip, type);
	else
		handle_non_critical_trips(tz, trip, type);
	/*
	 * Alright, we handled this trip successfully.
	 * So, start monitoring again.
	 */
	monitor_thermal_zone(tz);
}

static void update_temperature(struct thermal_zone_device *tz)
{
	long temp;
	int ret;

	mutex_lock(&tz->lock);

	ret = tz->ops->get_temp(tz, &temp);
	if (ret) {
		pr_warn("failed to read out thermal zone %d\n", tz->id);
		goto exit;
	}

	tz->last_temperature = tz->temperature;
	tz->temperature = temp;

exit:
	mutex_unlock(&tz->lock);
}

void thermal_zone_device_update(struct thermal_zone_device *tz)
{
	int count;

	update_temperature(tz);

	for (count = 0; count < tz->trips; count++)
		handle_thermal_trip(tz, count);
}
EXPORT_SYMBOL(thermal_zone_device_update);

static void thermal_zone_device_check(struct work_struct *work)
{
	struct thermal_zone_device *tz = container_of(work, struct
						      thermal_zone_device,
						      poll_queue.work);
	thermal_zone_device_update(tz);
}

/* sys I/F for thermal zone */

#define to_thermal_zone(_dev) \
	container_of(_dev, struct thermal_zone_device, device)

static ssize_t
type_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct thermal_zone_device *tz = to_thermal_zone(dev);

	return sprintf(buf, "%s\n", tz->type);
}

static ssize_t
temp_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct thermal_zone_device *tz = to_thermal_zone(dev);
	long temperature;
	int ret;

	if (!tz->ops->get_temp)
		return -EPERM;

	ret = tz->ops->get_temp(tz, &temperature);

	if (ret)
		return ret;

	return sprintf(buf, "%ld\n", temperature);
}

static ssize_t
mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct thermal_zone_device *tz = to_thermal_zone(dev);
	enum thermal_device_mode mode;
	int result;

	if (!tz->ops->get_mode)
		return -EPERM;

	result = tz->ops->get_mode(tz, &mode);
	if (result)
		return result;

	return sprintf(buf, "%s\n", mode == THERMAL_DEVICE_ENABLED ? "enabled"
		       : "disabled");
}

static ssize_t
mode_store(struct device *dev, struct device_attribute *attr,
	   const char *buf, size_t count)
{
	struct thermal_zone_device *tz = to_thermal_zone(dev);
	int result;

	if (!tz->ops->set_mode)
		return -EPERM;

	if (!strncmp(buf, "enabled", sizeof("enabled") - 1))
		result = tz->ops->set_mode(tz, THERMAL_DEVICE_ENABLED);
	else if (!strncmp(buf, "disabled", sizeof("disabled") - 1))
		result = tz->ops->set_mode(tz, THERMAL_DEVICE_DISABLED);
	else
		result = -EINVAL;

	if (result)
		return result;

	return count;
}

static ssize_t
trip_point_type_show(struct device *dev, struct device_attribute *attr,
		     char *buf)
{
	struct thermal_zone_device *tz = to_thermal_zone(dev);
	enum thermal_trip_type type;
	int trip, result;

	if (!tz->ops->get_trip_type)
		return -EPERM;

	if (!sscanf(attr->attr.name, "trip_point_%d_type", &trip))
		return -EINVAL;

	result = tz->ops->get_trip_type(tz, trip, &type);
	if (result)
		return result;

	switch (type) {
	case THERMAL_TRIP_CRITICAL:
		return sprintf(buf, "critical\n");
	case THERMAL_TRIP_HOT:
		return sprintf(buf, "hot\n");
	case THERMAL_TRIP_PASSIVE:
		return sprintf(buf, "passive\n");
	case THERMAL_TRIP_ACTIVE:
		return sprintf(buf, "active\n");
	default:
		return sprintf(buf, "unknown\n");
	}
}

static ssize_t
trip_point_temp_store(struct device *dev, struct device_attribute *attr,
		     const char *buf, size_t count)
{
	struct thermal_zone_device *tz = to_thermal_zone(dev);
	int trip, ret;
	unsigned long temperature;

	if (!tz->ops->set_trip_temp)
		return -EPERM;

	if (!sscanf(attr->attr.name, "trip_point_%d_temp", &trip))
		return -EINVAL;

	if (kstrtoul(buf, 10, &temperature))
		return -EINVAL;

	ret = tz->ops->set_trip_temp(tz, trip, temperature);

	return ret ? ret : count;
}

static ssize_t
trip_point_temp_show(struct device *dev, struct device_attribute *attr,
		     char *buf)
{
	struct thermal_zone_device *tz = to_thermal_zone(dev);
	int trip, ret;
	long temperature;

	if (!tz->ops->get_trip_temp)
		return -EPERM;

	if (!sscanf(attr->attr.name, "trip_point_%d_temp", &trip))
		return -EINVAL;

	ret = tz->ops->get_trip_temp(tz, trip, &temperature);

	if (ret)
		return ret;

	return sprintf(buf, "%ld\n", temperature);
}

static ssize_t
trip_point_hyst_store(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct thermal_zone_device *tz = to_thermal_zone(dev);
	int trip, ret;
	unsigned long temperature;

	if (!tz->ops->set_trip_hyst)
		return -EPERM;

	if (!sscanf(attr->attr.name, "trip_point_%d_hyst", &trip))
		return -EINVAL;

	if (kstrtoul(buf, 10, &temperature))
		return -EINVAL;

	/*
	 * We are not doing any check on the 'temperature' value
	 * here. The driver implementing 'set_trip_hyst' has to
	 * take care of this.
	 */
	ret = tz->ops->set_trip_hyst(tz, trip, temperature);

	return ret ? ret : count;
}

static ssize_t
trip_point_hyst_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct thermal_zone_device *tz = to_thermal_zone(dev);
	int trip, ret;
	unsigned long temperature;

	if (!tz->ops->get_trip_hyst)
		return -EPERM;

	if (!sscanf(attr->attr.name, "trip_point_%d_hyst", &trip))
		return -EINVAL;

	ret = tz->ops->get_trip_hyst(tz, trip, &temperature);

	return ret ? ret : sprintf(buf, "%ld\n", temperature);
}

static ssize_t
passive_store(struct device *dev, struct device_attribute *attr,
		    const char *buf, size_t count)
{
	struct thermal_zone_device *tz = to_thermal_zone(dev);
	struct thermal_cooling_device *cdev = NULL;
	int state;

	if (!sscanf(buf, "%d\n", &state))
		return -EINVAL;

	/* sanity check: values below 1000 millicelcius don't make sense
	 * and can cause the system to go into a thermal heart attack
	 */
	if (state && state < 1000)
		return -EINVAL;

	if (state && !tz->forced_passive) {
		mutex_lock(&thermal_list_lock);
		list_for_each_entry(cdev, &thermal_cdev_list, node) {
			if (!strncmp("Processor", cdev->type,
				     sizeof("Processor")))
				thermal_zone_bind_cooling_device(tz,
						THERMAL_TRIPS_NONE, cdev,
						THERMAL_NO_LIMIT,
						THERMAL_NO_LIMIT);
		}
		mutex_unlock(&thermal_list_lock);
		if (!tz->passive_delay)
			tz->passive_delay = 1000;
	} else if (!state && tz->forced_passive) {
		mutex_lock(&thermal_list_lock);
		list_for_each_entry(cdev, &thermal_cdev_list, node) {
			if (!strncmp("Processor", cdev->type,
				     sizeof("Processor")))
				thermal_zone_unbind_cooling_device(tz,
								   THERMAL_TRIPS_NONE,
								   cdev);
		}
		mutex_unlock(&thermal_list_lock);
		tz->passive_delay = 0;
	}

	tz->forced_passive = state;

	thermal_zone_device_update(tz);

	return count;
}

static ssize_t
passive_show(struct device *dev, struct device_attribute *attr,
		   char *buf)
{
	struct thermal_zone_device *tz = to_thermal_zone(dev);

	return sprintf(buf, "%d\n", tz->forced_passive);
}

static ssize_t
policy_store(struct device *dev, struct device_attribute *attr,
		    const char *buf, size_t count)
{
	int ret = -EINVAL;
	struct thermal_zone_device *tz = to_thermal_zone(dev);
	struct thermal_governor *gov;

	mutex_lock(&thermal_governor_lock);

	gov = __find_governor(buf);
	if (!gov)
		goto exit;

	tz->governor = gov;
	ret = count;

exit:
	mutex_unlock(&thermal_governor_lock);
	return ret;
}

static ssize_t
policy_show(struct device *dev, struct device_attribute *devattr, char *buf)
{
	struct thermal_zone_device *tz = to_thermal_zone(dev);

	return sprintf(buf, "%s\n", tz->governor->name);
}

static DEVICE_ATTR(type, 0444, type_show, NULL);
static DEVICE_ATTR(temp, 0444, temp_show, NULL);
static DEVICE_ATTR(mode, 0644, mode_show, mode_store);
static DEVICE_ATTR(passive, S_IRUGO | S_IWUSR, passive_show, passive_store);
static DEVICE_ATTR(policy, S_IRUGO | S_IWUSR, policy_show, policy_store);

/* sys I/F for cooling device */
#define to_cooling_device(_dev)	\
	container_of(_dev, struct thermal_cooling_device, device)

static ssize_t
thermal_cooling_device_type_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct thermal_cooling_device *cdev = to_cooling_device(dev);

	return sprintf(buf, "%s\n", cdev->type);
}

static ssize_t
thermal_cooling_device_max_state_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct thermal_cooling_device *cdev = to_cooling_device(dev);
	unsigned long state;
	int ret;

	ret = cdev->ops->get_max_state(cdev, &state);
	if (ret)
		return ret;
	return sprintf(buf, "%ld\n", state);
}

static ssize_t
thermal_cooling_device_cur_state_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct thermal_cooling_device *cdev = to_cooling_device(dev);
	unsigned long state;
	int ret;

	ret = cdev->ops->get_cur_state(cdev, &state);
	if (ret)
		return ret;
	return sprintf(buf, "%ld\n", state);
}

static ssize_t
thermal_cooling_device_cur_state_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	struct thermal_cooling_device *cdev = to_cooling_device(dev);
	unsigned long state;
	int result;

	if (!sscanf(buf, "%ld\n", &state))
		return -EINVAL;

	if ((long)state < 0)
		return -EINVAL;

	result = cdev->ops->set_cur_state(cdev, state);
	if (result)
		return result;
	return count;
}

static struct device_attribute dev_attr_cdev_type =
__ATTR(type, 0444, thermal_cooling_device_type_show, NULL);
static DEVICE_ATTR(max_state, 0444,
		   thermal_cooling_device_max_state_show, NULL);
static DEVICE_ATTR(cur_state, 0644,
		   thermal_cooling_device_cur_state_show,
		   thermal_cooling_device_cur_state_store);

static ssize_t
thermal_cooling_device_trip_point_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct thermal_instance *instance;

	instance =
	    container_of(attr, struct thermal_instance, attr);

	if (instance->trip == THERMAL_TRIPS_NONE)
		return sprintf(buf, "-1\n");
	else
		return sprintf(buf, "%d\n", instance->trip);
}

/* Device management */

#if defined(CONFIG_THERMAL_HWMON)

/* hwmon sys I/F */
#include <linux/hwmon.h>

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

static ssize_t
name_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct thermal_hwmon_device *hwmon = dev_get_drvdata(dev);
	return sprintf(buf, "%s\n", hwmon->type);
}
static DEVICE_ATTR(name, 0444, name_show, NULL);

static ssize_t
temp_input_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	long temperature;
	int ret;
	struct thermal_hwmon_attr *hwmon_attr
			= container_of(attr, struct thermal_hwmon_attr, attr);
	struct thermal_hwmon_temp *temp
			= container_of(hwmon_attr, struct thermal_hwmon_temp,
				       temp_input);
	struct thermal_zone_device *tz = temp->tz;

	ret = tz->ops->get_temp(tz, &temperature);

	if (ret)
		return ret;

	return sprintf(buf, "%ld\n", temperature);
}

static ssize_t
temp_crit_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct thermal_hwmon_attr *hwmon_attr
			= container_of(attr, struct thermal_hwmon_attr, attr);
	struct thermal_hwmon_temp *temp
			= container_of(hwmon_attr, struct thermal_hwmon_temp,
				       temp_crit);
	struct thermal_zone_device *tz = temp->tz;
	long temperature;
	int ret;

	ret = tz->ops->get_trip_temp(tz, 0, &temperature);
	if (ret)
		return ret;

	return sprintf(buf, "%ld\n", temperature);
}


static struct thermal_hwmon_device *
thermal_hwmon_lookup_by_type(const struct thermal_zone_device *tz)
{
	struct thermal_hwmon_device *hwmon;

	mutex_lock(&thermal_list_lock);
	list_for_each_entry(hwmon, &thermal_hwmon_list, node)
		if (!strcmp(hwmon->type, tz->type)) {
			mutex_unlock(&thermal_list_lock);
			return hwmon;
		}
	mutex_unlock(&thermal_list_lock);

	return NULL;
}

/* Find the temperature input matching a given thermal zone */
static struct thermal_hwmon_temp *
thermal_hwmon_lookup_temp(const struct thermal_hwmon_device *hwmon,
			  const struct thermal_zone_device *tz)
{
	struct thermal_hwmon_temp *temp;

	mutex_lock(&thermal_list_lock);
	list_for_each_entry(temp, &hwmon->tz_list, hwmon_node)
		if (temp->tz == tz) {
			mutex_unlock(&thermal_list_lock);
			return temp;
		}
	mutex_unlock(&thermal_list_lock);

	return NULL;
}

static int
thermal_add_hwmon_sysfs(struct thermal_zone_device *tz)
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

	hwmon = kzalloc(sizeof(struct thermal_hwmon_device), GFP_KERNEL);
	if (!hwmon)
		return -ENOMEM;

	INIT_LIST_HEAD(&hwmon->tz_list);
	strlcpy(hwmon->type, tz->type, THERMAL_NAME_LENGTH);
	hwmon->device = hwmon_device_register(NULL);
	if (IS_ERR(hwmon->device)) {
		result = PTR_ERR(hwmon->device);
		goto free_mem;
	}
	dev_set_drvdata(hwmon->device, hwmon);
	result = device_create_file(hwmon->device, &dev_attr_name);
	if (result)
		goto free_mem;

 register_sys_interface:
	temp = kzalloc(sizeof(struct thermal_hwmon_temp), GFP_KERNEL);
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

	if (tz->ops->get_crit_temp) {
		unsigned long temperature;
		if (!tz->ops->get_crit_temp(tz, &temperature)) {
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
	}

	mutex_lock(&thermal_list_lock);
	if (new_hwmon_device)
		list_add_tail(&hwmon->node, &thermal_hwmon_list);
	list_add_tail(&temp->hwmon_node, &hwmon->tz_list);
	mutex_unlock(&thermal_list_lock);

	return 0;

 unregister_input:
	device_remove_file(hwmon->device, &temp->temp_input.attr);
 free_temp_mem:
	kfree(temp);
 unregister_name:
	if (new_hwmon_device) {
		device_remove_file(hwmon->device, &dev_attr_name);
		hwmon_device_unregister(hwmon->device);
	}
 free_mem:
	if (new_hwmon_device)
		kfree(hwmon);

	return result;
}

static void
thermal_remove_hwmon_sysfs(struct thermal_zone_device *tz)
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
	if (tz->ops->get_crit_temp)
		device_remove_file(hwmon->device, &temp->temp_crit.attr);

	mutex_lock(&thermal_list_lock);
	list_del(&temp->hwmon_node);
	kfree(temp);
	if (!list_empty(&hwmon->tz_list)) {
		mutex_unlock(&thermal_list_lock);
		return;
	}
	list_del(&hwmon->node);
	mutex_unlock(&thermal_list_lock);

	device_remove_file(hwmon->device, &dev_attr_name);
	hwmon_device_unregister(hwmon->device);
	kfree(hwmon);
}
#else
static int
thermal_add_hwmon_sysfs(struct thermal_zone_device *tz)
{
	return 0;
}

static void
thermal_remove_hwmon_sysfs(struct thermal_zone_device *tz)
{
}
#endif

/**
 * thermal_zone_bind_cooling_device - bind a cooling device to a thermal zone
 * @tz:		thermal zone device
 * @trip:	indicates which trip point the cooling devices is
 *		associated with in this thermal zone.
 * @cdev:	thermal cooling device
 *
 * This function is usually called in the thermal zone device .bind callback.
 */
int thermal_zone_bind_cooling_device(struct thermal_zone_device *tz,
				     int trip,
				     struct thermal_cooling_device *cdev,
				     unsigned long upper, unsigned long lower)
{
	struct thermal_instance *dev;
	struct thermal_instance *pos;
	struct thermal_zone_device *pos1;
	struct thermal_cooling_device *pos2;
	unsigned long max_state;
	int result;

	if (trip >= tz->trips || (trip < 0 && trip != THERMAL_TRIPS_NONE))
		return -EINVAL;

	list_for_each_entry(pos1, &thermal_tz_list, node) {
		if (pos1 == tz)
			break;
	}
	list_for_each_entry(pos2, &thermal_cdev_list, node) {
		if (pos2 == cdev)
			break;
	}

	if (tz != pos1 || cdev != pos2)
		return -EINVAL;

	cdev->ops->get_max_state(cdev, &max_state);

	/* lower default 0, upper default max_state */
	lower = lower == THERMAL_NO_LIMIT ? 0 : lower;
	upper = upper == THERMAL_NO_LIMIT ? max_state : upper;

	if (lower > upper || upper > max_state)
		return -EINVAL;

	dev =
	    kzalloc(sizeof(struct thermal_instance), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;
	dev->tz = tz;
	dev->cdev = cdev;
	dev->trip = trip;
	dev->upper = upper;
	dev->lower = lower;
	dev->target = THERMAL_NO_TARGET;

	result = get_idr(&tz->idr, &tz->lock, &dev->id);
	if (result)
		goto free_mem;

	sprintf(dev->name, "cdev%d", dev->id);
	result =
	    sysfs_create_link(&tz->device.kobj, &cdev->device.kobj, dev->name);
	if (result)
		goto release_idr;

	sprintf(dev->attr_name, "cdev%d_trip_point", dev->id);
	sysfs_attr_init(&dev->attr.attr);
	dev->attr.attr.name = dev->attr_name;
	dev->attr.attr.mode = 0444;
	dev->attr.show = thermal_cooling_device_trip_point_show;
	result = device_create_file(&tz->device, &dev->attr);
	if (result)
		goto remove_symbol_link;

	mutex_lock(&tz->lock);
	mutex_lock(&cdev->lock);
	list_for_each_entry(pos, &tz->thermal_instances, tz_node)
	    if (pos->tz == tz && pos->trip == trip && pos->cdev == cdev) {
		result = -EEXIST;
		break;
	}
	if (!result) {
		list_add_tail(&dev->tz_node, &tz->thermal_instances);
		list_add_tail(&dev->cdev_node, &cdev->thermal_instances);
	}
	mutex_unlock(&cdev->lock);
	mutex_unlock(&tz->lock);

	if (!result)
		return 0;

	device_remove_file(&tz->device, &dev->attr);
remove_symbol_link:
	sysfs_remove_link(&tz->device.kobj, dev->name);
release_idr:
	release_idr(&tz->idr, &tz->lock, dev->id);
free_mem:
	kfree(dev);
	return result;
}
EXPORT_SYMBOL(thermal_zone_bind_cooling_device);

/**
 * thermal_zone_unbind_cooling_device - unbind a cooling device from a thermal zone
 * @tz:		thermal zone device
 * @trip:	indicates which trip point the cooling devices is
 *		associated with in this thermal zone.
 * @cdev:	thermal cooling device
 *
 * This function is usually called in the thermal zone device .unbind callback.
 */
int thermal_zone_unbind_cooling_device(struct thermal_zone_device *tz,
				       int trip,
				       struct thermal_cooling_device *cdev)
{
	struct thermal_instance *pos, *next;

	mutex_lock(&tz->lock);
	mutex_lock(&cdev->lock);
	list_for_each_entry_safe(pos, next, &tz->thermal_instances, tz_node) {
		if (pos->tz == tz && pos->trip == trip && pos->cdev == cdev) {
			list_del(&pos->tz_node);
			list_del(&pos->cdev_node);
			mutex_unlock(&cdev->lock);
			mutex_unlock(&tz->lock);
			goto unbind;
		}
	}
	mutex_unlock(&cdev->lock);
	mutex_unlock(&tz->lock);

	return -ENODEV;

unbind:
	device_remove_file(&tz->device, &pos->attr);
	sysfs_remove_link(&tz->device.kobj, pos->name);
	release_idr(&tz->idr, &tz->lock, pos->id);
	kfree(pos);
	return 0;
}
EXPORT_SYMBOL(thermal_zone_unbind_cooling_device);

static void thermal_release(struct device *dev)
{
	struct thermal_zone_device *tz;
	struct thermal_cooling_device *cdev;

	if (!strncmp(dev_name(dev), "thermal_zone",
		     sizeof("thermal_zone") - 1)) {
		tz = to_thermal_zone(dev);
		kfree(tz);
	} else {
		cdev = to_cooling_device(dev);
		kfree(cdev);
	}
}

static struct class thermal_class = {
	.name = "thermal",
	.dev_release = thermal_release,
};

/**
 * thermal_cooling_device_register - register a new thermal cooling device
 * @type:	the thermal cooling device type.
 * @devdata:	device private data.
 * @ops:		standard thermal cooling devices callbacks.
 */
struct thermal_cooling_device *
thermal_cooling_device_register(char *type, void *devdata,
				const struct thermal_cooling_device_ops *ops)
{
	struct thermal_cooling_device *cdev;
	int result;

	if (type && strlen(type) >= THERMAL_NAME_LENGTH)
		return ERR_PTR(-EINVAL);

	if (!ops || !ops->get_max_state || !ops->get_cur_state ||
	    !ops->set_cur_state)
		return ERR_PTR(-EINVAL);

	cdev = kzalloc(sizeof(struct thermal_cooling_device), GFP_KERNEL);
	if (!cdev)
		return ERR_PTR(-ENOMEM);

	result = get_idr(&thermal_cdev_idr, &thermal_idr_lock, &cdev->id);
	if (result) {
		kfree(cdev);
		return ERR_PTR(result);
	}

	strcpy(cdev->type, type ? : "");
	mutex_init(&cdev->lock);
	INIT_LIST_HEAD(&cdev->thermal_instances);
	cdev->ops = ops;
	cdev->updated = true;
	cdev->device.class = &thermal_class;
	cdev->devdata = devdata;
	dev_set_name(&cdev->device, "cooling_device%d", cdev->id);
	result = device_register(&cdev->device);
	if (result) {
		release_idr(&thermal_cdev_idr, &thermal_idr_lock, cdev->id);
		kfree(cdev);
		return ERR_PTR(result);
	}

	/* sys I/F */
	if (type) {
		result = device_create_file(&cdev->device, &dev_attr_cdev_type);
		if (result)
			goto unregister;
	}

	result = device_create_file(&cdev->device, &dev_attr_max_state);
	if (result)
		goto unregister;

	result = device_create_file(&cdev->device, &dev_attr_cur_state);
	if (result)
		goto unregister;

	/* Add 'this' new cdev to the global cdev list */
	mutex_lock(&thermal_list_lock);
	list_add(&cdev->node, &thermal_cdev_list);
	mutex_unlock(&thermal_list_lock);

	/* Update binding information for 'this' new cdev */
	bind_cdev(cdev);

	return cdev;

unregister:
	release_idr(&thermal_cdev_idr, &thermal_idr_lock, cdev->id);
	device_unregister(&cdev->device);
	return ERR_PTR(result);
}
EXPORT_SYMBOL(thermal_cooling_device_register);

/**
 * thermal_cooling_device_unregister - removes the registered thermal cooling device
 * @cdev:	the thermal cooling device to remove.
 *
 * thermal_cooling_device_unregister() must be called when the device is no
 * longer needed.
 */
void thermal_cooling_device_unregister(struct thermal_cooling_device *cdev)
{
	int i;
	const struct thermal_zone_params *tzp;
	struct thermal_zone_device *tz;
	struct thermal_cooling_device *pos = NULL;

	if (!cdev)
		return;

	mutex_lock(&thermal_list_lock);
	list_for_each_entry(pos, &thermal_cdev_list, node)
	    if (pos == cdev)
		break;
	if (pos != cdev) {
		/* thermal cooling device not found */
		mutex_unlock(&thermal_list_lock);
		return;
	}
	list_del(&cdev->node);

	/* Unbind all thermal zones associated with 'this' cdev */
	list_for_each_entry(tz, &thermal_tz_list, node) {
		if (tz->ops->unbind) {
			tz->ops->unbind(tz, cdev);
			continue;
		}

		if (!tz->tzp || !tz->tzp->tbp)
			continue;

		tzp = tz->tzp;
		for (i = 0; i < tzp->num_tbps; i++) {
			if (tzp->tbp[i].cdev == cdev) {
				__unbind(tz, tzp->tbp[i].trip_mask, cdev);
				tzp->tbp[i].cdev = NULL;
			}
		}
	}

	mutex_unlock(&thermal_list_lock);

	if (cdev->type[0])
		device_remove_file(&cdev->device, &dev_attr_cdev_type);
	device_remove_file(&cdev->device, &dev_attr_max_state);
	device_remove_file(&cdev->device, &dev_attr_cur_state);

	release_idr(&thermal_cdev_idr, &thermal_idr_lock, cdev->id);
	device_unregister(&cdev->device);
	return;
}
EXPORT_SYMBOL(thermal_cooling_device_unregister);

void thermal_cdev_update(struct thermal_cooling_device *cdev)
{
	struct thermal_instance *instance;
	unsigned long target = 0;

	/* cooling device is updated*/
	if (cdev->updated)
		return;

	mutex_lock(&cdev->lock);
	/* Make sure cdev enters the deepest cooling state */
	list_for_each_entry(instance, &cdev->thermal_instances, cdev_node) {
		if (instance->target == THERMAL_NO_TARGET)
			continue;
		if (instance->target > target)
			target = instance->target;
	}
	mutex_unlock(&cdev->lock);
	cdev->ops->set_cur_state(cdev, target);
	cdev->updated = true;
}
EXPORT_SYMBOL(thermal_cdev_update);

/**
 * notify_thermal_framework - Sensor drivers use this API to notify framework
 * @tz:		thermal zone device
 * @trip:	indicates which trip point has been crossed
 *
 * This function handles the trip events from sensor drivers. It starts
 * throttling the cooling devices according to the policy configured.
 * For CRITICAL and HOT trip points, this notifies the respective drivers,
 * and does actual throttling for other trip points i.e ACTIVE and PASSIVE.
 * The throttling policy is based on the configured platform data; if no
 * platform data is provided, this uses the step_wise throttling policy.
 */
void notify_thermal_framework(struct thermal_zone_device *tz, int trip)
{
	handle_thermal_trip(tz, trip);
}
EXPORT_SYMBOL(notify_thermal_framework);

/**
 * create_trip_attrs - create attributes for trip points
 * @tz:		the thermal zone device
 * @mask:	Writeable trip point bitmap.
 */
static int create_trip_attrs(struct thermal_zone_device *tz, int mask)
{
	int indx;
	int size = sizeof(struct thermal_attr) * tz->trips;

	tz->trip_type_attrs = kzalloc(size, GFP_KERNEL);
	if (!tz->trip_type_attrs)
		return -ENOMEM;

	tz->trip_temp_attrs = kzalloc(size, GFP_KERNEL);
	if (!tz->trip_temp_attrs) {
		kfree(tz->trip_type_attrs);
		return -ENOMEM;
	}

	if (tz->ops->get_trip_hyst) {
		tz->trip_hyst_attrs = kzalloc(size, GFP_KERNEL);
		if (!tz->trip_hyst_attrs) {
			kfree(tz->trip_type_attrs);
			kfree(tz->trip_temp_attrs);
			return -ENOMEM;
		}
	}


	for (indx = 0; indx < tz->trips; indx++) {
		/* create trip type attribute */
		snprintf(tz->trip_type_attrs[indx].name, THERMAL_NAME_LENGTH,
			 "trip_point_%d_type", indx);

		sysfs_attr_init(&tz->trip_type_attrs[indx].attr.attr);
		tz->trip_type_attrs[indx].attr.attr.name =
						tz->trip_type_attrs[indx].name;
		tz->trip_type_attrs[indx].attr.attr.mode = S_IRUGO;
		tz->trip_type_attrs[indx].attr.show = trip_point_type_show;

		device_create_file(&tz->device,
				   &tz->trip_type_attrs[indx].attr);

		/* create trip temp attribute */
		snprintf(tz->trip_temp_attrs[indx].name, THERMAL_NAME_LENGTH,
			 "trip_point_%d_temp", indx);

		sysfs_attr_init(&tz->trip_temp_attrs[indx].attr.attr);
		tz->trip_temp_attrs[indx].attr.attr.name =
						tz->trip_temp_attrs[indx].name;
		tz->trip_temp_attrs[indx].attr.attr.mode = S_IRUGO;
		tz->trip_temp_attrs[indx].attr.show = trip_point_temp_show;
		if (mask & (1 << indx)) {
			tz->trip_temp_attrs[indx].attr.attr.mode |= S_IWUSR;
			tz->trip_temp_attrs[indx].attr.store =
							trip_point_temp_store;
		}

		device_create_file(&tz->device,
				   &tz->trip_temp_attrs[indx].attr);

		/* create Optional trip hyst attribute */
		if (!tz->ops->get_trip_hyst)
			continue;
		snprintf(tz->trip_hyst_attrs[indx].name, THERMAL_NAME_LENGTH,
			 "trip_point_%d_hyst", indx);

		sysfs_attr_init(&tz->trip_hyst_attrs[indx].attr.attr);
		tz->trip_hyst_attrs[indx].attr.attr.name =
					tz->trip_hyst_attrs[indx].name;
		tz->trip_hyst_attrs[indx].attr.attr.mode = S_IRUGO;
		tz->trip_hyst_attrs[indx].attr.show = trip_point_hyst_show;
		if (tz->ops->set_trip_hyst) {
			tz->trip_hyst_attrs[indx].attr.attr.mode |= S_IWUSR;
			tz->trip_hyst_attrs[indx].attr.store =
					trip_point_hyst_store;
		}

		device_create_file(&tz->device,
				   &tz->trip_hyst_attrs[indx].attr);
	}
	return 0;
}

static void remove_trip_attrs(struct thermal_zone_device *tz)
{
	int indx;

	for (indx = 0; indx < tz->trips; indx++) {
		device_remove_file(&tz->device,
				   &tz->trip_type_attrs[indx].attr);
		device_remove_file(&tz->device,
				   &tz->trip_temp_attrs[indx].attr);
		if (tz->ops->get_trip_hyst)
			device_remove_file(&tz->device,
				  &tz->trip_hyst_attrs[indx].attr);
	}
	kfree(tz->trip_type_attrs);
	kfree(tz->trip_temp_attrs);
	kfree(tz->trip_hyst_attrs);
}

/**
 * thermal_zone_device_register - register a new thermal zone device
 * @type:	the thermal zone device type
 * @trips:	the number of trip points the thermal zone support
 * @mask:	a bit string indicating the writeablility of trip points
 * @devdata:	private device data
 * @ops:	standard thermal zone device callbacks
 * @tzp:	thermal zone platform parameters
 * @passive_delay: number of milliseconds to wait between polls when
 *		   performing passive cooling
 * @polling_delay: number of milliseconds to wait between polls when checking
 *		   whether trip points have been crossed (0 for interrupt
 *		   driven systems)
 *
 * thermal_zone_device_unregister() must be called when the device is no
 * longer needed. The passive cooling depends on the .get_trend() return value.
 */
struct thermal_zone_device *thermal_zone_device_register(const char *type,
	int trips, int mask, void *devdata,
	const struct thermal_zone_device_ops *ops,
	const struct thermal_zone_params *tzp,
	int passive_delay, int polling_delay)
{
	struct thermal_zone_device *tz;
	enum thermal_trip_type trip_type;
	int result;
	int count;
	int passive = 0;

	if (type && strlen(type) >= THERMAL_NAME_LENGTH)
		return ERR_PTR(-EINVAL);

	if (trips > THERMAL_MAX_TRIPS || trips < 0 || mask >> trips)
		return ERR_PTR(-EINVAL);

	if (!ops || !ops->get_temp)
		return ERR_PTR(-EINVAL);

	tz = kzalloc(sizeof(struct thermal_zone_device), GFP_KERNEL);
	if (!tz)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&tz->thermal_instances);
	idr_init(&tz->idr);
	mutex_init(&tz->lock);
	result = get_idr(&thermal_tz_idr, &thermal_idr_lock, &tz->id);
	if (result) {
		kfree(tz);
		return ERR_PTR(result);
	}

	strcpy(tz->type, type ? : "");
	tz->ops = ops;
	tz->tzp = tzp;
	tz->device.class = &thermal_class;
	tz->devdata = devdata;
	tz->trips = trips;
	tz->passive_delay = passive_delay;
	tz->polling_delay = polling_delay;

	dev_set_name(&tz->device, "thermal_zone%d", tz->id);
	result = device_register(&tz->device);
	if (result) {
		release_idr(&thermal_tz_idr, &thermal_idr_lock, tz->id);
		kfree(tz);
		return ERR_PTR(result);
	}

	/* sys I/F */
	if (type) {
		result = device_create_file(&tz->device, &dev_attr_type);
		if (result)
			goto unregister;
	}

	result = device_create_file(&tz->device, &dev_attr_temp);
	if (result)
		goto unregister;

	if (ops->get_mode) {
		result = device_create_file(&tz->device, &dev_attr_mode);
		if (result)
			goto unregister;
	}

	result = create_trip_attrs(tz, mask);
	if (result)
		goto unregister;

	for (count = 0; count < trips; count++) {
		tz->ops->get_trip_type(tz, count, &trip_type);
		if (trip_type == THERMAL_TRIP_PASSIVE)
			passive = 1;
	}

	if (!passive) {
		result = device_create_file(&tz->device, &dev_attr_passive);
		if (result)
			goto unregister;
	}

	/* Create policy attribute */
	result = device_create_file(&tz->device, &dev_attr_policy);
	if (result)
		goto unregister;

	/* Update 'this' zone's governor information */
	mutex_lock(&thermal_governor_lock);

	if (tz->tzp)
		tz->governor = __find_governor(tz->tzp->governor_name);
	else
		tz->governor = __find_governor(DEFAULT_THERMAL_GOVERNOR);

	mutex_unlock(&thermal_governor_lock);

	result = thermal_add_hwmon_sysfs(tz);
	if (result)
		goto unregister;

	mutex_lock(&thermal_list_lock);
	list_add_tail(&tz->node, &thermal_tz_list);
	mutex_unlock(&thermal_list_lock);

	/* Bind cooling devices for this zone */
	bind_tz(tz);

	INIT_DELAYED_WORK(&(tz->poll_queue), thermal_zone_device_check);

	thermal_zone_device_update(tz);

	if (!result)
		return tz;

unregister:
	release_idr(&thermal_tz_idr, &thermal_idr_lock, tz->id);
	device_unregister(&tz->device);
	return ERR_PTR(result);
}
EXPORT_SYMBOL(thermal_zone_device_register);

/**
 * thermal_device_unregister - removes the registered thermal zone device
 * @tz: the thermal zone device to remove
 */
void thermal_zone_device_unregister(struct thermal_zone_device *tz)
{
	int i;
	const struct thermal_zone_params *tzp;
	struct thermal_cooling_device *cdev;
	struct thermal_zone_device *pos = NULL;

	if (!tz)
		return;

	tzp = tz->tzp;

	mutex_lock(&thermal_list_lock);
	list_for_each_entry(pos, &thermal_tz_list, node)
	    if (pos == tz)
		break;
	if (pos != tz) {
		/* thermal zone device not found */
		mutex_unlock(&thermal_list_lock);
		return;
	}
	list_del(&tz->node);

	/* Unbind all cdevs associated with 'this' thermal zone */
	list_for_each_entry(cdev, &thermal_cdev_list, node) {
		if (tz->ops->unbind) {
			tz->ops->unbind(tz, cdev);
			continue;
		}

		if (!tzp || !tzp->tbp)
			break;

		for (i = 0; i < tzp->num_tbps; i++) {
			if (tzp->tbp[i].cdev == cdev) {
				__unbind(tz, tzp->tbp[i].trip_mask, cdev);
				tzp->tbp[i].cdev = NULL;
			}
		}
	}

	mutex_unlock(&thermal_list_lock);

	thermal_zone_device_set_polling(tz, 0);

	if (tz->type[0])
		device_remove_file(&tz->device, &dev_attr_type);
	device_remove_file(&tz->device, &dev_attr_temp);
	if (tz->ops->get_mode)
		device_remove_file(&tz->device, &dev_attr_mode);
	device_remove_file(&tz->device, &dev_attr_policy);
	remove_trip_attrs(tz);
	tz->governor = NULL;

	thermal_remove_hwmon_sysfs(tz);
	release_idr(&thermal_tz_idr, &thermal_idr_lock, tz->id);
	idr_destroy(&tz->idr);
	mutex_destroy(&tz->lock);
	device_unregister(&tz->device);
	return;
}
EXPORT_SYMBOL(thermal_zone_device_unregister);

#ifdef CONFIG_NET
static struct genl_family thermal_event_genl_family = {
	.id = GENL_ID_GENERATE,
	.name = THERMAL_GENL_FAMILY_NAME,
	.version = THERMAL_GENL_VERSION,
	.maxattr = THERMAL_GENL_ATTR_MAX,
};

static struct genl_multicast_group thermal_event_mcgrp = {
	.name = THERMAL_GENL_MCAST_GROUP_NAME,
};

int thermal_generate_netlink_event(u32 orig, enum events event)
{
	struct sk_buff *skb;
	struct nlattr *attr;
	struct thermal_genl_event *thermal_event;
	void *msg_header;
	int size;
	int result;
	static unsigned int thermal_event_seqnum;

	/* allocate memory */
	size = nla_total_size(sizeof(struct thermal_genl_event)) +
	       nla_total_size(0);

	skb = genlmsg_new(size, GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;

	/* add the genetlink message header */
	msg_header = genlmsg_put(skb, 0, thermal_event_seqnum++,
				 &thermal_event_genl_family, 0,
				 THERMAL_GENL_CMD_EVENT);
	if (!msg_header) {
		nlmsg_free(skb);
		return -ENOMEM;
	}

	/* fill the data */
	attr = nla_reserve(skb, THERMAL_GENL_ATTR_EVENT,
			   sizeof(struct thermal_genl_event));

	if (!attr) {
		nlmsg_free(skb);
		return -EINVAL;
	}

	thermal_event = nla_data(attr);
	if (!thermal_event) {
		nlmsg_free(skb);
		return -EINVAL;
	}

	memset(thermal_event, 0, sizeof(struct thermal_genl_event));

	thermal_event->orig = orig;
	thermal_event->event = event;

	/* send multicast genetlink message */
	result = genlmsg_end(skb, msg_header);
	if (result < 0) {
		nlmsg_free(skb);
		return result;
	}

	result = genlmsg_multicast(skb, 0, thermal_event_mcgrp.id, GFP_ATOMIC);
	if (result)
		pr_info("failed to send netlink event:%d\n", result);

	return result;
}
EXPORT_SYMBOL(thermal_generate_netlink_event);

static int genetlink_init(void)
{
	int result;

	result = genl_register_family(&thermal_event_genl_family);
	if (result)
		return result;

	result = genl_register_mc_group(&thermal_event_genl_family,
					&thermal_event_mcgrp);
	if (result)
		genl_unregister_family(&thermal_event_genl_family);
	return result;
}

static void genetlink_exit(void)
{
	genl_unregister_family(&thermal_event_genl_family);
}
#else /* !CONFIG_NET */
static inline int genetlink_init(void) { return 0; }
static inline void genetlink_exit(void) {}
#endif /* !CONFIG_NET */

static int __init thermal_init(void)
{
	int result = 0;

	result = class_register(&thermal_class);
	if (result) {
		idr_destroy(&thermal_tz_idr);
		idr_destroy(&thermal_cdev_idr);
		mutex_destroy(&thermal_idr_lock);
		mutex_destroy(&thermal_list_lock);
	}
	result = genetlink_init();
	return result;
}

static void __exit thermal_exit(void)
{
	class_unregister(&thermal_class);
	idr_destroy(&thermal_tz_idr);
	idr_destroy(&thermal_cdev_idr);
	mutex_destroy(&thermal_idr_lock);
	mutex_destroy(&thermal_list_lock);
	genetlink_exit();
}

fs_initcall(thermal_init);
module_exit(thermal_exit);
