/*
 * drivers/base/sw_sync.c
 *
 * Copyright (C) 2012 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/export.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>

#include "sw_sync.h"

struct sync_pt *sw_sync_pt_create(struct sw_sync_timeline *obj, u32 value)
{
	struct sw_sync_pt *pt;

	pt = (struct sw_sync_pt *)
		sync_pt_create(&obj->obj, sizeof(struct sw_sync_pt));

	pt->value = value;

	return (struct sync_pt *)pt;
}
EXPORT_SYMBOL(sw_sync_pt_create);

static int sw_sync_pt_has_signaled(struct sync_pt *sync_pt)
{
	struct sw_sync_pt *pt = (struct sw_sync_pt *)sync_pt;
	struct sw_sync_timeline *obj =
		(struct sw_sync_timeline *)sync_pt_parent(sync_pt);

	return (pt->value > obj->value) ? 0 : 1;
}

static int sw_sync_fill_driver_data(struct sync_pt *sync_pt,
				    void *data, int size)
{
	struct sw_sync_pt *pt = (struct sw_sync_pt *)sync_pt;

	if (size < sizeof(pt->value))
		return -ENOMEM;

	memcpy(data, &pt->value, sizeof(pt->value));

	return sizeof(pt->value);
}

static void sw_sync_timeline_value_str(struct sync_timeline *sync_timeline,
				       char *str, int size)
{
	struct sw_sync_timeline *timeline =
		(struct sw_sync_timeline *)sync_timeline;
	snprintf(str, size, "%d", timeline->value);
}

static void sw_sync_pt_value_str(struct sync_pt *sync_pt,
				 char *str, int size)
{
	struct sw_sync_pt *pt = (struct sw_sync_pt *)sync_pt;

	snprintf(str, size, "%d", pt->value);
}

static struct sync_timeline_ops sw_sync_timeline_ops = {
	.driver_name = "sw_sync",
	.has_signaled = sw_sync_pt_has_signaled,
	.fill_driver_data = sw_sync_fill_driver_data,
	.timeline_value_str = sw_sync_timeline_value_str,
	.pt_value_str = sw_sync_pt_value_str,
};

struct sw_sync_timeline *sw_sync_timeline_create(const char *name)
{
	struct sw_sync_timeline *obj = (struct sw_sync_timeline *)
		sync_timeline_create(&sw_sync_timeline_ops,
				     sizeof(struct sw_sync_timeline),
				     name);

	return obj;
}
EXPORT_SYMBOL(sw_sync_timeline_create);

void sw_sync_timeline_inc(struct sw_sync_timeline *obj, u32 inc)
{
	obj->value += inc;

	sync_timeline_signal(&obj->obj);
}
EXPORT_SYMBOL(sw_sync_timeline_inc);
