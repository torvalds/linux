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

struct fence *sw_sync_pt_create(struct sw_sync_timeline *obj, u32 value)
{
	struct sw_sync_pt *pt;

	pt = (struct sw_sync_pt *)
		sync_pt_create(&obj->obj, sizeof(struct sw_sync_pt), value);

	pt->value = value;

	return (struct fence *)pt;
}
EXPORT_SYMBOL(sw_sync_pt_create);

struct sw_sync_timeline *sw_sync_timeline_create(const char *name)
{
	struct sw_sync_timeline *obj = (struct sw_sync_timeline *)
		sync_timeline_create(sizeof(struct sw_sync_timeline),
				     "sw_sync", name);

	return obj;
}
EXPORT_SYMBOL(sw_sync_timeline_create);

void sw_sync_timeline_inc(struct sw_sync_timeline *obj, u32 inc)
{
	sync_timeline_signal(&obj->obj, inc);
}
EXPORT_SYMBOL(sw_sync_timeline_inc);
