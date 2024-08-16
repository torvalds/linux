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

#include <linux/bitfield.h>
#include <linux/cpu.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/suspend.h>
#include <asm/cpu_device_id.h>
#include <asm/intel-family.h>

#include "uncore-frequency-common.h"

/* Max instances for uncore data, one for each die */
static int uncore_max_entries __read_mostly;
/* Storage for uncore data for all instances */
static struct uncore_data *uncore_instances;
/* Stores the CPU mask of the target CPUs to use during uncore read/write */
static cpumask_t uncore_cpu_mask;
/* CPU online callback register instance */
static enum cpuhp_state uncore_hp_state __read_mostly;

#define MSR_UNCORE_RATIO_LIMIT	0x620
#define MSR_UNCORE_PERF_STATUS	0x621
#define UNCORE_FREQ_KHZ_MULTIPLIER	100000

#define UNCORE_MAX_RATIO_MASK	GENMASK_ULL(6, 0)
#define UNCORE_MIN_RATIO_MASK	GENMASK_ULL(14, 8)

#define UNCORE_CURRENT_RATIO_MASK	GENMASK_ULL(6, 0)

static int uncore_read_control_freq(struct uncore_data *data, unsigned int *value,
				    enum uncore_index index)
{
	u64 cap;
	int ret;

	if (data->control_cpu < 0)
		return -ENXIO;

	ret = rdmsrl_on_cpu(data->control_cpu, MSR_UNCORE_RATIO_LIMIT, &cap);
	if (ret)
		return ret;

	if (index == UNCORE_INDEX_MAX_FREQ)
		*value = FIELD_GET(UNCORE_MAX_RATIO_MASK, cap) * UNCORE_FREQ_KHZ_MULTIPLIER;
	else
		*value = FIELD_GET(UNCORE_MIN_RATIO_MASK, cap) * UNCORE_FREQ_KHZ_MULTIPLIER;

	return 0;
}

static int uncore_write_control_freq(struct uncore_data *data, unsigned int input,
				     enum uncore_index index)
{
	int ret;
	u64 cap;

	input /= UNCORE_FREQ_KHZ_MULTIPLIER;
	if (!input || input > FIELD_MAX(UNCORE_MAX_RATIO_MASK))
		return -EINVAL;

	if (data->control_cpu < 0)
		return -ENXIO;

	ret = rdmsrl_on_cpu(data->control_cpu, MSR_UNCORE_RATIO_LIMIT, &cap);
	if (ret)
		return ret;

	if (index == UNCORE_INDEX_MAX_FREQ) {
		cap &= ~UNCORE_MAX_RATIO_MASK;
		cap |= FIELD_PREP(UNCORE_MAX_RATIO_MASK, input);
	} else  {
		cap &= ~UNCORE_MIN_RATIO_MASK;
		cap |= FIELD_PREP(UNCORE_MIN_RATIO_MASK, input);
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

	if (data->control_cpu < 0)
		return -ENXIO;

	ret = rdmsrl_on_cpu(data->control_cpu, MSR_UNCORE_PERF_STATUS, &ratio);
	if (ret)
		return ret;

	*freq = FIELD_GET(UNCORE_CURRENT_RATIO_MASK, ratio) * UNCORE_FREQ_KHZ_MULTIPLIER;

	return 0;
}

static int uncore_read(struct uncore_data *data, unsigned int *value, enum uncore_index index)
{
	switch (index) {
	case UNCORE_INDEX_MIN_FREQ:
	case UNCORE_INDEX_MAX_FREQ:
		return uncore_read_control_freq(data, value, index);

	case UNCORE_INDEX_CURRENT_FREQ:
		return uncore_read_freq(data, value);

	default:
		break;
	}

	return -EOPNOTSUPP;
}

/* Caller provides protection */
static struct uncore_data *uncore_get_instance(unsigned int cpu)
{
	int id = topology_logical_die_id(cpu);

	if (id >= 0 && id < uncore_max_entries)
		return &uncore_instances[id];

	return NULL;
}

static int uncore_event_cpu_online(unsigned int cpu)
{
	struct uncore_data *data;
	int target;

	/* Check if there is an online cpu in the package for uncore MSR */
	target = cpumask_any_and(&uncore_cpu_mask, topology_die_cpumask(cpu));
	if (target < nr_cpu_ids)
		return 0;

	/* Use this CPU on this die as a control CPU */
	cpumask_set_cpu(cpu, &uncore_cpu_mask);

	data = uncore_get_instance(cpu);
	if (!data)
		return 0;

	data->package_id = topology_physical_package_id(cpu);
	data->die_id = topology_die_id(cpu);
	data->domain_id = UNCORE_DOMAIN_ID_INVALID;

	return uncore_freq_add_entry(data, cpu);
}

static int uncore_event_cpu_offline(unsigned int cpu)
{
	struct uncore_data *data;
	int target;

	data = uncore_get_instance(cpu);
	if (!data)
		return 0;

	/* Check if existing cpu is used for uncore MSRs */
	if (!cpumask_test_and_clear_cpu(cpu, &uncore_cpu_mask))
		return 0;

	/* Find a new cpu to set uncore MSR */
	target = cpumask_any_but(topology_die_cpumask(cpu), cpu);

	if (target < nr_cpu_ids) {
		cpumask_set_cpu(target, &uncore_cpu_mask);
		uncore_freq_add_entry(data, target);
	} else {
		uncore_freq_remove_die_entry(data);
	}

	return 0;
}

static int uncore_pm_notify(struct notifier_block *nb, unsigned long mode,
			    void *_unused)
{
	int i;

	switch (mode) {
	case PM_POST_HIBERNATION:
	case PM_POST_RESTORE:
	case PM_POST_SUSPEND:
		for (i = 0; i < uncore_max_entries; ++i) {
			struct uncore_data *data = &uncore_instances[i];

			if (!data || !data->valid || !data->stored_uncore_data)
				return 0;

			wrmsrl_on_cpu(data->control_cpu, MSR_UNCORE_RATIO_LIMIT,
				      data->stored_uncore_data);
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
	X86_MATCH_VFM(INTEL_BROADWELL_G,	NULL),
	X86_MATCH_VFM(INTEL_BROADWELL_X,	NULL),
	X86_MATCH_VFM(INTEL_BROADWELL_D,	NULL),
	X86_MATCH_VFM(INTEL_SKYLAKE_X,	NULL),
	X86_MATCH_VFM(INTEL_ICELAKE_X,	NULL),
	X86_MATCH_VFM(INTEL_ICELAKE_D,	NULL),
	X86_MATCH_VFM(INTEL_SAPPHIRERAPIDS_X, NULL),
	X86_MATCH_VFM(INTEL_EMERALDRAPIDS_X, NULL),
	X86_MATCH_VFM(INTEL_KABYLAKE, NULL),
	X86_MATCH_VFM(INTEL_KABYLAKE_L, NULL),
	X86_MATCH_VFM(INTEL_COMETLAKE, NULL),
	X86_MATCH_VFM(INTEL_COMETLAKE_L, NULL),
	X86_MATCH_VFM(INTEL_CANNONLAKE_L, NULL),
	X86_MATCH_VFM(INTEL_ICELAKE, NULL),
	X86_MATCH_VFM(INTEL_ICELAKE_L, NULL),
	X86_MATCH_VFM(INTEL_ROCKETLAKE, NULL),
	X86_MATCH_VFM(INTEL_TIGERLAKE, NULL),
	X86_MATCH_VFM(INTEL_TIGERLAKE_L, NULL),
	X86_MATCH_VFM(INTEL_ALDERLAKE, NULL),
	X86_MATCH_VFM(INTEL_ALDERLAKE_L, NULL),
	X86_MATCH_VFM(INTEL_RAPTORLAKE, NULL),
	X86_MATCH_VFM(INTEL_RAPTORLAKE_P, NULL),
	X86_MATCH_VFM(INTEL_RAPTORLAKE_S, NULL),
	X86_MATCH_VFM(INTEL_METEORLAKE, NULL),
	X86_MATCH_VFM(INTEL_METEORLAKE_L, NULL),
	X86_MATCH_VFM(INTEL_ARROWLAKE, NULL),
	X86_MATCH_VFM(INTEL_ARROWLAKE_H, NULL),
	X86_MATCH_VFM(INTEL_LUNARLAKE_M, NULL),
	{}
};
MODULE_DEVICE_TABLE(x86cpu, intel_uncore_cpu_ids);

static int __init intel_uncore_init(void)
{
	const struct x86_cpu_id *id;
	int ret;

	if (cpu_feature_enabled(X86_FEATURE_HYPERVISOR))
		return -ENODEV;

	id = x86_match_cpu(intel_uncore_cpu_ids);
	if (!id)
		return -ENODEV;

	uncore_max_entries = topology_max_packages() *
					topology_max_dies_per_package();
	uncore_instances = kcalloc(uncore_max_entries,
				   sizeof(*uncore_instances), GFP_KERNEL);
	if (!uncore_instances)
		return -ENOMEM;

	ret = uncore_freq_common_init(uncore_read, uncore_write_control_freq);
	if (ret)
		goto err_free;

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
	uncore_freq_common_exit();
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
	for (i = 0; i < uncore_max_entries; ++i)
		uncore_freq_remove_die_entry(&uncore_instances[i]);
	uncore_freq_common_exit();
	kfree(uncore_instances);
}
module_exit(intel_uncore_exit)

MODULE_IMPORT_NS(INTEL_UNCORE_FREQUENCY);
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Intel Uncore Frequency Limits Driver");
