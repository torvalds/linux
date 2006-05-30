/*
 * linux/drivers/cpufreq/freq_table.c
 *
 * Copyright (C) 2002 - 2003 Dominik Brodowski
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/cpufreq.h>

#define dprintk(msg...) cpufreq_debug_printk(CPUFREQ_DEBUG_CORE, "freq-table", msg)

/*********************************************************************
 *                     FREQUENCY TABLE HELPERS                       *
 *********************************************************************/

int cpufreq_frequency_table_cpuinfo(struct cpufreq_policy *policy,
				    struct cpufreq_frequency_table *table)
{
	unsigned int min_freq = ~0;
	unsigned int max_freq = 0;
	unsigned int i;

	for (i=0; (table[i].frequency != CPUFREQ_TABLE_END); i++) {
		unsigned int freq = table[i].frequency;
		if (freq == CPUFREQ_ENTRY_INVALID) {
			dprintk("table entry %u is invalid, skipping\n", i);

			continue;
		}
		dprintk("table entry %u: %u kHz, %u index\n", i, freq, table[i].index);
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
	unsigned int next_larger = ~0;
	unsigned int i;
	unsigned int count = 0;

	dprintk("request for verification of policy (%u - %u kHz) for cpu %u\n", policy->min, policy->max, policy->cpu);

	if (!cpu_online(policy->cpu))
		return -EINVAL;

	cpufreq_verify_within_limits(policy,
				     policy->cpuinfo.min_freq, policy->cpuinfo.max_freq);

	for (i=0; (table[i].frequency != CPUFREQ_TABLE_END); i++) {
		unsigned int freq = table[i].frequency;
		if (freq == CPUFREQ_ENTRY_INVALID)
			continue;
		if ((freq >= policy->min) && (freq <= policy->max))
			count++;
		else if ((next_larger > freq) && (freq > policy->max))
			next_larger = freq;
	}

	if (!count)
		policy->max = next_larger;

	cpufreq_verify_within_limits(policy,
				     policy->cpuinfo.min_freq, policy->cpuinfo.max_freq);

	dprintk("verification lead to (%u - %u kHz) for cpu %u\n", policy->min, policy->max, policy->cpu);

	return 0;
}
EXPORT_SYMBOL_GPL(cpufreq_frequency_table_verify);


int cpufreq_frequency_table_target(struct cpufreq_policy *policy,
				   struct cpufreq_frequency_table *table,
				   unsigned int target_freq,
				   unsigned int relation,
				   unsigned int *index)
{
	struct cpufreq_frequency_table optimal = { .index = ~0, };
	struct cpufreq_frequency_table suboptimal = { .index = ~0, };
	unsigned int i;

	dprintk("request for target %u kHz (relation: %u) for cpu %u\n", target_freq, relation, policy->cpu);

	switch (relation) {
	case CPUFREQ_RELATION_H:
		optimal.frequency = 0;
		suboptimal.frequency = ~0;
		break;
	case CPUFREQ_RELATION_L:
		optimal.frequency = ~0;
		suboptimal.frequency = 0;
		break;
	}

	if (!cpu_online(policy->cpu))
		return -EINVAL;

	for (i=0; (table[i].frequency != CPUFREQ_TABLE_END); i++) {
		unsigned int freq = table[i].frequency;
		if (freq == CPUFREQ_ENTRY_INVALID)
			continue;
		if ((freq < policy->min) || (freq > policy->max))
			continue;
		switch(relation) {
		case CPUFREQ_RELATION_H:
			if (freq <= target_freq) {
				if (freq >= optimal.frequency) {
					optimal.frequency = freq;
					optimal.index = i;
				}
			} else {
				if (freq <= suboptimal.frequency) {
					suboptimal.frequency = freq;
					suboptimal.index = i;
				}
			}
			break;
		case CPUFREQ_RELATION_L:
			if (freq >= target_freq) {
				if (freq <= optimal.frequency) {
					optimal.frequency = freq;
					optimal.index = i;
				}
			} else {
				if (freq >= suboptimal.frequency) {
					suboptimal.frequency = freq;
					suboptimal.index = i;
				}
			}
			break;
		}
	}
	if (optimal.index > i) {
		if (suboptimal.index > i)
			return -EINVAL;
		*index = suboptimal.index;
	} else
		*index = optimal.index;

	dprintk("target is %u (%u kHz, %u)\n", *index, table[*index].frequency,
		table[*index].index);

	return 0;
}
EXPORT_SYMBOL_GPL(cpufreq_frequency_table_target);

static struct cpufreq_frequency_table *show_table[NR_CPUS];
/**
 * show_scaling_governor - show the current policy for the specified CPU
 */
static ssize_t show_available_freqs (struct cpufreq_policy *policy, char *buf)
{
	unsigned int i = 0;
	unsigned int cpu = policy->cpu;
	ssize_t count = 0;
	struct cpufreq_frequency_table *table;

	if (!show_table[cpu])
		return -ENODEV;

	table = show_table[cpu];

	for (i=0; (table[i].frequency != CPUFREQ_TABLE_END); i++) {
		if (table[i].frequency == CPUFREQ_ENTRY_INVALID)
			continue;
		count += sprintf(&buf[count], "%d ", table[i].frequency);
	}
	count += sprintf(&buf[count], "\n");

	return count;

}

struct freq_attr cpufreq_freq_attr_scaling_available_freqs = {
	.attr = { .name = "scaling_available_frequencies", .mode = 0444, .owner=THIS_MODULE },
	.show = show_available_freqs,
};
EXPORT_SYMBOL_GPL(cpufreq_freq_attr_scaling_available_freqs);

/*
 * if you use these, you must assure that the frequency table is valid
 * all the time between get_attr and put_attr!
 */
void cpufreq_frequency_table_get_attr(struct cpufreq_frequency_table *table,
				      unsigned int cpu)
{
	dprintk("setting show_table for cpu %u to %p\n", cpu, table);
	show_table[cpu] = table;
}
EXPORT_SYMBOL_GPL(cpufreq_frequency_table_get_attr);

void cpufreq_frequency_table_put_attr(unsigned int cpu)
{
	dprintk("clearing show_table for cpu %u\n", cpu);
	show_table[cpu] = NULL;
}
EXPORT_SYMBOL_GPL(cpufreq_frequency_table_put_attr);

struct cpufreq_frequency_table *cpufreq_frequency_get_table(unsigned int cpu)
{
	return show_table[cpu];
}
EXPORT_SYMBOL_GPL(cpufreq_frequency_get_table);

MODULE_AUTHOR ("Dominik Brodowski <linux@brodo.de>");
MODULE_DESCRIPTION ("CPUfreq frequency table helpers");
MODULE_LICENSE ("GPL");
