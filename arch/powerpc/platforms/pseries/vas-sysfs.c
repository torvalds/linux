// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2022-23 IBM Corp.
 */

#define pr_fmt(fmt) "vas: " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/kobject.h>
#include <linux/slab.h>
#include <linux/mm.h>

#include "vas.h"

#ifdef CONFIG_SYSFS
static struct kobject *pseries_vas_kobj;
static struct kobject *gzip_caps_kobj;

struct vas_caps_entry {
	struct kobject kobj;
	struct vas_cop_feat_caps *caps;
};

#define to_caps_entry(entry) container_of(entry, struct vas_caps_entry, kobj)

#define sysfs_caps_entry_read(_name)					\
static ssize_t _name##_show(struct vas_cop_feat_caps *caps, char *buf) 	\
{									\
	return sprintf(buf, "%d\n", atomic_read(&caps->_name));	\
}

struct vas_sysfs_entry {
	struct attribute attr;
	ssize_t (*show)(struct vas_cop_feat_caps *, char *);
	ssize_t (*store)(struct vas_cop_feat_caps *, const char *, size_t);
};

#define VAS_ATTR_RO(_name)	\
	sysfs_caps_entry_read(_name);		\
	static struct vas_sysfs_entry _name##_attribute = __ATTR(_name,	\
				0444, _name##_show, NULL);

/*
 * Create sysfs interface:
 * /sys/devices/vas/vas0/gzip/default_capabilities
 *	This directory contains the following VAS GZIP capabilities
 *	for the defaule credit type.
 * /sys/devices/vas/vas0/gzip/default_capabilities/nr_total_credits
 *	Total number of default credits assigned to the LPAR which
 *	can be changed with DLPAR operation.
 * /sys/devices/vas/vas0/gzip/default_capabilities/nr_used_credits
 *	Number of credits used by the user space. One credit will
 *	be assigned for each window open.
 *
 * /sys/devices/vas/vas0/gzip/qos_capabilities
 *	This directory contains the following VAS GZIP capabilities
 *	for the Quality of Service (QoS) credit type.
 * /sys/devices/vas/vas0/gzip/qos_capabilities/nr_total_credits
 *	Total number of QoS credits assigned to the LPAR. The user
 *	has to define this value using HMC interface. It can be
 *	changed dynamically by the user.
 * /sys/devices/vas/vas0/gzip/qos_capabilities/nr_used_credits
 *	Number of credits used by the user space.
 */

VAS_ATTR_RO(nr_total_credits);
VAS_ATTR_RO(nr_used_credits);

static struct attribute *vas_capab_attrs[] = {
	&nr_total_credits_attribute.attr,
	&nr_used_credits_attribute.attr,
	NULL,
};

static ssize_t vas_type_show(struct kobject *kobj, struct attribute *attr,
			     char *buf)
{
	struct vas_caps_entry *centry;
	struct vas_cop_feat_caps *caps;
	struct vas_sysfs_entry *entry;

	centry = to_caps_entry(kobj);
	caps = centry->caps;
	entry = container_of(attr, struct vas_sysfs_entry, attr);

	if (!entry->show)
		return -EIO;

	return entry->show(caps, buf);
}

static ssize_t vas_type_store(struct kobject *kobj, struct attribute *attr,
			      const char *buf, size_t count)
{
	struct vas_caps_entry *centry;
	struct vas_cop_feat_caps *caps;
	struct vas_sysfs_entry *entry;

	centry = to_caps_entry(kobj);
	caps = centry->caps;
	entry = container_of(attr, struct vas_sysfs_entry, attr);
	if (!entry->store)
		return -EIO;

	return entry->store(caps, buf, count);
}

static void vas_type_release(struct kobject *kobj)
{
	struct vas_caps_entry *centry = to_caps_entry(kobj);
	kfree(centry);
}

static const struct sysfs_ops vas_sysfs_ops = {
	.show	=	vas_type_show,
	.store	=	vas_type_store,
};

static struct kobj_type vas_attr_type = {
		.release	=	vas_type_release,
		.sysfs_ops      =       &vas_sysfs_ops,
		.default_attrs  =       vas_capab_attrs,
};

static char *vas_caps_kobj_name(struct vas_cop_feat_caps *caps,
				struct kobject **kobj)
{
	if (caps->descriptor == VAS_GZIP_QOS_CAPABILITIES) {
		*kobj = gzip_caps_kobj;
		return "qos_capabilities";
	} else if (caps->descriptor == VAS_GZIP_DEFAULT_CAPABILITIES) {
		*kobj = gzip_caps_kobj;
		return "default_capabilities";
	} else
		return "Unknown";
}

/*
 * Add feature specific capability dir entry.
 * Ex: VDefGzip or VQosGzip
 */
int sysfs_add_vas_caps(struct vas_cop_feat_caps *caps)
{
	struct vas_caps_entry *centry;
	struct kobject *kobj = NULL;
	int ret = 0;
	char *name;

	centry = kzalloc(sizeof(*centry), GFP_KERNEL);
	if (!centry)
		return -ENOMEM;

	kobject_init(&centry->kobj, &vas_attr_type);
	centry->caps = caps;
	name  = vas_caps_kobj_name(caps, &kobj);

	if (kobj) {
		ret = kobject_add(&centry->kobj, kobj, "%s", name);

		if (ret) {
			pr_err("VAS: sysfs kobject add / event failed %d\n",
					ret);
			kobject_put(&centry->kobj);
		}
	}

	return ret;
}

static struct miscdevice vas_miscdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "vas",
};

/*
 * Add VAS and VasCaps (overall capabilities) dir entries.
 */
int __init sysfs_pseries_vas_init(struct vas_all_caps *vas_caps)
{
	int ret;

	ret = misc_register(&vas_miscdev);
	if (ret < 0) {
		pr_err("%s: register vas misc device failed\n", __func__);
		return ret;
	}

	/*
	 * The hypervisor does not expose multiple VAS instances, but can
	 * see multiple VAS instances on PowerNV. So create 'vas0' directory
	 * on pseries.
	 */
	pseries_vas_kobj = kobject_create_and_add("vas0",
					&vas_miscdev.this_device->kobj);
	if (!pseries_vas_kobj) {
		pr_err("Failed to create VAS sysfs entry\n");
		return -ENOMEM;
	}

	if ((vas_caps->feat_type & VAS_GZIP_QOS_FEAT_BIT) ||
		(vas_caps->feat_type & VAS_GZIP_DEF_FEAT_BIT)) {
		gzip_caps_kobj = kobject_create_and_add("gzip",
						       pseries_vas_kobj);
		if (!gzip_caps_kobj) {
			pr_err("Failed to create VAS GZIP capability entry\n");
			kobject_put(pseries_vas_kobj);
			return -ENOMEM;
		}
	}

	return 0;
}

#else
int sysfs_add_vas_caps(struct vas_cop_feat_caps *caps)
{
	return 0;
}

int __init sysfs_pseries_vas_init(struct vas_all_caps *vas_caps)
{
	return 0;
}
#endif
