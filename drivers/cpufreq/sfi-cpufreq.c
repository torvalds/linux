/*
 *  SFI Performance States Driver
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  Author: Vishwesh M Rudramuni <vishwesh.m.rudramuni@intel.com>
 *  Author: Srinidhi Kasagar <srinidhi.kasagar@intel.com>
 */

#include <linux/cpufreq.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sfi.h>
#include <linux/slab.h>
#include <linux/smp.h>

#include <asm/msr.h>

struct cpufreq_frequency_table *freq_table;
static struct sfi_freq_table_entry *sfi_cpufreq_array;
static int num_freq_table_entries;

static int sfi_parse_freq(struct sfi_table_header *table)
{
	struct sfi_table_simple *sb;
	struct sfi_freq_table_entry *pentry;
	int totallen;

	sb = (struct sfi_table_simple *)table;
	num_freq_table_entries = SFI_GET_NUM_ENTRIES(sb,
			struct sfi_freq_table_entry);
	if (num_freq_table_entries <= 1) {
		pr_err("No p-states discovered\n");
		return -ENODEV;
	}

	pentry = (struct sfi_freq_table_entry *)sb->pentry;
	totallen = num_freq_table_entries * sizeof(*pentry);

	sfi_cpufreq_array = kzalloc(totallen, GFP_KERNEL);
	if (!sfi_cpufreq_array)
		return -ENOMEM;

	memcpy(sfi_cpufreq_array, pentry, totallen);

	return 0;
}

static int sfi_cpufreq_target(struct cpufreq_policy *policy, unsigned int index)
{
	unsigned int next_perf_state = 0; /* Index into perf table */
	u32 lo, hi;

	next_perf_state = policy->freq_table[index].driver_data;

	rdmsr_on_cpu(policy->cpu, MSR_IA32_PERF_CTL, &lo, &hi);
	lo = (lo & ~INTEL_PERF_CTL_MASK) |
		((u32) sfi_cpufreq_array[next_perf_state].ctrl_val &
		INTEL_PERF_CTL_MASK);
	wrmsr_on_cpu(policy->cpu, MSR_IA32_PERF_CTL, lo, hi);

	return 0;
}

static int sfi_cpufreq_cpu_init(struct cpufreq_policy *policy)
{
	policy->shared_type = CPUFREQ_SHARED_TYPE_HW;
	policy->cpuinfo.transition_latency = 100000;	/* 100us */

	return cpufreq_table_validate_and_show(policy, freq_table);
}

static struct cpufreq_driver sfi_cpufreq_driver = {
	.flags		= CPUFREQ_CONST_LOOPS,
	.verify		= cpufreq_generic_frequency_table_verify,
	.target_index	= sfi_cpufreq_target,
	.init		= sfi_cpufreq_cpu_init,
	.name		= "sfi-cpufreq",
	.attr		= cpufreq_generic_attr,
};

static int __init sfi_cpufreq_init(void)
{
	int ret, i;

	/* parse the freq table from SFI */
	ret = sfi_table_parse(SFI_SIG_FREQ, NULL, NULL, sfi_parse_freq);
	if (ret)
		return ret;

	freq_table = kzalloc(sizeof(*freq_table) *
			(num_freq_table_entries + 1), GFP_KERNEL);
	if (!freq_table) {
		ret = -ENOMEM;
		goto err_free_array;
	}

	for (i = 0; i < num_freq_table_entries; i++) {
		freq_table[i].driver_data = i;
		freq_table[i].frequency = sfi_cpufreq_array[i].freq_mhz * 1000;
	}
	freq_table[i].frequency = CPUFREQ_TABLE_END;

	ret = cpufreq_register_driver(&sfi_cpufreq_driver);
	if (ret)
		goto err_free_tbl;

	return ret;

err_free_tbl:
	kfree(freq_table);
err_free_array:
	kfree(sfi_cpufreq_array);
	return ret;
}
late_initcall(sfi_cpufreq_init);

static void __exit sfi_cpufreq_exit(void)
{
	cpufreq_unregister_driver(&sfi_cpufreq_driver);
	kfree(freq_table);
	kfree(sfi_cpufreq_array);
}
module_exit(sfi_cpufreq_exit);

MODULE_AUTHOR("Vishwesh M Rudramuni <vishwesh.m.rudramuni@intel.com>");
MODULE_DESCRIPTION("SFI Performance-States Driver");
MODULE_LICENSE("GPL");
