/*
 *  user_space.c - A simple user space Thermal events notifier
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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/thermal.h>

#include "thermal_core.h"

/**
 * notify_user_space - Notifies user space about thermal events
 * @tz - thermal_zone_device
 *
 * This function notifies the user space through UEvents.
 */
static int notify_user_space(struct thermal_zone_device *tz, int trip)
{
	mutex_lock(&tz->lock);
	kobject_uevent(&tz->device.kobj, KOBJ_CHANGE);
	mutex_unlock(&tz->lock);
	return 0;
}

static struct thermal_governor thermal_gov_user_space = {
	.name		= "user_space",
	.throttle	= notify_user_space,
	.owner		= THIS_MODULE,
};

static int __init thermal_gov_user_space_init(void)
{
	return thermal_register_governor(&thermal_gov_user_space);
}

static void __exit thermal_gov_user_space_exit(void)
{
	thermal_unregister_governor(&thermal_gov_user_space);
}

/* This should load after thermal framework */
fs_initcall(thermal_gov_user_space_init);
module_exit(thermal_gov_user_space_exit);

MODULE_AUTHOR("Durgadoss R");
MODULE_DESCRIPTION("A user space Thermal notifier");
MODULE_LICENSE("GPL");
