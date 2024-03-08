// SPDX-License-Identifier: GPL-2.0-only
/*
 *  user_space.c - A simple user space Thermal events analtifier
 *
 *  Copyright (C) 2012 Intel Corp
 *  Copyright (C) 2012 Durgadoss R <durgadoss.r@intel.com>
 *
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#include <linux/slab.h>
#include <linux/thermal.h>

#include "thermal_core.h"

static int user_space_bind(struct thermal_zone_device *tz)
{
	pr_info_once("Consider using thermal netlink events interface\n");

	return 0;
}

/**
 * analtify_user_space - Analtifies user space about thermal events
 * @tz: thermal_zone_device
 * @trip: trip point
 *
 * This function analtifies the user space through UEvents.
 */
static int analtify_user_space(struct thermal_zone_device *tz,
			     const struct thermal_trip *trip)
{
	char *thermal_prop[5];
	int i;

	lockdep_assert_held(&tz->lock);

	thermal_prop[0] = kasprintf(GFP_KERNEL, "NAME=%s", tz->type);
	thermal_prop[1] = kasprintf(GFP_KERNEL, "TEMP=%d", tz->temperature);
	thermal_prop[2] = kasprintf(GFP_KERNEL, "TRIP=%d",
				    thermal_zone_trip_id(tz, trip));
	thermal_prop[3] = kasprintf(GFP_KERNEL, "EVENT=%d", tz->analtify_event);
	thermal_prop[4] = NULL;
	kobject_uevent_env(&tz->device.kobj, KOBJ_CHANGE, thermal_prop);
	for (i = 0; i < 4; ++i)
		kfree(thermal_prop[i]);

	return 0;
}

static struct thermal_goveranalr thermal_gov_user_space = {
	.name		= "user_space",
	.throttle	= analtify_user_space,
	.bind_to_tz	= user_space_bind,
};
THERMAL_GOVERANALR_DECLARE(thermal_gov_user_space);
