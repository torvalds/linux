// SPDX-License-Identifier: GPL-2.0
/*
 * Intel Uncore Frequency Setting
 * Copyright (c) 2022, Intel Corporation.
 * All rights reserved.
 *
 * Provide interface to set MSR 620 at a granularity of per die. On CPU online,
 * one control CPU is identified per die to read/write limit. This control CPU
 * is changed, if the CPU state is changed to offline. When the last CPU is
 * offline in a die then remove the sysfs object for that die.
 * The majority of actual code is related to sysfs create and read/write
 * attributes.
 *
 * Author: Srinivas Pandruvada <srinivas.pandruvada@linux.intel.com>
 */

#include <linux/cpu.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/suspend.h>
#include <asm/cpu_device_id.h>
#include <asm/intel-family.h>

#define MSR_UNCORE_RATIO_LIMIT			0x620
#define MSR_UNCORE_PERF_STATUS			0x621
#define UNCORE_FREQ_KHZ_MULTIPLIER		100000

/**
 * struct uncore_data -	Encapsulate all uncore data
 * @stored_uncore_data:	Last user changed MSR 620 value, which will be restored
 *			on system resume.
 * @initial_min_freq_khz: Sampled minimum uncore frequency at driver init
 * @initial_max_freq_khz: Sampled maximum uncore frequency at driver init
 * @control_cpu:	Designated CPU for a die to read/write
 * @valid:		Mark the data valid/invalid
 * @package_id:	Package id for this instance
 * @die_id:		Die id for this instance
 * @name:		Sysfs entry name for this instance
 * @uncore_attr_group:	Attribute group storage
 * @max_freq_khz_dev_attr: Storage for device attribute max_freq_khz
 * @mix_freq_khz_dev_attr: Storage for device attribute min_freq_khz
 * @initial_max_freq_khz_dev_attr: Storage for device attribute initial_max_freq_khz
 * @initial_min_freq_khz_dev_attr: Storage for device attribute initial_min_freq_khz
 * @current_freq_khz_dev_attr: Storage for device attribute current_freq_khz
 * @uncore_attrs:	Attribute storage for group creation
 *
 * This structure is used to encapsulate all data related to uncore sysfs
 * settings for a die/package.
 */
struct uncore_data {
	u64 stored_uncore_data;
	u32 initial_min_freq_khz;
	u32 initial_max_freq_khz;
	int control_cpu;
	bool valid;
	int package_id;
	int die_id;
	char name[32];

	struct attribute_group uncore_attr_group;
	struct device_attribute max_freq_khz_dev_attr;
	struct device_attribute min_freq_khz_dev_attr;
	struct device_attribute initial_max_freq_khz_dev_attr;
	struct device_attribute initial_min_freq_khz_dev_attr;
	struct device_attribute current_freq_khz_dev_attr;
	struct attribute *uncore_attrs[6];
};

/* Max instances for uncore data, one for each die */
static int uncore_max_entries __read_mostly;
/* Storage for uncore data for all instances */
static struct uncore_data *uncore_instances;
/* Root of the all uncore sysfs kobjs */
static struct kobject *uncore_root_kobj;
/* Stores the CPU mask of the target CPUs to use during uncore read/write */
static cpumask_t uncore_cpu_mask;
/* CPU online callback register instance */
static enum cpuhp_state uncore_hp_state __read_mostly;
/* Mutex to control all mutual exclusions */
static DEFINE_MUTEX(uncore_lock);

/* Common function to read MSR 0x620 and read min/max */
static int uncore_read_ratio(struct uncore_data *data, unsigned int *min,
			     unsigned int *max)
{
	u64 cap;
	int ret;

	if (data->control_cpu < 0)
		return -ENXIO;

	ret = rdmsrl_on_cpu(data->control_cpu, MSR_UNCORE_RATIO_LIMIT, &cap);
	if (ret)
		return ret;

	*max = (cap & 0x7F) * UNCORE_FREQ_KHZ_MULTIPLIER;
	*min = ((cap & GENMASK(14, 8)) >> 8) * UNCORE_FREQ_KHZ_MULTIPLIER;

	return 0;
}

/* Common function to set min/max ratios to be used by sysfs callbacks */
static int uncore_write_ratio(struct uncore_data *data, unsigned int input,
			      int set_max)
{
	int ret;
	u64 cap;

	if (data->control_cpu < 0)
		return -ENXIO;

	input /= UNCORE_FREQ_KHZ_MULTIPLIER;
	if (!input || input > 0x7F)
		return -EINVAL;

	ret = rdmsrl_on_cpu(data->control_cpu, MSR_UNCORE_RATIO_LIMIT, &cap);
	if (ret)
		return ret;

	if (set_max) {
		cap &= ~0x7F;
		cap |= input;
	} else  {
		cap &= ~GENMASK(14, 8);
		cap |= (input << 8);
	}

	ret = wrmsrl_on_cpu(data->control_cpu, MSR_UNCORE_RATIO_LIMIT, cap);
	if (ret)
		return ret;

	data->stored_uncore_data = cap;

	return 0;
}

static int uncore_read_freq(struct uncore_data *data, unsigned int *freq)
{
	u64 ratio;
	int ret;

	ret = rdmsrl_on_cpu(data->control_cpu, MSR_UNCORE_PERF_STATUS, &ratio);
	if (ret)
		return ret;

	*freq = (ratio & 0x7F) * UNCORE_FREQ_KHZ_MULTIPLIER;

	return 0;
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

static ssize_t store_min_max_freq_khz(struct uncore_data *data,
				      const char *buf, ssize_t count,
				      int min_max)
{
	unsigned int input;

	if (kstrtouint(buf, 10, &input))
		return -EINVAL;

	mutex_lock(&uncore_lock);
	uncore_write_ratio(data, input, min_max);
	mutex_unlock(&uncore_lock);

	return count;
}

static ssize_t show_min_max_freq_khz(struct uncore_data *data,
				     char *buf, int min_max)
{
	unsigned int min, max;
	int ret;

	mutex_lock(&uncore_lock);
	ret = uncore_read_ratio(data, &min, &max);
	mutex_unlock(&uncore_lock);
	if (ret)
		return ret;

	if (min_max)
		return sprintf(buf, "%u\n", max);

	return sprintf(buf, "%u\n", min);
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
		return scnprintf(buf, PAGE_SIZE, "%u\n",		\
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

/* Caller provides protection */
static struct uncore_data *uncore_get_instance(unsigned int cpu)
{
	int id = topology_logical_die_id(cpu);

	if (id >= 0 && id < uncore_max_entries)
		return &uncore_instances[id];

	return NULL;
}

static void uncore_add_die_entry(int cpu)
{
	struct uncore_data *data;

	mutex_lock(&uncore_lock);
	data = uncore_get_instance(cpu);
	if (!data) {
		mutex_unlock(&uncore_lock);
		return;
	}

	if (data->valid) {
		/* control cpu changed */
		data->control_cpu = cpu;
	} else {
		int ret;

		memset(data, 0, sizeof(*data));
		sprintf(data->name, "package_%02d_die_%02d",
			topology_physical_package_id(cpu),
			topology_die_id(cpu));

		uncore_read_ratio(data, &data->initial_min_freq_khz,
				  &data->initial_max_freq_khz);

		ret = create_attr_group(data, data->name);
		if (!ret) {
			data->control_cpu = cpu;
			data->valid = true;
		}
	}
	mutex_unlock(&uncore_lock);
}

/* Last CPU in this die is offline, make control cpu invalid */
static void uncore_remove_die_entry(int cpu)
{
	struct uncore_data *data;

	mutex_lock(&uncore_lock);
	data = uncore_get_instance(cpu);
	if (data) {
		delete_attr_group(data, data->name);
		data->control_cpu = -1;
		data->valid = false;
	}
	mutex_unlock(&uncore_lock);
}

static int uncore_event_cpu_online(unsigned int cpu)
{
	int target;

	/* Check if there is an online cpu in the package for uncore MSR */
	target = cpumask_any_and(&uncore_cpu_mask, topology_die_cpumask(cpu));
	if (target < nr_cpu_ids)
		return 0;

	/* Use this CPU on this die as a control CPU */
	cpumask_set_cpu(cpu, &uncore_cpu_mask);
	uncore_add_die_entry(cpu);

	return 0;
}

static int uncore_event_cpu_offline(unsigned int cpu)
{
	int target;

	/* Check if existing cpu is used for uncore MSRs */
	if (!cpumask_test_and_clear_cpu(cpu, &uncore_cpu_mask))
		return 0;

	/* Find a new cpu to set uncore MSR */
	target = cpumask_any_but(topology_die_cpumask(cpu), cpu);

	if (target < nr_cpu_ids) {
		cpumask_set_cpu(target, &uncore_cpu_mask);
		uncore_add_die_entry(target);
	} else {
		uncore_remove_die_entry(cpu);
	}

	return 0;
}

static int uncore_pm_notify(struct notifier_block *nb, unsigned long mode,
			    void *_unused)
{
	int cpu;

	switch (mode) {
	case PM_POST_HIBERNATION:
	case PM_POST_RESTORE:
	case PM_POST_SUSPEND:
		for_each_cpu(cpu, &uncore_cpu_mask) {
			struct uncore_data *data;
			int ret;

			data = uncore_get_instance(cpu);
			if (!data || !data->valid || !data->stored_uncore_data)
				continue;

			ret = wrmsrl_on_cpu(cpu, MSR_UNCORE_RATIO_LIMIT,
					    data->stored_uncore_data);
			if (ret)
				return ret;
		}
		break;
	default:
		break;
	}
	return 0;
}

static struct notifier_block uncore_pm_nb = {
	.notifier_call = uncore_pm_notify,
};

static const struct x86_cpu_id intel_uncore_cpu_ids[] = {
	X86_MATCH_INTEL_FAM6_MODEL(BROADWELL_G,	NULL),
	X86_MATCH_INTEL_FAM6_MODEL(BROADWELL_X,	NULL),
	X86_MATCH_INTEL_FAM6_MODEL(BROADWELL_D,	NULL),
	X86_MATCH_INTEL_FAM6_MODEL(SKYLAKE_X,	NULL),
	X86_MATCH_INTEL_FAM6_MODEL(ICELAKE_X,	NULL),
	X86_MATCH_INTEL_FAM6_MODEL(ICELAKE_D,	NULL),
	X86_MATCH_INTEL_FAM6_MODEL(SAPPHIRERAPIDS_X, NULL),
	{}
};

static int __init intel_uncore_init(void)
{
	const struct x86_cpu_id *id;
	int ret;

	id = x86_match_cpu(intel_uncore_cpu_ids);
	if (!id)
		return -ENODEV;

	uncore_max_entries = topology_max_packages() *
					topology_max_die_per_package();
	uncore_instances = kcalloc(uncore_max_entries,
				   sizeof(*uncore_instances), GFP_KERNEL);
	if (!uncore_instances)
		return -ENOMEM;

	uncore_root_kobj = kobject_create_and_add("intel_uncore_frequency",
						  &cpu_subsys.dev_root->kobj);
	if (!uncore_root_kobj) {
		ret = -ENOMEM;
		goto err_free;
	}

	ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN,
				"platform/x86/uncore-freq:online",
				uncore_event_cpu_online,
				uncore_event_cpu_offline);
	if (ret < 0)
		goto err_rem_kobj;

	uncore_hp_state = ret;

	ret = register_pm_notifier(&uncore_pm_nb);
	if (ret)
		goto err_rem_state;

	return 0;

err_rem_state:
	cpuhp_remove_state(uncore_hp_state);
err_rem_kobj:
	kobject_put(uncore_root_kobj);
err_free:
	kfree(uncore_instances);

	return ret;
}
module_init(intel_uncore_init)

static void __exit intel_uncore_exit(void)
{
	unregister_pm_notifier(&uncore_pm_nb);
	cpuhp_remove_state(uncore_hp_state);
	kobject_put(uncore_root_kobj);
	kfree(uncore_instances);
}
module_exit(intel_uncore_exit)

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Intel Uncore Frequency Limits Driver");
