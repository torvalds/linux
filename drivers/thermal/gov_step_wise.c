// SPDX-License-Identifier: GPL-2.0-only
/*
 *  step_wise.c - A step-by-step Thermal throttling governor
 *
 *  Copyright (C) 2012 Intel Corp
 *  Copyright (C) 2012 Durgadoss R <durgadoss.r@intel.com>
 *
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#include <linux/thermal.h>
#include <linux/minmax.h>
#include "thermal_trace.h"

#include "thermal_core.h"

/*
 * If the temperature is higher than a trip point,
 *    a. if the trend is THERMAL_TREND_RAISING, use higher cooling
 *       state for this trip point
 *    b. if the trend is THERMAL_TREND_DROPPING, use a lower cooling state
 *       for this trip point, but keep the cooling state above the applicable
 *       minimum
 * If the temperature is lower than a trip point,
 *    a. if the trend is THERMAL_TREND_RAISING, do nothing
 *    b. if the trend is THERMAL_TREND_DROPPING, use the minimum applicable
 *       cooling state for this trip point, or if the cooling state already
 *       equals lower limit, deactivate the thermal instance
 */
static unsigned long get_target_state(struct thermal_instance *instance,
				enum thermal_trend trend, bool throttle)
{
	struct thermal_cooling_device *cdev = instance->cdev;
	unsigned long cur_state;

	/*
	 * We keep this instance the way it is by default.
	 * Otherwise, we use the current state of the
	 * cdev in use to determine the next_target.
	 */
	cdev->ops->get_cur_state(cdev, &cur_state);
	dev_dbg(&cdev->device, "cur_state=%ld\n", cur_state);

	if (!instance->initialized) {
		if (throttle)
			return clamp(cur_state + 1, instance->lower, instance->upper);

		return THERMAL_NO_TARGET;
	}

	if (throttle) {
		if (trend == THERMAL_TREND_RAISING)
			return clamp(cur_state + 1, instance->lower, instance->upper);

		/*
		 * If the zone temperature is falling, the cooling level can
		 * be reduced, but it should still be above the lower state of
		 * the given thermal instance to pull the temperature further
		 * down.
		 */
		if (trend == THERMAL_TREND_DROPPING)
			return clamp(cur_state - 1,
				     min(instance->lower + 1, instance->upper),
				     instance->upper);
	} else if (trend == THERMAL_TREND_DROPPING) {
		if (cur_state <= instance->lower)
			return THERMAL_NO_TARGET;

		/*
		 * If 'throttle' is false, no mitigation is necessary, so
		 * request the lower state for this instance.
		 */
		return instance->lower;
	}

	return instance->target;
}

static void thermal_zone_trip_update(struct thermal_zone_device *tz,
				     const struct thermal_trip_desc *td,
				     int trip_threshold)
{
	bool throttle = tz->temperature >= trip_threshold;
	const struct thermal_trip *trip = &td->trip;
	enum thermal_trend trend = get_tz_trend(tz, trip);
	int trip_id = thermal_zone_trip_id(tz, trip);
	struct thermal_instance *instance;

	if (throttle)
		trace_thermal_zone_trip(tz, trip_id, trip->type);

	dev_dbg(&tz->device, "Trip%d[type=%d,temp=%d]:trend=%d,throttle=%d\n",
		trip_id, trip->type, trip_threshold, trend, throttle);

	list_for_each_entry(instance, &td->thermal_instances, trip_node) {
		int old_target;

		old_target = instance->target;
		instance->target = get_target_state(instance, trend, throttle);

		dev_dbg(&instance->cdev->device, "old_target=%d, target=%ld\n",
			old_target, instance->target);

		if (instance->initialized && old_target == instance->target)
			continue;

		instance->initialized = true;

		scoped_guard(cooling_dev, instance->cdev) {
			instance->cdev->updated = false; /* cdev needs update */
		}
	}
}

static void step_wise_manage(struct thermal_zone_device *tz)
{
	const struct thermal_trip_desc *td;
	struct thermal_instance *instance;

	lockdep_assert_held(&tz->lock);

	/*
	 * Throttling Logic: Use the trend of the thermal zone to throttle.
	 * If the thermal zone is 'heating up', throttle all of the cooling
	 * devices associated with each trip point by one step. If the zone
	 * is 'cooling down', it brings back the performance of the devices
	 * by one step.
	 */
	for_each_trip_desc(tz, td) {
		const struct thermal_trip *trip = &td->trip;

		if (trip->temperature == THERMAL_TEMP_INVALID ||
		    trip->type == THERMAL_TRIP_CRITICAL ||
		    trip->type == THERMAL_TRIP_HOT)
			continue;

		thermal_zone_trip_update(tz, td, td->threshold);
	}

	for_each_trip_desc(tz, td) {
		list_for_each_entry(instance, &td->thermal_instances, trip_node)
			thermal_cdev_update(instance->cdev);
	}
}

static struct thermal_governor thermal_gov_step_wise = {
	.name	= "step_wise",
	.manage	= step_wise_manage,
};
THERMAL_GOVERNOR_DECLARE(thermal_gov_step_wise);
