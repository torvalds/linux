// SPDX-License-Identifier: GPL-2.0
/*
 * Intel Uncore Frequency Control: Common code implementation
 * Copyright (c) 2022, Intel Corporation.
 * All rights reserved.
 *
 */
#include <linux/cpu.h>
#include <linux/module.h>
#include "uncore-frequency-common.h"

/* Mutex to control all mutual exclusions */
static DEFINE_MUTEX(uncore_lock);
/* Root of the all uncore sysfs kobjs */
static struct kobject *uncore_root_kobj;
/* uncore instance count */
static int uncore_instance_count;

/* callbacks for actual HW read/write */
static int (*uncore_read)(struct uncore_data *data, unsigned int *min, unsigned int *max);
static int (*uncore_write)(struct uncore_data *data, unsigned int input, unsigned int min_max);
static int (*uncore_read_freq)(struct uncore_data *data, unsigned int *freq);

static ssize_t show_min_max_freq_khz(struct uncore_data *data,
				      char *buf, int min_max)
{
	unsigned int min, max;
	int ret;

	mutex_lock(&uncore_lock);
	ret = uncore_read(data, &min, &max);
	mutex_unlock(&uncore_lock);
	if (ret)
		return ret;

	if (min_max)
		return sprintf(buf, "%u\n", max);

	return sprintf(buf, "%u\n", min);
}

static ssize_t store_min_max_freq_khz(struct uncore_data *data,
				      const char *buf, ssize_t count,
				      int min_max)
{
	unsigned int input;
	int ret;

	if (kstrtouint(buf, 10, &input))
		return -EINVAL;

	mutex_lock(&uncore_lock);
	ret = uncore_write(data, input, min_max);
	mutex_unlock(&uncore_lock);

	if (ret)
		return ret;

	return count;
}

static ssize_t show_perf_status_freq_khz(struct uncore_data *data, char *buf)
{
	unsigned int freq;
	int ret;

	mutex_lock(&uncore_lock);
	ret = uncore_read_freq(data, &freq);
	mutex_unlock(&uncore_lock);
	if (ret)
		return ret;

	return sprintf(buf, "%u\n", freq);
}

#define store_uncore_min_max(name, min_max)				\
	static ssize_t store_##name(struct device *dev,		\
				     struct device_attribute *attr,	\
				     const char *buf, size_t count)	\
	{								\
		struct uncore_data *data = container_of(attr, struct uncore_data, name##_dev_attr);\
									\
		return store_min_max_freq_khz(data, buf, count,	\
					      min_max);		\
	}

#define show_uncore_min_max(name, min_max)				\
	static ssize_t show_##name(struct device *dev,		\
				    struct device_attribute *attr, char *buf)\
	{                                                               \
		struct uncore_data *data = container_of(attr, struct uncore_data, name##_dev_attr);\
									\
		return show_min_max_freq_khz(data, buf, min_max);	\
	}

#define show_uncore_perf_status(name)					\
	static ssize_t show_##name(struct device *dev,		\
				   struct device_attribute *attr, char *buf)\
	{                                                               \
		struct uncore_data *data = container_of(attr, struct uncore_data, name##_dev_attr);\
									\
		return show_perf_status_freq_khz(data, buf); \
	}

store_uncore_min_max(min_freq_khz, 0);
store_uncore_min_max(max_freq_khz, 1);

show_uncore_min_max(min_freq_khz, 0);
show_uncore_min_max(max_freq_khz, 1);

show_uncore_perf_status(current_freq_khz);

#define show_uncore_data(member_name)					\
	static ssize_t show_##member_name(struct device *dev,	\
					   struct device_attribute *attr, char *buf)\
	{                                                               \
		struct uncore_data *data = container_of(attr, struct uncore_data,\
							  member_name##_dev_attr);\
									\
		return sysfs_emit(buf, "%u\n",				\
				 data->member_name);			\
	}								\

show_uncore_data(initial_min_freq_khz);
show_uncore_data(initial_max_freq_khz);

#define init_attribute_rw(_name)					\
	do {								\
		sysfs_attr_init(&data->_name##_dev_attr.attr);	\
		data->_name##_dev_attr.show = show_##_name;		\
		data->_name##_dev_attr.store = store_##_name;		\
		data->_name##_dev_attr.attr.name = #_name;		\
		data->_name##_dev_attr.attr.mode = 0644;		\
	} while (0)

#define init_attribute_ro(_name)					\
	do {								\
		sysfs_attr_init(&data->_name##_dev_attr.attr);	\
		data->_name##_dev_attr.show = show_##_name;		\
		data->_name##_dev_attr.store = NULL;			\
		data->_name##_dev_attr.attr.name = #_name;		\
		data->_name##_dev_attr.attr.mode = 0444;		\
	} while (0)

#define init_attribute_root_ro(_name)					\
	do {								\
		sysfs_attr_init(&data->_name##_dev_attr.attr);	\
		data->_name##_dev_attr.show = show_##_name;		\
		data->_name##_dev_attr.store = NULL;			\
		data->_name##_dev_attr.attr.name = #_name;		\
		data->_name##_dev_attr.attr.mode = 0400;		\
	} while (0)

static int create_attr_group(struct uncore_data *data, char *name)
{
	int ret, index = 0;

	init_attribute_rw(max_freq_khz);
	init_attribute_rw(min_freq_khz);
	init_attribute_ro(initial_min_freq_khz);
	init_attribute_ro(initial_max_freq_khz);
	init_attribute_root_ro(current_freq_khz);

	data->uncore_attrs[index++] = &data->max_freq_khz_dev_attr.attr;
	data->uncore_attrs[index++] = &data->min_freq_khz_dev_attr.attr;
	data->uncore_attrs[index++] = &data->initial_min_freq_khz_dev_attr.attr;
	data->uncore_attrs[index++] = &data->initial_max_freq_khz_dev_attr.attr;
	data->uncore_attrs[index++] = &data->current_freq_khz_dev_attr.attr;
	data->uncore_attrs[index] = NULL;

	data->uncore_attr_group.name = name;
	data->uncore_attr_group.attrs = data->uncore_attrs;
	ret = sysfs_create_group(uncore_root_kobj, &data->uncore_attr_group);

	return ret;
}

static void delete_attr_group(struct uncore_data *data, char *name)
{
	sysfs_remove_group(uncore_root_kobj, &data->uncore_attr_group);
}

int uncore_freq_add_entry(struct uncore_data *data, int cpu)
{
	int ret = 0;

	mutex_lock(&uncore_lock);
	if (data->valid) {
		/* control cpu changed */
		data->control_cpu = cpu;
		goto uncore_unlock;
	}

	sprintf(data->name, "package_%02d_die_%02d", data->package_id, data->die_id);

	uncore_read(data, &data->initial_min_freq_khz, &data->initial_max_freq_khz);

	ret = create_attr_group(data, data->name);
	if (!ret) {
		data->control_cpu = cpu;
		data->valid = true;
	}

uncore_unlock:
	mutex_unlock(&uncore_lock);

	return ret;
}
EXPORT_SYMBOL_NS_GPL(uncore_freq_add_entry, INTEL_UNCORE_FREQUENCY);

void uncore_freq_remove_die_entry(struct uncore_data *data)
{
	mutex_lock(&uncore_lock);
	delete_attr_group(data, data->name);
	data->control_cpu = -1;
	data->valid = false;
	mutex_unlock(&uncore_lock);
}
EXPORT_SYMBOL_NS_GPL(uncore_freq_remove_die_entry, INTEL_UNCORE_FREQUENCY);

int uncore_freq_common_init(int (*read_control_freq)(struct uncore_data *data, unsigned int *min, unsigned int *max),
			     int (*write_control_freq)(struct uncore_data *data, unsigned int input, unsigned int set_max),
			     int (*read_freq)(struct uncore_data *data, unsigned int *freq))
{
	mutex_lock(&uncore_lock);

	uncore_read = read_control_freq;
	uncore_write = write_control_freq;
	uncore_read_freq = read_freq;

	if (!uncore_root_kobj) {
		struct device *dev_root = bus_get_dev_root(&cpu_subsys);

		if (dev_root) {
			uncore_root_kobj = kobject_create_and_add("intel_uncore_frequency",
								  &dev_root->kobj);
			put_device(dev_root);
		}
	}
	if (uncore_root_kobj)
		++uncore_instance_count;
	mutex_unlock(&uncore_lock);

	return uncore_root_kobj ? 0 : -ENOMEM;
}
EXPORT_SYMBOL_NS_GPL(uncore_freq_common_init, INTEL_UNCORE_FREQUENCY);

void uncore_freq_common_exit(void)
{
	mutex_lock(&uncore_lock);
	--uncore_instance_count;
	if (!uncore_instance_count) {
		kobject_put(uncore_root_kobj);
		uncore_root_kobj = NULL;
	}
	mutex_unlock(&uncore_lock);
}
EXPORT_SYMBOL_NS_GPL(uncore_freq_common_exit, INTEL_UNCORE_FREQUENCY);


MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Intel Uncore Frequency Common Module");
