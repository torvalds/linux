/*
 * linux/drivers/cpufreq/freq_table.c
 *
 * Copyright (C) 2002 - 2003 Dominik Brodowski
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/cpufreq.h>
#include <linux/module.h>

/*********************************************************************
 *                     FREQUENCY TABLE HELPERS                       *
 *********************************************************************/

int cpufreq_frequency_table_cpuinfo(struct cpufreq_policy *policy,
				    struct cpufreq_frequency_table *table)
{
	unsigned int min_freq = ~0;
	unsigned int max_freq = 0;
	unsigned int i;

	for (i = 0; (table[i].frequency != CPUFREQ_TABLE_END); i++) {
		unsigned int freq = table[i].frequency;
		if (freq == CPUFREQ_ENTRY_INVALID) {
			pr_debug("table entry %u is invalid, skipping\n", i);

			continue;
		}
		if (!cpufreq_boost_enabled()
		    && table[i].driver_data == CPUFREQ_BOOST_FREQ)
			continue;

		pr_debug("table entry %u: %u kHz, %u driver_data\n",
					i, freq, table[i].driver_data);
		if (freq < min_freq)
			min_freq = freq;
		if (freq > max_freq)
			max_freq = freq;
	}

	policy->min = policy->cpuinfo.min_freq = min_freq;
	policy->max = policy->cpuinfo.max_freq = max_freq;

	if (policy->min == ~0)
		return -EINVAL;
	else
		return 0;
}
EXPORT_SYMBOL_GPL(cpufreq_frequency_table_cpuinfo);


int cpufreq_frequency_table_verify(struct cpufreq_policy *policy,
				   struct cpufreq_frequency_table *table)
{
	unsigned int next_larger = ~0, freq, i = 0;
	bool found = false;

	pr_debug("request for verification of policy (%u - %u kHz) for cpu %u\n",
					policy->min, policy->max, policy->cpu);

	cpufreq_verify_within_cpu_limits(policy);

	for (; freq = table[i].frequency, freq != CPUFREQ_TABLE_END; i++) {
		if (freq == CPUFREQ_ENTRY_INVALID)
			continue;
		if ((freq >= policy->min) && (freq <= policy->max)) {
			found = true;
			break;
		}

		if ((next_larger > freq) && (freq > policy->max))
			next_larger = freq;
	}

	if (!found) {
		policy->max = next_larger;
		cpufreq_verify_within_cpu_limits(policy);
	}

	pr_debug("verification lead to (%u - %u kHz) for cpu %u\n",
				policy->min, policy->max, policy->cpu);

	return 0;
}
EXPORT_SYMBOL_GPL(cpufreq_frequency_table_verify);

/*
 * Generic routine to verify policy & frequency table, requires driver to call
 * cpufreq_frequency_table_get_attr() prior to it.
 */
int cpufreq_generic_frequency_table_verify(struct cpufreq_policy *policy)
{
	struct cpufreq_frequency_table *table =
		cpufreq_frequency_get_table(policy->cpu);
	if (!table)
		return -ENODEV;

	return cpufreq_frequency_table_verify(policy, table);
}
EXPORT_SYMBOL_GPL(cpufreq_generic_frequency_table_verify);

int cpufreq_frequency_table_target(struct cpufreq_policy *policy,
				   struct cpufreq_frequency_table *table,
				   unsigned int target_freq,
				   unsigned int relation,
				   unsigned int *index)
{
	struct cpufreq_frequency_table optimal = {
		.driver_data = ~0,
		.frequency = 0,
	};
	struct cpufreq_frequency_table suboptimal = {
		.driver_data = ~0,
		.frequency = 0,
	};
	unsigned int i;

	pr_debug("request for target %u kHz (relation: %u) for cpu %u\n",
					target_freq, relation, policy->cpu);

	switch (relation) {
	case CPUFREQ_RELATION_H:
		suboptimal.frequency = ~0;
		break;
	case CPUFREQ_RELATION_L:
		optimal.frequency = ~0;
		break;
	}

	for (i = 0; (table[i].frequency != CPUFREQ_TABLE_END); i++) {
		unsigned int freq = table[i].frequency;
		if (freq == CPUFREQ_ENTRY_INVALID)
			continue;
		if ((freq < policy->min) || (freq > policy->max))
			continue;
		switch (relation) {
		case CPUFREQ_RELATION_H:
			if (freq <= target_freq) {
				if (freq >= optimal.frequency) {
					optimal.frequency = freq;
					optimal.driver_data = i;
				}
			} else {
				if (freq <= suboptimal.frequency) {
					suboptimal.frequency = freq;
					suboptimal.driver_data = i;
				}
			}
			break;
		case CPUFREQ_RELATION_L:
			if (freq >= target_freq) {
				if (freq <= optimal.frequency) {
					optimal.frequency = freq;
					optimal.driver_data = i;
				}
			} else {
				if (freq >= suboptimal.frequency) {
					suboptimal.frequency = freq;
					suboptimal.driver_data = i;
				}
			}
			break;
		}
	}
	if (optimal.driver_data > i) {
		if (suboptimal.driver_data > i)
			return -EINVAL;
		*index = suboptimal.driver_data;
	} else
		*index = optimal.driver_data;

	pr_debug("target is %u (%u kHz, %u)\n", *index, table[*index].frequency,
		table[*index].driver_data);

	return 0;
}
EXPORT_SYMBOL_GPL(cpufreq_frequency_table_target);

int cpufreq_frequency_table_get_index(struct cpufreq_policy *policy,
		unsigned int freq)
{
	struct cpufreq_frequency_table *table;
	int i;

	table = cpufreq_frequency_get_table(policy->cpu);
	if (unlikely(!table)) {
		pr_debug("%s: Unable to find frequency table\n", __func__);
		return -ENOENT;
	}

	for (i = 0; table[i].frequency != CPUFREQ_TABLE_END; i++) {
		if (table[i].frequency == freq)
			return i;
	}

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(cpufreq_frequency_table_get_index);

static DEFINE_PER_CPU(struct cpufreq_frequency_table *, cpufreq_show_table);

/**
 * show_available_freqs - show available frequencies for the specified CPU
 */
static ssize_t show_available_freqs(struct cpufreq_policy *policy, char *buf,
				    bool show_boost)
{
	unsigned int i = 0;
	unsigned int cpu = policy->cpu;
	ssize_t count = 0;
	struct cpufreq_frequency_table *table;

	if (!per_cpu(cpufreq_show_table, cpu))
		return -ENODEV;

	table = per_cpu(cpufreq_show_table, cpu);

	for (i = 0; (table[i].frequency != CPUFREQ_TABLE_END); i++) {
		if (table[i].frequency == CPUFREQ_ENTRY_INVALID)
			continue;
		/*
		 * show_boost = true and driver_data = BOOST freq
		 * display BOOST freqs
		 *
		 * show_boost = false and driver_data = BOOST freq
		 * show_boost = true and driver_data != BOOST freq
		 * continue - do not display anything
		 *
		 * show_boost = false and driver_data != BOOST freq
		 * display NON BOOST freqs
		 */
		if (show_boost ^ (table[i].driver_data == CPUFREQ_BOOST_FREQ))
			continue;

		count += sprintf(&buf[count], "%d ", table[i].frequency);
	}
	count += sprintf(&buf[count], "\n");

	return count;

}

#define cpufreq_attr_available_freq(_name)	  \
struct freq_attr cpufreq_freq_attr_##_name##_freqs =     \
__ATTR_RO(_name##_frequencies)

/**
 * show_scaling_available_frequencies - show available normal frequencies for
 * the specified CPU
 */
static ssize_t scaling_available_frequencies_show(struct cpufreq_policy *policy,
						  char *buf)
{
	return show_available_freqs(policy, buf, false);
}
cpufreq_attr_available_freq(scaling_available);
EXPORT_SYMBOL_GPL(cpufreq_freq_attr_scaling_available_freqs);

/**
 * show_available_boost_freqs - show available boost frequencies for
 * the specified CPU
 */
static ssize_t scaling_boost_frequencies_show(struct cpufreq_policy *policy,
					      char *buf)
{
	return show_available_freqs(policy, buf, true);
}
cpufreq_attr_available_freq(scaling_boost);
EXPORT_SYMBOL_GPL(cpufreq_freq_attr_scaling_boost_freqs);

struct freq_attr *cpufreq_generic_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
#ifdef CONFIG_CPU_FREQ_BOOST_SW
	&cpufreq_freq_attr_scaling_boost_freqs,
#endif
	NULL,
};
EXPORT_SYMBOL_GPL(cpufreq_generic_attr);

/*
 * if you use these, you must assure that the frequency table is valid
 * all the time between get_attr and put_attr!
 */
void cpufreq_frequency_table_get_attr(struct cpufreq_frequency_table *table,
				      unsigned int cpu)
{
	pr_debug("setting show_table for cpu %u to %p\n", cpu, table);
	per_cpu(cpufreq_show_table, cpu) = table;
}
EXPORT_SYMBOL_GPL(cpufreq_frequency_table_get_attr);

void cpufreq_frequency_table_put_attr(unsigned int cpu)
{
	pr_debug("clearing show_table for cpu %u\n", cpu);
	per_cpu(cpufreq_show_table, cpu) = NULL;
}
EXPORT_SYMBOL_GPL(cpufreq_frequency_table_put_attr);

int cpufreq_table_validate_and_show(struct cpufreq_policy *policy,
				      struct cpufreq_frequency_table *table)
{
	int ret = cpufreq_frequency_table_cpuinfo(policy, table);

	if (!ret)
		cpufreq_frequency_table_get_attr(table, policy->cpu);

	return ret;
}
EXPORT_SYMBOL_GPL(cpufreq_table_validate_and_show);

void cpufreq_frequency_table_update_policy_cpu(struct cpufreq_policy *policy)
{
	pr_debug("Updating show_table for new_cpu %u from last_cpu %u\n",
			policy->cpu, policy->last_cpu);
	per_cpu(cpufreq_show_table, policy->cpu) = per_cpu(cpufreq_show_table,
			policy->last_cpu);
	per_cpu(cpufreq_show_table, policy->last_cpu) = NULL;
}

struct cpufreq_frequency_table *cpufreq_frequency_get_table(unsigned int cpu)
{
	return per_cpu(cpufreq_show_table, cpu);
}
EXPORT_SYMBOL_GPL(cpufreq_frequency_get_table);

MODULE_AUTHOR("Dominik Brodowski <linux@brodo.de>");
MODULE_DESCRIPTION("CPUfreq frequency table helpers");
MODULE_LICENSE("GPL");
