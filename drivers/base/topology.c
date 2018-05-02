// SPDX-License-Identifier: GPL-2.0+
/*
 * driver/base/topology.c - Populate sysfs with cpu topology information
 *
 * Written by: Zhang Yanmin, Intel Corporation
 *
 * Copyright (C) 2006, Intel Corp.
 *
 * All rights reserved.
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

#ifdef CONFIG_SCHED_DRAWER
define_id_show_func(drawer_id);
static DEVICE_ATTR_RO(drawer_id);
define_siblings_show_func(drawer_siblings, drawer_cpumask);
static DEVICE_ATTR_RO(drawer_siblings);
static DEVICE_ATTR_RO(drawer_siblings_list);
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
#ifdef CONFIG_SCHED_DRAWER
	&dev_attr_drawer_id.attr,
	&dev_attr_drawer_siblings.attr,
	&dev_attr_drawer_siblings_list.attr,
#endif
	NULL
};

static const struct attribute_group topology_attr_group = {
	.attrs = default_attrs,
	.name = "topology"
};

/* Add/Remove cpu_topology interface for CPU device */
static int topology_add_dev(unsigned int cpu)
{
	struct device *dev = get_cpu_device(cpu);

	return sysfs_create_group(&dev->kobj, &topology_attr_group);
}

static int topology_remove_dev(unsigned int cpu)
{
	struct device *dev = get_cpu_device(cpu);

	sysfs_remove_group(&dev->kobj, &topology_attr_group);
	return 0;
}

static int topology_sysfs_init(void)
{
	return cpuhp_setup_state(CPUHP_TOPOLOGY_PREPARE,
				 "base/topology:prepare", topology_add_dev,
				 topology_remove_dev);
}

device_initcall(topology_sysfs_init);
