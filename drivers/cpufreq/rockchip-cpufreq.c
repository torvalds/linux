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

#include "../clk/rockchip/clk.h"

#define MAX_PROP_NAME_LEN	3
#define LEAKAGE_TABLE_END	~1
#define LEAKAGE_INVALID		0xff

#define REBOOT_FREQ		816000 /* kHz */

struct volt_sel_table {
	int min;
	int max;
	int sel;
};

struct cluster_info {
	struct list_head list_head;
	cpumask_t cpus;
	unsigned int reboot_freq;
	unsigned int threshold_freq;
	int leakage;
	int pvtm;
	int volt_sel;
	int soc_version;
	bool offline;
	bool rebooting;
	bool freq_limit;
};

struct pvtm_config {
	unsigned int freq;
	unsigned int volt;
	unsigned int ch[2];
	unsigned int sample_time;
	unsigned int num;
	unsigned int err;
	unsigned int ref_temp;
	int temp_prop[2];
	const char *tz_name;
	struct thermal_zone_device *tz;
};

static LIST_HEAD(cluster_info_list);

static struct cluster_info *rockchip_cluster_info_lookup(int cpu)
{
	struct cluster_info *cluster;

	list_for_each_entry(cluster, &cluster_info_list, list_head) {
		if (cpumask_test_cpu(cpu, &cluster->cpus))
			return cluster;
	}

	return NULL;
}

static int rockchip_get_efuse_value(struct device_node *np, char *porp_name,
				    int *value)
{
	struct nvmem_cell *cell;
	unsigned char *buf;
	size_t len;

	cell = of_nvmem_cell_get(np, porp_name);
	if (IS_ERR(cell))
		return PTR_ERR(cell);

	buf = (unsigned char *)nvmem_cell_read(cell, &len);

	nvmem_cell_put(cell);

	if (IS_ERR(buf))
		return PTR_ERR(buf);

	if (buf[0] == LEAKAGE_INVALID)
		return -EINVAL;

	*value = buf[0];

	kfree(buf);

	return 0;
}

static int rk3399_get_soc_version(struct device_node *np, int *soc_version)
{
	int ret, version;

	if (of_property_match_string(np, "nvmem-cell-names",
				     "soc_version") < 0)
		return 0;

	ret = rockchip_get_efuse_value(np, "soc_version", &version);
	if (ret)
		return ret;

	*soc_version = (version & 0xf0) >> 4;

	return 0;
}

static const struct of_device_id rockchip_cpufreq_of_match[] = {
	{
		.compatible = "rockchip,rk3399",
		.data = (void *)&rk3399_get_soc_version,
	},
	{},
};

static int rockchip_get_volt_sel_table(struct device_node *np, char *porp_name,
				       struct volt_sel_table **table)
{
	struct volt_sel_table *sel_table;
	const struct property *prop;
	int count, i;

	prop = of_find_property(np, porp_name, NULL);
	if (!prop)
		return -EINVAL;

	if (!prop->value)
		return -ENODATA;

	count = of_property_count_u32_elems(np, porp_name);
	if (count < 0)
		return -EINVAL;

	if (count % 3)
		return -EINVAL;

	sel_table = kzalloc(sizeof(*sel_table) * (count / 3 + 1), GFP_KERNEL);
	if (!sel_table)
		return -ENOMEM;

	for (i = 0; i < count / 3; i++) {
		of_property_read_u32_index(np, porp_name, 3 * i,
					   &sel_table[i].min);
		of_property_read_u32_index(np, porp_name, 3 * i + 1,
					   &sel_table[i].max);
		of_property_read_u32_index(np, porp_name, 3 * i + 2,
					   &sel_table[i].sel);
	}
	sel_table[i].min = 0;
	sel_table[i].max = 0;
	sel_table[i].sel = LEAKAGE_TABLE_END;

	*table = sel_table;

	return 0;
}

static int rockchip_get_volt_sel(struct device_node *np, char *name,
				 int value, int *sel)
{
	struct volt_sel_table *table;
	int i, j = -1, ret;

	ret = rockchip_get_volt_sel_table(np, name, &table);
	if (ret)
		return -EINVAL;

	for (i = 0; table[i].sel != LEAKAGE_TABLE_END; i++) {
		if (value >= table[i].min)
			j = i;
	}
	if (j != -1)
		*sel = table[j].sel;
	else
		ret = -EINVAL;

	kfree(table);

	return ret;
}

static int rockchip_parse_pvtm_config(struct device_node *np,
				      struct pvtm_config *pvtm)
{
	if (!of_find_property(np, "rockchip,pvtm-voltage-sel", NULL))
		return -EINVAL;
	if (of_property_read_u32(np, "rockchip,pvtm-freq", &pvtm->freq))
		return -EINVAL;
	if (of_property_read_u32(np, "rockchip,pvtm-volt", &pvtm->volt))
		return -EINVAL;
	if (of_property_read_u32_array(np, "rockchip,pvtm-ch", pvtm->ch, 2))
		return -EINVAL;
	if (of_property_read_u32(np, "rockchip,pvtm-sample-time",
				 &pvtm->sample_time))
		return -EINVAL;
	if (of_property_read_u32(np, "rockchip,pvtm-number", &pvtm->num))
		return -EINVAL;
	if (of_property_read_u32(np, "rockchip,pvtm-error", &pvtm->err))
		return -EINVAL;
	if (of_property_read_u32(np, "rockchip,pvtm-ref-temp", &pvtm->ref_temp))
		return -EINVAL;
	if (of_property_read_u32_array(np, "rockchip,pvtm-temp-prop",
				       pvtm->temp_prop, 2))
		return -EINVAL;
	if (of_property_read_string(np, "rockchip,pvtm-thermal-zone",
				    &pvtm->tz_name))
		return -EINVAL;
	pvtm->tz = thermal_zone_get_zone_by_name(pvtm->tz_name);
	if (IS_ERR(pvtm->tz))
		return -EINVAL;
	if (!pvtm->tz->ops->get_temp)
		return -EINVAL;

	return 0;
}

static int rockchip_get_pvtm_specific_value(struct device *dev,
					    struct device_node *np,
					    struct clk *clk,
					    struct regulator *reg,
					    int *target_value)
{
	struct pvtm_config *pvtm;
	unsigned long old_freq;
	unsigned int old_volt;
	int cur_temp, diff_temp;
	int cur_value, total_value, avg_value, diff_value;
	int min_value, max_value;
	int ret = 0, i = 0, retry = 2;

	pvtm = kzalloc(sizeof(*pvtm), GFP_KERNEL);
	if (!pvtm)
		return -ENOMEM;

	ret = rockchip_parse_pvtm_config(np, pvtm);
	if (ret)
		goto pvtm_value_out;

	old_freq = clk_get_rate(clk);
	old_volt = regulator_get_voltage(reg);

	/*
	 * Set pvtm_freq to the lowest frequency in dts,
	 * so change frequency first.
	 */
	ret = clk_set_rate(clk, pvtm->freq * 1000);
	if (ret) {
		dev_err(dev, "Failed to set pvtm freq\n");
		goto pvtm_value_out;
	}

	ret = regulator_set_voltage(reg, pvtm->volt, pvtm->volt);
	if (ret) {
		dev_err(dev, "Failed to set pvtm_volt\n");
		goto restore_clk;
	}

	/* The first few values may be fluctuant, if error is too big, retry*/
	while (retry--) {
		total_value = 0;
		min_value = INT_MAX;
		max_value = 0;

		for (i = 0; i < pvtm->num; i++) {
			cur_value = rockchip_get_pvtm_value(pvtm->ch[0],
							    pvtm->ch[1],
							    pvtm->sample_time);
			if (!cur_value)
				goto resetore_volt;
			if (cur_value < min_value)
				min_value = cur_value;
			if (cur_value > max_value)
				max_value = cur_value;
			total_value += cur_value;
		}
		if (max_value - min_value < pvtm->err)
			break;
	}
	avg_value = total_value / pvtm->num;

	/*
	 * As pvtm is influenced by temperature, compute difference between
	 * current temperature and reference temperature
	 */
	pvtm->tz->ops->get_temp(pvtm->tz, &cur_temp);
	diff_temp = (cur_temp / 1000 - pvtm->ref_temp);
	diff_value = diff_temp *
		(diff_temp < 0 ? pvtm->temp_prop[0] : pvtm->temp_prop[1]);
	*target_value = avg_value + diff_value;

	dev_info(dev, "temp=%d, pvtm=%d (%d + %d)\n",
		 cur_temp, *target_value, avg_value, diff_value);

resetore_volt:
	regulator_set_voltage(reg, old_volt, old_volt);
restore_clk:
	clk_set_rate(clk, old_freq);
pvtm_value_out:
	kfree(pvtm);

	return ret;
}

static int rockchip_cpufreq_cluster_init(int cpu, struct cluster_info *cluster)
{
	int (*get_soc_version)(struct device_node *np, int *soc_version);
	const struct of_device_id *match;
	struct device_node *node, *np;
	struct clk *clk;
	struct clk *cpu_clk;
	struct regulator *cpu_reg;
	struct device *dev;
	int lkg_volt_sel = -1, pvtm_volt_sel = -1, lkg_scale_sel = -1;
	int ret;

	dev = get_cpu_device(cpu);
	if (!dev)
		return -ENODEV;

	np = of_parse_phandle(dev->of_node, "operating-points-v2", 0);
	if (!np) {
		dev_warn(dev, "OPP-v2 not supported\n");
		return -ENOENT;
	}

	ret = dev_pm_opp_of_get_sharing_cpus(dev, &cluster->cpus);
	if (ret)
		return ret;

	cluster->soc_version = -1;
	node = of_find_node_by_path("/");
	match = of_match_node(rockchip_cpufreq_of_match, node);
	if (match && match->data) {
		get_soc_version = match->data;
		ret = get_soc_version(np, &cluster->soc_version);
		if (ret) {
			dev_err(dev, "Failed to get chip_version\n");
			return ret;
		}
	}

	if (of_property_read_u32(np, "rockchip,reboot-freq",
				 &cluster->reboot_freq))
		cluster->reboot_freq = REBOOT_FREQ;
	of_property_read_u32(np, "rockchip,threshold-freq",
			     &cluster->threshold_freq);
	cluster->freq_limit = of_property_read_bool(np, "rockchip,freq-limit");

	cluster->volt_sel = -1;
	ret = rockchip_get_efuse_value(np, "cpu_leakage", &cluster->leakage);
	if (!ret) {
		dev_info(dev, "leakage=%d\n", cluster->leakage);
		ret = rockchip_get_volt_sel(np, "rockchip,leakage-voltage-sel",
					    cluster->leakage, &lkg_volt_sel);
		if (!ret) {
			dev_info(dev, "leakage-sel=%d\n", lkg_volt_sel);
			cluster->volt_sel = lkg_volt_sel;
		}

		ret = rockchip_get_volt_sel(np, "rockchip,leakage-scaling-sel",
					    cluster->leakage,
					    &lkg_scale_sel);
		if (!ret) {
			dev_info(dev, "scale-sel=%d\n", lkg_scale_sel);
			clk = of_clk_get_by_name(np, NULL);
			if (IS_ERR(clk)) {
				dev_err(dev, "Failed to get opp clk");
				return PTR_ERR(clk);
			}
			ret = rockchip_pll_clk_adaptive_scaling(clk,
								lkg_scale_sel);
			if (ret) {
				dev_err(dev, "Failed to adaptive scaling\n");
				return ret;
			}
		}
	}

	cpu_clk = clk_get(dev, NULL);
	if (IS_ERR_OR_NULL(cpu_clk)) {
		dev_err(dev, "Failed to get clk\n");
		goto parse_dt_out;
	}
	cpu_reg = regulator_get_optional(dev, "cpu");
	if (IS_ERR_OR_NULL(cpu_reg)) {
		dev_err(dev, "Failed to get cpu_reg\n");
		goto put_clk;
	}
	ret = rockchip_get_pvtm_specific_value(dev, np, cpu_clk, cpu_reg,
					       &cluster->pvtm);
	if (!ret) {
		ret = rockchip_get_volt_sel(np, "rockchip,pvtm-voltage-sel",
					    cluster->pvtm, &pvtm_volt_sel);
		if (!ret) {
			dev_info(dev, "pvtm-sel=%d\n", pvtm_volt_sel);
			if (cluster->volt_sel < 0 ||
			    cluster->volt_sel > pvtm_volt_sel)
				cluster->volt_sel = pvtm_volt_sel;
		}
	}

	regulator_put(cpu_reg);
put_clk:
	clk_put(cpu_clk);
parse_dt_out:

	return 0;
}

static int rockchip_cpufreq_set_opp_info(int cpu, struct cluster_info *cluster)
{
	struct device *dev;
	char name[MAX_PROP_NAME_LEN];
	int ret, version;

	dev = get_cpu_device(cpu);
	if (!dev)
		return -ENODEV;

	if (cluster->soc_version >= 0) {
		if (cluster->volt_sel >= 0)
			snprintf(name, MAX_PROP_NAME_LEN, "S%d-L%d",
				 cluster->soc_version, cluster->volt_sel);
		else
			snprintf(name, MAX_PROP_NAME_LEN, "S%d",
				 cluster->soc_version);
	} else if (cluster->volt_sel >= 0) {
		snprintf(name, MAX_PROP_NAME_LEN, "L%d", cluster->volt_sel);
	} else {
		return 0;
	}

	ret = dev_pm_opp_set_prop_name(dev, name);
	if (ret) {
		dev_err(dev, "Failed to set prop name\n");
		return ret;
	}

	if (cluster->soc_version >= 0) {
		version = BIT(cluster->soc_version);
		ret = dev_pm_opp_set_supported_hw(dev, &version, 1);
		if (ret) {
			dev_err(dev, "Failed to set supported hardware\n");
			return ret;
		}
	}

	return 0;
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
