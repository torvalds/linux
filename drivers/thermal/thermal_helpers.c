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

#include "thermal_core.h"
#include "thermal_trace.h"

int get_tz_trend(struct thermal_zone_device *tz, const struct thermal_trip *trip)
{
	enum thermal_trend trend;

	if (tz->emul_temperature || !tz->ops.get_trend ||
	    tz->ops.get_trend(tz, trip, &trend)) {
		if (tz->temperature > tz->last_temperature)
			trend = THERMAL_TREND_RAISING;
		else if (tz->temperature < tz->last_temperature)
			trend = THERMAL_TREND_DROPPING;
		else
			trend = THERMAL_TREND_STABLE;
	}

	return trend;
}

static bool thermal_instance_present(struct thermal_zone_device *tz,
				     struct thermal_cooling_device *cdev,
				     const struct thermal_trip *trip)
{
	const struct thermal_trip_desc *td = trip_to_trip_desc(trip);
	struct thermal_instance *ti;

	list_for_each_entry(ti, &td->thermal_instances, trip_node) {
		if (ti->cdev == cdev)
			return true;
	}

	return false;
}

bool thermal_trip_is_bound_to_cdev(struct thermal_zone_device *tz,
				   const struct thermal_trip *trip,
				   struct thermal_cooling_device *cdev)
{
	guard(thermal_zone)(tz);
	guard(cooling_dev)(cdev);

	return thermal_instance_present(tz, cdev, trip);
}
EXPORT_SYMBOL_GPL(thermal_trip_is_bound_to_cdev);

/**
 * __thermal_zone_get_temp() - returns the temperature of a thermal zone
 * @tz: a valid pointer to a struct thermal_zone_device
 * @temp: a valid pointer to where to store the resulting temperature.
 *
 * When a valid thermal zone reference is passed, it will fetch its
 * temperature and fill @temp.
 *
 * Both tz and tz->ops must be valid pointers when calling this function,
 * and the tz->ops.get_temp callback must be provided.
 * The function must be called under tz->lock.
 *
 * Return: On success returns 0, an error code otherwise
 */
int __thermal_zone_get_temp(struct thermal_zone_device *tz, int *temp)
{
	const struct thermal_trip_desc *td;
	int crit_temp = INT_MAX;
	int ret = -EINVAL;

	lockdep_assert_held(&tz->lock);

	ret = tz->ops.get_temp(tz, temp);

	if (IS_ENABLED(CONFIG_THERMAL_EMULATION) && tz->emul_temperature) {
		for_each_trip_desc(tz, td) {
			const struct thermal_trip *trip = &td->trip;

			if (trip->type == THERMAL_TRIP_CRITICAL) {
				crit_temp = trip->temperature;
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

	if (ret)
		dev_dbg(&tz->device, "Failed to get temperature: %d\n", ret);

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

	guard(thermal_zone)(tz);

	if (!tz->ops.get_temp)
		return -EINVAL;

	ret = __thermal_zone_get_temp(tz, temp);
	if (!ret && *temp <= THERMAL_TEMP_INVALID)
		return -ENODATA;

	return ret;
}
EXPORT_SYMBOL_GPL(thermal_zone_get_temp);

static int thermal_cdev_set_cur_state(struct thermal_cooling_device *cdev, int state)
{
	int ret;

	/*
	 * No check is needed for the ops->set_cur_state as the
	 * registering function checked the ops are correctly set
	 */
	ret = cdev->ops->set_cur_state(cdev, state);
	if (ret)
		return ret;

	thermal_notify_cdev_state_update(cdev, state);
	thermal_cooling_device_stats_update(cdev, state);
	thermal_debug_cdev_state_update(cdev, state);

	return 0;
}

void __thermal_cdev_update(struct thermal_cooling_device *cdev)
{
	struct thermal_instance *instance;
	unsigned long target = 0;

	/* Make sure cdev enters the deepest cooling state */
	list_for_each_entry(instance, &cdev->thermal_instances, cdev_node) {
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
	guard(cooling_dev)(cdev);

	if (!cdev->updated) {
		__thermal_cdev_update(cdev);
		cdev->updated = true;
	}
}

/**
 * thermal_cdev_update_nocheck() - Unconditionally update cooling device state
 * @cdev: Target cooling device.
 */
void thermal_cdev_update_nocheck(struct thermal_cooling_device *cdev)
{
	guard(cooling_dev)(cdev);

	__thermal_cdev_update(cdev);
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
