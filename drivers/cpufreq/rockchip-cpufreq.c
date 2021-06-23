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
#include <linux/slab.h>
#include <linux/rockchip/cpu.h>
#include <soc/rockchip/rockchip_opp_select.h>
#include <soc/rockchip/rockchip_system_monitor.h>

#include "cpufreq-dt.h"
#include "rockchip-cpufreq.h"

struct cluster_info {
	struct opp_table *opp_table;
	struct list_head list_head;
	cpumask_t cpus;
	int scale;
	bool offline;
	bool freq_limit;
	bool is_check_init;
};
static LIST_HEAD(cluster_info_list);

static int px30_get_soc_info(struct device *dev, struct device_node *np,
			     int *bin, int *process)
{
	int ret = 0;
	u8 value = 0;

	if (!bin)
		return 0;

	if (of_property_match_string(np, "nvmem-cell-names",
				     "performance") >= 0) {
		ret = rockchip_nvmem_cell_read_u8(np, "performance", &value);
		if (ret) {
			dev_err(dev, "Failed to get soc performance value\n");
			return ret;
		}
		*bin = value;
	}
	if (*bin >= 0)
		dev_info(dev, "bin=%d\n", *bin);

	return ret;
}

static int rk3288_get_soc_info(struct device *dev, struct device_node *np,
			       int *bin, int *process)
{
	int ret = 0;
	u8 value = 0;
	char *name;

	if (!bin)
		goto next;
	if (of_property_match_string(np, "nvmem-cell-names", "special") >= 0) {
		ret = rockchip_nvmem_cell_read_u8(np, "special", &value);
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
		ret = rockchip_nvmem_cell_read_u8(np, name, &value);
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
		ret = rockchip_nvmem_cell_read_u8(np, "process", &value);
		if (ret) {
			dev_err(dev, "Failed to get soc process version\n");
			goto out;
		}
		if (soc_is_rk3288() && (value == 0 || value == 1))
			*process = 0;
	}
	if (*process >= 0)
		dev_info(dev, "process=%d\n", *process);

out:
	return ret;
}

static int rk3399_get_soc_info(struct device *dev, struct device_node *np,
			       int *bin, int *process)
{
	int ret = 0;
	u8 value = 0;

	if (!bin)
		return 0;

	if (of_property_match_string(np, "nvmem-cell-names",
				     "specification_serial_number") >= 0) {
		ret = rockchip_nvmem_cell_read_u8(np,
						  "specification_serial_number",
						  &value);
		if (ret) {
			dev_err(dev,
				"Failed to get specification_serial_number\n");
			goto out;
		}

		if (value == 0xb) {
			*bin = 0;
		} else if (value == 0x1) {
			if (of_property_match_string(np, "nvmem-cell-names",
						     "customer_demand") >= 0) {
				ret = rockchip_nvmem_cell_read_u8(np,
								  "customer_demand",
								  &value);
				if (ret) {
					dev_err(dev, "Failed to get customer_demand\n");
					goto out;
				}
				if (value == 0x0)
					*bin = 0;
				else
					*bin = 1;
			}
		} else if (value == 0x10) {
			*bin = 1;
		}
	}

out:
	if (*bin >= 0)
		dev_info(dev, "bin=%d\n", *bin);

	return ret;
}

static int rv1126_get_soc_info(struct device *dev, struct device_node *np,
			       int *bin, int *process)
{
	int ret = 0;
	u8 value = 0;

	if (of_property_match_string(np, "nvmem-cell-names", "performance") >= 0) {
		ret = rockchip_nvmem_cell_read_u8(np, "performance", &value);
		if (ret) {
			dev_err(dev, "Failed to get soc performance value\n");
			return ret;
		}
		if (value == 0x1)
			*bin = 1;
		else
			*bin = 0;
	}
	if (*bin >= 0)
		dev_info(dev, "bin=%d\n", *bin);

	return ret;
}

static const struct of_device_id rockchip_cpufreq_of_match[] = {
	{
		.compatible = "rockchip,px30",
		.data = (void *)&px30_get_soc_info,
	},
	{
		.compatible = "rockchip,rk3288",
		.data = (void *)&rk3288_get_soc_info,
	},
	{
		.compatible = "rockchip,rk3288w",
		.data = (void *)&rk3288_get_soc_info,
	},
	{
		.compatible = "rockchip,rk3326",
		.data = (void *)&px30_get_soc_info,
	},
	{
		.compatible = "rockchip,rk3399",
		.data = (void *)&rk3399_get_soc_info,
	},
	{
		.compatible = "rockchip,rv1109",
		.data = (void *)&rv1126_get_soc_info,
	},
	{
		.compatible = "rockchip,rv1126",
		.data = (void *)&rv1126_get_soc_info,
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

static int rockchip_cpufreq_cluster_init(int cpu, struct cluster_info *cluster)
{
	struct opp_table *opp_table;
	struct device_node *np;
	struct device *dev;
	char *reg_name = NULL;
	int bin = -EINVAL;
	int process = -EINVAL;
	int volt_sel = -EINVAL;
	int ret = 0;

	dev = get_cpu_device(cpu);
	if (!dev)
		return -ENODEV;

	if (of_find_property(dev->of_node, "cpu-supply", NULL))
		reg_name = "cpu";
	else if (of_find_property(dev->of_node, "cpu0-supply", NULL))
		reg_name = "cpu0";
	else
		return -ENOENT;

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

	rockchip_get_soc_info(dev, rockchip_cpufreq_of_match, &bin, &process);
	rockchip_get_scale_volt_sel(dev, "cpu_leakage", reg_name, bin, process,
				    &cluster->scale, &volt_sel);
	opp_table = rockchip_set_opp_prop_name(dev, process, volt_sel);
	if (IS_ERR(opp_table))
		ret = PTR_ERR(opp_table);
np_err:
	of_node_put(np);

	return ret;
}

int rockchip_cpufreq_adjust_power_scale(struct device *dev)
{
	struct cluster_info *cluster;

	cluster = rockchip_cluster_info_lookup(dev->id);
	if (!cluster)
		return -EINVAL;
	rockchip_adjust_power_scale(dev, cluster->scale);

	return 0;
}
EXPORT_SYMBOL_GPL(rockchip_cpufreq_adjust_power_scale);

static int rockchip_cpufreq_suspend(struct cpufreq_policy *policy)
{
	int ret = 0;

	ret = cpufreq_generic_suspend(policy);
	if (!ret)
		rockchip_monitor_suspend_low_temp_adjust(policy->cpu);

	return ret;
}

static int __init rockchip_cpufreq_driver_init(void)
{
	struct cluster_info *cluster, *pos;
	struct cpufreq_dt_platform_data pdata = {0};
	int cpu, ret;

	for_each_possible_cpu(cpu) {
		cluster = rockchip_cluster_info_lookup(cpu);
		if (cluster)
			continue;

		cluster = kzalloc(sizeof(*cluster), GFP_KERNEL);
		if (!cluster) {
			ret = -ENOMEM;
			goto release_cluster_info;
		}

		ret = rockchip_cpufreq_cluster_init(cpu, cluster);
		if (ret) {
			pr_err("Failed to initialize dvfs info cpu%d\n", cpu);
			goto release_cluster_info;
		}
		list_add(&cluster->list_head, &cluster_info_list);
	}

	pdata.have_governor_per_policy = true;
	pdata.suspend = rockchip_cpufreq_suspend;

	return PTR_ERR_OR_ZERO(platform_device_register_data(NULL, "cpufreq-dt",
			       -1, (void *)&pdata,
			       sizeof(struct cpufreq_dt_platform_data)));

release_cluster_info:
	list_for_each_entry_safe(cluster, pos, &cluster_info_list, list_head) {
		list_del(&cluster->list_head);
		kfree(cluster);
	}
	return ret;
}
module_init(rockchip_cpufreq_driver_init);

MODULE_AUTHOR("Finley Xiao <finley.xiao@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip cpufreq driver");
MODULE_LICENSE("GPL v2");
