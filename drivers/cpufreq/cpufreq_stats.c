/*
 *  drivers/cpufreq/cpufreq_stats.c
 *
 *  Copyright (C) 2003-2004 Venkatesh Pallipadi <venkatesh.pallipadi@intel.com>.
 *  (C) 2004 Zou Nan hai <nanhai.zou@intel.com>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <asm/cputime.h>

static spinlock_t cpufreq_stats_lock;

struct cpufreq_stats {
	unsigned int cpu;
	unsigned int total_trans;
	unsigned long long last_time;
	unsigned int max_state;
	unsigned int state_num;
	unsigned int last_index;
	u64 *time_in_state;
	unsigned int *freq_table;
#ifdef CONFIG_CPU_FREQ_STAT_DETAILS
	unsigned int *trans_table;
#endif
};

static DEFINE_PER_CPU(struct cpufreq_stats *, cpufreq_stats_table);

struct cpufreq_stats_attribute {
	struct attribute attr;
	ssize_t(*show) (struct cpufreq_stats *, char *);
};

static int cpufreq_stats_update(unsigned int cpu)
{
	struct cpufreq_stats *stat;
	unsigned long long cur_time;

	cur_time = get_jiffies_64();
	spin_lock(&cpufreq_stats_lock);
	stat = per_cpu(cpufreq_stats_table, cpu);
	if (stat->time_in_state)
		stat->time_in_state[stat->last_index] +=
			cur_time - stat->last_time;
	stat->last_time = cur_time;
	spin_unlock(&cpufreq_stats_lock);
	return 0;
}

static ssize_t show_total_trans(struct cpufreq_policy *policy, char *buf)
{
	struct cpufreq_stats *stat = per_cpu(cpufreq_stats_table, policy->cpu);
	if (!stat)
		return 0;
	return sprintf(buf, "%d\n",
			per_cpu(cpufreq_stats_table, stat->cpu)->total_trans);
}

static ssize_t show_time_in_state(struct cpufreq_policy *policy, char *buf)
{
	ssize_t len = 0;
	int i;
	struct cpufreq_stats *stat = per_cpu(cpufreq_stats_table, policy->cpu);
	if (!stat)
		return 0;
	cpufreq_stats_update(stat->cpu);
	for (i = 0; i < stat->state_num; i++) {
		len += sprintf(buf + len, "%u %llu\n", stat->freq_table[i],
			(unsigned long long)
			cputime64_to_clock_t(stat->time_in_state[i]));
	}
	return len;
}

#ifdef CONFIG_CPU_FREQ_STAT_DETAILS
static ssize_t show_trans_table(struct cpufreq_policy *policy, char *buf)
{
	ssize_t len = 0;
	int i, j;

	struct cpufreq_stats *stat = per_cpu(cpufreq_stats_table, policy->cpu);
	if (!stat)
		return 0;
	cpufreq_stats_update(stat->cpu);
	len += snprintf(buf + len, PAGE_SIZE - len, "   From  :    To\n");
	len += snprintf(buf + len, PAGE_SIZE - len, "         : ");
	for (i = 0; i < stat->state_num; i++) {
		if (len >= PAGE_SIZE)
			break;
		len += snprintf(buf + len, PAGE_SIZE - len, "%9u ",
				stat->freq_table[i]);
	}
	if (len >= PAGE_SIZE)
		return PAGE_SIZE;

	len += snprintf(buf + len, PAGE_SIZE - len, "\n");

	for (i = 0; i < stat->state_num; i++) {
		if (len >= PAGE_SIZE)
			break;

		len += snprintf(buf + len, PAGE_SIZE - len, "%9u: ",
				stat->freq_table[i]);

		for (j = 0; j < stat->state_num; j++) {
			if (len >= PAGE_SIZE)
				break;
			len += snprintf(buf + len, PAGE_SIZE - len, "%9u ",
					stat->trans_table[i*stat->max_state+j]);
		}
		if (len >= PAGE_SIZE)
			break;
		len += snprintf(buf + len, PAGE_SIZE - len, "\n");
	}
	if (len >= PAGE_SIZE)
		return PAGE_SIZE;
	return len;
}
cpufreq_freq_attr_ro(trans_table);
#endif

cpufreq_freq_attr_ro(total_trans);
cpufreq_freq_attr_ro(time_in_state);

static struct attribute *default_attrs[] = {
	&total_trans.attr,
	&time_in_state.attr,
#ifdef CONFIG_CPU_FREQ_STAT_DETAILS
	&trans_table.attr,
#endif
	NULL
};
static struct attribute_group stats_attr_group = {
	.attrs = default_attrs,
	.name = "stats"
};

static int freq_table_get_index(struct cpufreq_stats *stat, unsigned int freq)
{
	int index;
	for (index = 0; index < stat->max_state; index++)
		if (stat->freq_table[index] == freq)
			return index;
	return -1;
}

/* should be called late in the CPU removal sequence so that the stats
 * memory is still available in case someone tries to use it.
 */
static void cpufreq_stats_free_table(unsigned int cpu)
{
	struct cpufreq_stats *stat = per_cpu(cpufreq_stats_table, cpu);

	if (stat) {
		pr_debug("%s: Free stat table\n", __func__);
		kfree(stat->time_in_state);
		kfree(stat);
		per_cpu(cpufreq_stats_table, cpu) = NULL;
	}
}

/* must be called early in the CPU removal sequence (before
 * cpufreq_remove_dev) so that policy is still valid.
 */
static void cpufreq_stats_free_sysfs(unsigned int cpu)
{
	struct cpufreq_policy *policy = cpufreq_cpu_get(cpu);

	if (!policy)
		return;

	if (!cpufreq_frequency_get_table(cpu))
		goto put_ref;

	if (!policy_is_shared(policy)) {
		pr_debug("%s: Free sysfs stat\n", __func__);
		sysfs_remove_group(&policy->kobj, &stats_attr_group);
	}

put_ref:
	cpufreq_cpu_put(policy);
}

static int cpufreq_stats_create_table(struct cpufreq_policy *policy,
		struct cpufreq_frequency_table *table)
{
	unsigned int i, j, count = 0, ret = 0;
	struct cpufreq_stats *stat;
	struct cpufreq_policy *current_policy;
	unsigned int alloc_size;
	unsigned int cpu = policy->cpu;
	if (per_cpu(cpufreq_stats_table, cpu))
		return -EBUSY;
	stat = kzalloc(sizeof(*stat), GFP_KERNEL);
	if ((stat) == NULL)
		return -ENOMEM;

	current_policy = cpufreq_cpu_get(cpu);
	if (current_policy == NULL) {
		ret = -EINVAL;
		goto error_get_fail;
	}

	ret = sysfs_create_group(&current_policy->kobj, &stats_attr_group);
	if (ret)
		goto error_out;

	stat->cpu = cpu;
	per_cpu(cpufreq_stats_table, cpu) = stat;

	for (i = 0; table[i].frequency != CPUFREQ_TABLE_END; i++) {
		unsigned int freq = table[i].frequency;
		if (freq == CPUFREQ_ENTRY_INVALID)
			continue;
		count++;
	}

	alloc_size = count * sizeof(int) + count * sizeof(u64);

#ifdef CONFIG_CPU_FREQ_STAT_DETAILS
	alloc_size += count * count * sizeof(int);
#endif
	stat->max_state = count;
	stat->time_in_state = kzalloc(alloc_size, GFP_KERNEL);
	if (!stat->time_in_state) {
		ret = -ENOMEM;
		goto error_out;
	}
	stat->freq_table = (unsigned int *)(stat->time_in_state + count);

#ifdef CONFIG_CPU_FREQ_STAT_DETAILS
	stat->trans_table = stat->freq_table + count;
#endif
	j = 0;
	for (i = 0; table[i].frequency != CPUFREQ_TABLE_END; i++) {
		unsigned int freq = table[i].frequency;
		if (freq == CPUFREQ_ENTRY_INVALID)
			continue;
		if (freq_table_get_index(stat, freq) == -1)
			stat->freq_table[j++] = freq;
	}
	stat->state_num = j;
	spin_lock(&cpufreq_stats_lock);
	stat->last_time = get_jiffies_64();
	stat->last_index = freq_table_get_index(stat, policy->cur);
	spin_unlock(&cpufreq_stats_lock);
	cpufreq_cpu_put(current_policy);
	return 0;
error_out:
	cpufreq_cpu_put(current_policy);
error_get_fail:
	kfree(stat);
	per_cpu(cpufreq_stats_table, cpu) = NULL;
	return ret;
}

static void cpufreq_stats_update_policy_cpu(struct cpufreq_policy *policy)
{
	struct cpufreq_stats *stat = per_cpu(cpufreq_stats_table,
			policy->last_cpu);

	pr_debug("Updating stats_table for new_cpu %u from last_cpu %u\n",
			policy->cpu, policy->last_cpu);
	per_cpu(cpufreq_stats_table, policy->cpu) = per_cpu(cpufreq_stats_table,
			policy->last_cpu);
	per_cpu(cpufreq_stats_table, policy->last_cpu) = NULL;
	stat->cpu = policy->cpu;
}

static int cpufreq_stat_notifier_policy(struct notifier_block *nb,
		unsigned long val, void *data)
{
	int ret;
	struct cpufreq_policy *policy = data;
	struct cpufreq_frequency_table *table;
	unsigned int cpu = policy->cpu;

	if (val == CPUFREQ_UPDATE_POLICY_CPU) {
		cpufreq_stats_update_policy_cpu(policy);
		return 0;
	}

	if (val != CPUFREQ_NOTIFY)
		return 0;
	table = cpufreq_frequency_get_table(cpu);
	if (!table)
		return 0;
	ret = cpufreq_stats_create_table(policy, table);
	if (ret)
		return ret;
	return 0;
}

static int cpufreq_stat_notifier_trans(struct notifier_block *nb,
		unsigned long val, void *data)
{
	struct cpufreq_freqs *freq = data;
	struct cpufreq_stats *stat;
	int old_index, new_index;

	if (val != CPUFREQ_POSTCHANGE)
		return 0;

	stat = per_cpu(cpufreq_stats_table, freq->cpu);
	if (!stat)
		return 0;

	old_index = stat->last_index;
	new_index = freq_table_get_index(stat, freq->new);

	/* We can't do stat->time_in_state[-1]= .. */
	if (old_index == -1 || new_index == -1)
		return 0;

	cpufreq_stats_update(freq->cpu);

	if (old_index == new_index)
		return 0;

	spin_lock(&cpufreq_stats_lock);
	stat->last_index = new_index;
#ifdef CONFIG_CPU_FREQ_STAT_DETAILS
	stat->trans_table[old_index * stat->max_state + new_index]++;
#endif
	stat->total_trans++;
	spin_unlock(&cpufreq_stats_lock);
	return 0;
}

static int cpufreq_stat_cpu_callback(struct notifier_block *nfb,
					       unsigned long action,
					       void *hcpu)
{
	unsigned int cpu = (unsigned long)hcpu;

	switch (action) {
	case CPU_DOWN_PREPARE:
		cpufreq_stats_free_sysfs(cpu);
		break;
	case CPU_DEAD:
		cpufreq_stats_free_table(cpu);
		break;
	}
	return NOTIFY_OK;
}

/* priority=1 so this will get called before cpufreq_remove_dev */
static struct notifier_block cpufreq_stat_cpu_notifier __refdata = {
	.notifier_call = cpufreq_stat_cpu_callback,
	.priority = 1,
};

static struct notifier_block notifier_policy_block = {
	.notifier_call = cpufreq_stat_notifier_policy
};

static struct notifier_block notifier_trans_block = {
	.notifier_call = cpufreq_stat_notifier_trans
};

static int __init cpufreq_stats_init(void)
{
	int ret;
	unsigned int cpu;

	spin_lock_init(&cpufreq_stats_lock);
	ret = cpufreq_register_notifier(&notifier_policy_block,
				CPUFREQ_POLICY_NOTIFIER);
	if (ret)
		return ret;

	register_hotcpu_notifier(&cpufreq_stat_cpu_notifier);

	ret = cpufreq_register_notifier(&notifier_trans_block,
				CPUFREQ_TRANSITION_NOTIFIER);
	if (ret) {
		cpufreq_unregister_notifier(&notifier_policy_block,
				CPUFREQ_POLICY_NOTIFIER);
		unregister_hotcpu_notifier(&cpufreq_stat_cpu_notifier);
		for_each_online_cpu(cpu)
			cpufreq_stats_free_table(cpu);
		return ret;
	}

	return 0;
}
static void __exit cpufreq_stats_exit(void)
{
	unsigned int cpu;

	cpufreq_unregister_notifier(&notifier_policy_block,
			CPUFREQ_POLICY_NOTIFIER);
	cpufreq_unregister_notifier(&notifier_trans_block,
			CPUFREQ_TRANSITION_NOTIFIER);
	unregister_hotcpu_notifier(&cpufreq_stat_cpu_notifier);
	for_each_online_cpu(cpu) {
		cpufreq_stats_free_table(cpu);
		cpufreq_stats_free_sysfs(cpu);
	}
}

MODULE_AUTHOR("Zou Nan hai <nanhai.zou@intel.com>");
MODULE_DESCRIPTION("'cpufreq_stats' - A driver to export cpufreq stats "
				"through sysfs filesystem");
MODULE_LICENSE("GPL");

module_init(cpufreq_stats_init);
module_exit(cpufreq_stats_exit);
