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

#define define_id_show_func(name, fmt)					\
static ssize_t name##_show(struct device *dev,				\
			   struct device_attribute *attr, char *buf)	\
{									\
	return sysfs_emit(buf, fmt "\n", topology_##name(dev->id));	\
}

#define define_siblings_read_func(name, mask)					\
static ssize_t name##_read(struct file *file, struct kobject *kobj,		\
			   struct bin_attribute *attr, char *buf,		\
			   loff_t off, size_t count)				\
{										\
	struct device *dev = kobj_to_dev(kobj);                                 \
										\
	return cpumap_print_bitmask_to_buf(buf, topology_##mask(dev->id),	\
					   off, count);                         \
}										\
										\
static ssize_t name##_list_read(struct file *file, struct kobject *kobj,	\
				struct bin_attribute *attr, char *buf,		\
				loff_t off, size_t count)			\
{										\
	struct device *dev = kobj_to_dev(kobj);					\
										\
	return cpumap_print_list_to_buf(buf, topology_##mask(dev->id),		\
					off, count);				\
}

define_id_show_func(physical_package_id, "%d");
static DEVICE_ATTR_RO(physical_package_id);

#ifdef TOPOLOGY_DIE_SYSFS
define_id_show_func(die_id, "%d");
static DEVICE_ATTR_RO(die_id);
#endif

#ifdef TOPOLOGY_CLUSTER_SYSFS
define_id_show_func(cluster_id, "%d");
static DEVICE_ATTR_RO(cluster_id);
#endif

define_id_show_func(core_id, "%d");
static DEVICE_ATTR_RO(core_id);

define_id_show_func(ppin, "0x%llx");
static DEVICE_ATTR_ADMIN_RO(ppin);

define_siblings_read_func(thread_siblings, sibling_cpumask);
static BIN_ATTR_RO(thread_siblings, CPUMAP_FILE_MAX_BYTES);
static BIN_ATTR_RO(thread_siblings_list, CPULIST_FILE_MAX_BYTES);

define_siblings_read_func(core_cpus, sibling_cpumask);
static BIN_ATTR_RO(core_cpus, CPUMAP_FILE_MAX_BYTES);
static BIN_ATTR_RO(core_cpus_list, CPULIST_FILE_MAX_BYTES);

define_siblings_read_func(core_siblings, core_cpumask);
static BIN_ATTR_RO(core_siblings, CPUMAP_FILE_MAX_BYTES);
static BIN_ATTR_RO(core_siblings_list, CPULIST_FILE_MAX_BYTES);

#ifdef TOPOLOGY_CLUSTER_SYSFS
define_siblings_read_func(cluster_cpus, cluster_cpumask);
static BIN_ATTR_RO(cluster_cpus, CPUMAP_FILE_MAX_BYTES);
static BIN_ATTR_RO(cluster_cpus_list, CPULIST_FILE_MAX_BYTES);
#endif

#ifdef TOPOLOGY_DIE_SYSFS
define_siblings_read_func(die_cpus, die_cpumask);
static BIN_ATTR_RO(die_cpus, CPUMAP_FILE_MAX_BYTES);
static BIN_ATTR_RO(die_cpus_list, CPULIST_FILE_MAX_BYTES);
#endif

define_siblings_read_func(package_cpus, core_cpumask);
static BIN_ATTR_RO(package_cpus, CPUMAP_FILE_MAX_BYTES);
static BIN_ATTR_RO(package_cpus_list, CPULIST_FILE_MAX_BYTES);

#ifdef TOPOLOGY_BOOK_SYSFS
define_id_show_func(book_id, "%d");
static DEVICE_ATTR_RO(book_id);
define_siblings_read_func(book_siblings, book_cpumask);
static BIN_ATTR_RO(book_siblings, CPUMAP_FILE_MAX_BYTES);
static BIN_ATTR_RO(book_siblings_list, CPULIST_FILE_MAX_BYTES);
#endif

#ifdef TOPOLOGY_DRAWER_SYSFS
define_id_show_func(drawer_id, "%d");
static DEVICE_ATTR_RO(drawer_id);
define_siblings_read_func(drawer_siblings, drawer_cpumask);
static BIN_ATTR_RO(drawer_siblings, CPUMAP_FILE_MAX_BYTES);
static BIN_ATTR_RO(drawer_siblings_list, CPULIST_FILE_MAX_BYTES);
#endif

static struct bin_attribute *bin_attrs[] = {
	&bin_attr_core_cpus,
	&bin_attr_core_cpus_list,
	&bin_attr_thread_siblings,
	&bin_attr_thread_siblings_list,
	&bin_attr_core_siblings,
	&bin_attr_core_siblings_list,
#ifdef TOPOLOGY_CLUSTER_SYSFS
	&bin_attr_cluster_cpus,
	&bin_attr_cluster_cpus_list,
#endif
#ifdef TOPOLOGY_DIE_SYSFS
	&bin_attr_die_cpus,
	&bin_attr_die_cpus_list,
#endif
	&bin_attr_package_cpus,
	&bin_attr_package_cpus_list,
#ifdef TOPOLOGY_BOOK_SYSFS
	&bin_attr_book_siblings,
	&bin_attr_book_siblings_list,
#endif
#ifdef TOPOLOGY_DRAWER_SYSFS
	&bin_attr_drawer_siblings,
	&bin_attr_drawer_siblings_list,
#endif
	NULL
};

static struct attribute *default_attrs[] = {
	&dev_attr_physical_package_id.attr,
#ifdef TOPOLOGY_DIE_SYSFS
	&dev_attr_die_id.attr,
#endif
#ifdef TOPOLOGY_CLUSTER_SYSFS
	&dev_attr_cluster_id.attr,
#endif
	&dev_attr_core_id.attr,
#ifdef TOPOLOGY_BOOK_SYSFS
	&dev_attr_book_id.attr,
#endif
#ifdef TOPOLOGY_DRAWER_SYSFS
	&dev_attr_drawer_id.attr,
#endif
	&dev_attr_ppin.attr,
	NULL
};

static umode_t topology_is_visible(struct kobject *kobj,
				   struct attribute *attr, int unused)
{
	if (attr == &dev_attr_ppin.attr && !topology_ppin(kobj_to_dev(kobj)->id))
		return 0;

	return attr->mode;
}

static const struct attribute_group topology_attr_group = {
	.attrs = default_attrs,
	.bin_attrs = bin_attrs,
	.is_visible = topology_is_visible,
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

static int __init topology_sysfs_init(void)
{
	return cpuhp_setup_state(CPUHP_TOPOLOGY_PREPARE,
				 "base/topology:prepare", topology_add_dev,
				 topology_remove_dev);
}

device_initcall(topology_sysfs_init);
