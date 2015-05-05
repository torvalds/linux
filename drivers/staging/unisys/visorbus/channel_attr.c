/* channel_attr.c
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

/* Implement publishing of channel attributes under:
 *
 *     /sys/bus/visorbus<x>/dev<y>/channel
 *
 */

#include "channel_attr.h"
#define CURRENT_FILE_PC VISOR_BUS_PC_channel_attr_c
#define to_channel_attr(_attr) \
	container_of(_attr, struct channel_attribute, attr)
#define to_visor_device_from_kobjchannel(obj) \
	container_of(obj, struct visor_device, kobjchannel)

struct channel_attribute {
	struct attribute attr;
	 ssize_t (*show)(struct visor_device*, char *buf);
	 ssize_t (*store)(struct visor_device*, const char *buf, size_t count);
};

/* begin implementation of specific channel attributes to appear under
* /sys/bus/visorbus<x>/dev<y>/channel
*/
static ssize_t devicechannel_attr_physaddr(struct visor_device *dev, char *buf)
{
	if (!dev->visorchannel)
		return 0;
	return snprintf(buf, PAGE_SIZE, "0x%Lx\n",
			visorchannel_get_physaddr(dev->visorchannel));
}

static ssize_t devicechannel_attr_nbytes(struct visor_device *dev, char *buf)
{
	if (!dev->visorchannel)
		return 0;
	return snprintf(buf, PAGE_SIZE, "0x%lx\n",
			visorchannel_get_nbytes(dev->visorchannel));
}

static ssize_t devicechannel_attr_clientpartition(struct visor_device *dev,
						  char *buf) {
	if (!dev->visorchannel)
		return 0;
	return snprintf(buf, PAGE_SIZE, "0x%Lx\n",
			visorchannel_get_clientpartition(dev->visorchannel));
}

static ssize_t devicechannel_attr_typeguid(struct visor_device *dev, char *buf)
{
	char s[99];

	if (!dev->visorchannel)
		return 0;
	return snprintf(buf, PAGE_SIZE, "%s\n",
			visorchannel_id(dev->visorchannel, s));
}

static ssize_t devicechannel_attr_zoneguid(struct visor_device *dev, char *buf)
{
	char s[99];

	if (!dev->visorchannel)
		return 0;
	return snprintf(buf, PAGE_SIZE, "%s\n",
			visorchannel_zoneid(dev->visorchannel, s));
}

static ssize_t devicechannel_attr_typename(struct visor_device *dev, char *buf)
{
	int i = 0;
	struct bus_type *xbus = dev->device.bus;
	struct device_driver *xdrv = dev->device.driver;
	struct visor_driver *drv = NULL;

	if (!dev->visorchannel || !xbus || !xdrv)
		return 0;
	i = xbus->match(&dev->device, xdrv);
	if (!i)
		return 0;
	drv = to_visor_driver(xdrv);
	return snprintf(buf, PAGE_SIZE, "%s\n", drv->channel_types[i - 1].name);
}

static ssize_t devicechannel_attr_dump(struct visor_device *dev, char *buf)
{
	int count = 0;
/* TODO: replace this with debugfs code
	struct seq_file *m = NULL;
	if (dev->visorchannel == NULL)
		return 0;
	m = visor_seq_file_new_buffer(buf, PAGE_SIZE - 1);
	if (m == NULL)
		return 0;
	visorchannel_debug(dev->visorchannel, 1, m, 0);
	count = m->count;
	visor_seq_file_done_buffer(m);
	m = NULL;
*/
	return count;
}

static struct channel_attribute all_channel_attrs[] = {
	__ATTR(physaddr, S_IRUGO,
	       devicechannel_attr_physaddr, NULL),
	__ATTR(nbytes, S_IRUGO,
	       devicechannel_attr_nbytes, NULL),
	__ATTR(clientpartition, S_IRUGO,
	       devicechannel_attr_clientpartition, NULL),
	__ATTR(typeguid, S_IRUGO,
	       devicechannel_attr_typeguid, NULL),
	__ATTR(zoneguid, S_IRUGO,
	       devicechannel_attr_zoneguid, NULL),
	__ATTR(typename, S_IRUGO,
	       devicechannel_attr_typename, NULL),
	__ATTR(dump, S_IRUGO,
	       devicechannel_attr_dump, NULL),
};

/* end implementation of specific channel attributes */

static ssize_t channel_attr_show(struct kobject *kobj, struct attribute *attr,
				 char *buf)
{
	struct channel_attribute *channel_attr = to_channel_attr(attr);
	struct visor_device *dev = to_visor_device_from_kobjchannel(kobj);
	ssize_t ret = 0;

	if (channel_attr->show)
		ret = channel_attr->show(dev, buf);
	return ret;
}

static ssize_t channel_attr_store(struct kobject *kobj, struct attribute *attr,
				  const char *buf, size_t count)
{
	struct channel_attribute *channel_attr = to_channel_attr(attr);
	struct visor_device *dev = to_visor_device_from_kobjchannel(kobj);
	ssize_t ret = 0;

	if (channel_attr->store)
		ret = channel_attr->store(dev, buf, count);
	return ret;
}

static int channel_create_file(struct visor_device *dev,
			       struct channel_attribute *attr)
{
	return sysfs_create_file(&dev->kobjchannel, &attr->attr);
}

static void channel_remove_file(struct visor_device *dev,
				struct channel_attribute *attr)
{
	sysfs_remove_file(&dev->kobjchannel, &attr->attr);
}

static const struct sysfs_ops channel_sysfs_ops = {
	.show = channel_attr_show,
	.store = channel_attr_store,
};

static struct kobj_type channel_kobj_type = {
	.sysfs_ops = &channel_sysfs_ops
};

int register_channel_attributes(struct visor_device *dev)
{
	int rc = 0, i = 0, x = 0;

	if (dev->kobjchannel.parent)
		goto away;	/* already registered */
	x = kobject_init_and_add(&dev->kobjchannel, &channel_kobj_type,
				 &dev->device.kobj, "channel");
	if (x < 0) {
		rc = x;
		goto away;
	}

	kobject_uevent(&dev->kobjchannel, KOBJ_ADD);

	for (i = 0;
	     i < sizeof(all_channel_attrs) / sizeof(struct channel_attribute);
	     i++)
		x = channel_create_file(dev, &all_channel_attrs[i]);
	if (x < 0) {
		while (--i >= 0)
			channel_remove_file(dev, &all_channel_attrs[i]);
		kobject_del(&dev->kobjchannel);
		kobject_put(&dev->kobjchannel);
		rc = x;
		goto away;
	}
away:
	return rc;
}

void unregister_channel_attributes(struct visor_device *dev)
{
	int i = 0;

	if (!dev->kobjchannel.parent)
		return;		/* already unregistered */
	for (i = 0;
	     i < sizeof(all_channel_attrs) / sizeof(struct channel_attribute);
	     i++)
		channel_remove_file(dev, &all_channel_attrs[i]);

	kobject_del(&dev->kobjchannel);
	kobject_put(&dev->kobjchannel);
	dev->kobjchannel.parent = NULL;
}
