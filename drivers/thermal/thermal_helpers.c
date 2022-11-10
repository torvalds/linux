// SPDX-License-Identifier: GPL-2.0
/*
 *  thermal_helpers.c - helper functions to handle thermal devices
 *
 *  Copyright (C) 2016 Eduardo Valentin <edubezval@gmail.com>
 *
 *  Highly based on original thermal_core.c
 *  Copyright (C) 2008 Intel Corp
 *  Copyright (C) 2008 Zhang Rui <rui.zhang@intel.com>
 *  Copyright (C) 2008 Sujith Thomas <sujith.thomas@intel.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/device.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/sysfs.h>

#include <trace/events/thermal.h>

#include "thermal_core.h"

int get_tz_trend(struct thermal_zone_device *tz, int trip)
{
	enum thermal_trend trend;

	if (tz->emul_temperature || !tz->ops->get_trend ||
	    tz->ops->get_trend(tz, trip, &trend)) {
		if (tz->temperature > tz->last_temperature)
			trend = THERMAL_TREND_RAISING;
		else if (tz->temperature < tz->last_temperature)
			trend = THERMAL_TREND_DROPPING;
		else
			trend = THERMAL_TREND_STABLE;
	}

	return trend;
}

struct thermal_instance *
get_thermal_instance(struct thermal_zone_device *tz,
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

/**
 * __thermal_zone_get_temp() - returns the temperature of a thermal zone
 * @tz: a valid pointer to a struct thermal_zone_device
 * @temp: a valid pointer to where to store the resulting temperature.
 *
 * When a valid thermal zone reference is passed, it will fetch its
 * temperature and fill @temp.
 *
 * Both tz and tz->ops must be valid pointers when calling this function,
 * and the tz->ops->get_temp callback must be provided.
 * The function must be called under tz->lock.
 *
 * Return: On success returns 0, an error code otherwise
 */
int __thermal_zone_get_temp(struct thermal_zone_device *tz, int *temp)
{
	int ret = -EINVAL;
	int count;
	int crit_temp = INT_MAX;
	enum thermal_trip_type type;

	lockdep_assert_held(&tz->lock);

	ret = tz->ops->get_temp(tz, temp);

	if (IS_ENABLED(CONFIG_THERMAL_EMULATION) && tz->emul_temperature) {
		for (count = 0; count < tz->num_trips; count++) {
			ret = tz->ops->get_trip_type(tz, count, &type);
			if (!ret && type == THERMAL_TRIP_CRITICAL) {
				ret = tz->ops->get_trip_temp(tz, count,
						&crit_temp);
				break;
			}
		}

		/*
		 * Only allow emulating a temperature when the real temperature
		 * is below the critical temperature so that the emulation code
		 * cannot hide critical conditions.
		 */
		if (!ret && *temp < crit_temp)
			*temp = tz->emul_temperature;
	}

	return ret;
}

/**
 * thermal_zone_get_temp() - returns the temperature of a thermal zone
 * @tz: a valid pointer to a struct thermal_zone_device
 * @temp: a valid pointer to where to store the resulting temperature.
 *
 * When a valid thermal zone reference is passed, it will fetch its
 * temperature and fill @temp.
 *
 * Return: On success returns 0, an error code otherwise
 */
int thermal_zone_get_temp(struct thermal_zone_device *tz, int *temp)
{
	int ret;

	if (IS_ERR_OR_NULL(tz))
		return -EINVAL;

	mutex_lock(&tz->lock);

	if (!tz->ops->get_temp) {
		ret = -EINVAL;
		goto unlock;
	}

	if (device_is_registered(&tz->device))
		ret = __thermal_zone_get_temp(tz, temp);
	else
		ret = -ENODEV;

unlock:
	mutex_unlock(&tz->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(thermal_zone_get_temp);

void __thermal_zone_set_trips(struct thermal_zone_device *tz)
{
	int low = -INT_MAX;
	int high = INT_MAX;
	int trip_temp, hysteresis;
	int i, ret;

	lockdep_assert_held(&tz->lock);

	if (!tz->ops->set_trips || !tz->ops->get_trip_hyst)
		return;

	for (i = 0; i < tz->num_trips; i++) {
		int trip_low;

		tz->ops->get_trip_temp(tz, i, &trip_temp);
		tz->ops->get_trip_hyst(tz, i, &hysteresis);

		trip_low = trip_temp - hysteresis;

		if (trip_low < tz->temperature && trip_low > low)
			low = trip_low;

		if (trip_temp > tz->temperature && trip_temp < high)
			high = trip_temp;
	}

	/* No need to change trip points */
	if (tz->prev_low_trip == low && tz->prev_high_trip == high)
		return;

	tz->prev_low_trip = low;
	tz->prev_high_trip = high;

	dev_dbg(&tz->device,
		"new temperature boundaries: %d < x < %d\n", low, high);

	/*
	 * Set a temperature window. When this window is left the driver
	 * must inform the thermal core via thermal_zone_device_update.
	 */
	ret = tz->ops->set_trips(tz, low, high);
	if (ret)
		dev_err(&tz->device, "Failed to set trips: %d\n", ret);
}

/**
 * thermal_zone_set_trips - Computes the next trip points for the driver
 * @tz: a pointer to a thermal zone device structure
 *
 * The function computes the next temperature boundaries by browsing
 * the trip points. The result is the closer low and high trip points
 * to the current temperature. These values are passed to the backend
 * driver to let it set its own notification mechanism (usually an
 * interrupt).
 *
 * It does not return a value
 */
void thermal_zone_set_trips(struct thermal_zone_device *tz)
{
	mutex_lock(&tz->lock);
	__thermal_zone_set_trips(tz);
	mutex_unlock(&tz->lock);
}

static void thermal_cdev_set_cur_state(struct thermal_cooling_device *cdev,
				       int target)
{
	if (cdev->ops->set_cur_state(cdev, target))
		return;

	thermal_notify_cdev_state_update(cdev->id, target);
	thermal_cooling_device_stats_update(cdev, target);
}

void __thermal_cdev_update(struct thermal_cooling_device *cdev)
{
	struct thermal_instance *instance;
	unsigned long target = 0;

	/* Make sure cdev enters the deepest cooling state */
	list_for_each_entry(instance, &cdev->thermal_instances, cdev_node) {
		dev_dbg(&cdev->device, "zone%d->target=%lu\n",
			instance->tz->id, instance->target);
		if (instance->target == THERMAL_NO_TARGET)
			continue;
		if (instance->target > target)
			target = instance->target;
	}

	thermal_cdev_set_cur_state(cdev, target);

	trace_cdev_update(cdev, target);
	dev_dbg(&cdev->device, "set to state %lu\n", target);
}

/**
 * thermal_cdev_update - update cooling device state if needed
 * @cdev:	pointer to struct thermal_cooling_device
 *
 * Update the cooling device state if there is a need.
 */
void thermal_cdev_update(struct thermal_cooling_device *cdev)
{
	mutex_lock(&cdev->lock);
	if (!cdev->updated) {
		__thermal_cdev_update(cdev);
		cdev->updated = true;
	}
	mutex_unlock(&cdev->lock);
}

/**
 * thermal_zone_get_slope - return the slope attribute of the thermal zone
 * @tz: thermal zone device with the slope attribute
 *
 * Return: If the thermal zone device has a slope attribute, return it, else
 * return 1.
 */
int thermal_zone_get_slope(struct thermal_zone_device *tz)
{
	if (tz && tz->tzp)
		return tz->tzp->slope;
	return 1;
}
EXPORT_SYMBOL_GPL(thermal_zone_get_slope);

/**
 * thermal_zone_get_offset - return the offset attribute of the thermal zone
 * @tz: thermal zone device with the offset attribute
 *
 * Return: If the thermal zone device has a offset attribute, return it, else
 * return 0.
 */
int thermal_zone_get_offset(struct thermal_zone_device *tz)
{
	if (tz && tz->tzp)
		return tz->tzp->offset;
	return 0;
}
EXPORT_SYMBOL_GPL(thermal_zone_get_offset);
