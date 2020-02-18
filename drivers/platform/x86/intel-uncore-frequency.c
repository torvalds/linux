// SPDX-License-Identifier: GPL-2.0
/*
 * Intel Uncore Frequency Setting
 * Copyright (c) 2019, Intel Corporation.
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
#define UNCORE_FREQ_KHZ_MULTIPLIER		100000

/**
 * struct uncore_data -	Encapsulate all uncore data
 * @stored_uncore_data:	Last user changed MSR 620 value, which will be restored
 *			on system resume.
 * @initial_min_freq_khz: Sampled minimum uncore frequency at driver init
 * @initial_max_freq_khz: Sampled maximum uncore frequency at driver init
 * @control_cpu:	Designated CPU for a die to read/write
 * @valid:		Mark the data valid/invalid
 *
 * This structure is used to encapsulate all data related to uncore sysfs
 * settings for a die/package.
 */
struct uncore_data {
	struct kobject kobj;
	struct completion kobj_unregister;
	u64 stored_uncore_data;
	u32 initial_min_freq_khz;
	u32 initial_max_freq_khz;
	int control_cpu;
	bool valid;
};

#define to_uncore_data(a) container_of(a, struct uncore_data, kobj)

/* Max instances for uncore data, one for each die */
static int uncore_max_entries __read_mostly;
/* Storage for uncore data for all instances */
static struct uncore_data *uncore_instances;
/* Root of the all uncore sysfs kobjs */
struct kobject *uncore_root_kobj;
/* Stores the CPU mask of the target CPUs to use during uncore read/write */
static cpumask_t uncore_cpu_mask;
/* CPU online callback register instance */
static enum cpuhp_state uncore_hp_state __read_mostly;
/* Mutex to control all mutual exclusions */
static DEFINE_MUTEX(uncore_lock);

struct uncore_attr {
	struct attribute attr;
	ssize_t (*show)(struct kobject *kobj,
			struct attribute *attr, char *buf);
	ssize_t (*store)(struct kobject *kobj,
			 struct attribute *attr, const char *c, ssize_t count);
};

#define define_one_uncore_ro(_name) \
static struct uncore_attr _name = \
__ATTR(_name, 0444, show_##_name, NULL)

#define define_one_uncore_rw(_name) \
static struct uncore_attr _name = \
__ATTR(_name, 0644, show_##_name, store_##_name)

#define show_uncore_data(member_name)					\
	static ssize_t show_##member_name(struct kobject *kobj,         \
					  struct attribute *attr,	\
					  char *buf)			\
	{                                                               \
		struct uncore_data *data = to_uncore_data(kobj);	\
		return scnprintf(buf, PAGE_SIZE, "%u\n",		\
				 data->member_name);			\
	}								\
	define_one_uncore_ro(member_name)

show_uncore_data(initial_min_freq_khz);
show_uncore_data(initial_max_freq_khz);

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

	mutex_lock(&uncore_lock);

	if (data->control_cpu < 0) {
		ret = -ENXIO;
		goto finish_write;
	}

	input /= UNCORE_FREQ_KHZ_MULTIPLIER;
	if (!input || input > 0x7F) {
		ret = -EINVAL;
		goto finish_write;
	}

	ret = rdmsrl_on_cpu(data->control_cpu, MSR_UNCORE_RATIO_LIMIT, &cap);
	if (ret)
		goto finish_write;

	if (set_max) {
		cap &= ~0x7F;
		cap |= input;
	} else  {
		cap &= ~GENMASK(14, 8);
		cap |= (input << 8);
	}

	ret = wrmsrl_on_cpu(data->control_cpu, MSR_UNCORE_RATIO_LIMIT, cap);
	if (ret)
		goto finish_write;

	data->stored_uncore_data = cap;

finish_write:
	mutex_unlock(&uncore_lock);

	return ret;
}

static ssize_t store_min_max_freq_khz(struct kobject *kobj,
				      struct attribute *attr,
				      const char *buf, ssize_t count,
				      int min_max)
{
	struct uncore_data *data = to_uncore_data(kobj);
	unsigned int input;

	if (kstrtouint(buf, 10, &input))
		return -EINVAL;

	uncore_write_ratio(data, input, min_max);

	return count;
}

static ssize_t show_min_max_freq_khz(struct kobject *kobj,
				     struct attribute *attr,
				     char *buf, int min_max)
{
	struct uncore_data *data = to_uncore_data(kobj);
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
	static ssize_t store_##name(struct kobject *kobj,		\
				    struct attribute *attr,		\
				    const char *buf, ssize_t count)	\
	{                                                               \
									\
		return store_min_max_freq_khz(kobj, attr, buf, count,	\
					      min_max);			\
	}

#define show_uncore_min_max(name, min_max)				\
	static ssize_t show_##name(struct kobject *kobj,		\
				   struct attribute *attr, char *buf)	\
	{                                                               \
									\
		return show_min_max_freq_khz(kobj, attr, buf, min_max); \
	}

store_uncore_min_max(min_freq_khz, 0);
store_uncore_min_max(max_freq_khz, 1);

show_uncore_min_max(min_freq_khz, 0);
show_uncore_min_max(max_freq_khz, 1);

define_one_uncore_rw(min_freq_khz);
define_one_uncore_rw(max_freq_khz);

static struct attribute *uncore_attrs[] = {
	&initial_min_freq_khz.attr,
	&initial_max_freq_khz.attr,
	&max_freq_khz.attr,
	&min_freq_khz.attr,
	NULL
};

static void uncore_sysfs_entry_release(struct kobject *kobj)
{
	struct uncore_data *data = to_uncore_data(kobj);

	complete(&data->kobj_unregister);
}

static struct kobj_type uncore_ktype = {
	.release = uncore_sysfs_entry_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_attrs = uncore_attrs,
};

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
		char str[64];
		int ret;

		memset(data, 0, sizeof(*data));
		sprintf(str, "package_%02d_die_%02d",
			topology_physical_package_id(cpu),
			topology_die_id(cpu));

		uncore_read_ratio(data, &data->initial_min_freq_khz,
				  &data->initial_max_freq_khz);

		init_completion(&data->kobj_unregister);

		ret = kobject_init_and_add(&data->kobj, &uncore_ktype,
					   uncore_root_kobj, str);
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
	if (data)
		data->control_cpu = -1;
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

#define ICPU(model)     { X86_VENDOR_INTEL, 6, model, X86_FEATURE_ANY, }

static const struct x86_cpu_id intel_uncore_cpu_ids[] = {
	ICPU(INTEL_FAM6_BROADWELL_G),
	ICPU(INTEL_FAM6_BROADWELL_X),
	ICPU(INTEL_FAM6_BROADWELL_D),
	ICPU(INTEL_FAM6_SKYLAKE_X),
	ICPU(INTEL_FAM6_ICELAKE_X),
	ICPU(INTEL_FAM6_ICELAKE_D),
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
	int i;

	unregister_pm_notifier(&uncore_pm_nb);
	cpuhp_remove_state(uncore_hp_state);
	for (i = 0; i < uncore_max_entries; ++i) {
		if (uncore_instances[i].valid) {
			kobject_put(&uncore_instances[i].kobj);
			wait_for_completion(&uncore_instances[i].kobj_unregister);
		}
	}
	kobject_put(uncore_root_kobj);
	kfree(uncore_instances);
}
module_exit(intel_uncore_exit)

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Intel Uncore Frequency Limits Driver");
