// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */
#define pr_fmt(fmt) "%s:%s " fmt, KBUILD_MODNAME, __func__

#include <linux/err.h>
#include <linux/slab.h>
#include <linux/cpufreq.h>
#include <linux/thermal.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/cpu.h>
#include <linux/pm_opp.h>
#include <linux/pm_qos.h>

#define CPU_MAP_CT 2
#define CC_CDEV_DRIVER "CPU-voltage-cdev"

struct limits_freq_table {
	unsigned long frequency;
	unsigned long volt;
};

struct limits_freq_map {
	unsigned long frequency[CPU_MAP_CT];
};

struct cc_limits_data {
	struct list_head		node;
	int				map_freq_ct;
	int				thermal_state;
	int				cpu_map[CPU_MAP_CT];
	struct limits_freq_map		*map_freq;
	struct freq_qos_request		cc_qos_req[CPU_MAP_CT];
	char				cdev_name[THERMAL_NAME_LENGTH];
	struct thermal_cooling_device	*cdev;
};

static DEFINE_MUTEX(cc_list_lock);
static LIST_HEAD(cc_cdev_list);

static int cc_set_cur_state(struct thermal_cooling_device *cdev,
				 unsigned long state)
{
	struct cc_limits_data *cc_cdev = cdev->devdata;
	int idx = 0, ret = 0;

	if (state > cc_cdev->map_freq_ct)
		return -EINVAL;

	if (state == cc_cdev->thermal_state)
		return 0;

	cc_cdev->thermal_state = state;

	for (idx = 0; idx < CPU_MAP_CT; idx++) {
		if (cc_cdev->cpu_map[idx] == -1)
			break;

		pr_debug("Mitigate CPU:%d to freq:%lu\n", cc_cdev->cpu_map[idx],
				cc_cdev->map_freq[state].frequency[idx]);
		ret = freq_qos_update_request(&cc_cdev->cc_qos_req[idx],
				cc_cdev->map_freq[state].frequency[idx]);
		if (ret < 0)
			return ret;
	}
	return 0;
}

static int cc_get_cur_state(struct thermal_cooling_device *cdev,
				 unsigned long *state)
{
	struct cc_limits_data *cc_cdev = cdev->devdata;

	*state = cc_cdev->thermal_state;

	return 0;
}

static int cc_get_max_state(struct thermal_cooling_device *cdev,
				 unsigned long *state)
{
	struct cc_limits_data *cc_cdev = cdev->devdata;

	*state = cc_cdev->map_freq_ct;

	return 0;
}

static struct thermal_cooling_device_ops cc_cooling_ops = {
	.get_max_state = cc_get_max_state,
	.get_cur_state = cc_get_cur_state,
	.set_cur_state = cc_set_cur_state,
};

static int fetch_opp_table(struct device *dev,
		struct limits_freq_table **freq_table_inp)
{
	int idx = 0, max_opp_ct;
	struct limits_freq_table *freq_table = NULL;
	struct dev_pm_opp *opp;
	unsigned long freq = 0;

	max_opp_ct = dev_pm_opp_get_opp_count(dev);
	if (max_opp_ct <= 0)
		return max_opp_ct;

	freq_table = kcalloc(max_opp_ct, sizeof(*freq_table), GFP_KERNEL);
	if (!freq_table)
		return -ENOMEM;

	for (; idx < max_opp_ct; idx++, freq++) {
		opp = dev_pm_opp_find_freq_ceil(dev, &freq);
		if (IS_ERR(opp)) {
			pr_err("Error fetching freq\n");
			goto fetch_err_exit;
		}
		freq_table[idx].frequency = freq / 1000; //MHz
		freq_table[idx].volt = dev_pm_opp_get_voltage(opp) / 1000; //mV
		pr_debug("%d: freq:%lu Mhz volt:%lu mv\n", idx,
				freq_table[idx].frequency,
				freq_table[idx].volt);
		dev_pm_opp_put(opp);
	}
	*freq_table_inp = freq_table;

	return max_opp_ct;
fetch_err_exit:
	kfree(freq_table);
	return -EINVAL;
}

static int build_unified_table(struct cc_limits_data *cc_cdev,
		struct limits_freq_table **table, int *table_ct,
		int *cpu, int cpu_ct)
{
	struct limits_freq_map *freq_map = NULL;
	int idx = 0, idy = 0, idz = 0, min_idx = 0, max_v = 0, max_idx = 0;

	for (idx = 0; idx < cpu_ct; idx++) {
		int table_v = table[idx][table_ct[idx] - 1].volt;

		if ((table_v > max_v) || (table_v == max_v &&
			table_ct[idx] > table_ct[max_idx])) {
			max_v = table_v;
			max_idx = idx;
		}
	}

	cc_cdev->thermal_state = 0;
	cc_cdev->map_freq_ct = table_ct[max_idx] - 1;
	min_idx = !max_idx;
	cc_cdev->cpu_map[0] = cpu[max_idx];
	cc_cdev->cpu_map[1] = cpu[min_idx];
	freq_map = kcalloc(table_ct[max_idx], sizeof(*freq_map), GFP_KERNEL);
	if (!freq_map)
		return -ENOMEM;
	pr_info("CPU1:%d CPU2:%d\n", cc_cdev->cpu_map[0], cc_cdev->cpu_map[1]);
	for (idx = table_ct[max_idx] - 1, idy = table_ct[min_idx] - 1, idz = 0;
			idx >= 0 && idz < table_ct[max_idx]; idx--, idz++) {
		int volt = table[max_idx][idx].volt;

		freq_map[idz].frequency[0] = table[max_idx][idx].frequency;
		for (; idy >= 0 ; idy--) {
			if (table[min_idx][idy].volt <= volt)
				break;
		}
		if (idy < 0)
			idy = 0;
		freq_map[idz].frequency[1] = table[min_idx][idy].frequency;
		pr_info("freq1:%u freq2:%u\n", freq_map[idz].frequency[0],
				freq_map[idz].frequency[1]);
	}

	cc_cdev->map_freq = freq_map;
	return 0;
}

static struct cc_limits_data *opp_init(int *cpus)
{
	int cpu1, cpu2;
	struct device *cpu1_dev, *cpu2_dev;
	struct limits_freq_table *cpu1_freq_table, *cpu2_freq_table;
	struct limits_freq_table *cpu_freq_table[CPU_MAP_CT];
	int table_ct[CPU_MAP_CT], ret = 0;
	struct cc_limits_data *cc_cdev = NULL;

	cpu1 = cpus[0];
	cpu2 = cpus[1];
	cpu1_dev = get_cpu_device(cpu1);
	if (!cpu1_dev) {
		pr_err("couldn't find cpu:%d\n", cpu1);
		return ERR_PTR(-ENODEV);
	}
	cpu2_dev = get_cpu_device(cpu2);
	if (!cpu2_dev) {
		pr_err("couldn't find cpu:%d\n", cpu2);
		return ERR_PTR(-ENODEV);
	}
	table_ct[0] = fetch_opp_table(cpu1_dev, &cpu1_freq_table);
	if (table_ct[0] <= 0)
		return ERR_PTR(-ENOMEM);

	table_ct[1] = fetch_opp_table(cpu2_dev, &cpu2_freq_table);
	if (table_ct[1] <= 0)
		goto opp_err_cpu1_exit;

	cc_cdev = kzalloc(sizeof(*cc_cdev), GFP_KERNEL);
	if (!cc_cdev)
		goto opp_err_cpu2_exit;
	cpu_freq_table[0] = cpu1_freq_table;
	cpu_freq_table[1] = cpu2_freq_table;
	ret = build_unified_table(cc_cdev, cpu_freq_table, table_ct, cpus,
					CPU_MAP_CT);
	if (ret < 0)
		goto opp_err_exit;

	kfree(cpu1_freq_table);
	kfree(cpu2_freq_table);
	return cc_cdev;
opp_err_exit:
	if (cc_cdev) {
		kfree(cc_cdev->map_freq);
		kfree(cc_cdev);
	}
opp_err_cpu2_exit:
	kfree(cpu2_freq_table);
opp_err_cpu1_exit:
	kfree(cpu1_freq_table);

	return ERR_PTR(-EPROBE_DEFER);
}

static int cc_init(struct device_node *np, int *cpus)
{
	struct cc_limits_data *cc_cdev;
	int idx = 0, ret = 0;
	struct cpufreq_policy *policy;

	mutex_lock(&cc_list_lock);
	list_for_each_entry(cc_cdev, &cc_cdev_list, node) {
		if ((cpus[0] == cc_cdev->cpu_map[0] &&
			cpus[1] == cc_cdev->cpu_map[1]) ||
			(cpus[0] == cc_cdev->cpu_map[1] &&
			cpus[1] == cc_cdev->cpu_map[0])) {
			mutex_unlock(&cc_list_lock);
			return 0;
		}
	}
	policy = cpufreq_cpu_get(cpus[0]);
	if (!policy) {
		pr_err("No policy for CPU:%d. Defer.\n", cpus[0]);
		mutex_unlock(&cc_list_lock);
		return -EPROBE_DEFER;
	}
	if (cpumask_test_cpu(cpus[1], policy->related_cpus)) {
		pr_err("CPUs:%d %d are related.\n", cpus[0], cpus[1]);
		cpufreq_cpu_put(policy);
		mutex_unlock(&cc_list_lock);
		return -EINVAL;
	}
	cpufreq_cpu_put(policy);

	cc_cdev = opp_init(cpus);
	if (IS_ERR(cc_cdev)) {
		ret = PTR_ERR(cc_cdev);
		mutex_unlock(&cc_list_lock);
		return ret;
	}
	for (idx = 0; idx < CPU_MAP_CT; idx++) {
		policy = cpufreq_cpu_get(cc_cdev->cpu_map[idx]);
		if (!policy) {
			pr_err("No policy for CPU:%d\n", cc_cdev->cpu_map[idx]);
			ret = -ENODEV;
			goto cc_err_exit;
		}
		ret = freq_qos_add_request(&policy->constraints,
				   &cc_cdev->cc_qos_req[idx], FREQ_QOS_MAX,
				   cc_cdev->map_freq[0].frequency[idx]);
		cpufreq_cpu_put(policy);
		if (ret < 0) {
			pr_err("CPU%d Failed to add freq constraint (%d)\n",
					cc_cdev->cpu_map[idx], ret);
			goto cc_err_exit;
		}
	}
	scnprintf(cc_cdev->cdev_name, THERMAL_NAME_LENGTH, np->name);
	cc_cdev->cdev = thermal_of_cooling_device_register(
					np, cc_cdev->cdev_name, cc_cdev,
					&cc_cooling_ops);
	list_add(&cc_cdev->node, &cc_cdev_list);
	mutex_unlock(&cc_list_lock);

	return 0;
cc_err_exit:
	mutex_unlock(&cc_list_lock);
	for (idx = 0; idx < CPU_MAP_CT; idx++)
		freq_qos_remove_request(&cc_cdev->cc_qos_req[idx]);
	kfree(cc_cdev->map_freq);
	kfree(cc_cdev);

	return ret;
}

static int cc_init_single_cluster(struct device_node *np, int cpu)
{
	struct cc_limits_data *cc_cdev;
	struct cpufreq_policy *policy;
	struct limits_freq_map *freq_map = NULL;
	int freq_count = 0, ret = 0, i;

	policy = cpufreq_cpu_get(cpu);
	if (!policy) {
		pr_err("No policy for CPU:%d. Defer\n", cpu);
		return -EPROBE_DEFER;
	}

	cc_cdev = kzalloc(sizeof(*cc_cdev), GFP_KERNEL);
	if (!cc_cdev) {
		cpufreq_cpu_put(policy);
		return -ENOMEM;
	}

	freq_count = cpufreq_table_count_valid_entries(policy);
	if (!freq_count) {
		pr_err("CPU%d freq table not found or has no valid entries\n",
			cpu);
		goto cc_err_exit;
	}

	freq_map = kcalloc(freq_count, sizeof(*freq_map), GFP_KERNEL);
	if (!freq_map) {
		cpufreq_cpu_put(policy);
		kfree(cc_cdev);
		return -ENOMEM;
	}

	for (i = 0; i < freq_count; i++) {
		if (policy->freq_table_sorted == CPUFREQ_TABLE_SORTED_ASCENDING)
			freq_map[i].frequency[0] =
				policy->freq_table[freq_count - i - 1].frequency;
		else
			freq_map[i].frequency[0] =
				policy->freq_table[i].frequency;
	}

	cc_cdev->thermal_state = 0;
	cc_cdev->map_freq_ct = freq_count - 1;
	cc_cdev->cpu_map[0] = cpu;
	cc_cdev->map_freq = freq_map;

	ret = freq_qos_add_request(&policy->constraints,
			   &cc_cdev->cc_qos_req[0], FREQ_QOS_MAX,
			   cc_cdev->map_freq[0].frequency[0]);


	if (ret < 0) {
		pr_err("CPU%d Failed to add freq constraint (%d)\n",
				cc_cdev->cpu_map[0], ret);
		goto rem_qos_req;
	}

	cpufreq_cpu_put(policy);
	scnprintf(cc_cdev->cdev_name, THERMAL_NAME_LENGTH, np->name);
	cc_cdev->cdev = thermal_of_cooling_device_register(
					np, cc_cdev->cdev_name, cc_cdev,
					&cc_cooling_ops);

	if (IS_ERR(cc_cdev->cdev))
		return PTR_ERR(cc_cdev->cdev);

	list_add(&cc_cdev->node, &cc_cdev_list);

	return 0;

rem_qos_req:
	freq_qos_remove_request(&cc_cdev->cc_qos_req[0]);
cc_err_exit:
	if (policy)
		cpufreq_cpu_put(policy);
	kfree(cc_cdev->map_freq);
	kfree(cc_cdev);

	return ret;
}

static int cc_cooling_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct device_node *dev_phandle = NULL, *subsys_np = NULL;
	struct device *cpu_dev;
	int ret = 0, idx = 0, count = 0, cpu;
	int cpu_count = 0, first_cluster = 0;
	int cpu_map[CPU_MAP_CT] = { -1, -1};

	for_each_available_child_of_node(np, subsys_np) {
		cpu_count = of_count_phandle_with_args(subsys_np, "qcom,cluster0",
							NULL);
		for (idx = 0; idx < cpu_count; idx++) {
			dev_phandle = of_parse_phandle(subsys_np, "qcom,cluster0",
							idx);
			for_each_possible_cpu(cpu) {
				cpu_dev = get_cpu_device(cpu);
				if (cpu_dev && cpu_dev->of_node ==
						dev_phandle) {
					cpu_map[count] = cpu;
					first_cluster = 1;
					count++;
					break;
				}
			}

			if (first_cluster == 1)
				break;
		}

		if (dev_phandle) {
			of_node_put(dev_phandle);
			dev_phandle = NULL;
		}

		cpu_count = of_count_phandle_with_args(subsys_np, "qcom,cluster1",
							NULL);
		for (idx = 0; idx < cpu_count; idx++) {
			dev_phandle = of_parse_phandle(subsys_np, "qcom,cluster1",
							idx);
			for_each_possible_cpu(cpu) {
				cpu_dev = get_cpu_device(cpu);
				if (cpu_dev && cpu_dev->of_node ==
						dev_phandle) {
					cpu_map[count] = cpu;
					count++;
					break;
				}
			}

			if ((first_cluster && count == CPU_MAP_CT) ||
			    (!first_cluster && (count == 1)))
				break;
		}

		if (dev_phandle)
			of_node_put(dev_phandle);

		if (count == 0) {
			dev_err(dev, "No cluster available\n");
			return -EINVAL;
		}

		if (count == CPU_MAP_CT)
			ret = cc_init(subsys_np, cpu_map);
		else
			ret = cc_init_single_cluster(subsys_np, cpu_map[0]);
	}

	return ret;
}

static int cc_cooling_remove(struct platform_device *pdev)
{
	struct cc_limits_data *cc_cdev, *cc_next;
	int idx = 0;

	mutex_lock(&cc_list_lock);
	list_for_each_entry_safe(cc_cdev, cc_next, &cc_cdev_list, node) {
		if (cc_cdev->cdev)
			thermal_cooling_device_unregister(cc_cdev->cdev);

		list_del(&cc_cdev->node);
		for (idx = 0; idx < CPU_MAP_CT; idx++)
			freq_qos_remove_request(&cc_cdev->cc_qos_req[idx]);
		kfree(cc_cdev->map_freq);
		kfree(cc_cdev);
	}
	mutex_unlock(&cc_list_lock);
	return 0;
}

static const struct of_device_id cc_cooling_device_match[] = {
	{.compatible = "qcom,cc-cooling-devices"},
	{}
};

static struct platform_driver cc_cooling_driver = {
	.probe          = cc_cooling_probe,
	.remove         = cc_cooling_remove,
	.driver         = {
		.name   = CC_CDEV_DRIVER,
		.of_match_table = cc_cooling_device_match,
	},
};

module_platform_driver(cc_cooling_driver);
MODULE_DESCRIPTION("CPU Voltage cooling device driver");
MODULE_LICENSE("GPL");
