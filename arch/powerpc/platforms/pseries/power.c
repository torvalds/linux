// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Interface for power-management for ppc64 compliant platform
 *
 *  Manish Ahuja <mahuja@us.ibm.com>
 *
 *  Feb 2007
 *
 *  Copyright (C) 2007 IBM Corporation.
 */

#include <linux/kobject.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <asm/machdep.h>

#include "pseries.h"

unsigned long rtas_poweron_auto; /* default and normal state is 0 */

static ssize_t auto_poweron_show(struct kobject *kobj,
				 struct kobj_attribute *attr, char *buf)
{
        return sprintf(buf, "%lu\n", rtas_poweron_auto);
}

static ssize_t auto_poweron_store(struct kobject *kobj,
				  struct kobj_attribute *attr,
				  const char *buf, size_t n)
{
	int ret;
	unsigned long ups_restart;
	ret = sscanf(buf, "%lu", &ups_restart);

	if ((ret == 1) && ((ups_restart == 1) || (ups_restart == 0))){
		rtas_poweron_auto = ups_restart;
		return n;
	}
	return -EINVAL;
}

static struct kobj_attribute auto_poweron_attr =
	__ATTR(auto_poweron, 0644, auto_poweron_show, auto_poweron_store);

#ifndef CONFIG_PM
struct kobject *power_kobj;

static struct attribute *g[] = {
        &auto_poweron_attr.attr,
        NULL,
};

static const struct attribute_group attr_group = {
        .attrs = g,
};

static int __init pm_init(void)
{
	power_kobj = kobject_create_and_add("power", NULL);
	if (!power_kobj)
		return -ENOMEM;
	return sysfs_create_group(power_kobj, &attr_group);
}
machine_core_initcall(pseries, pm_init);
#else
static int __init apo_pm_init(void)
{
	return (sysfs_create_file(power_kobj, &auto_poweron_attr.attr));
}
machine_device_initcall(pseries, apo_pm_init);
#endif
