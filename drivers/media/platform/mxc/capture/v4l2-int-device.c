/*
 * drivers/media/video/v4l2-int-device.c
 *
 * V4L2 internal ioctl interface.
 *
 * Copyright 2005-2014 Freescale Semiconductor, Inc.
 * Copyright (C) 2007 Nokia Corporation.
 *
 * Contact: Sakari Ailus <sakari.ailus@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/sort.h>
#include <linux/string.h>
#include <linux/module.h>

#include "v4l2-int-device.h"

static DEFINE_MUTEX(mutex);
static LIST_HEAD(int_list);

void v4l2_int_device_try_attach_all(void)
{
	struct v4l2_int_device *m, *s;

	list_for_each_entry(m, &int_list, head) {
		if (m->type != v4l2_int_type_master)
			continue;

		list_for_each_entry(s, &int_list, head) {
			if (s->type != v4l2_int_type_slave)
				continue;

			/* Slave is connected? */
			if (s->u.slave->master)
				continue;

			/* Slave wants to attach to master? */
			if (s->u.slave->attach_to[0] != 0
			    && strncmp(m->name, s->u.slave->attach_to,
				       V4L2NAMESIZE))
				continue;

			if (!try_module_get(m->module))
				continue;

			s->u.slave->master = m;
			if (m->u.master->attach(s)) {
				s->u.slave->master = NULL;
				module_put(m->module);
				continue;
			}
		}
	}
}
EXPORT_SYMBOL_GPL(v4l2_int_device_try_attach_all);

static int ioctl_sort_cmp(const void *a, const void *b)
{
	const struct v4l2_int_ioctl_desc *d1 = a, *d2 = b;

	if (d1->num > d2->num)
		return 1;

	if (d1->num < d2->num)
		return -1;

	return 0;
}

int v4l2_int_device_register(struct v4l2_int_device *d)
{
	if (d->type == v4l2_int_type_slave)
		sort(d->u.slave->ioctls, d->u.slave->num_ioctls,
		     sizeof(struct v4l2_int_ioctl_desc),
		     &ioctl_sort_cmp, NULL);
	mutex_lock(&mutex);
	list_add(&d->head, &int_list);
	v4l2_int_device_try_attach_all();
	mutex_unlock(&mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(v4l2_int_device_register);

void v4l2_int_device_unregister(struct v4l2_int_device *d)
{
	mutex_lock(&mutex);
	list_del(&d->head);
	if (d->type == v4l2_int_type_slave
	    && d->u.slave->master != NULL) {
		d->u.slave->master->u.master->detach(d);
		module_put(d->u.slave->master->module);
		d->u.slave->master = NULL;
	}
	mutex_unlock(&mutex);
}
EXPORT_SYMBOL_GPL(v4l2_int_device_unregister);

/* Adapted from search_extable in extable.c. */
static v4l2_int_ioctl_func *find_ioctl(struct v4l2_int_slave *slave, int cmd,
				       v4l2_int_ioctl_func *no_such_ioctl)
{
	const struct v4l2_int_ioctl_desc *first = slave->ioctls;
	const struct v4l2_int_ioctl_desc *last =
		first + slave->num_ioctls - 1;

	while (first <= last) {
		const struct v4l2_int_ioctl_desc *mid;

		mid = (last - first) / 2 + first;

		if (mid->num < cmd)
			first = mid + 1;
		else if (mid->num > cmd)
			last = mid - 1;
		else
			return mid->func;
	}

	return no_such_ioctl;
}

static int no_such_ioctl_0(struct v4l2_int_device *d)
{
	return -ENOIOCTLCMD;
}

int v4l2_int_ioctl_0(struct v4l2_int_device *d, int cmd)
{
	return ((v4l2_int_ioctl_func_0 *)
		find_ioctl(d->u.slave, cmd,
			   (v4l2_int_ioctl_func *)no_such_ioctl_0))(d);
}
EXPORT_SYMBOL_GPL(v4l2_int_ioctl_0);

static int no_such_ioctl_1(struct v4l2_int_device *d, void *arg)
{
	return -ENOIOCTLCMD;
}

int v4l2_int_ioctl_1(struct v4l2_int_device *d, int cmd, void *arg)
{
	return ((v4l2_int_ioctl_func_1 *)
		find_ioctl(d->u.slave, cmd,
			   (v4l2_int_ioctl_func *)no_such_ioctl_1))(d, arg);
}
EXPORT_SYMBOL_GPL(v4l2_int_ioctl_1);

MODULE_LICENSE("GPL");
