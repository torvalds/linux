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

static DEFINE_IDA(intel_uncore_ida);

/* callbacks for actual HW read/write */
static int (*uncore_read)(struct uncore_data *data, unsigned int *value, enum uncore_index index);
static int (*uncore_write)(struct uncore_data *data, unsigned int input, enum uncore_index index);

static ssize_t show_domain_id(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct uncore_data *data = container_of(attr, struct uncore_data, domain_id_kobj_attr);

	return sprintf(buf, "%u\n", data->domain_id);
}

static ssize_t show_fabric_cluster_id(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct uncore_data *data = container_of(attr, struct uncore_data, fabric_cluster_id_kobj_attr);

	return sprintf(buf, "%u\n", data->cluster_id);
}

static ssize_t show_package_id(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct uncore_data *data = container_of(attr, struct uncore_data, package_id_kobj_attr);

	return sprintf(buf, "%u\n", data->package_id);
}

static ssize_t show_attr(struct uncore_data *data, char *buf, enum uncore_index index)
{
	unsigned int value;
	int ret;

	mutex_lock(&uncore_lock);
	ret = uncore_read(data, &value, index);
	mutex_unlock(&uncore_lock);
	if (ret)
		return ret;

	return sprintf(buf, "%u\n", value);
}

static ssize_t store_attr(struct uncore_data *data, const char *buf, ssize_t count,
			  enum uncore_index index)
{
	unsigned int input = 0;
	int ret;

	if (index == UNCORE_INDEX_EFF_LAT_CTRL_HIGH_THRESHOLD_ENABLE) {
		if (kstrtobool(buf, (bool *)&input))
			return -EINVAL;
	} else {
		if (kstrtouint(buf, 10, &input))
			return -EINVAL;
	}

	mutex_lock(&uncore_lock);
	ret = uncore_write(data, input, index);
	mutex_unlock(&uncore_lock);

	if (ret)
		return ret;

	return count;
}

#define store_uncore_attr(name, index)					\
	static ssize_t store_##name(struct kobject *kobj,		\
				     struct kobj_attribute *attr,	\
				     const char *buf, size_t count)	\
	{								\
		struct uncore_data *data = container_of(attr, struct uncore_data, name##_kobj_attr);\
									\
		return store_attr(data, buf, count, index);		\
	}

#define show_uncore_attr(name, index)					\
	static ssize_t show_##name(struct kobject *kobj,		\
				    struct kobj_attribute *attr, char *buf)\
	{                                                               \
		struct uncore_data *data = container_of(attr, struct uncore_data, name##_kobj_attr);\
									\
		return show_attr(data, buf, index);			\
	}

store_uncore_attr(min_freq_khz, UNCORE_INDEX_MIN_FREQ);
store_uncore_attr(max_freq_khz, UNCORE_INDEX_MAX_FREQ);

show_uncore_attr(min_freq_khz, UNCORE_INDEX_MIN_FREQ);
show_uncore_attr(max_freq_khz, UNCORE_INDEX_MAX_FREQ);

show_uncore_attr(current_freq_khz, UNCORE_INDEX_CURRENT_FREQ);

store_uncore_attr(elc_low_threshold_percent, UNCORE_INDEX_EFF_LAT_CTRL_LOW_THRESHOLD);
store_uncore_attr(elc_high_threshold_percent, UNCORE_INDEX_EFF_LAT_CTRL_HIGH_THRESHOLD);
store_uncore_attr(elc_high_threshold_enable,
		  UNCORE_INDEX_EFF_LAT_CTRL_HIGH_THRESHOLD_ENABLE);
store_uncore_attr(elc_floor_freq_khz, UNCORE_INDEX_EFF_LAT_CTRL_FREQ);

show_uncore_attr(elc_low_threshold_percent, UNCORE_INDEX_EFF_LAT_CTRL_LOW_THRESHOLD);
show_uncore_attr(elc_high_threshold_percent, UNCORE_INDEX_EFF_LAT_CTRL_HIGH_THRESHOLD);
show_uncore_attr(elc_high_threshold_enable,
		 UNCORE_INDEX_EFF_LAT_CTRL_HIGH_THRESHOLD_ENABLE);
show_uncore_attr(elc_floor_freq_khz, UNCORE_INDEX_EFF_LAT_CTRL_FREQ);

#define show_uncore_data(member_name)					\
	static ssize_t show_##member_name(struct kobject *kobj,	\
					   struct kobj_attribute *attr, char *buf)\
	{                                                               \
		struct uncore_data *data = container_of(attr, struct uncore_data,\
							  member_name##_kobj_attr);\
									\
		return sysfs_emit(buf, "%u\n",				\
				 data->member_name);			\
	}								\

show_uncore_data(initial_min_freq_khz);
show_uncore_data(initial_max_freq_khz);

#define init_attribute_rw(_name)					\
	do {								\
		sysfs_attr_init(&data->_name##_kobj_attr.attr);	\
		data->_name##_kobj_attr.show = show_##_name;		\
		data->_name##_kobj_attr.store = store_##_name;		\
		data->_name##_kobj_attr.attr.name = #_name;		\
		data->_name##_kobj_attr.attr.mode = 0644;		\
	} while (0)

#define init_attribute_ro(_name)					\
	do {								\
		sysfs_attr_init(&data->_name##_kobj_attr.attr);	\
		data->_name##_kobj_attr.show = show_##_name;		\
		data->_name##_kobj_attr.store = NULL;			\
		data->_name##_kobj_attr.attr.name = #_name;		\
		data->_name##_kobj_attr.attr.mode = 0444;		\
	} while (0)

#define init_attribute_root_ro(_name)					\
	do {								\
		sysfs_attr_init(&data->_name##_kobj_attr.attr);	\
		data->_name##_kobj_attr.show = show_##_name;		\
		data->_name##_kobj_attr.store = NULL;			\
		data->_name##_kobj_attr.attr.name = #_name;		\
		data->_name##_kobj_attr.attr.mode = 0400;		\
	} while (0)

static int create_attr_group(struct uncore_data *data, char *name)
{
	int ret, index = 0;
	unsigned int val;

	init_attribute_rw(max_freq_khz);
	init_attribute_rw(min_freq_khz);
	init_attribute_ro(initial_min_freq_khz);
	init_attribute_ro(initial_max_freq_khz);
	init_attribute_root_ro(current_freq_khz);

	if (data->domain_id != UNCORE_DOMAIN_ID_INVALID) {
		init_attribute_root_ro(domain_id);
		data->uncore_attrs[index++] = &data->domain_id_kobj_attr.attr;
		init_attribute_root_ro(fabric_cluster_id);
		data->uncore_attrs[index++] = &data->fabric_cluster_id_kobj_attr.attr;
		init_attribute_root_ro(package_id);
		data->uncore_attrs[index++] = &data->package_id_kobj_attr.attr;
	}

	data->uncore_attrs[index++] = &data->max_freq_khz_kobj_attr.attr;
	data->uncore_attrs[index++] = &data->min_freq_khz_kobj_attr.attr;
	data->uncore_attrs[index++] = &data->initial_min_freq_khz_kobj_attr.attr;
	data->uncore_attrs[index++] = &data->initial_max_freq_khz_kobj_attr.attr;

	ret = uncore_read(data, &val, UNCORE_INDEX_CURRENT_FREQ);
	if (!ret)
		data->uncore_attrs[index++] = &data->current_freq_khz_kobj_attr.attr;

	ret = uncore_read(data, &val, UNCORE_INDEX_EFF_LAT_CTRL_LOW_THRESHOLD);
	if (!ret) {
		init_attribute_rw(elc_low_threshold_percent);
		init_attribute_rw(elc_high_threshold_percent);
		init_attribute_rw(elc_high_threshold_enable);
		init_attribute_rw(elc_floor_freq_khz);

		data->uncore_attrs[index++] = &data->elc_low_threshold_percent_kobj_attr.attr;
		data->uncore_attrs[index++] = &data->elc_high_threshold_percent_kobj_attr.attr;
		data->uncore_attrs[index++] =
			&data->elc_high_threshold_enable_kobj_attr.attr;
		data->uncore_attrs[index++] = &data->elc_floor_freq_khz_kobj_attr.attr;
	}

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

	if (data->domain_id != UNCORE_DOMAIN_ID_INVALID) {
		ret = ida_alloc(&intel_uncore_ida, GFP_KERNEL);
		if (ret < 0)
			goto uncore_unlock;

		data->instance_id = ret;
		sprintf(data->name, "uncore%02d", ret);
	} else {
		sprintf(data->name, "package_%02d_die_%02d", data->package_id, data->die_id);
	}

	uncore_read(data, &data->initial_min_freq_khz, UNCORE_INDEX_MIN_FREQ);
	uncore_read(data, &data->initial_max_freq_khz, UNCORE_INDEX_MAX_FREQ);

	ret = create_attr_group(data, data->name);
	if (ret) {
		if (data->domain_id != UNCORE_DOMAIN_ID_INVALID)
			ida_free(&intel_uncore_ida, data->instance_id);
	} else {
		data->control_cpu = cpu;
		data->valid = true;
	}

uncore_unlock:
	mutex_unlock(&uncore_lock);

	return ret;
}
EXPORT_SYMBOL_NS_GPL(uncore_freq_add_entry, "INTEL_UNCORE_FREQUENCY");

void uncore_freq_remove_die_entry(struct uncore_data *data)
{
	mutex_lock(&uncore_lock);
	delete_attr_group(data, data->name);
	data->control_cpu = -1;
	data->valid = false;
	if (data->domain_id != UNCORE_DOMAIN_ID_INVALID)
		ida_free(&intel_uncore_ida, data->instance_id);

	mutex_unlock(&uncore_lock);
}
EXPORT_SYMBOL_NS_GPL(uncore_freq_remove_die_entry, "INTEL_UNCORE_FREQUENCY");

int uncore_freq_common_init(int (*read)(struct uncore_data *data, unsigned int *value,
					enum uncore_index index),
			    int (*write)(struct uncore_data *data, unsigned int input,
					 enum uncore_index index))
{
	mutex_lock(&uncore_lock);

	uncore_read = read;
	uncore_write = write;

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
EXPORT_SYMBOL_NS_GPL(uncore_freq_common_init, "INTEL_UNCORE_FREQUENCY");

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
EXPORT_SYMBOL_NS_GPL(uncore_freq_common_exit, "INTEL_UNCORE_FREQUENCY");


MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Intel Uncore Frequency Common Module");
