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
	const struct thermal_trip_desc *level_td = NULL;
	const struct thermal_trip_desc *td;
	int trip_level = -1;

	for_each_trip_desc(tz, td) {
		if (td->threshold > tz->temperature)
			continue;

		trip_level++;

		if (!level_td || td->threshold > level_td->threshold)
			level_td = td;
	}

	/*  Bail out if the temperature is not greater than any trips. */
	if (trip_level < 0)
		return 0;

	trace_thermal_zone_trip(tz, thermal_zone_trip_id(tz, &level_td->trip),
				level_td->trip.type);

	return trip_level;
}

/**
 * fair_share_throttle - throttles devices associated with the given zone
 * @tz: thermal_zone_device
 * @trip: trip point
 * @trip_level: number of trips crossed by the zone temperature
 *
 * Throttling Logic: This uses three parameters to calculate the new
 * throttle state of the cooling devices associated with the given zone.
 *
 * Parameters used for Throttling:
 * P1. max_state: Maximum throttle state exposed by the cooling device.
 * P2. weight[i]/total_weight:
 *	How 'effective' the 'i'th device is, in cooling the given zone.
 * P3. trip_level/max_no_of_trips:
 *	This describes the extent to which the devices should be throttled.
 *	We do not want to throttle too much when we trip a lower temperature,
 *	whereas the throttling is at full swing if we trip critical levels.
 * new_state of cooling device = P3 * P2 * P1
 */
static void fair_share_throttle(struct thermal_zone_device *tz,
				const struct thermal_trip *trip,
				int trip_level)
{
	struct thermal_instance *instance;
	int total_weight = 0;
	int nr_instances = 0;

	list_for_each_entry(instance, &tz->thermal_instances, tz_node) {
		if (instance->trip != trip)
			continue;

		total_weight += instance->weight;
		nr_instances++;
	}

	list_for_each_entry(instance, &tz->thermal_instances, tz_node) {
		struct thermal_cooling_device *cdev = instance->cdev;
		u64 dividend;
		u32 divisor;

		if (instance->trip != trip)
			continue;

		dividend = trip_level;
		dividend *= cdev->max_state;
		divisor = tz->num_trips;
		if (total_weight) {
			dividend *= instance->weight;
			divisor *= total_weight;
		} else {
			divisor *= nr_instances;
		}
		instance->target = div_u64(dividend, divisor);

		mutex_lock(&cdev->lock);
		__thermal_cdev_update(cdev);
		mutex_unlock(&cdev->lock);
	}
}

static void fair_share_manage(struct thermal_zone_device *tz)
{
	int trip_level = get_trip_level(tz);
	const struct thermal_trip_desc *td;

	lockdep_assert_held(&tz->lock);

	for_each_trip_desc(tz, td) {
		const struct thermal_trip *trip = &td->trip;

		if (trip->temperature == THERMAL_TEMP_INVALID ||
		    trip->type == THERMAL_TRIP_CRITICAL ||
		    trip->type == THERMAL_TRIP_HOT)
			continue;

		fair_share_throttle(tz, trip, trip_level);
	}
}

static struct thermal_governor thermal_gov_fair_share = {
	.name	= "fair_share",
	.manage	= fair_share_manage,
};
THERMAL_GOVERNOR_DECLARE(thermal_gov_fair_share);
