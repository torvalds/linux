/*
 * amd_freq_sensitivity.c: AMD frequency sensitivity feedback powersave bias
 *                         for the ondemand governor.
 *
 * Copyright (C) 2013 Advanced Micro Devices, Inc.
 *
 * Author: Jacob Shin <jacob.shin@amd.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/percpu-defs.h>
#include <linux/init.h>
#include <linux/mod_devicetable.h>

#include <asm/msr.h>
#include <asm/cpufeature.h>

#include "cpufreq_governor.h"

#define MSR_AMD64_FREQ_SENSITIVITY_ACTUAL	0xc0010080
#define MSR_AMD64_FREQ_SENSITIVITY_REFERENCE	0xc0010081
#define CLASS_CODE_SHIFT			56
#define POWERSAVE_BIAS_MAX			1000
#define POWERSAVE_BIAS_DEF			400

struct cpu_data_t {
	u64 actual;
	u64 reference;
	unsigned int freq_prev;
};

static DEFINE_PER_CPU(struct cpu_data_t, cpu_data);

static unsigned int amd_powersave_bias_target(struct cpufreq_policy *policy,
					      unsigned int freq_next,
					      unsigned int relation)
{
	int sensitivity;
	long d_actual, d_reference;
	struct msr actual, reference;
	struct cpu_data_t *data = &per_cpu(cpu_data, policy->cpu);
	struct dbs_data *od_data = policy->governor_data;
	struct od_dbs_tuners *od_tuners = od_data->tuners;
	struct od_cpu_dbs_info_s *od_info =
		dbs_governor_of(policy)->get_cpu_dbs_info_s(policy->cpu);

	if (!od_info->freq_table)
		return freq_next;

	rdmsr_on_cpu(policy->cpu, MSR_AMD64_FREQ_SENSITIVITY_ACTUAL,
		&actual.l, &actual.h);
	rdmsr_on_cpu(policy->cpu, MSR_AMD64_FREQ_SENSITIVITY_REFERENCE,
		&reference.l, &reference.h);
	actual.h &= 0x00ffffff;
	reference.h &= 0x00ffffff;

	/* counter wrapped around, so stay on current frequency */
	if (actual.q < data->actual || reference.q < data->reference) {
		freq_next = policy->cur;
		goto out;
	}

	d_actual = actual.q - data->actual;
	d_reference = reference.q - data->reference;

	/* divide by 0, so stay on current frequency as well */
	if (d_reference == 0) {
		freq_next = policy->cur;
		goto out;
	}

	sensitivity = POWERSAVE_BIAS_MAX -
		(POWERSAVE_BIAS_MAX * (d_reference - d_actual) / d_reference);

	clamp(sensitivity, 0, POWERSAVE_BIAS_MAX);

	/* this workload is not CPU bound, so choose a lower freq */
	if (sensitivity < od_tuners->powersave_bias) {
		if (data->freq_prev == policy->cur)
			freq_next = policy->cur;

		if (freq_next > policy->cur)
			freq_next = policy->cur;
		else if (freq_next < policy->cur)
			freq_next = policy->min;
		else {
			unsigned int index;

			cpufreq_frequency_table_target(policy,
				od_info->freq_table, policy->cur - 1,
				CPUFREQ_RELATION_H, &index);
			freq_next = od_info->freq_table[index].frequency;
		}

		data->freq_prev = freq_next;
	} else
		data->freq_prev = 0;

out:
	data->actual = actual.q;
	data->reference = reference.q;
	return freq_next;
}

static int __init amd_freq_sensitivity_init(void)
{
	u64 val;

	if (boot_cpu_data.x86_vendor != X86_VENDOR_AMD)
		return -ENODEV;

	if (!static_cpu_has(X86_FEATURE_PROC_FEEDBACK))
		return -ENODEV;

	if (rdmsrl_safe(MSR_AMD64_FREQ_SENSITIVITY_ACTUAL, &val))
		return -ENODEV;

	if (!(val >> CLASS_CODE_SHIFT))
		return -ENODEV;

	od_register_powersave_bias_handler(amd_powersave_bias_target,
			POWERSAVE_BIAS_DEF);
	return 0;
}
late_initcall(amd_freq_sensitivity_init);

static void __exit amd_freq_sensitivity_exit(void)
{
	od_unregister_powersave_bias_handler();
}
module_exit(amd_freq_sensitivity_exit);

static const struct x86_cpu_id amd_freq_sensitivity_ids[] = {
	X86_FEATURE_MATCH(X86_FEATURE_PROC_FEEDBACK),
	{}
};
MODULE_DEVICE_TABLE(x86cpu, amd_freq_sensitivity_ids);

MODULE_AUTHOR("Jacob Shin <jacob.shin@amd.com>");
MODULE_DESCRIPTION("AMD frequency sensitivity feedback powersave bias for "
		"the ondemand governor.");
MODULE_LICENSE("GPL");
