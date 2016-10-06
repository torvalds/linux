/*
 *  gov_bang_bang.c - A simple thermal throttling governor using hysteresis
 *
 *  Copyright (C) 2014 Peter Feuerer <peter@piie.net>
 *
 *  Based on step_wise.c with following Copyrights:
 *  Copyright (C) 2012 Intel Corp
 *  Copyright (C) 2012 Durgadoss R <durgadoss.r@intel.com>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See
 * the GNU General Public License for more details.
 *
 */

#include <linux/thermal.h>

#include "thermal_core.h"

static void thermal_zone_trip_update(struct thermal_zone_device *tz, int trip)
{
	int trip_temp, trip_hyst;
	struct thermal_instance *instance;

	tz->ops->get_trip_temp(tz, trip, &trip_temp);

	if (!tz->ops->get_trip_hyst) {
		pr_warn_once("Undefined get_trip_hyst for thermal zone %s - "
				"running with default hysteresis zero\n", tz->type);
		trip_hyst = 0;
	} else
		tz->ops->get_trip_hyst(tz, trip, &trip_hyst);

	dev_dbg(&tz->device, "Trip%d[temp=%d]:temp=%d:hyst=%d\n",
				trip, trip_temp, tz->temperature,
				trip_hyst);

	mutex_lock(&tz->lock);

	list_for_each_entry(instance, &tz->thermal_instances, tz_node) {
		if (instance->trip != trip)
			continue;

		/* in case fan is in initial state, switch the fan off */
		if (instance->target == THERMAL_NO_TARGET)
			instance->target = 0;

		/* in case fan is neither on nor off set the fan to active */
		if (instance->target != 0 && instance->target != 1) {
			pr_warn("Thermal instance %s controlled by bang-bang has unexpected state: %ld\n",
					instance->name, instance->target);
			instance->target = 1;
		}

		/*
		 * enable fan when temperature exceeds trip_temp and disable
		 * the fan in case it falls below trip_temp minus hysteresis
		 */
		if (instance->target == 0 && tz->temperature >= trip_temp)
			instance->target = 1;
		else if (instance->target == 1 &&
				tz->temperature < trip_temp - trip_hyst)
			instance->target = 0;

		dev_dbg(&instance->cdev->device, "target=%d\n",
					(int)instance->target);

		mutex_lock(&instance->cdev->lock);
		instance->cdev->updated = false; /* cdev needs update */
		mutex_unlock(&instance->cdev->lock);
	}

	mutex_unlock(&tz->lock);
}

/**
 * bang_bang_control - controls devices associated with the given zone
 * @tz - thermal_zone_device
 * @trip - the trip point
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
static int bang_bang_control(struct thermal_zone_device *tz, int trip)
{
	struct thermal_instance *instance;

	thermal_zone_trip_update(tz, trip);

	mutex_lock(&tz->lock);

	list_for_each_entry(instance, &tz->thermal_instances, tz_node)
		thermal_cdev_update(instance->cdev);

	mutex_unlock(&tz->lock);

	return 0;
}

static struct thermal_governor thermal_gov_bang_bang = {
	.name		= "bang_bang",
	.throttle	= bang_bang_control,
};

int thermal_gov_bang_bang_register(void)
{
	return thermal_register_governor(&thermal_gov_bang_bang);
}

void thermal_gov_bang_bang_unregister(void)
{
	thermal_unregister_governor(&thermal_gov_bang_bang);
}
