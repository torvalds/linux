/*
 * Interface for Dynamic Logical Partitioning of I/O Slots on
 * RPA-compliant PPC64 platform.
 *
 * John Rose <johnrose@austin.ibm.com>
 * October 2003
 *
 * Copyright (C) 2003 IBM.
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */
#include <linux/kobject.h>
#include <linux/string.h>
#include "pci_hotplug.h"
#include "rpadlpar.h"

#define DLPAR_KOBJ_NAME       "control"
#define ADD_SLOT_ATTR_NAME    "add_slot"
#define REMOVE_SLOT_ATTR_NAME "remove_slot"

#define MAX_DRC_NAME_LEN 64

/* Store return code of dlpar operation in attribute struct */
struct dlpar_io_attr {
	int rc;
	struct attribute attr;
	ssize_t (*store)(struct dlpar_io_attr *dlpar_attr, const char *buf,
		size_t nbytes);
};

/* Common show callback for all attrs, display the return code
 * of the dlpar op */
static ssize_t
dlpar_attr_show(struct kobject * kobj, struct attribute * attr, char * buf)
{
	struct dlpar_io_attr *dlpar_attr = container_of(attr,
						struct dlpar_io_attr, attr);
	return sprintf(buf, "%d\n", dlpar_attr->rc);
}

static ssize_t
dlpar_attr_store(struct kobject * kobj, struct attribute * attr,
		 const char *buf, size_t nbytes)
{
	struct dlpar_io_attr *dlpar_attr = container_of(attr,
						struct dlpar_io_attr, attr);
	return dlpar_attr->store ?
		dlpar_attr->store(dlpar_attr, buf, nbytes) : 0;
}

static struct sysfs_ops dlpar_attr_sysfs_ops = {
	.show = dlpar_attr_show,
	.store = dlpar_attr_store,
};

static ssize_t add_slot_store(struct dlpar_io_attr *dlpar_attr,
				const char *buf, size_t nbytes)
{
	char drc_name[MAX_DRC_NAME_LEN];
	char *end;

	if (nbytes > MAX_DRC_NAME_LEN)
		return 0;

	memcpy(drc_name, buf, nbytes);

	end = strchr(drc_name, '\n');
	if (!end)
		end = &drc_name[nbytes];
	*end = '\0';

	dlpar_attr->rc = dlpar_add_slot(drc_name);

	return nbytes;
}

static ssize_t remove_slot_store(struct dlpar_io_attr *dlpar_attr,
		 		const char *buf, size_t nbytes)
{
	char drc_name[MAX_DRC_NAME_LEN];
	char *end;

	if (nbytes > MAX_DRC_NAME_LEN)
		return 0;

	memcpy(drc_name, buf, nbytes);

	end = strchr(drc_name, '\n');
	if (!end)
		end = &drc_name[nbytes];
	*end = '\0';

	dlpar_attr->rc = dlpar_remove_slot(drc_name);

	return nbytes;
}

static struct dlpar_io_attr add_slot_attr = {
	.rc = 0,
	.attr = { .name = ADD_SLOT_ATTR_NAME, .mode = 0644, },
	.store = add_slot_store,
};

static struct dlpar_io_attr remove_slot_attr = {
	.rc = 0,
	.attr = { .name = REMOVE_SLOT_ATTR_NAME, .mode = 0644},
	.store = remove_slot_store,
};

static struct attribute *default_attrs[] = {
	&add_slot_attr.attr,
	&remove_slot_attr.attr,
	NULL,
};

static void dlpar_io_release(struct kobject *kobj)
{
	/* noop */
	return;
}

struct kobj_type ktype_dlpar_io = {
	.release = dlpar_io_release,
	.sysfs_ops = &dlpar_attr_sysfs_ops,
	.default_attrs = default_attrs,
};

struct kset dlpar_io_kset = {
	.subsys = &pci_hotplug_slots_subsys,
	.kobj = {.name = DLPAR_KOBJ_NAME, .ktype=&ktype_dlpar_io,},
	.ktype = &ktype_dlpar_io,
};

int dlpar_sysfs_init(void)
{
	if (kset_register(&dlpar_io_kset)) {
		printk(KERN_ERR "rpadlpar_io: cannot register kset for %s\n",
				dlpar_io_kset.kobj.name);
		return -EINVAL;
	}

	return 0;
}

void dlpar_sysfs_exit(void)
{
	kset_unregister(&dlpar_io_kset);
}
