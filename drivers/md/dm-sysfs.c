/*
 * Copyright (C) 2008 Red Hat, Inc. All rights reserved.
 *
 * This file is released under the GPL.
 */

#include <linux/sysfs.h>
#include <linux/dm-ioctl.h>
#include "dm.h"

struct dm_sysfs_attr {
	struct attribute attr;
	ssize_t (*show)(struct mapped_device *, char *);
	ssize_t (*store)(struct mapped_device *, char *);
};

#define DM_ATTR_RO(_name) \
struct dm_sysfs_attr dm_attr_##_name = \
	__ATTR(_name, S_IRUGO, dm_attr_##_name##_show, NULL)

static ssize_t dm_attr_show(struct kobject *kobj, struct attribute *attr,
			    char *page)
{
	struct dm_sysfs_attr *dm_attr;
	struct mapped_device *md;
	ssize_t ret;

	dm_attr = container_of(attr, struct dm_sysfs_attr, attr);
	if (!dm_attr->show)
		return -EIO;

	md = dm_get_from_kobject(kobj);
	if (!md)
		return -EINVAL;

	ret = dm_attr->show(md, page);
	dm_put(md);

	return ret;
}

static ssize_t dm_attr_name_show(struct mapped_device *md, char *buf)
{
	if (dm_copy_name_and_uuid(md, buf, NULL))
		return -EIO;

	strcat(buf, "\n");
	return strlen(buf);
}

static ssize_t dm_attr_uuid_show(struct mapped_device *md, char *buf)
{
	if (dm_copy_name_and_uuid(md, NULL, buf))
		return -EIO;

	strcat(buf, "\n");
	return strlen(buf);
}

static ssize_t dm_attr_suspended_show(struct mapped_device *md, char *buf)
{
	sprintf(buf, "%d\n", dm_suspended(md));

	return strlen(buf);
}

static DM_ATTR_RO(name);
static DM_ATTR_RO(uuid);
static DM_ATTR_RO(suspended);

static struct attribute *dm_attrs[] = {
	&dm_attr_name.attr,
	&dm_attr_uuid.attr,
	&dm_attr_suspended.attr,
	NULL,
};

static struct sysfs_ops dm_sysfs_ops = {
	.show	= dm_attr_show,
};

/*
 * dm kobject is embedded in mapped_device structure
 * no need to define release function here
 */
static struct kobj_type dm_ktype = {
	.sysfs_ops	= &dm_sysfs_ops,
	.default_attrs	= dm_attrs,
};

/*
 * Initialize kobj
 * because nobody using md yet, no need to call explicit dm_get/put
 */
int dm_sysfs_init(struct mapped_device *md)
{
	return kobject_init_and_add(dm_kobject(md), &dm_ktype,
				    &disk_to_dev(dm_disk(md))->kobj,
				    "%s", "dm");
}

/*
 * Remove kobj, called after all references removed
 */
void dm_sysfs_exit(struct mapped_device *md)
{
	kobject_put(dm_kobject(md));
}
