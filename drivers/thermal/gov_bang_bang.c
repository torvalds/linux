// SPDX-License-Identifier: GPL-2.0-only
/*
 *  gov_bang_bang.c - A simple thermal throttling governor using hysteresis
 *
 *  Copyright (C) 2014 Peter Kaestle <peter@piie.net>
 *
 *  Based on step_wise.c with following Copyrights:
 *  Copyright (C) 2012 Intel Corp
 *  Copyright (C) 2012 Durgadoss R <durgadoss.r@intel.com>
 *
 * Regulation Logic: a two point regulation, deliver cooling state depending
 * on the previous state shown in this diagram:
 *
 *                Fan:   OFF    ON
 *
 *                              |
 *                              |
 *          trip_temp:    +---->+
 *                        |     |        ^
 *                        |     |        |
 *                        |     |   Temperature
 * (trip_temp - hyst):    +<----+
 *                        |
 *                        |
 *                        |
 *
 *   * If the fan is not running and temperature exceeds trip_temp, the fan
 *     gets turned on.
 *   * In case the fan is running, temperature must fall below
 *     (trip_temp - hyst) so that the fan gets turned off again.
 */

#include <linux/thermal.h>

#include "thermal_core.h"

static void bang_bang_set_instance_target(struct thermal_instance *instance,
					  unsigned int target)
{
	if (instance->target != 0 && instance->target != 1 &&
	    instance->target != THERMAL_NO_TARGET)
		pr_debug("Unexpected state %ld of thermal instance %s in bang-bang\n",
			 instance->target, instance->name);

	/*
	 * Enable the fan when the trip is crossed on the way up and disable it
	 * when the trip is crossed on the way down.
	 */
	instance->target = target;
	instance->initialized = true;

	dev_dbg(&instance->cdev->device, "target=%ld\n", instance->target);

	thermal_cdev_update_nocheck(instance->cdev);
}

/**
 * bang_bang_trip_crossed - controls devices associated with the given zone
 * @tz: thermal_zone_device
 * @trip: the trip point
 * @upward: whether or not the trip has been crossed on the way up
 */
static void bang_bang_trip_crossed(struct thermal_zone_device *tz,
				   const struct thermal_trip *trip,
				   bool upward)
{
	const struct thermal_trip_desc *td = trip_to_trip_desc(trip);
	struct thermal_instance *instance;

	lockdep_assert_held(&tz->lock);

	dev_dbg(&tz->device, "Trip%d[temp=%d]:temp=%d:hyst=%d\n",
		thermal_zone_trip_id(tz, trip), trip->temperature,
		tz->temperature, trip->hysteresis);

	list_for_each_entry(instance, &td->thermal_instances, trip_node)
		bang_bang_set_instance_target(instance, upward);
}

static void bang_bang_manage(struct thermal_zone_device *tz)
{
	const struct thermal_trip_desc *td;
	struct thermal_instance *instance;

	/* If the code below has run already, nothing needs to be done. */
	if (tz->governor_data)
		return;

	for_each_trip_desc(tz, td) {
		const struct thermal_trip *trip = &td->trip;
		bool turn_on;

		if (trip->temperature == THERMAL_TEMP_INVALID ||
		    trip->type == THERMAL_TRIP_CRITICAL ||
		    trip->type == THERMAL_TRIP_HOT)
			continue;

		/*
		 * Adjust the target states for uninitialized thermal instances
		 * to the thermal zone temperature and the trip point threshold.
		 */
		turn_on = tz->temperature >= td->threshold;
		list_for_each_entry(instance, &td->thermal_instances, trip_node) {
			if (!instance->initialized)
				bang_bang_set_instance_target(instance, turn_on);
		}
	}

	tz->governor_data = (void *)true;
}

static void bang_bang_update_tz(struct thermal_zone_device *tz,
				enum thermal_notify_event reason)
{
	/*
	 * Let bang_bang_manage() know that it needs to walk trips after binding
	 * a new cdev and after system resume.
	 */
	if (reason == THERMAL_TZ_BIND_CDEV || reason == THERMAL_TZ_RESUME)
		tz->governor_data = NULL;
}

static struct thermal_governor thermal_gov_bang_bang = {
	.name		= "bang_bang",
	.trip_crossed	= bang_bang_trip_crossed,
	.manage		= bang_bang_manage,
	.update_tz	= bang_bang_update_tz,
};
THERMAL_GOVERNOR_DECLARE(thermal_gov_bang_bang);
