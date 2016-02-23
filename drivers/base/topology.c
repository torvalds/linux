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
#include <linux/mm.h>
#include <linux/cpu.h>
#include <linux/module.h>
#include <linux/hardirq.h>
#include <linux/topology.h>

#define define_id_show_func(name)				\
static ssize_t name##_show(struct device *dev,			\
		struct device_attribute *attr, char *buf)	\
{								\
	return sprintf(buf, "%d\n", topology_##name(dev->id));	\
}

#define define_siblings_show_map(name, mask)				\
static ssize_t name##_show(struct device *dev,				\
			   struct device_attribute *attr, char *buf)	\
{									\
	return cpumap_print_to_pagebuf(false, buf, topology_##mask(dev->id));\
}

#define define_siblings_show_list(name, mask)				\
static ssize_t name##_list_show(struct device *dev,			\
				struct device_attribute *attr,		\
				char *buf)				\
{									\
	return cpumap_print_to_pagebuf(true, buf, topology_##mask(dev->id));\
}

#define define_siblings_show_func(name, mask)	\
	define_siblings_show_map(name, mask);	\
	define_siblings_show_list(name, mask)

define_id_show_func(physical_package_id);
static DEVICE_ATTR_RO(physical_package_id);

define_id_show_func(core_id);
static DEVICE_ATTR_RO(core_id);

define_siblings_show_func(thread_siblings, sibling_cpumask);
static DEVICE_ATTR_RO(thread_siblings);
static DEVICE_ATTR_RO(thread_siblings_list);

define_siblings_show_func(core_siblings, core_cpumask);
static DEVICE_ATTR_RO(core_siblings);
static DEVICE_ATTR_RO(core_siblings_list);

#ifdef CONFIG_SCHED_BOOK
define_id_show_func(book_id);
static DEVICE_ATTR_RO(book_id);
define_siblings_show_func(book_siblings, book_cpumask);
static DEVICE_ATTR_RO(book_siblings);
static DEVICE_ATTR_RO(book_siblings_list);
#endif

static struct attribute *default_attrs[] = {
	&dev_attr_physical_package_id.attr,
	&dev_attr_core_id.attr,
	&dev_attr_thread_siblings.attr,
	&dev_attr_thread_siblings_list.attr,
	&dev_attr_core_siblings.attr,
	&dev_attr_core_siblings_list.attr,
#ifdef CONFIG_SCHED_BOOK
	&dev_attr_book_id.attr,
	&dev_attr_book_siblings.attr,
	&dev_attr_book_siblings_list.attr,
#endif
	NULL
};

static struct attribute_group topology_attr_group = {
	.attrs = default_attrs,
	.name = "topology"
};

/* Add/Remove cpu_topology interface for CPU device */
static int topology_add_dev(unsigned int cpu)
{
	struct device *dev = get_cpu_device(cpu);

	return sysfs_create_group(&dev->kobj, &topology_attr_group);
}

static void topology_remove_dev(unsigned int cpu)
{
	struct device *dev = get_cpu_device(cpu);

	sysfs_remove_group(&dev->kobj, &topology_attr_group);
}

static int topology_cpu_callback(struct notifier_block *nfb,
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
	return notifier_from_errno(rc);
}

static int topology_sysfs_init(void)
{
	int cpu;
	int rc = 0;

	cpu_notifier_register_begin();

	for_each_online_cpu(cpu) {
		rc = topology_add_dev(cpu);
		if (rc)
			goto out;
	}
	__hotcpu_notifier(topology_cpu_callback, 0);

out:
	cpu_notifier_register_done();
	return rc;
}

device_initcall(topology_sysfs_init);
