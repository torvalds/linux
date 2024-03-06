// SPDX-License-Identifier: GPL-2.0-only
/*
 *  fair_share.c - A simple weight based Thermal governor
 *
 *  Copyright (C) 2012 Intel Corp
 *  Copyright (C) 2012 Durgadoss R <durgadoss.r@intel.com>
 *
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#include <linux/thermal.h>
#include "thermal_trace.h"

#include "thermal_core.h"

static int get_trip_level(struct thermal_zone_device *tz)
{
	const struct thermal_trip *trip, *level_trip = NULL;
	int trip_level;

	for_each_trip(tz, trip) {
		if (trip->temperature >= tz->temperature)
			break;

		level_trip = trip;
	}

	/*  Bail out if the temperature is not greater than any trips. */
	if (!level_trip)
		return 0;

	trip_level = thermal_zone_trip_id(tz, level_trip);

	trace_thermal_zone_trip(tz, trip_level, level_trip->type);

	return trip_level;
}

static long get_target_state(struct thermal_zone_device *tz,
		struct thermal_cooling_device *cdev, int percentage, int level)
{
	return (long)(percentage * level * cdev->max_state) / (100 * tz->num_trips);
}

/**
 * fair_share_throttle - throttles devices associated with the given zone
 * @tz: thermal_zone_device
 * @trip: trip point
 *
 * Throttling Logic: This uses three parameters to calculate the new
 * throttle state of the cooling devices associated with the given zone.
 *
 * Parameters used for Throttling:
 * P1. max_state: Maximum throttle state exposed by the cooling device.
 * P2. percentage[i]/100:
 *	How 'effective' the 'i'th device is, in cooling the given zone.
 * P3. cur_trip_level/max_no_of_trips:
 *	This describes the extent to which the devices should be throttled.
 *	We do not want to throttle too much when we trip a lower temperature,
 *	whereas the throttling is at full swing if we trip critical levels.
 *	(Heavily assumes the trip points are in ascending order)
 * new_state of cooling device = P3 * P2 * P1
 */
static int fair_share_throttle(struct thermal_zone_device *tz,
			       const struct thermal_trip *trip)
{
	struct thermal_instance *instance;
	int total_weight = 0;
	int total_instance = 0;
	int cur_trip_level = get_trip_level(tz);

	lockdep_assert_held(&tz->lock);

	list_for_each_entry(instance, &tz->thermal_instances, tz_node) {
		if (instance->trip != trip)
			continue;

		total_weight += instance->weight;
		total_instance++;
	}

	list_for_each_entry(instance, &tz->thermal_instances, tz_node) {
		int percentage;
		struct thermal_cooling_device *cdev = instance->cdev;

		if (instance->trip != trip)
			continue;

		if (!total_weight)
			percentage = 100 / total_instance;
		else
			percentage = (instance->weight * 100) / total_weight;

		instance->target = get_target_state(tz, cdev, percentage,
						    cur_trip_level);

		mutex_lock(&cdev->lock);
		__thermal_cdev_update(cdev);
		mutex_unlock(&cdev->lock);
	}

	return 0;
}

static struct thermal_governor thermal_gov_fair_share = {
	.name		= "fair_share",
	.throttle	= fair_share_throttle,
};
THERMAL_GOVERNOR_DECLARE(thermal_gov_fair_share);
