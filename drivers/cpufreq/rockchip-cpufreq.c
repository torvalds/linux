/*
 * Rockchip CPUFreq Driver
 *
 * Copyright (C) 2017 Fuzhou Rockchip Electronics Co., Ltd
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

#include <linux/clk.h>
#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/nvmem-consumer.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/reboot.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/soc/rockchip/pvtm.h>
#include <linux/thermal.h>
#include <linux/rockchip/cpu.h>
#include <soc/rockchip/rockchip_opp_select.h>

#include "../clk/rockchip/clk.h"

#define LEAKAGE_INVALID		0xff
#define REBOOT_FREQ		816000 /* kHz */

struct cluster_info {
	struct list_head list_head;
	cpumask_t cpus;
	unsigned int reboot_freq;
	unsigned int threshold_freq;
	unsigned int scale_rate;
	unsigned int temp_limit_rate;
	int volt_sel;
	int scale;
	int process;
	bool offline;
	bool rebooting;
	bool freq_limit;
	bool is_check_init;
};
static LIST_HEAD(cluster_info_list);

static int rk3288_get_soc_info(struct device *dev, struct device_node *np,
			       int *bin, int *process)
{
	int ret = 0, value = -EINVAL;
	char *name;

	if (!bin)
		goto next;
	if (of_property_match_string(np, "nvmem-cell-names", "special") >= 0) {
		ret = rockchip_get_efuse_value(np, "special", &value);
		if (ret) {
			dev_err(dev, "Failed to get soc special value\n");
			goto out;
		}
		if (value == 0xc)
			*bin = 0;
		else
			*bin = 1;
	}

	if (soc_is_rk3288w())
		name = "performance-w";
	else
		name = "performance";

	if (of_property_match_string(np, "nvmem-cell-names", name) >= 0) {
		ret = rockchip_get_efuse_value(np, name, &value);
		if (ret) {
			dev_err(dev, "Failed to get soc performance value\n");
			goto out;
		}
		if (value & 0x2)
			*bin = 3;
		else if (value & 0x01)
			*bin = 2;
	}
	if (*bin >= 0)
		dev_info(dev, "bin=%d\n", *bin);

next:
	if (!process)
		goto out;
	if (of_property_match_string(np, "nvmem-cell-names",
				     "process") >= 0) {
		ret = rockchip_get_efuse_value(np, "process", &value);
		if (ret) {
			dev_err(dev, "Failed to get soc process version\n");
			goto out;
		}
		if (value == 0 || value == 1)
			*process = 0;
	}
	if (*process >= 0)
		dev_info(dev, "process=%d\n", *process);

out:
	return ret;
}

static const struct of_device_id rockchip_cpufreq_of_match[] = {
	{
		.compatible = "rockchip,rk3288",
		.data = (void *)&rk3288_get_soc_info,
	},
	{
		.compatible = "rockchip,rk3288w",
		.data = (void *)&rk3288_get_soc_info,
	},
	{},
};

static struct cluster_info *rockchip_cluster_info_lookup(int cpu)
{
	struct cluster_info *cluster;

	list_for_each_entry(cluster, &cluster_info_list, list_head) {
		if (cpumask_test_cpu(cpu, &cluster->cpus))
			return cluster;
	}

	return NULL;
}

static struct cluster_info *rockchip_cluster_lookup_by_dev(struct device *dev)
{
	struct cluster_info *cluster;
	struct device *cpu_dev;
	int cpu;

	list_for_each_entry(cluster, &cluster_info_list, list_head) {
		for_each_cpu(cpu, &cluster->cpus) {
			cpu_dev = get_cpu_device(cpu);
			if (!cpu_dev)
				continue;
			if (cpu_dev == dev)
				return cluster;
		}
	}

	return NULL;
}

int rockchip_cpufreq_get_scale(int cpu)
{
	struct cluster_info *cluster;

	cluster = rockchip_cluster_info_lookup(cpu);
	if (!cluster)
		return 0;
	else
		return cluster->scale;
}
EXPORT_SYMBOL_GPL(rockchip_cpufreq_get_scale);

int rockchip_cpufreq_set_scale_rate(struct device *dev, unsigned long rate)
{
	struct cluster_info *cluster;

	cluster = rockchip_cluster_lookup_by_dev(dev);
	if (!cluster)
		return -EINVAL;
	cluster->scale_rate = rate / 1000;

	return 0;
}
EXPORT_SYMBOL_GPL(rockchip_cpufreq_set_scale_rate);

int rockchip_cpufreq_check_rate_volt(struct device *dev)
{
	struct cluster_info *cluster;

	cluster = rockchip_cluster_lookup_by_dev(dev);
	if (!cluster)
		return -EINVAL;
	if (cluster->is_check_init)
		return 0;
	dev_pm_opp_check_rate_volt(dev, true);
	cluster->is_check_init = true;

	return 0;
}
EXPORT_SYMBOL_GPL(rockchip_cpufreq_check_rate_volt);

int rockchip_cpufreq_set_temp_limit_rate(struct device *dev, unsigned long rate)
{
	struct cluster_info *cluster;

	cluster = rockchip_cluster_lookup_by_dev(dev);
	if (!cluster)
		return -EINVAL;
	cluster->temp_limit_rate = rate / 1000;

	return 0;
}
EXPORT_SYMBOL_GPL(rockchip_cpufreq_set_temp_limit_rate);

int rockchip_cpufreq_update_policy(struct device *dev)
{
	struct cluster_info *cluster;
	unsigned int cpu;

	cluster = rockchip_cluster_lookup_by_dev(dev);
	if (!cluster)
		return -EINVAL;
	cpu = cpumask_any(&cluster->cpus);
	cpufreq_update_policy(cpu);

	return 0;
}
EXPORT_SYMBOL_GPL(rockchip_cpufreq_update_policy);

int rockchip_cpufreq_update_cur_volt(struct device *dev)
{
	struct cluster_info *cluster;
	struct cpufreq_policy *policy;
	unsigned int cpu;

	cluster = rockchip_cluster_lookup_by_dev(dev);
	if (!cluster)
		return -EINVAL;
	cpu = cpumask_any(&cluster->cpus);

	policy = cpufreq_cpu_get(cpu);
	if (!policy)
		return -ENODEV;
	down_write(&policy->rwsem);
	dev_pm_opp_check_rate_volt(dev, false);
	up_write(&policy->rwsem);

	return 0;
}
EXPORT_SYMBOL_GPL(rockchip_cpufreq_update_cur_volt);

static int rockchip_cpufreq_cluster_init(int cpu, struct cluster_info *cluster)
{
	struct device_node *np;
	struct device *dev;
	int ret = 0, bin = -EINVAL;

	cluster->process = -EINVAL;
	cluster->volt_sel = -EINVAL;
	cluster->scale = 0;

	dev = get_cpu_device(cpu);
	if (!dev)
		return -ENODEV;

	np = of_parse_phandle(dev->of_node, "operating-points-v2", 0);
	if (!np) {
		dev_warn(dev, "OPP-v2 not supported\n");
		return -ENOENT;
	}

	ret = dev_pm_opp_of_get_sharing_cpus(dev, &cluster->cpus);
	if (ret) {
		dev_err(dev, "Failed to get sharing cpus\n");
		goto np_err;
	}

	if (of_property_read_u32(np, "rockchip,reboot-freq",
				 &cluster->reboot_freq))
		cluster->reboot_freq = REBOOT_FREQ;
	of_property_read_u32(np, "rockchip,threshold-freq",
			     &cluster->threshold_freq);
	cluster->freq_limit = of_property_read_bool(np, "rockchip,freq-limit");

	rockchip_get_soc_info(dev, rockchip_cpufreq_of_match,
			      &bin, &cluster->process);
	rockchip_get_scale_volt_sel(dev, "cpu_leakage", "cpu",
				    bin, cluster->process,
				    &cluster->scale, &cluster->volt_sel);
np_err:
	of_node_put(np);
	return ret;
}

static int rockchip_cpufreq_set_opp_info(int cpu, struct cluster_info *cluster)
{
	struct device *dev = get_cpu_device(cpu);

	if (!dev)
		return -ENODEV;
	return rockchip_set_opp_info(dev, cluster->process,
				     cluster->volt_sel);
}

static int rockchip_hotcpu_notifier(struct notifier_block *nb,
				    unsigned long action, void *hcpu)
{
	unsigned int cpu = (unsigned long)hcpu;
	struct cluster_info *cluster;
	cpumask_t cpus;
	int number, ret;

	cluster = rockchip_cluster_info_lookup(cpu);
	if (!cluster)
		return NOTIFY_OK;

	switch (action & ~CPU_TASKS_FROZEN) {
	case CPU_ONLINE:
		if (cluster->offline) {
			ret = rockchip_cpufreq_set_opp_info(cpu, cluster);
			if (ret)
				pr_err("Failed to set cpu%d opp_info\n", cpu);
			cluster->offline = false;
		}
		break;

	case CPU_POST_DEAD:
		cpumask_and(&cpus, &cluster->cpus, cpu_online_mask);
		number = cpumask_weight(&cpus);
		if (!number)
			cluster->offline = true;
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block rockchip_hotcpu_nb = {
	.notifier_call = rockchip_hotcpu_notifier,
};

static int rockchip_reboot_notifier(struct notifier_block *nb,
				    unsigned long action, void *ptr)
{
	int cpu;
	struct cluster_info *cluster;

	list_for_each_entry(cluster, &cluster_info_list, list_head) {
		cpu = cpumask_first_and(&cluster->cpus, cpu_online_mask);
		if (cpu >= nr_cpu_ids)
			continue;
		cluster->rebooting = true;
		cpufreq_update_policy(cpu);
	}

	return NOTIFY_OK;
}

static struct notifier_block rockchip_reboot_nb = {
	.notifier_call = rockchip_reboot_notifier,
};

static int rockchip_cpufreq_policy_notifier(struct notifier_block *nb,
					    unsigned long event, void *data)
{
	struct cpufreq_policy *policy = data;
	struct cluster_info *cluster;
	int cpu = policy->cpu;

	if (event != CPUFREQ_ADJUST)
		return NOTIFY_OK;

	cluster = rockchip_cluster_info_lookup(cpu);
	if (!cluster)
		return NOTIFY_DONE;

	if (cluster->scale_rate) {
		if (cluster->scale_rate < policy->max)
			policy->max = cluster->scale_rate;
	}

	if (cluster->temp_limit_rate) {
		if (cluster->temp_limit_rate < policy->max)
			policy->max = cluster->temp_limit_rate;
	}

	if (cluster->rebooting) {
		if (cluster->reboot_freq < policy->max)
			policy->max = cluster->reboot_freq;
		policy->min = policy->max;
		pr_info("cpu%d limit freq=%d min=%d max=%d\n",
			policy->cpu, cluster->reboot_freq,
			policy->min, policy->max);
		return NOTIFY_OK;
	}

	return NOTIFY_OK;
}

static struct notifier_block rockchip_cpufreq_policy_nb = {
	.notifier_call = rockchip_cpufreq_policy_notifier,
};

static struct cpufreq_policy *rockchip_get_policy(struct cluster_info *cluster)
{
	int first_cpu;

	first_cpu = cpumask_first_and(&cluster->cpus, cpu_online_mask);
	if (first_cpu >= nr_cpu_ids)
		return NULL;

	return cpufreq_cpu_get(first_cpu);
}

/**
 * rockchip_cpufreq_adjust_target() - Adjust cpu target frequency
 * @cpu:	 CPU number
 * @freq: Expected target frequency
 *
 * This adjusts cpu target frequency for reducing power consumption.
 * Only one cluster can eanble frequency limit, and the cluster's
 * maximum frequency will be limited to its threshold frequency, if the
 * other cluster's frequency is geater than or equal to its threshold
 * frequency.
 */
unsigned int rockchip_cpufreq_adjust_target(int cpu, unsigned int freq)
{
	struct cpufreq_policy *policy;
	struct cluster_info *cluster, *temp;

	cluster = rockchip_cluster_info_lookup(cpu);
	if (!cluster || !cluster->threshold_freq)
		goto adjust_out;

	if (cluster->freq_limit) {
		if (freq <= cluster->threshold_freq)
			goto adjust_out;

		list_for_each_entry(temp, &cluster_info_list, list_head) {
			if (temp->freq_limit || temp == cluster ||
			    temp->offline)
				continue;

			policy = rockchip_get_policy(temp);
			if (!policy)
				continue;

			if (temp->threshold_freq &&
			    temp->threshold_freq <= policy->cur) {
				cpufreq_cpu_put(policy);
				return cluster->threshold_freq;
			}
			cpufreq_cpu_put(policy);
		}
	} else {
		if (freq < cluster->threshold_freq)
			goto adjust_out;

		list_for_each_entry(temp, &cluster_info_list, list_head) {
			if (!temp->freq_limit || temp == cluster ||
			    temp->offline)
				continue;

			policy = rockchip_get_policy(temp);
			if (!policy)
				continue;

			if (temp->threshold_freq &&
			    temp->threshold_freq < policy->cur)
				cpufreq_driver_target(policy,
						      temp->threshold_freq,
						      CPUFREQ_RELATION_H);
			cpufreq_cpu_put(policy);
		}
	}

adjust_out:

	return freq;
}
EXPORT_SYMBOL_GPL(rockchip_cpufreq_adjust_target);

static int __init rockchip_cpufreq_driver_init(void)
{
	struct platform_device *pdev;
	struct cluster_info *cluster, *pos;
	int cpu, first_cpu, ret, i = 0;

	for_each_possible_cpu(cpu) {
		cluster = rockchip_cluster_info_lookup(cpu);
		if (cluster)
			continue;

		cluster = kzalloc(sizeof(*cluster), GFP_KERNEL);
		if (!cluster)
			return -ENOMEM;

		ret = rockchip_cpufreq_cluster_init(cpu, cluster);
		if (ret) {
			if (ret != -ENOENT) {
				pr_err("Failed to cpu%d parse_dt\n", cpu);
				return ret;
			}

			/*
			 * As the OPP document said, only one OPP binding
			 * should be used per device.
			 * And if there are multiple clusters on rockchip
			 * platforms, we should use operating-points-v2.
			 * So if don't support operating-points-v2, there must
			 * be only one cluster, the list shuold be null.
			 */
			list_for_each_entry(pos, &cluster_info_list, list_head)
				i++;
			if (i)
				return ret;
			/*
			 * If don't support operating-points-v2, there is no
			 * need to register notifiers.
			 */
			goto next;
		}

		first_cpu = cpumask_first_and(&cluster->cpus, cpu_online_mask);
		ret = rockchip_cpufreq_set_opp_info(first_cpu, cluster);
		if (ret) {
			pr_err("Failed to set cpu%d opp_info\n", first_cpu);
			return ret;
		}

		list_add(&cluster->list_head, &cluster_info_list);
	}

	register_hotcpu_notifier(&rockchip_hotcpu_nb);
	register_reboot_notifier(&rockchip_reboot_nb);
	cpufreq_register_notifier(&rockchip_cpufreq_policy_nb,
				  CPUFREQ_POLICY_NOTIFIER);

next:
	pdev = platform_device_register_simple("cpufreq-dt", -1, NULL, 0);

	return PTR_ERR_OR_ZERO(pdev);
}
module_init(rockchip_cpufreq_driver_init);

MODULE_AUTHOR("Finley Xiao <finley.xiao@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip cpufreq driver");
MODULE_LICENSE("GPL v2");
