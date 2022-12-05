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
#include <linux/cpuidle.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/nvmem-consumer.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/pm_qos.h>
#include <linux/slab.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/rockchip/cpu.h>
#include <soc/rockchip/rockchip_opp_select.h>
#include <soc/rockchip/rockchip_system_monitor.h>

#include "cpufreq-dt.h"
#include "rockchip-cpufreq.h"

struct cluster_info {
	struct list_head list_head;
	struct monitor_dev_info *mdev_info;
	struct rockchip_opp_info opp_info;
	struct freq_qos_request dsu_qos_req;
	cpumask_t cpus;
	unsigned int idle_threshold_freq;
	int scale;
	bool is_idle_disabled;
	bool is_opp_shared_dsu;
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

static int rk3588_get_soc_info(struct device *dev, struct device_node *np,
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
			return ret;
		}
		/* RK3588M */
		if (value == 0xd)
			*bin = 1;
		/* RK3588J */
		else if (value == 0xa)
			*bin = 2;
	}
	if (*bin < 0)
		*bin = 0;
	dev_info(dev, "bin=%d\n", *bin);

	return ret;
}

static int rk3588_change_length(struct device *dev, struct device_node *np,
				int bin, int process, int volt_sel)
{
	struct clk *clk;
	unsigned long old_rate;
	unsigned int low_len_sel;
	u32 opp_flag = 0;
	int ret = 0;

	clk = clk_get(dev, NULL);
	if (IS_ERR(clk)) {
		dev_warn(dev, "failed to get cpu clk\n");
		return PTR_ERR(clk);
	}

	/* RK3588 low speed grade should change to low length */
	if (of_property_read_u32(np, "rockchip,pvtm-low-len-sel",
				 &low_len_sel))
		goto out;
	if (volt_sel > low_len_sel)
		goto out;
	opp_flag = OPP_LENGTH_LOW;

	old_rate = clk_get_rate(clk);
	ret = clk_set_rate(clk, old_rate | opp_flag);
	if (ret) {
		dev_err(dev, "failed to change length\n");
		goto out;
	}
	clk_set_rate(clk, old_rate);
out:
	clk_put(clk);

	return ret;
}

static int rk3588_set_supported_hw(struct device *dev, struct device_node *np,
				   int bin, int process, int volt_sel)
{
	struct opp_table *opp_table;
	u32 supported_hw[2];

	if (!of_property_read_bool(np, "rockchip,supported-hw"))
		return 0;

	/* SoC Version */
	supported_hw[0] = BIT(bin);
	/* Speed Grade */
	supported_hw[1] = BIT(volt_sel);
	opp_table = dev_pm_opp_set_supported_hw(dev, supported_hw, 2);
	if (IS_ERR(opp_table)) {
		dev_err(dev, "failed to set supported opp\n");
		return PTR_ERR(opp_table);
	}

	return 0;
}

static int rk3588_set_soc_info(struct device *dev, struct device_node *np,
			       int bin, int process, int volt_sel)
{
	if (volt_sel < 0)
		return 0;
	if (bin < 0)
		bin = 0;

	rk3588_change_length(dev, np, bin, process, volt_sel);
	rk3588_set_supported_hw(dev, np, bin, process, volt_sel);

	return 0;
}

static int rk3588_cpu_set_read_margin(struct device *dev,
				      struct rockchip_opp_info *opp_info,
				      u32 rm)
{
	if (!opp_info->volt_rm_tbl)
		return 0;
	if (rm == opp_info->current_rm || rm  == UINT_MAX)
		return 0;

	dev_dbg(dev, "set rm to %d\n", rm);
	if (opp_info->grf) {
		regmap_write(opp_info->grf, 0x20, 0x001c0000 | (rm << 2));
		regmap_write(opp_info->grf, 0x28, 0x003c0000 | (rm << 2));
		regmap_write(opp_info->grf, 0x2c, 0x003c0000 | (rm << 2));
		regmap_write(opp_info->grf, 0x30, 0x00200020);
		udelay(1);
		regmap_write(opp_info->grf, 0x30, 0x00200000);
	}
	if (opp_info->dsu_grf) {
		regmap_write(opp_info->dsu_grf, 0x20, 0x001c0000 | (rm << 2));
		regmap_write(opp_info->dsu_grf, 0x28, 0x003c0000 | (rm << 2));
		regmap_write(opp_info->dsu_grf, 0x2c, 0x003c0000 | (rm << 2));
		regmap_write(opp_info->dsu_grf, 0x30, 0x001c0000 | (rm << 2));
		regmap_write(opp_info->dsu_grf, 0x38, 0x001c0000 | (rm << 2));
		regmap_write(opp_info->dsu_grf, 0x18, 0x40004000);
		udelay(1);
		regmap_write(opp_info->dsu_grf, 0x18, 0x40000000);
	}

	opp_info->current_rm = rm;

	return 0;
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

static const struct rockchip_opp_data px30_cpu_opp_data = {
	.get_soc_info = px30_get_soc_info,
};

static const struct rockchip_opp_data rk3288_cpu_opp_data = {
	.get_soc_info = rk3288_get_soc_info,
};

static const struct rockchip_opp_data rk3399_cpu_opp_data = {
	.get_soc_info = rk3399_get_soc_info,
};

static const struct rockchip_opp_data rk3588_cpu_opp_data = {
	.get_soc_info = rk3588_get_soc_info,
	.set_soc_info = rk3588_set_soc_info,
	.set_read_margin = rk3588_cpu_set_read_margin,
};

static const struct rockchip_opp_data rv1126_cpu_opp_data = {
	.get_soc_info = rv1126_get_soc_info,
};

static const struct of_device_id rockchip_cpufreq_of_match[] = {
	{
		.compatible = "rockchip,px30",
		.data = (void *)&px30_cpu_opp_data,
	},
	{
		.compatible = "rockchip,rk3288",
		.data = (void *)&rk3288_cpu_opp_data,
	},
	{
		.compatible = "rockchip,rk3288w",
		.data = (void *)&rk3288_cpu_opp_data,
	},
	{
		.compatible = "rockchip,rk3326",
		.data = (void *)&px30_cpu_opp_data,
	},
	{
		.compatible = "rockchip,rk3399",
		.data = (void *)&rk3399_cpu_opp_data,
	},
	{
		.compatible = "rockchip,rk3588",
		.data = (void *)&rk3588_cpu_opp_data,
	},
	{
		.compatible = "rockchip,rv1109",
		.data = (void *)&rv1126_cpu_opp_data,
	},
	{
		.compatible = "rockchip,rv1126",
		.data = (void *)&rv1126_cpu_opp_data,
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

static int rockchip_cpufreq_set_volt(struct device *dev,
				     struct regulator *reg,
				     struct dev_pm_opp_supply *supply,
				     char *reg_name)
{
	int ret;

	dev_dbg(dev, "%s: %s voltages (uV): %lu %lu %lu\n", __func__, reg_name,
		supply->u_volt_min, supply->u_volt, supply->u_volt_max);

	ret = regulator_set_voltage_triplet(reg, supply->u_volt_min,
					    supply->u_volt, supply->u_volt_max);
	if (ret)
		dev_err(dev, "%s: failed to set voltage (%lu %lu %lu uV): %d\n",
			__func__, supply->u_volt_min, supply->u_volt,
			supply->u_volt_max, ret);

	return ret;
}

static int cpu_opp_helper(struct dev_pm_set_opp_data *data)
{
	struct dev_pm_opp_supply *old_supply_vdd = &data->old_opp.supplies[0];
	struct dev_pm_opp_supply *old_supply_mem = &data->old_opp.supplies[1];
	struct dev_pm_opp_supply *new_supply_vdd = &data->new_opp.supplies[0];
	struct dev_pm_opp_supply *new_supply_mem = &data->new_opp.supplies[1];
	struct regulator *vdd_reg = data->regulators[0];
	struct regulator *mem_reg = data->regulators[1];
	struct device *dev = data->dev;
	struct clk *clk = data->clk;
	struct cluster_info *cluster;
	struct rockchip_opp_info *opp_info;
	unsigned long old_freq = data->old_opp.rate;
	unsigned long new_freq = data->new_opp.rate;
	u32 target_rm = UINT_MAX;
	int ret = 0;

	cluster = rockchip_cluster_info_lookup(dev->id);
	if (!cluster)
		return -EINVAL;
	opp_info = &cluster->opp_info;
	rockchip_get_read_margin(dev, opp_info, new_supply_vdd->u_volt,
				 &target_rm);

	/* Change frequency */
	dev_dbg(dev, "%s: switching OPP: %lu Hz --> %lu Hz\n", __func__,
		old_freq, new_freq);
	/* Scaling up? Scale voltage before frequency */
	if (new_freq >= old_freq) {
		ret = rockchip_set_intermediate_rate(dev, opp_info, clk,
						     old_freq, new_freq,
						     true, true);
		if (ret) {
			dev_err(dev, "%s: failed to set clk rate: %lu\n",
				__func__, new_freq);
			return -EINVAL;
		}
		ret = rockchip_cpufreq_set_volt(dev, mem_reg, new_supply_mem,
						"mem");
		if (ret)
			goto restore_voltage;
		ret = rockchip_cpufreq_set_volt(dev, vdd_reg, new_supply_vdd,
						"vdd");
		if (ret)
			goto restore_voltage;
		rockchip_set_read_margin(dev, opp_info, target_rm, true);
		ret = clk_set_rate(clk, new_freq);
		if (ret) {
			dev_err(dev, "%s: failed to set clk rate: %lu %d\n",
				__func__, new_freq, ret);
			goto restore_rm;
		}
	/* Scaling down? Scale voltage after frequency */
	} else {
		ret = rockchip_set_intermediate_rate(dev, opp_info, clk,
						     old_freq, new_freq,
						     false, true);
		if (ret) {
			dev_err(dev, "%s: failed to set clk rate: %lu\n",
				__func__, new_freq);
			return -EINVAL;
		}
		rockchip_set_read_margin(dev, opp_info, target_rm, true);
		ret = clk_set_rate(clk, new_freq);
		if (ret) {
			dev_err(dev, "%s: failed to set clk rate: %lu %d\n",
				__func__, new_freq, ret);
			goto restore_rm;
		}
		ret = rockchip_cpufreq_set_volt(dev, vdd_reg, new_supply_vdd,
						"vdd");
		if (ret)
			goto restore_freq;
		ret = rockchip_cpufreq_set_volt(dev, mem_reg, new_supply_mem,
						"mem");
		if (ret)
			goto restore_freq;
	}

	return 0;

restore_freq:
	if (clk_set_rate(clk, old_freq))
		dev_err(dev, "%s: failed to restore old-freq (%lu Hz)\n",
			__func__, old_freq);
restore_rm:
	rockchip_get_read_margin(dev, opp_info, old_supply_vdd->u_volt,
				 &target_rm);
	rockchip_set_read_margin(dev, opp_info, target_rm, true);
restore_voltage:
	rockchip_cpufreq_set_volt(dev, mem_reg, old_supply_mem, "mem");
	rockchip_cpufreq_set_volt(dev, vdd_reg, old_supply_vdd, "vdd");

	return ret;
}

static int rockchip_cpufreq_cluster_init(int cpu, struct cluster_info *cluster)
{
	struct rockchip_opp_info *opp_info = &cluster->opp_info;
	struct opp_table *pname_table = NULL;
	struct opp_table *reg_table = NULL;
	struct opp_table *opp_table;
	struct device_node *np;
	struct device *dev;
	const char * const reg_names[] = {"cpu", "mem"};
	char *reg_name = NULL;
	int bin = -EINVAL;
	int process = -EINVAL;
	int volt_sel = -EINVAL;
	int ret = 0;
	u32 freq = 0;

	dev = get_cpu_device(cpu);
	if (!dev)
		return -ENODEV;

	opp_info->dev = dev;

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

	opp_info->grf = syscon_regmap_lookup_by_phandle(np,
							"rockchip,grf");
	if (IS_ERR(opp_info->grf))
		opp_info->grf = NULL;

	ret = dev_pm_opp_of_get_sharing_cpus(dev, &cluster->cpus);
	if (ret) {
		dev_err(dev, "Failed to get sharing cpus\n");
		goto np_err;
	}

	cluster->is_opp_shared_dsu = of_property_read_bool(np, "rockchip,opp-shared-dsu");
	if (!of_property_read_u32(np, "rockchip,idle-threshold-freq", &freq))
		cluster->idle_threshold_freq = freq;
	rockchip_get_opp_data(rockchip_cpufreq_of_match, opp_info);
	if (opp_info->data && opp_info->data->set_read_margin) {
		opp_info->current_rm = UINT_MAX;
		opp_info->target_rm = UINT_MAX;
		opp_info->dsu_grf =
			syscon_regmap_lookup_by_phandle(np, "rockchip,dsu-grf");
		if (IS_ERR(opp_info->dsu_grf))
			opp_info->dsu_grf = NULL;
		rockchip_get_volt_rm_table(dev, np, "volt-mem-read-margin",
					   &opp_info->volt_rm_tbl);
		of_property_read_u32(np, "low-volt-mem-read-margin",
				     &opp_info->low_rm);
		if (!of_property_read_u32(np, "intermediate-threshold-freq", &freq))
			opp_info->intermediate_threshold_freq = freq * 1000;
		rockchip_init_read_margin(dev, opp_info, reg_name);
	}
	if (opp_info->data && opp_info->data->get_soc_info)
		opp_info->data->get_soc_info(dev, np, &bin, &process);
	rockchip_get_scale_volt_sel(dev, "cpu_leakage", reg_name, bin, process,
				    &cluster->scale, &volt_sel);
	if (opp_info->data && opp_info->data->set_soc_info)
		opp_info->data->set_soc_info(dev, np, bin, process, volt_sel);
	pname_table = rockchip_set_opp_prop_name(dev, process, volt_sel);

	if (of_find_property(dev->of_node, "cpu-supply", NULL) &&
	    of_find_property(dev->of_node, "mem-supply", NULL)) {
		reg_table = dev_pm_opp_set_regulators(dev, reg_names,
						      ARRAY_SIZE(reg_names));
		if (IS_ERR(reg_table)) {
			ret = PTR_ERR(reg_table);
			goto pname_opp_table;
		}
		opp_table = dev_pm_opp_register_set_opp_helper(dev,
							       cpu_opp_helper);
		if (IS_ERR(opp_table)) {
			ret = PTR_ERR(opp_table);
			goto reg_opp_table;
		}
	}

	of_node_put(np);

	return 0;

reg_opp_table:
	if (reg_table)
		dev_pm_opp_put_regulators(reg_table);
pname_opp_table:
	if (!IS_ERR_OR_NULL(pname_table))
		dev_pm_opp_put_prop_name(pname_table);
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
	rockchip_pvtpll_calibrate_opp(&cluster->opp_info);

	return 0;
}
EXPORT_SYMBOL_GPL(rockchip_cpufreq_adjust_power_scale);

int rockchip_cpufreq_opp_set_rate(struct device *dev, unsigned long target_freq)
{
	struct cluster_info *cluster;
	int ret = 0;

	cluster = rockchip_cluster_info_lookup(dev->id);
	if (!cluster)
		return -EINVAL;

	rockchip_monitor_volt_adjust_lock(cluster->mdev_info);
	ret = dev_pm_opp_set_rate(dev, target_freq);
	rockchip_monitor_volt_adjust_unlock(cluster->mdev_info);

	return ret;
}
EXPORT_SYMBOL_GPL(rockchip_cpufreq_opp_set_rate);

static int rockchip_cpufreq_suspend(struct cpufreq_policy *policy)
{
	int ret = 0;

	ret = cpufreq_generic_suspend(policy);
	if (!ret)
		rockchip_monitor_suspend_low_temp_adjust(policy->cpu);

	return ret;
}

static int rockchip_cpufreq_add_monitor(struct cluster_info *cluster,
					struct cpufreq_policy *policy)
{
	struct device *dev = cluster->opp_info.dev;
	struct monitor_dev_profile *mdevp = NULL;
	struct monitor_dev_info *mdev_info = NULL;

	mdevp = kzalloc(sizeof(*mdevp), GFP_KERNEL);
	if (!mdevp)
		return -ENOMEM;

	mdevp->type = MONITOR_TPYE_CPU;
	mdevp->low_temp_adjust = rockchip_monitor_cpu_low_temp_adjust;
	mdevp->high_temp_adjust = rockchip_monitor_cpu_high_temp_adjust;
	mdevp->update_volt = rockchip_monitor_check_rate_volt;
	mdevp->data = (void *)policy;
	mdevp->opp_info = &cluster->opp_info;
	cpumask_copy(&mdevp->allowed_cpus, policy->cpus);
	mdev_info = rockchip_system_monitor_register(dev, mdevp);
	if (IS_ERR(mdev_info)) {
		kfree(mdevp);
		dev_err(dev, "failed to register system monitor\n");
		return -EINVAL;
	}
	mdev_info->devp = mdevp;
	cluster->mdev_info = mdev_info;

	return 0;
}

static int rockchip_cpufreq_remove_monitor(struct cluster_info *cluster)
{
	if (cluster->mdev_info) {
		kfree(cluster->mdev_info->devp);
		rockchip_system_monitor_unregister(cluster->mdev_info);
		cluster->mdev_info = NULL;
	}

	return 0;
}

static int rockchip_cpufreq_remove_dsu_qos(struct cluster_info *cluster)
{
	struct cluster_info *ci;

	if (!cluster->is_opp_shared_dsu)
		return 0;

	list_for_each_entry(ci, &cluster_info_list, list_head) {
		if (ci->is_opp_shared_dsu)
			continue;
		if (freq_qos_request_active(&ci->dsu_qos_req))
			freq_qos_remove_request(&ci->dsu_qos_req);
	}

	return 0;
}

static int rockchip_cpufreq_add_dsu_qos_req(struct cluster_info *cluster,
					    struct cpufreq_policy *policy)
{
	struct device *dev = cluster->opp_info.dev;
	struct cluster_info *ci;
	int ret;

	if (!cluster->is_opp_shared_dsu)
		return 0;

	list_for_each_entry(ci, &cluster_info_list, list_head) {
		if (ci->is_opp_shared_dsu)
			continue;
		ret = freq_qos_add_request(&policy->constraints,
					   &ci->dsu_qos_req,
					   FREQ_QOS_MIN,
					   FREQ_QOS_MIN_DEFAULT_VALUE);
		if (ret < 0) {
			dev_err(dev, "failed to add dsu freq constraint\n");
			goto error;
		}
	}

	return 0;

error:
	rockchip_cpufreq_remove_dsu_qos(cluster);

	return ret;
}

static int rockchip_cpufreq_notifier(struct notifier_block *nb,
				     unsigned long event, void *data)
{
	struct cpufreq_policy *policy = data;
	struct cluster_info *cluster;

	cluster = rockchip_cluster_info_lookup(policy->cpu);
	if (!cluster)
		return NOTIFY_BAD;

	if (event == CPUFREQ_CREATE_POLICY) {
		if (rockchip_cpufreq_add_monitor(cluster, policy))
			return NOTIFY_BAD;
		if (rockchip_cpufreq_add_dsu_qos_req(cluster, policy))
			return NOTIFY_BAD;
	} else if (event == CPUFREQ_REMOVE_POLICY) {
		rockchip_cpufreq_remove_monitor(cluster);
		rockchip_cpufreq_remove_dsu_qos(cluster);
	}

	return NOTIFY_OK;
}

static struct notifier_block rockchip_cpufreq_notifier_block = {
	.notifier_call = rockchip_cpufreq_notifier,
};

#ifdef MODULE
static struct pm_qos_request idle_pm_qos;
static int idle_disable_refcnt;
static DEFINE_MUTEX(idle_disable_lock);

static int rockchip_cpufreq_idle_state_disable(struct cpumask *cpumask,
					       int index, bool disable)
{
	mutex_lock(&idle_disable_lock);

	if (disable) {
		if (idle_disable_refcnt == 0)
			cpu_latency_qos_update_request(&idle_pm_qos, 0);
		idle_disable_refcnt++;
	} else {
		if (--idle_disable_refcnt == 0)
			cpu_latency_qos_update_request(&idle_pm_qos,
						       PM_QOS_DEFAULT_VALUE);
	}

	mutex_unlock(&idle_disable_lock);

	return 0;
}
#else
static int rockchip_cpufreq_idle_state_disable(struct cpumask *cpumask,
					       int index, bool disable)
{
	unsigned int cpu;

	for_each_cpu(cpu, cpumask) {
		struct cpuidle_device *dev = per_cpu(cpuidle_devices, cpu);
		struct cpuidle_driver *drv = cpuidle_get_cpu_driver(dev);

		if (!dev || !drv)
			continue;
		if (index >= drv->state_count)
			continue;
		cpuidle_driver_state_disabled(drv, index, disable);
	}

	if (disable) {
		preempt_disable();
		for_each_cpu(cpu, cpumask) {
			if (cpu != smp_processor_id() && cpu_online(cpu))
				wake_up_if_idle(cpu);
		}
		preempt_enable();
	}

	return 0;
}
#endif

#define cpu_to_dsu_freq(freq)  ((freq) * 4 / 5)

static int rockchip_cpufreq_update_dsu_req(struct cluster_info *cluster,
					   unsigned int freq)
{
	struct device *dev = cluster->opp_info.dev;
	unsigned int dsu_freq = rounddown(cpu_to_dsu_freq(freq), 100000);

	if (cluster->is_opp_shared_dsu ||
	    !freq_qos_request_active(&cluster->dsu_qos_req))
		return 0;

	dev_dbg(dev, "cpu to dsu: %u -> %u\n", freq, dsu_freq);

	return freq_qos_update_request(&cluster->dsu_qos_req, dsu_freq);
}

static int rockchip_cpufreq_transition_notifier(struct notifier_block *nb,
						unsigned long event, void *data)
{
	struct cpufreq_freqs *freqs = data;
	struct cpufreq_policy *policy = freqs->policy;
	struct cluster_info *cluster;

	cluster = rockchip_cluster_info_lookup(policy->cpu);
	if (!cluster)
		return NOTIFY_BAD;

	if (event == CPUFREQ_PRECHANGE) {
		if (cluster->idle_threshold_freq &&
		    freqs->new >= cluster->idle_threshold_freq &&
		    !cluster->is_idle_disabled) {
			rockchip_cpufreq_idle_state_disable(policy->cpus, 1,
							    true);
			cluster->is_idle_disabled = true;
		}
	} else if (event == CPUFREQ_POSTCHANGE) {
		if (cluster->idle_threshold_freq &&
		    freqs->new < cluster->idle_threshold_freq &&
		    cluster->is_idle_disabled) {
			rockchip_cpufreq_idle_state_disable(policy->cpus, 1,
							    false);
			cluster->is_idle_disabled = false;
		}
		rockchip_cpufreq_update_dsu_req(cluster, freqs->new);
	}

	return NOTIFY_OK;
}

static struct notifier_block rockchip_cpufreq_transition_notifier_block = {
	.notifier_call = rockchip_cpufreq_transition_notifier,
};

static int rockchip_cpufreq_panic_notifier(struct notifier_block *nb,
					   unsigned long v, void *p)
{
	struct cluster_info *ci;

	list_for_each_entry(ci, &cluster_info_list, list_head) {
		rockchip_opp_dump_cur_state(ci->opp_info.dev);
	}

	return 0;
}

static struct notifier_block rockchip_cpufreq_panic_notifier_block = {
	.notifier_call = rockchip_cpufreq_panic_notifier,
};

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

	ret = cpufreq_register_notifier(&rockchip_cpufreq_notifier_block,
					CPUFREQ_POLICY_NOTIFIER);
	if (ret) {
		pr_err("failed to register cpufreq notifier\n");
		goto release_cluster_info;
	}

	if (of_machine_is_compatible("rockchip,rk3588")) {
		ret = cpufreq_register_notifier(&rockchip_cpufreq_transition_notifier_block,
						CPUFREQ_TRANSITION_NOTIFIER);
		if (ret) {
			cpufreq_unregister_notifier(&rockchip_cpufreq_notifier_block,
						    CPUFREQ_POLICY_NOTIFIER);
			pr_err("failed to register cpufreq notifier\n");
			goto release_cluster_info;
		}
#ifdef MODULE
		cpu_latency_qos_add_request(&idle_pm_qos, PM_QOS_DEFAULT_VALUE);
#endif
	}

	ret = atomic_notifier_chain_register(&panic_notifier_list,
					     &rockchip_cpufreq_panic_notifier_block);
	if (ret)
		pr_err("failed to register cpufreq panic notifier\n");

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
