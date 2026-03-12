// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 IBM Corporation, Srish Srinivasan <ssrish@linux.ibm.com>
 *
 * This code exposes PLPKS config to user via sysfs
 */

#define pr_fmt(fmt) "plpks-sysfs: "fmt

#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <asm/machdep.h>
#include <asm/plpks.h>

/* config attributes for sysfs */
#define PLPKS_CONFIG_ATTR(name, fmt, func)			\
	static ssize_t name##_show(struct kobject *kobj,	\
				   struct kobj_attribute *attr,	\
				   char *buf)			\
	{							\
		return sysfs_emit(buf, fmt, func());		\
	}							\
	static struct kobj_attribute attr_##name = __ATTR_RO(name)

PLPKS_CONFIG_ATTR(version, "%u\n", plpks_get_version);
PLPKS_CONFIG_ATTR(max_object_size, "%u\n", plpks_get_maxobjectsize);
PLPKS_CONFIG_ATTR(total_size, "%u\n", plpks_get_totalsize);
PLPKS_CONFIG_ATTR(used_space, "%u\n", plpks_get_usedspace);
PLPKS_CONFIG_ATTR(supported_policies, "%08x\n", plpks_get_supportedpolicies);
PLPKS_CONFIG_ATTR(signed_update_algorithms, "%016llx\n",
		  plpks_get_signedupdatealgorithms);
PLPKS_CONFIG_ATTR(wrapping_features, "%016llx\n", plpks_get_wrappingfeatures);

static const struct attribute *config_attrs[] = {
	&attr_version.attr,
	&attr_max_object_size.attr,
	&attr_total_size.attr,
	&attr_used_space.attr,
	&attr_supported_policies.attr,
	&attr_signed_update_algorithms.attr,
	&attr_wrapping_features.attr,
	NULL,
};

static struct kobject *plpks_kobj, *plpks_config_kobj;

int plpks_config_create_softlink(struct kobject *from)
{
	if (!plpks_config_kobj)
		return -EINVAL;
	return sysfs_create_link(from, plpks_config_kobj, "config");
}

static __init int plpks_sysfs_config(struct kobject *kobj)
{
	struct attribute_group config_group = {
		.name = NULL,
		.attrs = (struct attribute **)config_attrs,
	};

	return sysfs_create_group(kobj, &config_group);
}

static __init int plpks_sysfs_init(void)
{
	int rc;

	if (!plpks_is_available())
		return -ENODEV;

	plpks_kobj = kobject_create_and_add("plpks", firmware_kobj);
	if (!plpks_kobj) {
		pr_err("Failed to create plpks kobj\n");
		return -ENOMEM;
	}

	plpks_config_kobj = kobject_create_and_add("config", plpks_kobj);
	if (!plpks_config_kobj) {
		pr_err("Failed to create plpks config kobj\n");
		kobject_put(plpks_kobj);
		return -ENOMEM;
	}

	rc = plpks_sysfs_config(plpks_config_kobj);
	if (rc) {
		pr_err("Failed to create attribute group for plpks config\n");
		kobject_put(plpks_config_kobj);
		kobject_put(plpks_kobj);
		return rc;
	}

	return 0;
}

machine_subsys_initcall(pseries, plpks_sysfs_init);
