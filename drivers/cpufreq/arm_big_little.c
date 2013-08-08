/*
 * ARM big.LITTLE Platforms CPUFreq support
 *
 * Copyright (C) 2013 ARM Ltd.
 * Sudeep KarkadaNagesha <sudeep.karkadanagesha@arm.com>
 *
 * Copyright (C) 2013 Linaro.
 * Viresh Kumar <viresh.kumar@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/clk.h>
#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/cpumask.h>
#include <linux/export.h>
#include <linux/mutex.h>
#include <linux/of_platform.h>
#include <linux/opp.h>
#include <linux/slab.h>
#include <linux/topology.h>
#include <linux/types.h>
#include <asm/bL_switcher.h>

#include "arm_big_little.h"

#ifdef CONFIG_BL_SWITCHER
bool bL_switching_enabled;
#endif

#define ACTUAL_FREQ(cluster, freq)	((cluster == A7_CLUSTER) ? freq << 1 : freq)
#define VIRT_FREQ(cluster, freq)	((cluster == A7_CLUSTER) ? freq >> 1 : freq)

static struct cpufreq_arm_bL_ops *arm_bL_ops;
static struct clk *clk[MAX_CLUSTERS];
static struct cpufreq_frequency_table *freq_table[MAX_CLUSTERS + 1];
static atomic_t cluster_usage[MAX_CLUSTERS + 1] = {ATOMIC_INIT(0),
	ATOMIC_INIT(0)};

static unsigned int clk_big_min;	/* (Big) clock frequencies */
static unsigned int clk_little_max;	/* Maximum clock frequency (Little) */

static DEFINE_PER_CPU(unsigned int, physical_cluster);
static DEFINE_PER_CPU(unsigned int, cpu_last_req_freq);

static struct mutex cluster_lock[MAX_CLUSTERS];

static unsigned int find_cluster_maxfreq(int cluster)
{
	int j;
	u32 max_freq = 0, cpu_freq;

	for_each_online_cpu(j) {
		cpu_freq = per_cpu(cpu_last_req_freq, j);

		if ((cluster == per_cpu(physical_cluster, j)) &&
				(max_freq < cpu_freq))
			max_freq = cpu_freq;
	}

	pr_debug("%s: cluster: %d, max freq: %d\n", __func__, cluster,
			max_freq);

	return max_freq;
}

static unsigned int clk_get_cpu_rate(unsigned int cpu)
{
	u32 cur_cluster = per_cpu(physical_cluster, cpu);
	u32 rate = clk_get_rate(clk[cur_cluster]) / 1000;

	/* For switcher we use virtual A15 clock rates */
	if (is_bL_switching_enabled())
		rate = VIRT_FREQ(cur_cluster, rate);

	pr_debug("%s: cpu: %d, cluster: %d, freq: %u\n", __func__, cpu,
			cur_cluster, rate);

	return rate;
}

static unsigned int bL_cpufreq_get_rate(unsigned int cpu)
{
	if (is_bL_switching_enabled()) {
		pr_debug("%s: freq: %d\n", __func__, per_cpu(cpu_last_req_freq,
					cpu));

		return per_cpu(cpu_last_req_freq, cpu);
	} else {
		return clk_get_cpu_rate(cpu);
	}
}

static unsigned int
bL_cpufreq_set_rate(u32 cpu, u32 old_cluster, u32 new_cluster, u32 rate)
{
	u32 new_rate, prev_rate;
	int ret;
	bool bLs = is_bL_switching_enabled();

	mutex_lock(&cluster_lock[new_cluster]);

	if (bLs) {
		prev_rate = per_cpu(cpu_last_req_freq, cpu);
		per_cpu(cpu_last_req_freq, cpu) = rate;
		per_cpu(physical_cluster, cpu) = new_cluster;

		new_rate = find_cluster_maxfreq(new_cluster);
		new_rate = ACTUAL_FREQ(new_cluster, new_rate);
	} else {
		new_rate = rate;
	}

	pr_debug("%s: cpu: %d, old cluster: %d, new cluster: %d, freq: %d\n",
			__func__, cpu, old_cluster, new_cluster, new_rate);

	ret = clk_set_rate(clk[new_cluster], new_rate * 1000);
	if (WARN_ON(ret)) {
		pr_err("clk_set_rate failed: %d, new cluster: %d\n", ret,
				new_cluster);
		if (bLs) {
			per_cpu(cpu_last_req_freq, cpu) = prev_rate;
			per_cpu(physical_cluster, cpu) = old_cluster;
		}

		mutex_unlock(&cluster_lock[new_cluster]);

		return ret;
	}

	mutex_unlock(&cluster_lock[new_cluster]);

	/* Recalc freq for old cluster when switching clusters */
	if (old_cluster != new_cluster) {
		pr_debug("%s: cpu: %d, old cluster: %d, new cluster: %d\n",
				__func__, cpu, old_cluster, new_cluster);

		/* Switch cluster */
		bL_switch_request(cpu, new_cluster);

		mutex_lock(&cluster_lock[old_cluster]);

		/* Set freq of old cluster if there are cpus left on it */
		new_rate = find_cluster_maxfreq(old_cluster);
		new_rate = ACTUAL_FREQ(old_cluster, new_rate);

		if (new_rate) {
			pr_debug("%s: Updating rate of old cluster: %d, to freq: %d\n",
					__func__, old_cluster, new_rate);

			if (clk_set_rate(clk[old_cluster], new_rate * 1000))
				pr_err("%s: clk_set_rate failed: %d, old cluster: %d\n",
						__func__, ret, old_cluster);
		}
		mutex_unlock(&cluster_lock[old_cluster]);
	}

	return 0;
}

/* Validate policy frequency range */
static int bL_cpufreq_verify_policy(struct cpufreq_policy *policy)
{
	u32 cur_cluster = cpu_to_cluster(policy->cpu);

	return cpufreq_frequency_table_verify(policy, freq_table[cur_cluster]);
}

/* Set clock frequency */
static int bL_cpufreq_set_target(struct cpufreq_policy *policy,
		unsigned int target_freq, unsigned int relation)
{
	struct cpufreq_freqs freqs;
	u32 cpu = policy->cpu, freq_tab_idx, cur_cluster, new_cluster,
	    actual_cluster;
	int ret = 0;

	cur_cluster = cpu_to_cluster(cpu);
	new_cluster = actual_cluster = per_cpu(physical_cluster, cpu);

	freqs.old = bL_cpufreq_get_rate(cpu);

	/* Determine valid target frequency using freq_table */
	cpufreq_frequency_table_target(policy, freq_table[cur_cluster],
			target_freq, relation, &freq_tab_idx);
	freqs.new = freq_table[cur_cluster][freq_tab_idx].frequency;

	pr_debug("%s: cpu: %d, cluster: %d, oldfreq: %d, target freq: %d, new freq: %d\n",
			__func__, cpu, cur_cluster, freqs.old, target_freq,
			freqs.new);

	if (freqs.old == freqs.new)
		return 0;

	if (is_bL_switching_enabled()) {
		if ((actual_cluster == A15_CLUSTER) &&
				(freqs.new < clk_big_min)) {
			new_cluster = A7_CLUSTER;
		} else if ((actual_cluster == A7_CLUSTER) &&
				(freqs.new > clk_little_max)) {
			new_cluster = A15_CLUSTER;
		}
	}

	cpufreq_notify_transition(policy, &freqs, CPUFREQ_PRECHANGE);

	ret = bL_cpufreq_set_rate(cpu, actual_cluster, new_cluster, freqs.new);
	if (ret)
		return ret;

	policy->cur = freqs.new;

	cpufreq_notify_transition(policy, &freqs, CPUFREQ_POSTCHANGE);

	return ret;
}

static inline u32 get_table_count(struct cpufreq_frequency_table *table)
{
	int count;

	for (count = 0; table[count].frequency != CPUFREQ_TABLE_END; count++)
		;

	return count;
}

/* get the minimum frequency in the cpufreq_frequency_table */
static inline u32 get_table_min(struct cpufreq_frequency_table *table)
{
	int i;
	uint32_t min_freq = ~0;
	for (i = 0; (table[i].frequency != CPUFREQ_TABLE_END); i++)
		if (table[i].frequency < min_freq)
			min_freq = table[i].frequency;
	return min_freq;
}

/* get the maximum frequency in the cpufreq_frequency_table */
static inline u32 get_table_max(struct cpufreq_frequency_table *table)
{
	int i;
	uint32_t max_freq = 0;
	for (i = 0; (table[i].frequency != CPUFREQ_TABLE_END); i++)
		if (table[i].frequency > max_freq)
			max_freq = table[i].frequency;
	return max_freq;
}

static int merge_cluster_tables(void)
{
	int i, j, k = 0, count = 1;
	struct cpufreq_frequency_table *table;

	for (i = 0; i < MAX_CLUSTERS; i++)
		count += get_table_count(freq_table[i]);

	table = kzalloc(sizeof(*table) * count, GFP_KERNEL);
	if (!table)
		return -ENOMEM;

	freq_table[MAX_CLUSTERS] = table;

	/* Add in reverse order to get freqs in increasing order */
	for (i = MAX_CLUSTERS - 1; i >= 0; i--) {
		for (j = 0; freq_table[i][j].frequency != CPUFREQ_TABLE_END;
				j++) {
			table[k].frequency = VIRT_FREQ(i,
					freq_table[i][j].frequency);
			pr_debug("%s: index: %d, freq: %d\n", __func__, k,
					table[k].frequency);
			k++;
		}
	}

	table[k].index = k;
	table[k].frequency = CPUFREQ_TABLE_END;

	pr_debug("%s: End, table: %p, count: %d\n", __func__, table, k);

	return 0;
}

static void _put_cluster_clk_and_freq_table(struct device *cpu_dev)
{
	u32 cluster = cpu_to_cluster(cpu_dev->id);

	if (!atomic_dec_return(&cluster_usage[cluster])) {
		clk_put(clk[cluster]);
		opp_free_cpufreq_table(cpu_dev, &freq_table[cluster]);
		dev_dbg(cpu_dev, "%s: cluster: %d\n", __func__, cluster);
	}
}

static void put_cluster_clk_and_freq_table(struct device *cpu_dev)
{
	u32 cluster = cpu_to_cluster(cpu_dev->id);
	int i;

	if (cluster < MAX_CLUSTERS)
		return _put_cluster_clk_and_freq_table(cpu_dev);

	if (atomic_dec_return(&cluster_usage[MAX_CLUSTERS]))
		return;

	for (i = 0; i < MAX_CLUSTERS; i++) {
		struct device *cdev = get_cpu_device(i);
		if (!cdev) {
			pr_err("%s: failed to get cpu%d device\n", __func__, i);
			return;
		}

		_put_cluster_clk_and_freq_table(cdev);
	}

	/* free virtual table */
	kfree(freq_table[MAX_CLUSTERS]);
}

static int _get_cluster_clk_and_freq_table(struct device *cpu_dev)
{
	u32 cluster = cpu_to_cluster(cpu_dev->id);
	char name[14] = "cpu-cluster.X";
	int ret;

	if (atomic_inc_return(&cluster_usage[cluster]) != 1)
		return 0;

	ret = arm_bL_ops->init_opp_table(cpu_dev);
	if (ret) {
		dev_err(cpu_dev, "%s: init_opp_table failed, cpu: %d, err: %d\n",
				__func__, cpu_dev->id, ret);
		goto atomic_dec;
	}

	ret = opp_init_cpufreq_table(cpu_dev, &freq_table[cluster]);
	if (ret) {
		dev_err(cpu_dev, "%s: failed to init cpufreq table, cpu: %d, err: %d\n",
				__func__, cpu_dev->id, ret);
		goto atomic_dec;
	}

	name[12] = cluster + '0';
	clk[cluster] = clk_get_sys(name, NULL);
	if (!IS_ERR(clk[cluster])) {
		dev_dbg(cpu_dev, "%s: clk: %p & freq table: %p, cluster: %d\n",
				__func__, clk[cluster], freq_table[cluster],
				cluster);
		return 0;
	}

	dev_err(cpu_dev, "%s: Failed to get clk for cpu: %d, cluster: %d\n",
			__func__, cpu_dev->id, cluster);
	ret = PTR_ERR(clk[cluster]);
	opp_free_cpufreq_table(cpu_dev, &freq_table[cluster]);

atomic_dec:
	atomic_dec(&cluster_usage[cluster]);
	dev_err(cpu_dev, "%s: Failed to get data for cluster: %d\n", __func__,
			cluster);
	return ret;
}

static int get_cluster_clk_and_freq_table(struct device *cpu_dev)
{
	u32 cluster = cpu_to_cluster(cpu_dev->id);
	int i, ret;

	if (cluster < MAX_CLUSTERS)
		return _get_cluster_clk_and_freq_table(cpu_dev);

	if (atomic_inc_return(&cluster_usage[MAX_CLUSTERS]) != 1)
		return 0;

	/*
	 * Get data for all clusters and fill virtual cluster with a merge of
	 * both
	 */
	for (i = 0; i < MAX_CLUSTERS; i++) {
		struct device *cdev = get_cpu_device(i);
		if (!cdev) {
			pr_err("%s: failed to get cpu%d device\n", __func__, i);
			return -ENODEV;
		}

		ret = _get_cluster_clk_and_freq_table(cdev);
		if (ret)
			goto put_clusters;
	}

	ret = merge_cluster_tables();
	if (ret)
		goto put_clusters;

	/* Assuming 2 cluster, set clk_big_min and clk_little_max */
	clk_big_min = get_table_min(freq_table[0]);
	clk_little_max = VIRT_FREQ(1, get_table_max(freq_table[1]));

	pr_debug("%s: cluster: %d, clk_big_min: %d, clk_little_max: %d\n",
			__func__, cluster, clk_big_min, clk_little_max);

	return 0;

put_clusters:
	while (i--) {
		struct device *cdev = get_cpu_device(i);
		if (!cdev) {
			pr_err("%s: failed to get cpu%d device\n", __func__, i);
			return -ENODEV;
		}

		_put_cluster_clk_and_freq_table(cdev);
	}

	atomic_dec(&cluster_usage[MAX_CLUSTERS]);

	return ret;
}

/* Per-CPU initialization */
static int bL_cpufreq_init(struct cpufreq_policy *policy)
{
	u32 cur_cluster = cpu_to_cluster(policy->cpu);
	struct device *cpu_dev;
	int ret;

	cpu_dev = get_cpu_device(policy->cpu);
	if (!cpu_dev) {
		pr_err("%s: failed to get cpu%d device\n", __func__,
				policy->cpu);
		return -ENODEV;
	}

	ret = get_cluster_clk_and_freq_table(cpu_dev);
	if (ret)
		return ret;

	ret = cpufreq_frequency_table_cpuinfo(policy, freq_table[cur_cluster]);
	if (ret) {
		dev_err(cpu_dev, "CPU %d, cluster: %d invalid freq table\n",
				policy->cpu, cur_cluster);
		put_cluster_clk_and_freq_table(cpu_dev);
		return ret;
	}

	cpufreq_frequency_table_get_attr(freq_table[cur_cluster], policy->cpu);

	if (cur_cluster < MAX_CLUSTERS) {
		cpumask_copy(policy->cpus, topology_core_cpumask(policy->cpu));

		per_cpu(physical_cluster, policy->cpu) = cur_cluster;
	} else {
		/* Assumption: during init, we are always running on A15 */
		per_cpu(physical_cluster, policy->cpu) = A15_CLUSTER;
	}

	if (arm_bL_ops->get_transition_latency)
		policy->cpuinfo.transition_latency =
			arm_bL_ops->get_transition_latency(cpu_dev);
	else
		policy->cpuinfo.transition_latency = CPUFREQ_ETERNAL;

	policy->cur = clk_get_cpu_rate(policy->cpu);

	if (is_bL_switching_enabled())
		per_cpu(cpu_last_req_freq, policy->cpu) = policy->cur;

	dev_info(cpu_dev, "%s: CPU %d initialized\n", __func__, policy->cpu);
	return 0;
}

/* Export freq_table to sysfs */
static struct freq_attr *bL_cpufreq_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
	NULL,
};

static struct cpufreq_driver bL_cpufreq_driver = {
	.name			= "arm-big-little",
	.flags			= CPUFREQ_STICKY,
	.verify			= bL_cpufreq_verify_policy,
	.target			= bL_cpufreq_set_target,
	.get			= bL_cpufreq_get_rate,
	.init			= bL_cpufreq_init,
	.have_governor_per_policy = true,
	.attr			= bL_cpufreq_attr,
};

static int bL_cpufreq_switcher_notifier(struct notifier_block *nfb,
					unsigned long action, void *_arg)
{
	pr_debug("%s: action: %ld\n", __func__, action);

	switch (action) {
	case BL_NOTIFY_PRE_ENABLE:
	case BL_NOTIFY_PRE_DISABLE:
		cpufreq_unregister_driver(&bL_cpufreq_driver);
		break;

	case BL_NOTIFY_POST_ENABLE:
		set_switching_enabled(true);
		cpufreq_register_driver(&bL_cpufreq_driver);
		break;

	case BL_NOTIFY_POST_DISABLE:
		set_switching_enabled(false);
		cpufreq_register_driver(&bL_cpufreq_driver);
		break;

	default:
		return NOTIFY_DONE;
	}

	return NOTIFY_OK;
}

static struct notifier_block bL_switcher_notifier = {
	.notifier_call = bL_cpufreq_switcher_notifier,
};

int bL_cpufreq_register(struct cpufreq_arm_bL_ops *ops)
{
	int ret, i;

	if (arm_bL_ops) {
		pr_debug("%s: Already registered: %s, exiting\n", __func__,
				arm_bL_ops->name);
		return -EBUSY;
	}

	if (!ops || !strlen(ops->name) || !ops->init_opp_table) {
		pr_err("%s: Invalid arm_bL_ops, exiting\n", __func__);
		return -ENODEV;
	}

	arm_bL_ops = ops;

	ret = bL_switcher_get_enabled();
	set_switching_enabled(ret);

	for (i = 0; i < MAX_CLUSTERS; i++)
		mutex_init(&cluster_lock[i]);

	ret = cpufreq_register_driver(&bL_cpufreq_driver);
	if (ret) {
		pr_info("%s: Failed registering platform driver: %s, err: %d\n",
				__func__, ops->name, ret);
		arm_bL_ops = NULL;
	} else {
		ret = bL_switcher_register_notifier(&bL_switcher_notifier);
		if (ret) {
			cpufreq_unregister_driver(&bL_cpufreq_driver);
			arm_bL_ops = NULL;
		} else {
			pr_info("%s: Registered platform driver: %s\n",
					__func__, ops->name);
		}
	}

	bL_switcher_put_enabled();
	return ret;
}
EXPORT_SYMBOL_GPL(bL_cpufreq_register);

void bL_cpufreq_unregister(struct cpufreq_arm_bL_ops *ops)
{
	if (arm_bL_ops != ops) {
		pr_err("%s: Registered with: %s, can't unregister, exiting\n",
				__func__, arm_bL_ops->name);
		return;
	}

	bL_switcher_get_enabled();
	bL_switcher_unregister_notifier(&bL_switcher_notifier);
	cpufreq_unregister_driver(&bL_cpufreq_driver);
	bL_switcher_put_enabled();
	pr_info("%s: Un-registered platform driver: %s\n", __func__,
			arm_bL_ops->name);

	/* For saving table get/put on every cpu in/out */
	if (is_bL_switching_enabled()) {
		put_cluster_clk_and_freq_table(get_cpu_device(0));
	} else {
		int i;

		for (i = 0; i < MAX_CLUSTERS; i++) {
			struct device *cdev = get_cpu_device(i);
			if (!cdev) {
				pr_err("%s: failed to get cpu%d device\n",
						__func__, i);
				return;
			}

			put_cluster_clk_and_freq_table(cdev);
		}
	}

	arm_bL_ops = NULL;
}
EXPORT_SYMBOL_GPL(bL_cpufreq_unregister);
