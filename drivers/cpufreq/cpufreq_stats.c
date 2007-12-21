/*
 *  drivers/cpufreq/cpufreq_stats.c
 *
 *  Copyright (C) 2003-2004 Venkatesh Pallipadi <venkatesh.pallipadi@intel.com>.
 *	      (C) 2004 Zou Nan hai <nanhai.zou@intel.com>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/sysdev.h>
#include <linux/cpu.h>
#include <linux/sysfs.h>
#include <linux/cpufreq.h>
#include <linux/jiffies.h>
#include <linux/percpu.h>
#include <linux/kobject.h>
#include <linux/spinlock.h>
#include <linux/notifier.h>
#include <asm/cputime.h>

static spinlock_t cpufreq_stats_lock;

#define CPUFREQ_STATDEVICE_ATTR(_name,_mode,_show) \
static struct freq_attr _attr_##_name = {\
	.attr = {.name = __stringify(_name), .mode = _mode, }, \
	.show = _show,\
};

struct cpufreq_stats {
	unsigned int cpu;
	unsigned int total_trans;
	unsigned long long  last_time;
	unsigned int max_state;
	unsigned int state_num;
	unsigned int last_index;
	cputime64_t *time_in_state;
	unsigned int *freq_table;
#ifdef CONFIG_CPU_FREQ_STAT_DETAILS
	unsigned int *trans_table;
#endif
};

static struct cpufreq_stats *cpufreq_stats_table[NR_CPUS];

struct cpufreq_stats_attribute {
	struct attribute attr;
	ssize_t(*show) (struct cpufreq_stats *, char *);
};

static int
cpufreq_stats_update (unsigned int cpu)
{
	struct cpufreq_stats *stat;
	unsigned long long cur_time;

	cur_time = get_jiffies_64();
	spin_lock(&cpufreq_stats_lock);
	stat = cpufreq_stats_table[cpu];
	if (stat->time_in_state)
		stat->time_in_state[stat->last_index] =
			cputime64_add(stat->time_in_state[stat->last_index],
				      cputime_sub(cur_time, stat->last_time));
	stat->last_time = cur_time;
	spin_unlock(&cpufreq_stats_lock);
	return 0;
}

static ssize_t
show_total_trans(struct cpufreq_policy *policy, char *buf)
{
	struct cpufreq_stats *stat = cpufreq_stats_table[policy->cpu];
	if (!stat)
		return 0;
	return sprintf(buf, "%d\n",
			cpufreq_stats_table[stat->cpu]->total_trans);
}

static ssize_t
show_time_in_state(struct cpufreq_policy *policy, char *buf)
{
	ssize_t len = 0;
	int i;
	struct cpufreq_stats *stat = cpufreq_stats_table[policy->cpu];
	if (!stat)
		return 0;
	cpufreq_stats_update(stat->cpu);
	for (i = 0; i < stat->state_num; i++) {
		len += sprintf(buf + len, "%u %llu\n", stat->freq_table[i],
			(unsigned long long)cputime64_to_clock_t(stat->time_in_state[i]));
	}
	return len;
}

#ifdef CONFIG_CPU_FREQ_STAT_DETAILS
static ssize_t
show_trans_table(struct cpufreq_policy *policy, char *buf)
{
	ssize_t len = 0;
	int i, j;

	struct cpufreq_stats *stat = cpufreq_stats_table[policy->cpu];
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
		return len;

	len += snprintf(buf + len, PAGE_SIZE - len, "\n");

	for (i = 0; i < stat->state_num; i++) {
		if (len >= PAGE_SIZE)
			break;

		len += snprintf(buf + len, PAGE_SIZE - len, "%9u: ",
				stat->freq_table[i]);

		for (j = 0; j < stat->state_num; j++)   {
			if (len >= PAGE_SIZE)
				break;
			len += snprintf(buf + len, PAGE_SIZE - len, "%9u ",
					stat->trans_table[i*stat->max_state+j]);
		}
		len += snprintf(buf + len, PAGE_SIZE - len, "\n");
	}
	return len;
}
CPUFREQ_STATDEVICE_ATTR(trans_table,0444,show_trans_table);
#endif

CPUFREQ_STATDEVICE_ATTR(total_trans,0444,show_total_trans);
CPUFREQ_STATDEVICE_ATTR(time_in_state,0444,show_time_in_state);

static struct attribute *default_attrs[] = {
	&_attr_total_trans.attr,
	&_attr_time_in_state.attr,
#ifdef CONFIG_CPU_FREQ_STAT_DETAILS
	&_attr_trans_table.attr,
#endif
	NULL
};
static struct attribute_group stats_attr_group = {
	.attrs = default_attrs,
	.name = "stats"
};

static int
freq_table_get_index(struct cpufreq_stats *stat, unsigned int freq)
{
	int index;
	for (index = 0; index < stat->max_state; index++)
		if (stat->freq_table[index] == freq)
			return index;
	return -1;
}

static void cpufreq_stats_free_table(unsigned int cpu)
{
	struct cpufreq_stats *stat = cpufreq_stats_table[cpu];
	struct cpufreq_policy *policy = cpufreq_cpu_get(cpu);
	if (policy && policy->cpu == cpu)
		sysfs_remove_group(&policy->kobj, &stats_attr_group);
	if (stat) {
		kfree(stat->time_in_state);
		kfree(stat);
	}
	cpufreq_stats_table[cpu] = NULL;
	if (policy)
		cpufreq_cpu_put(policy);
}

static int
cpufreq_stats_create_table (struct cpufreq_policy *policy,
		struct cpufreq_frequency_table *table)
{
	unsigned int i, j, count = 0, ret = 0;
	struct cpufreq_stats *stat;
	struct cpufreq_policy *data;
	unsigned int alloc_size;
	unsigned int cpu = policy->cpu;
	if (cpufreq_stats_table[cpu])
		return -EBUSY;
	if ((stat = kzalloc(sizeof(struct cpufreq_stats), GFP_KERNEL)) == NULL)
		return -ENOMEM;

	data = cpufreq_cpu_get(cpu);
	if (data == NULL) {
		ret = -EINVAL;
		goto error_get_fail;
	}

	if ((ret = sysfs_create_group(&data->kobj, &stats_attr_group)))
		goto error_out;

	stat->cpu = cpu;
	cpufreq_stats_table[cpu] = stat;

	for (i=0; table[i].frequency != CPUFREQ_TABLE_END; i++) {
		unsigned int freq = table[i].frequency;
		if (freq == CPUFREQ_ENTRY_INVALID)
			continue;
		count++;
	}

	alloc_size = count * sizeof(int) + count * sizeof(cputime64_t);

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
	cpufreq_cpu_put(data);
	return 0;
error_out:
	cpufreq_cpu_put(data);
error_get_fail:
	kfree(stat);
	cpufreq_stats_table[cpu] = NULL;
	return ret;
}

static int
cpufreq_stat_notifier_policy (struct notifier_block *nb, unsigned long val,
		void *data)
{
	int ret;
	struct cpufreq_policy *policy = data;
	struct cpufreq_frequency_table *table;
	unsigned int cpu = policy->cpu;
	if (val != CPUFREQ_NOTIFY)
		return 0;
	table = cpufreq_frequency_get_table(cpu);
	if (!table)
		return 0;
	if ((ret = cpufreq_stats_create_table(policy, table)))
		return ret;
	return 0;
}

static int
cpufreq_stat_notifier_trans (struct notifier_block *nb, unsigned long val,
		void *data)
{
	struct cpufreq_freqs *freq = data;
	struct cpufreq_stats *stat;
	int old_index, new_index;

	if (val != CPUFREQ_POSTCHANGE)
		return 0;

	stat = cpufreq_stats_table[freq->cpu];
	if (!stat)
		return 0;

	old_index = freq_table_get_index(stat, freq->old);
	new_index = freq_table_get_index(stat, freq->new);

	cpufreq_stats_update(freq->cpu);
	if (old_index == new_index)
		return 0;

	if (old_index == -1 || new_index == -1)
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

static int __cpuinit cpufreq_stat_cpu_callback(struct notifier_block *nfb,
					       unsigned long action,
					       void *hcpu)
{
	unsigned int cpu = (unsigned long)hcpu;

	switch (action) {
	case CPU_ONLINE:
	case CPU_ONLINE_FROZEN:
		cpufreq_update_policy(cpu);
		break;
	case CPU_DEAD:
	case CPU_DEAD_FROZEN:
		cpufreq_stats_free_table(cpu);
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block cpufreq_stat_cpu_notifier __cpuinitdata =
{
	.notifier_call = cpufreq_stat_cpu_callback,
};

static struct notifier_block notifier_policy_block = {
	.notifier_call = cpufreq_stat_notifier_policy
};

static struct notifier_block notifier_trans_block = {
	.notifier_call = cpufreq_stat_notifier_trans
};

static int
__init cpufreq_stats_init(void)
{
	int ret;
	unsigned int cpu;

	spin_lock_init(&cpufreq_stats_lock);
	if ((ret = cpufreq_register_notifier(&notifier_policy_block,
				CPUFREQ_POLICY_NOTIFIER)))
		return ret;

	if ((ret = cpufreq_register_notifier(&notifier_trans_block,
				CPUFREQ_TRANSITION_NOTIFIER))) {
		cpufreq_unregister_notifier(&notifier_policy_block,
				CPUFREQ_POLICY_NOTIFIER);
		return ret;
	}

	register_hotcpu_notifier(&cpufreq_stat_cpu_notifier);
	for_each_online_cpu(cpu) {
		cpufreq_update_policy(cpu);
	}
	return 0;
}
static void
__exit cpufreq_stats_exit(void)
{
	unsigned int cpu;

	cpufreq_unregister_notifier(&notifier_policy_block,
			CPUFREQ_POLICY_NOTIFIER);
	cpufreq_unregister_notifier(&notifier_trans_block,
			CPUFREQ_TRANSITION_NOTIFIER);
	unregister_hotcpu_notifier(&cpufreq_stat_cpu_notifier);
	for_each_online_cpu(cpu) {
		cpufreq_stats_free_table(cpu);
	}
}

MODULE_AUTHOR ("Zou Nan hai <nanhai.zou@intel.com>");
MODULE_DESCRIPTION ("'cpufreq_stats' - A driver to export cpufreq stats "
				"through sysfs filesystem");
MODULE_LICENSE ("GPL");

module_init(cpufreq_stats_init);
module_exit(cpufreq_stats_exit);
