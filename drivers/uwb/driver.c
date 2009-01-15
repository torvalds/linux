/*
 * Ultra Wide Band
 * Driver initialization, etc
 *
 * Copyright (C) 2005-2006 Intel Corporation
 * Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 *
 * FIXME: docs
 *
 * Life cycle: FIXME: explain
 *
 *  UWB radio controller:
 *
 *    1. alloc a uwb_rc, zero it
 *    2. call uwb_rc_init() on it to set it up + ops (won't do any
 *       kind of allocation)
 *    3. register (now it is owned by the UWB stack--deregister before
 *       freeing/destroying).
 *    4. It lives on it's own now (UWB stack handles)--when it
 *       disconnects, call unregister()
 *    5. free it.
 *
 *    Make sure you have a reference to the uwb_rc before calling
 *    any of the UWB API functions.
 *
 * TODO:
 *
 * 1. Locking and life cycle management is crappy still. All entry
 *    points to the UWB HCD API assume you have a reference on the
 *    uwb_rc structure and that it won't go away. They mutex lock it
 *    before doing anything.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/kdev_t.h>
#include <linux/random.h>

#include "uwb-internal.h"


/* UWB stack attributes (or 'global' constants) */


/**
 * If a beacon dissapears for longer than this, then we consider the
 * device who was represented by that beacon to be gone.
 *
 * ECMA-368[17.2.3, last para] establishes that a device must not
 * consider a device to be its neighbour if he doesn't receive a beacon
 * for more than mMaxLostBeacons. mMaxLostBeacons is defined in
 * ECMA-368[17.16] as 3; because we can get only one beacon per
 * superframe, that'd be 3 * 65ms = 195 ~ 200 ms. Let's give it time
 * for jitter and stuff and make it 500 ms.
 */
unsigned long beacon_timeout_ms = 500;

static
ssize_t beacon_timeout_ms_show(struct class *class, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%lu\n", beacon_timeout_ms);
}

static
ssize_t beacon_timeout_ms_store(struct class *class,
				const char *buf, size_t size)
{
	unsigned long bt;
	ssize_t result;
	result = sscanf(buf, "%lu", &bt);
	if (result != 1)
		return -EINVAL;
	beacon_timeout_ms = bt;
	return size;
}

static struct class_attribute uwb_class_attrs[] = {
	__ATTR(beacon_timeout_ms, S_IWUSR | S_IRUGO,
	       beacon_timeout_ms_show, beacon_timeout_ms_store),
	__ATTR_NULL,
};

/** Device model classes */
struct class uwb_rc_class = {
	.name        = "uwb_rc",
	.class_attrs = uwb_class_attrs,
};


static int __init uwb_subsys_init(void)
{
	int result = 0;

	result = uwb_est_create();
	if (result < 0) {
		printk(KERN_ERR "uwb: Can't initialize EST subsystem\n");
		goto error_est_init;
	}

	result = class_register(&uwb_rc_class);
	if (result < 0)
		goto error_uwb_rc_class_register;
	uwb_dbg_init();
	return 0;

error_uwb_rc_class_register:
	uwb_est_destroy();
error_est_init:
	return result;
}
module_init(uwb_subsys_init);

static void __exit uwb_subsys_exit(void)
{
	uwb_dbg_exit();
	class_unregister(&uwb_rc_class);
	uwb_est_destroy();
	return;
}
module_exit(uwb_subsys_exit);

MODULE_AUTHOR("Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>");
MODULE_DESCRIPTION("Ultra Wide Band core");
MODULE_LICENSE("GPL");
