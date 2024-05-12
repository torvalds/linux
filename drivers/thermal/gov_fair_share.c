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

/**
 * get_trip_level: - obtains the current trip level for a zone
 * @tz:		thermal zone device
 */
static int get_trip_level(struct thermal_zone_device *tz)
{
	struct thermal_trip trip;
	int count;

	for (count = 0; count < tz->num_trips; count++) {
		__thermal_zone_get_trip(tz, count, &trip);
		if (tz->temperature < trip.temperature)
			break;
	}

	/*
	 * count > 0 only if temperature is greater than first trip
	 * point, in which case, trip_point = count - 1
	 */
	if (count > 0)
		trace_thermal_zone_trip(tz, count - 1, trip.type);

	return count;
}

static long get_target_state(struct thermal_zone_device *tz,
		struct thermal_cooling_device *cdev, int percentage, int level)
{
	return (long)(percentage * level * cdev->max_state) / (100 * tz->num_trips);
}

/**
 * fair_share_throttle - throttles devices associated with the given zone
 * @tz: thermal_zone_device
 * @trip: trip point index
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
static int fair_share_throttle(struct thermal_zone_device *tz, int trip)
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
