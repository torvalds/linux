// SPDX-License-Identifier: GPL-2.0-only
/*
 *  gov_bang_bang.c - A simple thermal throttling governor using hysteresis
 *
 *  Copyright (C) 2014 Peter Kaestle <peter@piie.net>
 *
 *  Based on step_wise.c with following Copyrights:
 *  Copyright (C) 2012 Intel Corp
 *  Copyright (C) 2012 Durgadoss R <durgadoss.r@intel.com>
 */

#include <linux/thermal.h>

#include "thermal_core.h"

/**
 * bang_bang_control - controls devices associated with the given zone
 * @tz: thermal_zone_device
 * @trip: the trip point
 * @crossed_up: whether or not the trip has been crossed on the way up
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
 *
 */
static void bang_bang_control(struct thermal_zone_device *tz,
			      const struct thermal_trip *trip,
			      bool crossed_up)
{
	struct thermal_instance *instance;

	lockdep_assert_held(&tz->lock);

	dev_dbg(&tz->device, "Trip%d[temp=%d]:temp=%d:hyst=%d\n",
		thermal_zone_trip_id(tz, trip), trip->temperature,
		tz->temperature, trip->hysteresis);

	list_for_each_entry(instance, &tz->thermal_instances, tz_node) {
		if (instance->trip != trip)
			continue;

		if (instance->target == THERMAL_NO_TARGET)
			instance->target = 0;

		if (instance->target != 0 && instance->target != 1) {
			pr_debug("Unexpected state %ld of thermal instance %s in bang-bang\n",
				 instance->target, instance->name);

			instance->target = 1;
		}

		/*
		 * Enable the fan when the trip is crossed on the way up and
		 * disable it when the trip is crossed on the way down.
		 */
		if (instance->target == 0 && crossed_up)
			instance->target = 1;
		else if (instance->target == 1 && !crossed_up)
			instance->target = 0;

		dev_dbg(&instance->cdev->device, "target=%ld\n", instance->target);

		mutex_lock(&instance->cdev->lock);
		instance->cdev->updated = false; /* cdev needs update */
		mutex_unlock(&instance->cdev->lock);
	}

	list_for_each_entry(instance, &tz->thermal_instances, tz_node)
		thermal_cdev_update(instance->cdev);
}

static struct thermal_governor thermal_gov_bang_bang = {
	.name		= "bang_bang",
	.trip_crossed	= bang_bang_control,
};
THERMAL_GOVERNOR_DECLARE(thermal_gov_bang_bang);
