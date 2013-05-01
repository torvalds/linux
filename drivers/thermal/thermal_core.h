/*
 *  thermal_core.h
 *
 *  Copyright (C) 2012  Intel Corp
 *  Author: Durgadoss R <durgadoss.r@intel.com>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
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

#ifndef __THERMAL_CORE_H__
#define __THERMAL_CORE_H__

#include <linux/device.h>
#include <linux/thermal.h>

/* Initial state of a cooling device during binding */
#define THERMAL_NO_TARGET -1UL

/*
 * This structure is used to describe the behavior of
 * a certain cooling device on a certain trip point
 * in a certain thermal zone
 */
struct thermal_instance {
	int id;
	char name[THERMAL_NAME_LENGTH];
	struct thermal_zone_device *tz;
	struct thermal_cooling_device *cdev;
	int trip;
	unsigned long upper;	/* Highest cooling state for this trip point */
	unsigned long lower;	/* Lowest cooling state for this trip point */
	unsigned long target;	/* expected cooling state */
	char attr_name[THERMAL_NAME_LENGTH];
	struct device_attribute attr;
	struct list_head tz_node; /* node in tz->thermal_instances */
	struct list_head cdev_node; /* node in cdev->thermal_instances */
};

#endif /* __THERMAL_CORE_H__ */
