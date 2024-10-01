// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt) "dev-cpufreq: " fmt

#include <linux/devfreq.h>
#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/cpumask.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/module.h>
#include "governor.h"

struct cpu_state {
	unsigned int freq;
	unsigned int min_freq;
	unsigned int max_freq;
	bool on;
	unsigned int first_cpu;
};
static struct cpu_state *state[NR_CPUS];
static int cpufreq_cnt;

struct freq_map {
	unsigned int cpu_khz;
	unsigned int target_freq;
};

struct devfreq_node {
	struct devfreq *df;
	void *orig_data;
	struct device *dev;
	struct device_node *of_node;
	struct list_head list;
	struct freq_map **map;
	struct freq_map *common_map;
	unsigned int timeout;
	struct delayed_work dwork;
	bool drop;
	unsigned long prev_tgt;
};
static LIST_HEAD(devfreq_list);
static DEFINE_MUTEX(state_lock);
static DEFINE_MUTEX(cpufreq_reg_lock);

#define show_attr(name) \
static ssize_t name##_show(struct device *dev,				\
			struct device_attribute *attr, char *buf)	\
{									\
	struct devfreq *df = to_devfreq(dev);				\
	struct devfreq_node *n = df->data;				\
	return scnprintf(buf, PAGE_SIZE, "%u\n", n->name);		\
}

#define store_attr(name, _min, _max) \
static ssize_t name##_store(struct device *dev,				\
			struct device_attribute *attr, const char *buf,	\
			size_t count)					\
{									\
	struct devfreq *df = to_devfreq(dev);				\
	struct devfreq_node *n = df->data;				\
	int ret;							\
	unsigned int val;						\
	ret = kstrtoint(buf, 10, &val);					\
	if (ret)							\
		return ret;						\
	val = max(val, _min);						\
	val = min(val, _max);						\
	n->name = val;							\
	return count;							\
}

static int update_node(struct devfreq_node *node)
{
	int ret;
	struct devfreq *df = node->df;

	if (!df)
		return 0;

	cancel_delayed_work_sync(&node->dwork);

	mutex_lock(&df->lock);
	node->drop = false;
	ret = update_devfreq(df);
	if (ret) {
		dev_err(df->dev.parent, "Unable to update frequency\n");
		goto out;
	}

	if (!node->timeout)
		goto out;

	if (df->previous_freq <= df->scaling_min_freq)
		goto out;

	schedule_delayed_work(&node->dwork,
			      msecs_to_jiffies(node->timeout));
out:
	mutex_unlock(&df->lock);
	return ret;
}

static void update_all_devfreqs(void)
{
	struct devfreq_node *node;

	list_for_each_entry(node, &devfreq_list, list) {
		update_node(node);
	}
}

static void do_timeout(struct work_struct *work)
{
	struct devfreq_node *node = container_of(to_delayed_work(work),
						struct devfreq_node, dwork);
	struct devfreq *df = node->df;

	mutex_lock(&df->lock);
	node->drop = true;
	update_devfreq(df);
	mutex_unlock(&df->lock);
}

static struct devfreq_node *find_devfreq_node(struct device *dev)
{
	struct devfreq_node *node;

	list_for_each_entry(node, &devfreq_list, list)
		if (node->dev == dev || node->of_node == dev->of_node)
			return node;

	return NULL;
}

/* ==================== cpufreq part ==================== */
static void add_policy(struct cpufreq_policy *policy)
{
	struct cpu_state *new_state;
	unsigned int cpu, first_cpu;

	if (state[policy->cpu]) {
		state[policy->cpu]->freq = policy->cur;
		state[policy->cpu]->on = true;
	} else {
		new_state = kzalloc(sizeof(struct cpu_state), GFP_KERNEL);
		if (!new_state)
			return;

		first_cpu = cpumask_first(policy->related_cpus);
		new_state->first_cpu = first_cpu;
		new_state->freq = policy->cur;
		new_state->min_freq = policy->cpuinfo.min_freq;
		new_state->max_freq = policy->cpuinfo.max_freq;
		new_state->on = true;

		for_each_cpu(cpu, policy->related_cpus)
			state[cpu] = new_state;
	}
}

static int cpufreq_trans_notifier(struct notifier_block *nb,
		unsigned long event, void *data)
{
	struct cpufreq_freqs *freq = data;
	struct cpu_state *s;

	if (event != CPUFREQ_POSTCHANGE)
		return 0;

	mutex_lock(&state_lock);

	s = state[freq->policy->cpu];
	if (!s)
		goto out;

	if (s->freq != freq->new) {
		s->freq = freq->new;
		update_all_devfreqs();
	}

out:
	mutex_unlock(&state_lock);
	return 0;
}

static struct notifier_block cpufreq_trans_nb = {
	.notifier_call = cpufreq_trans_notifier
};

static int devfreq_cpufreq_hotplug_coming_up(unsigned int cpu)
{
	struct cpufreq_policy *policy;

	policy = cpufreq_cpu_get(cpu);
	if (!policy) {
		pr_err("Policy is null for cpu =%d\n", cpu);
		return 0;
	}
	mutex_lock(&state_lock);
	add_policy(policy);
	update_all_devfreqs();
	mutex_unlock(&state_lock);
	return 0;
}

static int devfreq_cpufreq_hotplug_going_down(unsigned int cpu)
{
	struct cpufreq_policy *policy;

	policy = cpufreq_cpu_get(cpu);
	if (!policy) {
		pr_err("Policy is null for cpu =%d\n", cpu);
		return 0;
	}
	mutex_lock(&state_lock);
	if (state[policy->cpu]) {
		state[policy->cpu]->on = false;
		update_all_devfreqs();
	}
	mutex_unlock(&state_lock);
	return 0;
}

static int devfreq_cpufreq_cpu_hp_init(void)
{
	int ret = 0;

	ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN,
				"DEVFREQ_CPUFREQ",
				devfreq_cpufreq_hotplug_coming_up,
				devfreq_cpufreq_hotplug_going_down);
	if (ret < 0) {
		cpuhp_remove_state(CPUHP_AP_ONLINE_DYN);
		pr_err("devfreq-cpufreq: failed to register HP notifier: %d\n",
									ret);
	} else
		ret = 0;
	return ret;
}

static int register_cpufreq(void)
{
	int ret = 0;
	unsigned int cpu;
	struct cpufreq_policy *policy;

	mutex_lock(&cpufreq_reg_lock);

	if (cpufreq_cnt)
		goto cnt_not_zero;

	cpus_read_lock();

	ret = devfreq_cpufreq_cpu_hp_init();
	if (ret < 0)
		goto out;

	ret = cpufreq_register_notifier(&cpufreq_trans_nb,
				CPUFREQ_TRANSITION_NOTIFIER);

	if (ret)
		goto out;

	for_each_online_cpu(cpu) {
		policy = cpufreq_cpu_get(cpu);
		if (policy) {
			add_policy(policy);
			cpufreq_cpu_put(policy);
		}
	}
out:
	cpus_read_unlock();
cnt_not_zero:
	if (!ret)
		cpufreq_cnt++;
	mutex_unlock(&cpufreq_reg_lock);
	return ret;
}

static int unregister_cpufreq(void)
{
	int cpu;

	mutex_lock(&cpufreq_reg_lock);

	if (cpufreq_cnt > 1)
		goto out;

	cpuhp_remove_state(CPUHP_AP_ONLINE_DYN);

	cpufreq_unregister_notifier(&cpufreq_trans_nb,
				CPUFREQ_TRANSITION_NOTIFIER);

	for (cpu = ARRAY_SIZE(state) - 1; cpu >= 0; cpu--) {
		if (!state[cpu])
			continue;
		if (state[cpu]->first_cpu == cpu)
			kfree(state[cpu]);
		state[cpu] = NULL;
	}

out:
	cpufreq_cnt--;
	mutex_unlock(&cpufreq_reg_lock);
	return 0;
}

/* ==================== devfreq part ==================== */

static unsigned int interpolate_freq(struct devfreq *df, unsigned int cpu)
{
	unsigned long *freq_table = df->profile->freq_table;
	unsigned int cpu_min = state[cpu]->min_freq;
	unsigned int cpu_max = state[cpu]->max_freq;
	unsigned int cpu_freq = state[cpu]->freq;
	unsigned int dev_min, dev_max, cpu_percent;

	if (freq_table) {
		dev_min = freq_table[0];
		dev_max = freq_table[df->profile->max_state - 1];
	} else {
		if (df->scaling_max_freq <= df->scaling_min_freq)
			return 0;
		dev_min = df->scaling_min_freq;
		dev_max = df->scaling_max_freq;
	}

	cpu_percent = ((cpu_freq - cpu_min) * 100) / (cpu_max - cpu_min);
	return dev_min + mult_frac(dev_max - dev_min, cpu_percent, 100);
}

static unsigned int cpu_to_dev_freq(struct devfreq *df, unsigned int cpu)
{
	struct freq_map *map = NULL;
	unsigned int cpu_khz = 0, freq;
	struct devfreq_node *n = df->data;

	if (!state[cpu] || !state[cpu]->on || state[cpu]->first_cpu != cpu) {
		freq = 0;
		goto out;
	}

	if (n->common_map)
		map = n->common_map;
	else if (n->map)
		map = n->map[cpu];

	cpu_khz = state[cpu]->freq;

	if (!map) {
		freq = interpolate_freq(df, cpu);
		goto out;
	}

	while (map->cpu_khz && map->cpu_khz < cpu_khz)
		map++;
	if (!map->cpu_khz)
		map--;
	freq = map->target_freq;

out:
	dev_dbg(df->dev.parent, "CPU%u: %d -> dev: %u\n", cpu, cpu_khz, freq);
	return freq;
}

static int devfreq_cpufreq_get_freq(struct devfreq *df,
					unsigned long *freq)
{
	unsigned int cpu, tgt_freq = 0;
	struct devfreq_node *node;

	node = df->data;
	if (!node) {
		pr_err("Unable to find devfreq node!\n");
		return -ENODEV;
	}

	if (node->drop) {
		*freq = 0;
		return 0;
	}

	for_each_possible_cpu(cpu)
		tgt_freq = max(tgt_freq, cpu_to_dev_freq(df, cpu));

	if (node->timeout && tgt_freq < node->prev_tgt)
		*freq = 0;
	else
		*freq = tgt_freq;

	node->prev_tgt = tgt_freq;

	return 0;
}

static unsigned int show_table(char *buf, unsigned int len,
				struct freq_map *map)
{
	unsigned int cnt = 0;

	cnt += scnprintf(buf + cnt, len - cnt, "CPU freq\tDevice freq\n");

	while (map->cpu_khz && cnt < len) {
		cnt += scnprintf(buf + cnt, len - cnt, "%8u\t%11u\n",
				map->cpu_khz, map->target_freq);
		map++;
	}
	if (cnt < len)
		cnt += scnprintf(buf + cnt, len - cnt, "\n");

	return cnt;
}

static ssize_t freq_map_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct devfreq *df = to_devfreq(dev);
	struct devfreq_node *n = df->data;
	struct freq_map *map;
	unsigned int cnt = 0, cpu;

	mutex_lock(&state_lock);
	if (n->common_map) {
		map = n->common_map;
		cnt += scnprintf(buf + cnt, PAGE_SIZE - cnt,
				"Common table for all CPUs:\n");
		cnt += show_table(buf + cnt, PAGE_SIZE - cnt, map);
	} else if (n->map) {
		for_each_possible_cpu(cpu) {
			map = n->map[cpu];
			if (!map)
				continue;
			cnt += scnprintf(buf + cnt, PAGE_SIZE - cnt,
					"CPU %u:\n", cpu);
			if (cnt >= PAGE_SIZE)
				break;
			cnt += show_table(buf + cnt, PAGE_SIZE - cnt, map);
			if (cnt >= PAGE_SIZE)
				break;
		}
	} else {
		cnt += scnprintf(buf + cnt, PAGE_SIZE - cnt,
				"Device freq interpolated based on CPU freq\n");
	}
	mutex_unlock(&state_lock);

	return cnt;
}

static DEVICE_ATTR_RO(freq_map);
show_attr(timeout);
store_attr(timeout, 0U, 100U);
static DEVICE_ATTR_RW(timeout);

static struct attribute *dev_attr[] = {
	&dev_attr_freq_map.attr,
	&dev_attr_timeout.attr,
	NULL,
};

static struct attribute_group dev_attr_group = {
	.name = "cpufreq",
	.attrs = dev_attr,
};

static int devfreq_cpufreq_gov_start(struct devfreq *devfreq)
{
	int ret = 0;
	struct devfreq_node *node;
	bool alloc = false;

	ret = register_cpufreq();
	if (ret)
		return ret;

	ret = sysfs_create_group(&devfreq->dev.kobj, &dev_attr_group);
	if (ret) {
		unregister_cpufreq();
		return ret;
	}

	mutex_lock(&state_lock);

	node = find_devfreq_node(devfreq->dev.parent);
	if (node == NULL) {
		node = kzalloc(sizeof(struct devfreq_node), GFP_KERNEL);
		if (!node) {
			ret = -ENOMEM;
			goto alloc_fail;
		}
		alloc = true;
		node->dev = devfreq->dev.parent;
		list_add_tail(&node->list, &devfreq_list);
	}

	INIT_DELAYED_WORK(&node->dwork, do_timeout);

	node->df = devfreq;
	node->orig_data = devfreq->data;
	devfreq->data = node;

	ret = update_node(node);
	if (ret)
		goto update_fail;

	mutex_unlock(&state_lock);
	return 0;

update_fail:
	devfreq->data = node->orig_data;
	if (alloc) {
		list_del(&node->list);
		kfree(node);
	}
alloc_fail:
	mutex_unlock(&state_lock);
	sysfs_remove_group(&devfreq->dev.kobj, &dev_attr_group);
	unregister_cpufreq();
	return ret;
}

static void devfreq_cpufreq_gov_stop(struct devfreq *devfreq)
{
	struct devfreq_node *node = devfreq->data;

	cancel_delayed_work_sync(&node->dwork);

	mutex_lock(&state_lock);
	devfreq->data = node->orig_data;
	if (node->map || node->common_map) {
		node->df = NULL;
	} else {
		list_del(&node->list);
		kfree(node);
	}
	mutex_unlock(&state_lock);

	sysfs_remove_group(&devfreq->dev.kobj, &dev_attr_group);
	unregister_cpufreq();
}

static int devfreq_cpufreq_ev_handler(struct devfreq *devfreq,
					unsigned int event, void *data)
{
	int ret;

	switch (event) {
	case DEVFREQ_GOV_START:

		ret = devfreq_cpufreq_gov_start(devfreq);
		if (ret) {
			pr_err("Governor start failed!\n");
			return ret;
		}
		pr_debug("Enabled dev CPUfreq governor\n");
		break;

	case DEVFREQ_GOV_STOP:

		devfreq_cpufreq_gov_stop(devfreq);
		pr_debug("Disabled dev CPUfreq governor\n");
		break;
	}

	return 0;
}

static struct devfreq_governor devfreq_cpufreq = {
	.name = "cpufreq",
	.get_target_freq = devfreq_cpufreq_get_freq,
	.event_handler = devfreq_cpufreq_ev_handler,
};

#define NUM_COLS	2
static struct freq_map *read_tbl(struct device_node *of_node, char *prop_name)
{
	int len, nf, i, j;
	u32 data;
	struct freq_map *tbl;

	if (!of_find_property(of_node, prop_name, &len))
		return NULL;
	len /= sizeof(data);

	if (len % NUM_COLS || len == 0)
		return NULL;
	nf = len / NUM_COLS;

	tbl = kzalloc((nf + 1) * sizeof(*tbl), GFP_KERNEL);
	if (!tbl)
		return NULL;

	for (i = 0, j = 0; i < nf; i++, j += 2) {
		of_property_read_u32_index(of_node, prop_name, j, &data);
		tbl[i].cpu_khz = data;

		of_property_read_u32_index(of_node, prop_name, j + 1, &data);
		tbl[i].target_freq = data;
	}
	tbl[i].cpu_khz = 0;

	return tbl;
}

#define PROP_TARGET "target-dev"
#define PROP_TABLE "cpu-to-dev-map"
static int add_table_from_of(struct device_node *of_node)
{
	struct device_node *target_of_node;
	struct devfreq_node *node;
	struct freq_map *common_tbl;
	struct freq_map **tbl_list = NULL;
	static char prop_name[] = PROP_TABLE "-999999";
	int cpu, ret, cnt = 0, prop_sz = ARRAY_SIZE(prop_name);

	target_of_node = of_parse_phandle(of_node, PROP_TARGET, 0);
	if (!target_of_node)
		return -EINVAL;

	node = kzalloc(sizeof(struct devfreq_node), GFP_KERNEL);
	if (!node)
		return -ENOMEM;

	common_tbl = read_tbl(of_node, PROP_TABLE);
	if (!common_tbl) {
		tbl_list = kcalloc(num_possible_cpus(), sizeof(*tbl_list), GFP_KERNEL);
		if (!tbl_list) {
			ret = -ENOMEM;
			goto err_list;
		}

		for_each_possible_cpu(cpu) {
			ret = scnprintf(prop_name, prop_sz, "%s-%d",
					PROP_TABLE, cpu);
			if (ret >= prop_sz) {
				pr_warn("More CPUs than I can handle!\n");
				pr_warn("Skipping rest of the tables!\n");
				break;
			}
			tbl_list[cpu] = read_tbl(of_node, prop_name);
			if (tbl_list[cpu])
				cnt++;
		}
	}
	if (!common_tbl && !cnt) {
		ret = -EINVAL;
		goto err_tbl;
	}

	mutex_lock(&state_lock);
	node->of_node = target_of_node;
	node->map = tbl_list;
	node->common_map = common_tbl;
	list_add_tail(&node->list, &devfreq_list);
	mutex_unlock(&state_lock);

	return 0;
err_tbl:
	kfree(tbl_list);
err_list:
	kfree(node);
	return ret;
}

static int __init devfreq_cpufreq_init(void)
{
	int ret;
	struct device_node *of_par, *of_child;

	of_par = of_find_node_by_name(NULL, "devfreq-cpufreq");
	if (of_par) {
		for_each_child_of_node(of_par, of_child) {
			ret = add_table_from_of(of_child);
			if (ret)
				pr_err("Parsing %s failed!\n", of_child->name);
			else
				pr_debug("Parsed %s.\n", of_child->name);
		}
		of_node_put(of_par);
	} else {
		pr_info("No tables parsed from DT.\n");
		return 0;
	}

	ret = devfreq_add_governor(&devfreq_cpufreq);
	if (ret) {
		pr_err("Governor add failed!\n");
		return ret;
	}
	pr_err("Governor add success for cpufreq!\n");

	return 0;
}
subsys_initcall(devfreq_cpufreq_init);

static void __exit devfreq_cpufreq_exit(void)
{
	int ret, cpu;
	struct devfreq_node *node, *tmp;
	struct device_node *of_par;

	of_par = of_find_node_by_name(NULL, "devfreq-cpufreq");
	if (!of_par)
		return;

	ret = devfreq_remove_governor(&devfreq_cpufreq);
	if (ret)
		pr_err("Governor remove failed!\n");

	mutex_lock(&state_lock);
	list_for_each_entry_safe(node, tmp, &devfreq_list, list) {
		kfree(node->common_map);
		for_each_possible_cpu(cpu)
			kfree(node->map[cpu]);
		kfree(node->map);
		list_del(&node->list);
		kfree(node);
	}
	mutex_unlock(&state_lock);
}
module_exit(devfreq_cpufreq_exit);

MODULE_DESCRIPTION("CPU freq based generic governor for devfreq devices");
MODULE_LICENSE("GPL");
