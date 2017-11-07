// SPDX-License-Identifier: GPL-2.0
/*
 * dim2_sysfs.c - MediaLB sysfs information
 *
 * Copyright (C) 2015, Microchip Technology Germany II GmbH & Co. KG
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * This file is licensed under GPLv2.
 */

/* Author: Andrey Shvetsov <andrey.shvetsov@k2l.de> */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include "dim2_sysfs.h"

struct bus_attr {
	struct attribute attr;
	ssize_t (*show)(struct medialb_bus *bus, char *buf);
	ssize_t (*store)(struct medialb_bus *bus, const char *buf,
			 size_t count);
};

static ssize_t state_show(struct medialb_bus *bus, char *buf)
{
	bool state = dim2_sysfs_get_state_cb();

	return sprintf(buf, "%s\n", state ? "locked" : "");
}

static struct bus_attr state_attr = __ATTR_RO(state);

static struct attribute *bus_default_attrs[] = {
	&state_attr.attr,
	NULL,
};

static const struct attribute_group bus_attr_group = {
	.attrs = bus_default_attrs,
};

static void bus_kobj_release(struct kobject *kobj)
{
}

static ssize_t bus_kobj_attr_show(struct kobject *kobj, struct attribute *attr,
				  char *buf)
{
	struct medialb_bus *bus =
		container_of(kobj, struct medialb_bus, kobj_group);
	struct bus_attr *xattr = container_of(attr, struct bus_attr, attr);

	if (!xattr->show)
		return -EIO;

	return xattr->show(bus, buf);
}

static ssize_t bus_kobj_attr_store(struct kobject *kobj, struct attribute *attr,
				   const char *buf, size_t count)
{
	struct medialb_bus *bus =
		container_of(kobj, struct medialb_bus, kobj_group);
	struct bus_attr *xattr = container_of(attr, struct bus_attr, attr);

	if (!xattr->store)
		return -EIO;

	return xattr->store(bus, buf, count);
}

static struct sysfs_ops const bus_kobj_sysfs_ops = {
	.show = bus_kobj_attr_show,
	.store = bus_kobj_attr_store,
};

static struct kobj_type bus_ktype = {
	.release = bus_kobj_release,
	.sysfs_ops = &bus_kobj_sysfs_ops,
};

int dim2_sysfs_probe(struct medialb_bus *bus, struct kobject *parent_kobj)
{
	int err;

	kobject_init(&bus->kobj_group, &bus_ktype);
	err = kobject_add(&bus->kobj_group, parent_kobj, "bus");
	if (err) {
		pr_err("kobject_add() failed: %d\n", err);
		goto err_kobject_add;
	}

	err = sysfs_create_group(&bus->kobj_group, &bus_attr_group);
	if (err) {
		pr_err("sysfs_create_group() failed: %d\n", err);
		goto err_create_group;
	}

	return 0;

err_create_group:
	kobject_put(&bus->kobj_group);

err_kobject_add:
	return err;
}

void dim2_sysfs_destroy(struct medialb_bus *bus)
{
	kobject_put(&bus->kobj_group);
}
