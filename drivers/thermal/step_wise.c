/*
 *  step_wise.c - A step-by-step Thermal throttling governor
 *
 *  Copyright (C) 2012 Intel Corp
 *  Copyright (C) 2012 Durgadoss R <durgadoss.r@intel.com>
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

#include <linux/thermal.h>

#include "thermal_core.h"

/*
 * If the temperature is higher than a trip point,
 *    a. if the trend is THERMAL_TREND_RAISING, use higher cooling
 *       state for this trip point
 *    b. if the trend is THERMAL_TREND_DROPPING, use lower cooling
 *       state for this trip point
 *    c. if the trend is THERMAL_TREND_RAISE_FULL, use upper limit
 *       for this trip point
 *    d. if the trend is THERMAL_TREND_DROP_FULL, use lower limit
 *       for this trip point
 * If the temperature is lower than a trip point,
 *    a. if the trend is THERMAL_TREND_RAISING, do nothing
 *    b. if the trend is THERMAL_TREND_DROPPING, use lower cooling
 *       state for this trip point, if the cooling state already
 *       equals lower limit, deactivate the thermal instance
 *    c. if the trend is THERMAL_TREND_RAISE_FULL, do nothing
 *    d. if the trend is THERMAL_TREND_DROP_FULL, use lower limit,
 *       if the cooling state already equals lower limit,
 *       deactive the thermal instance
 */
static unsigned long get_target_state(struct thermal_instance *instance,
				enum thermal_trend trend, bool throttle)
{
	struct thermal_cooling_device *cdev = instance->cdev;
	unsigned long cur_state;
	unsigned long next_target;

	/*
	 * We keep this instance the way it is by default.
	 * Otherwise, we use the current state of the
	 * cdev in use to determine the next_target.
	 */
	cdev->ops->get_cur_state(cdev, &cur_state);
	next_target = instance->target;
	dev_dbg(&cdev->device, "cur_state=%ld\n", cur_state);

	switch (trend) {
	case THERMAL_TREND_RAISING:
		if (throttle) {
			next_target = cur_state < instance->upper ?
				    (cur_state + 1) : instance->upper;
			if (next_target < instance->lower)
				next_target = instance->lower;
		}
		break;
	case THERMAL_TREND_RAISE_FULL:
		if (throttle)
			next_target = instance->upper;
		break;
	case THERMAL_TREND_DROPPING:
		if (cur_state == instance->lower) {
			if (!throttle)
				next_target = THERMAL_NO_TARGET;
		} else {
			next_target = cur_state - 1;
			if (next_target > instance->upper)
				next_target = instance->upper;
		}
		break;
	case THERMAL_TREND_DROP_FULL:
		if (cur_state == instance->lower) {
			if (!throttle)
				next_target = THERMAL_NO_TARGET;
		} else
			next_target = instance->lower;
		break;
	default:
		break;
	}

	return next_target;
}

static void update_passive_instance(struct thermal_zone_device *tz,
				enum thermal_trip_type type, int value)
{
	/*
	 * If value is +1, activate a passive instance.
	 * If value is -1, deactivate a passive instance.
	 */
	if (type == THERMAL_TRIP_PASSIVE || type == THERMAL_TRIPS_NONE)
		tz->passive += value;
}

static void thermal_zone_trip_update(struct thermal_zone_device *tz, int trip)
{
	long trip_temp;
	enum thermal_trip_type trip_type;
	enum thermal_trend trend;
	struct thermal_instance *instance;
	bool throttle = false;
	int old_target;

	if (trip == THERMAL_TRIPS_NONE) {
		trip_temp = tz->forced_passive;
		trip_type = THERMAL_TRIPS_NONE;
	} else {
		tz->ops->get_trip_temp(tz, trip, &trip_temp);
		tz->ops->get_trip_type(tz, trip, &trip_type);
	}

	trend = get_tz_trend(tz, trip);

	if (tz->temperature >= trip_temp)
		throttle = true;

	dev_dbg(&tz->device, "Trip%d[type=%d,temp=%ld]:trend=%d,throttle=%d\n",
				trip, trip_type, trip_temp, trend, throttle);

	mutex_lock(&tz->lock);

	list_for_each_entry(instance, &tz->thermal_instances, tz_node) {
		if (instance->trip != trip)
			continue;

		old_target = instance->target;
		instance->target = get_target_state(instance, trend, throttle);
		dev_dbg(&instance->cdev->device, "old_target=%d, target=%d\n",
					old_target, (int)instance->target);

		if (old_target == instance->target)
			continue;

		/* Activate a passive thermal instance */
		if (old_target == THERMAL_NO_TARGET &&
			instance->target != THERMAL_NO_TARGET)
			update_passive_instance(tz, trip_type, 1);
		/* Deactivate a passive thermal instance */
		else if (old_target != THERMAL_NO_TARGET &&
			instance->target == THERMAL_NO_TARGET)
			update_passive_instance(tz, trip_type, -1);


		instance->cdev->updated = false; /* cdev needs update */
	}

	mutex_unlock(&tz->lock);
}

/**
 * step_wise_throttle - throttles devices asscciated with the given zone
 * @tz - thermal_zone_device
 * @trip - the trip point
 * @trip_type - type of the trip point
 *
 * Throttling Logic: This uses the trend of the thermal zone to throttle.
 * If the thermal zone is 'heating up' this throttles all the cooling
 * devices associated with the zone and its particular trip point, by one
 * step. If the zone is 'cooling down' it brings back the performance of
 * the devices by one step.
 */
static int step_wise_throttle(struct thermal_zone_device *tz, int trip)
{
	struct thermal_instance *instance;

	thermal_zone_trip_update(tz, trip);

	if (tz->forced_passive)
		thermal_zone_trip_update(tz, THERMAL_TRIPS_NONE);

	mutex_lock(&tz->lock);

	list_for_each_entry(instance, &tz->thermal_instances, tz_node)
		thermal_cdev_update(instance->cdev);

	mutex_unlock(&tz->lock);

	return 0;
}

static struct thermal_governor thermal_gov_step_wise = {
	.name		= "step_wise",
	.throttle	= step_wise_throttle,
};

int thermal_gov_step_wise_register(void)
{
	return thermal_register_governor(&thermal_gov_step_wise);
}

void thermal_gov_step_wise_unregister(void)
{
	thermal_unregister_governor(&thermal_gov_step_wise);
}
