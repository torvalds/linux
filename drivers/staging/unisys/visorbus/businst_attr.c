/* businst_attr.c
 *
 * Copyright (C) 2010 - 2013 UNISYS CORPORATION
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 */

/*   This is actually something they forgot to put in the kernel.
 *   struct bus_type in the kernel SHOULD have a "busses" member, which
 *   should be treated similarly to the "devices" and "drivers" members.
 *   There SHOULD be:
 *   - a "businst_attribute" analogous to the existing "bus_attribute"
 *   - a "businst_create_file" and "businst_remove_file" analogous to the
 *     existing "bus_create_file" and "bus_remove_file".
 *   That's what I created businst.c and businst.h to do.
 *
 *   We want to add the "busses" sub-tree in sysfs, where we will house the
 *   names and properties of each bus instance:
 *
 *       /sys/bus/<bustypename>/
 *           version
 *           devices
 *               <devname1> --> /sys/devices/<businstancename><devname1>
 *               <devname2> --> /sys/devices/<businstancename><devname2>
 *           drivers
 *               <driverinstancename1>
 *                   <driverinstance1property1>
 *                   <driverinstance1property2>
 *                   ...
 *               <driverinstancename2>
 *                   <driverinstance2property1>
 *                   <driverinstance2property2>
 *                   ...
 *   >>      busses
 *   >>          <businstancename1>
 *   >>              <businstance1property1>
 *   >>              <businstance1property2>
 *   >>              ...
 *   >>          <businstancename2>
 *   >>              <businstance2property1>
 *   >>              <businstance2property2>
 *   >>              ...
 *
 *   I considered adding bus instance properties under
 *   /sys/devices/<businstancename>.  But I thought there may be existing
 *   notions that ONLY device sub-trees should live under
 *   /sys/devices/<businstancename>.  So I stayed out of there.
 *
 */

#include "businst_attr.h"

#define to_businst_attr(_attr) \
	container_of(_attr, struct businst_attribute, attr)
#define to_visorbus_devdata(obj) \
	container_of(obj, struct visorbus_devdata, kobj)
#define CURRENT_FILE_PC VISOR_BUS_PC_businst_attr_c

ssize_t businst_attr_show(struct kobject *kobj, struct attribute *attr,
			  char *buf)
{
	struct businst_attribute *businst_attr = to_businst_attr(attr);
	struct visorbus_devdata *bus = to_visorbus_devdata(kobj);
	ssize_t ret = 0;

	if (businst_attr->show)
		ret = businst_attr->show(bus, buf);
	return ret;
}

ssize_t businst_attr_store(struct kobject *kobj, struct attribute *attr,
			   const char *buf, size_t count)
{
	struct businst_attribute *businst_attr = to_businst_attr(attr);
	struct visorbus_devdata *bus = to_visorbus_devdata(kobj);
	ssize_t ret = 0;

	if (businst_attr->store)
		ret = businst_attr->store(bus, buf, count);
	return ret;
}

int businst_create_file(struct visorbus_devdata *bus,
			struct businst_attribute *attr)
{
	return sysfs_create_file(&bus->kobj, &attr->attr);
}

void businst_remove_file(struct visorbus_devdata *bus,
			 struct businst_attribute *attr)
{
	sysfs_remove_file(&bus->kobj, &attr->attr);
}
