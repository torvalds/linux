/*
 * driver/base/topology.c - Populate sysfs with cpu topology information
 *
 * Written by: Zhang Yanmin, Intel Corporation
 *
 * Copyright (C) 2006, Intel Corp.
 *
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
#include <linux/sysdev.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/cpu.h>
#include <linux/module.h>
#include <linux/topology.h>

#define define_one_ro(_name) 		\
static SYSDEV_ATTR(_name, 0444, show_##_name, NULL)

#define define_id_show_func(name)				\
static ssize_t show_##name(struct sys_device *dev, char *buf)	\
{								\
	unsigned int cpu = dev->id;				\
	return sprintf(buf, "%d\n", topology_##name(cpu));	\
}

#define define_siblings_show_func(name)					\
static ssize_t show_##name(struct sys_device *dev, char *buf)		\
{									\
	ssize_t len = -1;						\
	unsigned int cpu = dev->id;					\
	len = cpumask_scnprintf(buf, NR_CPUS+1, topology_##name(cpu));	\
	return (len + sprintf(buf + len, "\n"));			\
}

#ifdef	topology_physical_package_id
define_id_show_func(physical_package_id);
define_one_ro(physical_package_id);
#define ref_physical_package_id_attr	&attr_physical_package_id.attr,
#else
#define ref_physical_package_id_attr
#endif

#ifdef topology_core_id
define_id_show_func(core_id);
define_one_ro(core_id);
#define ref_core_id_attr		&attr_core_id.attr,
#else
#define ref_core_id_attr
#endif

#ifdef topology_thread_siblings
define_siblings_show_func(thread_siblings);
define_one_ro(thread_siblings);
#define ref_thread_siblings_attr	&attr_thread_siblings.attr,
#else
#define ref_thread_siblings_attr
#endif

#ifdef topology_core_siblings
define_siblings_show_func(core_siblings);
define_one_ro(core_siblings);
#define ref_core_siblings_attr		&attr_core_siblings.attr,
#else
#define ref_core_siblings_attr
#endif

static struct attribute *default_attrs[] = {
	ref_physical_package_id_attr
	ref_core_id_attr
	ref_thread_siblings_attr
	ref_core_siblings_attr
	NULL
};

static struct attribute_group topology_attr_group = {
	.attrs = default_attrs,
	.name = "topology"
};

/* Add/Remove cpu_topology interface for CPU device */
static int __cpuinit topology_add_dev(unsigned int cpu)
{
	struct sys_device *sys_dev = get_cpu_sysdev(cpu);

	return sysfs_create_group(&sys_dev->kobj, &topology_attr_group);
}

static void __cpuinit topology_remove_dev(unsigned int cpu)
{
	struct sys_device *sys_dev = get_cpu_sysdev(cpu);

	sysfs_remove_group(&sys_dev->kobj, &topology_attr_group);
}

static int __cpuinit topology_cpu_callback(struct notifier_block *nfb,
					   unsigned long action, void *hcpu)
{
	unsigned int cpu = (unsigned long)hcpu;
	int rc = 0;

	switch (action) {
	case CPU_UP_PREPARE:
	case CPU_UP_PREPARE_FROZEN:
		rc = topology_add_dev(cpu);
		break;
	case CPU_UP_CANCELED:
	case CPU_UP_CANCELED_FROZEN:
	case CPU_DEAD:
	case CPU_DEAD_FROZEN:
		topology_remove_dev(cpu);
		break;
	}
	return rc ? NOTIFY_BAD : NOTIFY_OK;
}

static int __cpuinit topology_sysfs_init(void)
{
	int cpu;
	int rc;

	for_each_online_cpu(cpu) {
		rc = topology_add_dev(cpu);
		if (rc)
			return rc;
	}
	hotcpu_notifier(topology_cpu_callback, 0);

	return 0;
}

device_initcall(topology_sysfs_init);
