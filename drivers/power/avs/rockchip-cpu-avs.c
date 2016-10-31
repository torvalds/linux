/*
 * Rockchip CPU AVS support.
 *
 * Copyright (c) 2016 Rockchip Electronics Co. Ltd.
 * Author: Finley Xiao <finley.xiao@rock-chips.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/cpumask.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/nvmem-consumer.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/slab.h>
#include <linux/types.h>

#define MAX_CLUSTERS		2
#define LEAKAGE_TABLE_END	~1
#define INVALID_VALUE		0xff

struct leakage_volt_table {
	unsigned int min;
	unsigned int max;
	int volt;
};

struct cluster_info {
	unsigned int id;
	int offset_volt;
	unsigned char leakage;
	unsigned int min_freq;
	unsigned int min_volt;
	struct leakage_volt_table *table;
};

struct rockchip_cpu_avs {
	unsigned int num_clusters;
	struct cluster_info *cluster;
	struct notifier_block cpufreq_notify;
};

#define notifier_to_avs(_n) container_of(_n, struct rockchip_cpu_avs, \
	cpufreq_notify)

static void rockchip_adjust_opp_table(struct device *cpu_dev,
				      struct cpufreq_frequency_table *table,
				      struct cluster_info *cluster)
{
	struct cpufreq_frequency_table *pos;
	struct dev_pm_opp *opp;
	unsigned long adjust_volt;

	if (!cluster->offset_volt)
		return;

	cpufreq_for_each_valid_entry(pos, table) {
		if (pos->frequency < cluster->min_freq)
			continue;

		rcu_read_lock();

		opp = dev_pm_opp_find_freq_exact(cpu_dev, pos->frequency * 1000,
						 true);
		if (IS_ERR(opp)) {
			rcu_read_unlock();
			continue;
		}

		adjust_volt = dev_pm_opp_get_voltage(opp) +
			cluster->offset_volt;

		rcu_read_unlock();

		if (adjust_volt < cluster->min_volt)
			continue;

		dev_pm_opp_adjust_voltage(cpu_dev, pos->frequency * 1000,
					  adjust_volt);
	}
}

static int rockchip_cpu_avs_notifier(struct notifier_block *nb,
				     unsigned long event, void *data)
{
	struct rockchip_cpu_avs *avs = notifier_to_avs(nb);
	struct cpufreq_policy *policy = data;
	struct cluster_info *cluster;
	struct device *cpu_dev;
	int i, id, cpu = policy->cpu;

	if (event != CPUFREQ_START)
		goto out;

	id = topology_physical_package_id(cpu);
	if (id < 0 || id >= MAX_CLUSTERS) {
		pr_err("cpu%d invalid cluster id\n", cpu);
		goto out;
	}

	for (i = 0; i < avs->num_clusters; i++) {
		if (avs->cluster[i].id == id)
			break;
	}
	if (i == avs->num_clusters)
		goto out;
	else
		cluster = &avs->cluster[i];

	if (!policy->freq_table) {
		pr_err("cpu%d freq table not found\n", cpu);
		goto out;
	}

	cpu_dev = get_cpu_device(cpu);
	if (!cpu_dev) {
		pr_err("cpu%d failed to get device\n", cpu);
		goto out;
	}

	rockchip_adjust_opp_table(cpu_dev, policy->freq_table, cluster);
out:

	return NOTIFY_OK;
}

static int rockchip_get_leakage_volt_table(struct device_node *np,
					   struct leakage_volt_table **table)
{
	struct leakage_volt_table *volt_table;
	const struct property *prop;
	int count, i;

	prop = of_find_property(np, "leakage-adjust-volt", NULL);
	if (!prop) {
		pr_err("failed to find property leakage-adjust-volt\n");
		return -EINVAL;
	}
	if (!prop->value) {
		pr_err("property leakage-adjust-volt is NULL\n");
		return -ENODATA;
	}

	count = of_property_count_u32_elems(np, "leakage-adjust-volt");
	if (count < 0) {
		pr_err("Invalid property (%d)\n", count);
		return -EINVAL;
	}
	if (count % 3) {
		pr_err("Invalid number of elements in property (%d)\n", count);
		return -EINVAL;
	}

	volt_table = kzalloc(sizeof(*volt_table) * (count / 3 + 1), GFP_KERNEL);
	if (!volt_table)
		return -ENOMEM;

	for (i = 0; i < count / 3; i++) {
		of_property_read_u32_index(np, "leakage-adjust-volt", 3 * i,
					   &volt_table[i].min);
		of_property_read_u32_index(np, "leakage-adjust-volt", 3 * i + 1,
					   &volt_table[i].max);
		of_property_read_u32_index(np, "leakage-adjust-volt", 3 * i + 2,
					   (u32 *)&volt_table[i].volt);
	}
	volt_table[i].min = 0;
	volt_table[i].max = 0;
	volt_table[i].volt = LEAKAGE_TABLE_END;

	*table = volt_table;

	return 0;
}

static int rockchip_get_leakage(struct device_node *np, unsigned char *leakage)
{
	struct nvmem_cell *cell;
	unsigned char *buf;
	size_t len;

	cell = of_nvmem_cell_get(np, "cpu_leakage");
	if (IS_ERR(cell)) {
		pr_err("avs failed to get cpu_leakage cell\n");
		return PTR_ERR(cell);
	}

	buf = (unsigned char *)nvmem_cell_read(cell, &len);

	nvmem_cell_put(cell);

	if (IS_ERR(buf))
		return PTR_ERR(buf);

	if (buf[0] == INVALID_VALUE)
		return -EINVAL;

	*leakage = buf[0];
	kfree(buf);

	return 0;
}

static int rockchip_get_offset_volt(unsigned char leakage,
				    struct leakage_volt_table *table, int *volt)
{
	int i, j = -1;

	if (!table)
		return -EINVAL;

	for (i = 0; table[i].volt != LEAKAGE_TABLE_END; i++) {
		if (leakage >= (unsigned char)table[i].min)
			j = i;
	}

	if (j == -1)
		*volt = 0;
	else
		*volt = table[j].volt;

	return 0;
}

static int rockchip_of_parse_cpu_avs(struct device_node *np,
				     struct cluster_info *cluster)
{
	int ret;

	ret = of_property_read_u32(np, "cluster-id", &cluster->id);
	if (ret < 0) {
		pr_err("prop cluster-id missing\n");
		ret = -EINVAL;
		goto out;
	}
	if (cluster->id >= MAX_CLUSTERS) {
		pr_err("prop cluster-id invalid\n");
		ret = -EINVAL;
		goto out;
	}

	ret = of_property_read_u32(np, "min-freq", &cluster->min_freq);
	if (ret < 0) {
		pr_err("prop min_freq missing\n");
		ret = -EINVAL;
		goto out;
	}

	ret = of_property_read_u32(np, "min-volt", &cluster->min_volt);
	if (ret < 0) {
		pr_err("prop min_volt missing\n");
		ret = -EINVAL;
		goto out;
	}

	ret = rockchip_get_leakage_volt_table(np, &cluster->table);
	if (ret) {
		pr_err("prop leakage-adjust-volt invalid\n");
		ret = -EINVAL;
		goto out;
	}

	ret = rockchip_get_leakage(np, &cluster->leakage);
	if (ret) {
		pr_err("get leakage invalid\n");
		goto out;
	}

	ret = rockchip_get_offset_volt(cluster->leakage, cluster->table,
				       &cluster->offset_volt);
	if (ret) {
		pr_err("get offset volt err\n");
		goto out;
	}

	pr_info("cluster%d leakage=%d adjust_volt=%d\n", cluster->id,
		cluster->leakage, cluster->offset_volt);

out:
	return ret;
}

static int rockchip_cpu_avs_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np, *node;
	struct rockchip_cpu_avs *avs;
	int ret, num_clusters = 0, i = 0;

	np = of_find_node_by_name(NULL, "cpu-avs");
	if (!np) {
		pr_info("unable to find cpu-avs\n");
		return 0;
	}

	if (!of_device_is_available(np)) {
		pr_info("cpu-avs disabled\n");
		ret = 0;
		goto err;
	}

	for_each_available_child_of_node(np, node)
		num_clusters++;

	if (!num_clusters) {
		pr_info("cpu-avs child disabled\n");
		ret = 0;
		goto err;
	}

	avs = devm_kzalloc(dev, sizeof(*avs), GFP_KERNEL);
	if (!avs) {
		ret = -ENOMEM;
		goto err;
	}

	avs->cluster = devm_kzalloc(dev, sizeof(*avs->cluster) * num_clusters,
				    GFP_KERNEL);
	if (!avs->cluster) {
		ret = -ENOMEM;
		goto err;
	}

	avs->num_clusters = num_clusters;

	for_each_available_child_of_node(np, node) {
		ret = rockchip_of_parse_cpu_avs(node, &avs->cluster[i++]);
		if (ret)
			goto err;
	}

	of_node_put(np);

	avs->cpufreq_notify.notifier_call = rockchip_cpu_avs_notifier;

	return cpufreq_register_notifier(&avs->cpufreq_notify,
		CPUFREQ_POLICY_NOTIFIER);

err:
	of_node_put(np);

	return ret;
}

static struct platform_driver rockchip_cpu_avs_platdrv = {
	.driver = {
		.name	= "rockchip-cpu-avs",
	},
	.probe		= rockchip_cpu_avs_probe,
};

static int __init rockchip_cpu_avs_init(void)
{
	struct platform_device *pdev;
	int ret;

	ret = platform_driver_register(&rockchip_cpu_avs_platdrv);
	if (ret)
		return ret;

	/*
	 * Since there's no place to hold device registration code and no
	 * device tree based way to match cpu-avs driver yet, both the driver
	 * and the device registration codes are put here to handle defer
	 * probing.
	 */
	pdev = platform_device_register_simple("rockchip-cpu-avs", -1, NULL, 0);
	if (IS_ERR(pdev)) {
		pr_err("failed to register rockchip-cpu-avs platform device\n");
		return PTR_ERR(pdev);
	}

	return 0;
}

module_init(rockchip_cpu_avs_init);

MODULE_DESCRIPTION("Rockchip CPU AVS driver");
MODULE_AUTHOR("Finley Xiao <finley.xiao@rock-chips.com>");
MODULE_LICENSE("GPL v2");
