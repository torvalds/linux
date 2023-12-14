// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright (C) 2008 Intel Corp
 *  Copyright (C) 2008 Zhang Rui <rui.zhang@intel.com>
 *  Copyright (C) 2008 Sujith Thomas <sujith.thomas@intel.com>
 *  Copyright 2022 Linaro Limited
 *
 * Thermal trips handling
 */
#include "thermal_core.h"

int for_each_thermal_trip(struct thermal_zone_device *tz,
			  int (*cb)(struct thermal_trip *, void *),
			  void *data)
{
	int i, ret;

	lockdep_assert_held(&tz->lock);

	if (!tz->trips)
		return -ENODATA;

	for (i = 0; i < tz->num_trips; i++) {
		ret = cb(&tz->trips[i], data);
		if (ret)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(for_each_thermal_trip);

int thermal_zone_get_num_trips(struct thermal_zone_device *tz)
{
	return tz->num_trips;
}
EXPORT_SYMBOL_GPL(thermal_zone_get_num_trips);

/**
 * __thermal_zone_set_trips - Computes the next trip points for the driver
 * @tz: a pointer to a thermal zone device structure
 *
 * The function computes the next temperature boundaries by browsing
 * the trip points. The result is the closer low and high trip points
 * to the current temperature. These values are passed to the backend
 * driver to let it set its own notification mechanism (usually an
 * interrupt).
 *
 * This function must be called with tz->lock held. Both tz and tz->ops
 * must be valid pointers.
 *
 * It does not return a value
 */
void __thermal_zone_set_trips(struct thermal_zone_device *tz)
{
	struct thermal_trip trip;
	int low = -INT_MAX, high = INT_MAX;
	bool same_trip = false;
	int i, ret;

	lockdep_assert_held(&tz->lock);

	if (!tz->ops->set_trips)
		return;

	for (i = 0; i < tz->num_trips; i++) {
		bool low_set = false;
		int trip_low;

		ret = __thermal_zone_get_trip(tz, i , &trip);
		if (ret)
			return;

		trip_low = trip.temperature - trip.hysteresis;

		if (trip_low < tz->temperature && trip_low > low) {
			low = trip_low;
			low_set = true;
			same_trip = false;
		}

		if (trip.temperature > tz->temperature &&
		    trip.temperature < high) {
			high = trip.temperature;
			same_trip = low_set;
		}
	}

	/* No need to change trip points */
	if (tz->prev_low_trip == low && tz->prev_high_trip == high)
		return;

	/*
	 * If "high" and "low" are the same, skip the change unless this is the
	 * first time.
	 */
	if (same_trip && (tz->prev_low_trip != -INT_MAX ||
	    tz->prev_high_trip != INT_MAX))
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

int __thermal_zone_get_trip(struct thermal_zone_device *tz, int trip_id,
			    struct thermal_trip *trip)
{
	if (!tz || !tz->trips || trip_id < 0 || trip_id >= tz->num_trips || !trip)
		return -EINVAL;

	*trip = tz->trips[trip_id];
	return 0;
}
EXPORT_SYMBOL_GPL(__thermal_zone_get_trip);

int thermal_zone_get_trip(struct thermal_zone_device *tz, int trip_id,
			  struct thermal_trip *trip)
{
	int ret;

	mutex_lock(&tz->lock);
	ret = __thermal_zone_get_trip(tz, trip_id, trip);
	mutex_unlock(&tz->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(thermal_zone_get_trip);

int thermal_zone_set_trip(struct thermal_zone_device *tz, int trip_id,
			  const struct thermal_trip *trip)
{
	struct thermal_trip t;
	int ret;

	if (!tz->ops->set_trip_temp && !tz->ops->set_trip_hyst && !tz->trips)
		return -EINVAL;

	ret = __thermal_zone_get_trip(tz, trip_id, &t);
	if (ret)
		return ret;

	if (t.type != trip->type)
		return -EINVAL;

	if (t.temperature != trip->temperature && tz->ops->set_trip_temp) {
		ret = tz->ops->set_trip_temp(tz, trip_id, trip->temperature);
		if (ret)
			return ret;
	}

	if (t.hysteresis != trip->hysteresis && tz->ops->set_trip_hyst) {
		ret = tz->ops->set_trip_hyst(tz, trip_id, trip->hysteresis);
		if (ret)
			return ret;
	}

	if (tz->trips && (t.temperature != trip->temperature || t.hysteresis != trip->hysteresis))
		tz->trips[trip_id] = *trip;

	thermal_notify_tz_trip_change(tz->id, trip_id, trip->type,
				      trip->temperature, trip->hysteresis);

	__thermal_zone_device_update(tz, THERMAL_TRIP_CHANGED);

	return 0;
}
