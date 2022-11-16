// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018, Fuzhou Rockchip Electronics Co., Ltd.
 * Author: Tony Xie <tony.xie@rock-chips.com>
 */

#include <linux/arm-smccc.h>
#include <linux/clk.h>
#include <linux/cpufreq.h>
#include <linux/delay.h>
#include <linux/devfreq.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pm_opp.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/rockchip/rockchip_sip.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <soc/rockchip/rockchip_opp_select.h>

#define CLUSTER0	0
#define CLUSTER1	1
#define MAX_CLUSTERS	2

#define to_rockchip_bus_clk_nb(nb) \
	container_of(nb, struct rockchip_bus, clk_nb)
#define to_rockchip_bus_cpufreq_nb(nb) \
	container_of(nb, struct rockchip_bus, cpufreq_nb)

struct busfreq_table {
	unsigned long freq;
	unsigned long volt;
};

struct rockchip_bus {
	struct device *dev;
	struct regulator *regulator;
	struct clk *clk;
	struct notifier_block clk_nb;
	struct notifier_block cpufreq_nb;
	struct busfreq_table *freq_table;

	unsigned int max_state;

	unsigned long cur_volt;
	unsigned long cur_rate;

	/*
	 * Busfreq-policy-cpufreq:
	 * If the cpu frequency of two clusters are both less than or equal to
	 * cpu_high_freq, change bus rate to low_rate, otherwise change it to
	 * high_rate.
	 */
	unsigned long high_rate;
	unsigned long low_rate;
	unsigned int cpu_high_freq;
	unsigned int cpu_freq[MAX_CLUSTERS];
};

static int rockchip_sip_bus_smc_config(u32 bus_id, u32 cfg, u32 enable_msk)
{
	struct arm_smccc_res res;

	res = sip_smc_bus_config(bus_id, cfg, enable_msk);

	return res.a0;
}

static int rockchip_bus_smc_config(struct rockchip_bus *bus)
{
	struct device *dev = bus->dev;
	struct device_node *np = dev->of_node;
	struct device_node *child;
	unsigned int enable_msk, bus_id, cfg;
	char *prp_name = "rockchip,soc-bus-table";
	u32 *table = NULL;
	int ret = 0, config_cnt, i;

	for_each_available_child_of_node(np, child) {
		ret = of_property_read_u32_index(child, "bus-id", 0,
						 &bus_id);
		if (ret)
			continue;

		ret = of_property_read_u32_index(child, "cfg-val", 0,
						 &cfg);
		if (ret) {
			dev_info(dev, "get cfg-val error\n");
			continue;
		}

		if (!cfg) {
			dev_info(dev, "cfg-val invalid\n");
			continue;
		}

		ret = of_property_read_u32_index(child, "enable-msk", 0,
						 &enable_msk);
		if (ret) {
			dev_info(dev, "get enable_msk error\n");
			continue;
		}

		ret = rockchip_sip_bus_smc_config(bus_id, cfg,
						  enable_msk);
		if (ret) {
			dev_info(dev, "bus smc config error: %x!\n", ret);
			break;
		}
	}

	config_cnt = of_property_count_u32_elems(np, prp_name);
	if (config_cnt <= 0) {
		return 0;
	} else if (config_cnt % 3) {
		dev_err(dev, "Invalid count of %s\n", prp_name);
		return -EINVAL;
	}

	table = kmalloc_array(config_cnt, sizeof(u32), GFP_KERNEL);
	if (!table)
		return -ENOMEM;

	ret = of_property_read_u32_array(np, prp_name, table, config_cnt);
	if (ret) {
		dev_err(dev, "get %s error\n", prp_name);
		goto free_table;
	}

	/* table[3n]: bus_id
	 * table[3n + 1]: config
	 * table[3n + 2]: enable_mask
	 */
	for (i = 0; i < config_cnt; i += 3) {
		bus_id = table[i];
		cfg = table[i + 1];
		enable_msk = table[i + 2];

		if (!cfg) {
			dev_info(dev, "cfg-val invalid in %s-%d\n", prp_name, bus_id);
			continue;
		}

		ret = rockchip_sip_bus_smc_config(bus_id, cfg, enable_msk);
		if (ret) {
			dev_err(dev, "bus smc config error: %x!\n", ret);
			goto free_table;
		}
	}

free_table:
	kfree(table);

	return ret;
}

static int rockchip_bus_set_freq_table(struct rockchip_bus *bus)
{
	struct device *dev = bus->dev;
	struct dev_pm_opp *opp;
	unsigned long freq;
	int i, count;

	count = dev_pm_opp_get_opp_count(dev);
	if (count <= 0)
		return -EINVAL;

	bus->max_state = count;
	bus->freq_table = devm_kcalloc(dev,
				       bus->max_state,
				       sizeof(*bus->freq_table),
				       GFP_KERNEL);
	if (!bus->freq_table) {
		bus->max_state = 0;
		return -ENOMEM;
	}

	for (i = 0, freq = 0; i < bus->max_state; i++, freq++) {
		opp = dev_pm_opp_find_freq_ceil(dev, &freq);
		if (IS_ERR(opp)) {
			devm_kfree(dev, bus->freq_table);
			bus->max_state = 0;
			return PTR_ERR(opp);
		}
		bus->freq_table[i].volt = dev_pm_opp_get_voltage(opp);
		bus->freq_table[i].freq = freq;
		dev_pm_opp_put(opp);
	}

	return 0;
}

static int rockchip_bus_power_control_init(struct rockchip_bus *bus)
{
	struct device *dev = bus->dev;
	int ret = 0;

	bus->clk = devm_clk_get(dev, "bus");
	if (IS_ERR(bus->clk)) {
		dev_err(dev, "failed to get bus clock\n");
		return PTR_ERR(bus->clk);
	}

	bus->regulator = devm_regulator_get(dev, "bus");
	if (IS_ERR(bus->regulator)) {
		dev_err(dev, "failed to get bus regulator\n");
		return PTR_ERR(bus->regulator);
	}

	ret = rockchip_init_opp_table(dev, NULL, "leakage", "pvtm");
	if (ret < 0) {
		dev_err(dev, "failed to get OPP table\n");
		return ret;
	}

	ret = rockchip_bus_set_freq_table(bus);
	if (ret < 0) {
		dev_err(dev, "failed to set bus freq table\n");
		return ret;
	}

	return 0;
}

static int rockchip_bus_clkfreq_target(struct device *dev, unsigned long freq)
{
	struct rockchip_bus *bus = dev_get_drvdata(dev);
	unsigned long target_volt = bus->freq_table[bus->max_state - 1].volt;
	int i;

	for (i = 0; i < bus->max_state; i++) {
		if (freq <= bus->freq_table[i].freq) {
			target_volt = bus->freq_table[i].volt;
			break;
		}
	}

	if (bus->cur_volt != target_volt) {
		dev_dbg(bus->dev, "target_volt: %lu\n", target_volt);
		if (regulator_set_voltage(bus->regulator, target_volt,
					  INT_MAX)) {
			dev_err(dev, "failed to set voltage %lu uV\n",
				target_volt);
			return -EINVAL;
		}
		bus->cur_volt = target_volt;
	}

	return 0;
}

static int rockchip_bus_clk_notifier(struct notifier_block *nb,
				     unsigned long event, void *data)
{
	struct clk_notifier_data *ndata = data;
	struct rockchip_bus *bus = to_rockchip_bus_clk_nb(nb);
	int ret = 0;

	dev_dbg(bus->dev, "event %lu, old_rate %lu, new_rate: %lu\n",
		event, ndata->old_rate, ndata->new_rate);

	switch (event) {
	case PRE_RATE_CHANGE:
		if (ndata->new_rate > ndata->old_rate)
			ret = rockchip_bus_clkfreq_target(bus->dev,
							  ndata->new_rate);
		break;
	case POST_RATE_CHANGE:
		if (ndata->new_rate < ndata->old_rate)
			ret = rockchip_bus_clkfreq_target(bus->dev,
							  ndata->new_rate);
		break;
	case ABORT_RATE_CHANGE:
		if (ndata->new_rate > ndata->old_rate)
			ret = rockchip_bus_clkfreq_target(bus->dev,
							  ndata->old_rate);
		break;
	default:
		break;
	}

	return notifier_from_errno(ret);
}

static int rockchip_bus_clkfreq(struct rockchip_bus *bus)
{
	struct device *dev = bus->dev;
	unsigned long init_rate;
	int ret = 0;

	ret = rockchip_bus_power_control_init(bus);
	if (ret) {
		dev_err(dev, "failed to init power control\n");
		return ret;
	}

	init_rate = clk_get_rate(bus->clk);
	ret = rockchip_bus_clkfreq_target(dev, init_rate);
	if (ret)
		return ret;

	bus->clk_nb.notifier_call = rockchip_bus_clk_notifier;
	ret = clk_notifier_register(bus->clk, &bus->clk_nb);
	if (ret) {
		dev_err(dev, "failed to register clock notifier\n");
		return ret;
	}

	return 0;
}

static int rockchip_bus_cpufreq_target(struct device *dev, unsigned long freq,
				       u32 flags)
{
	struct rockchip_bus *bus = dev_get_drvdata(dev);
	struct dev_pm_opp *opp;
	unsigned long target_volt, target_rate = freq;
	int ret = 0;

	if (!bus->regulator) {
		dev_dbg(dev, "%luHz -> %luHz\n", bus->cur_rate, target_rate);
		ret = clk_set_rate(bus->clk, target_rate);
		if (ret)
			dev_err(bus->dev, "failed to set bus rate %lu\n",
				target_rate);
		else
			bus->cur_rate = target_rate;
		return ret;
	}

	opp = devfreq_recommended_opp(dev, &target_rate, flags);
	if (IS_ERR(opp)) {
		dev_err(dev, "failed to recommended opp %lu\n", target_rate);
		return PTR_ERR(opp);
	}
	target_volt = dev_pm_opp_get_voltage(opp);
	dev_pm_opp_put(opp);

	if (bus->cur_rate == target_rate) {
		if (bus->cur_volt == target_volt)
			return 0;
		ret = regulator_set_voltage(bus->regulator, target_volt,
					    INT_MAX);
		if (ret) {
			dev_err(dev, "failed to set voltage %lu\n",
				target_volt);
			return ret;
		}
		bus->cur_volt = target_volt;
		return 0;
	} else if (!bus->cur_volt) {
		bus->cur_volt = regulator_get_voltage(bus->regulator);
	}

	if (bus->cur_rate < target_rate) {
		ret = regulator_set_voltage(bus->regulator, target_volt,
					    INT_MAX);
		if (ret) {
			dev_err(dev, "failed to set voltage %lu\n",
				target_volt);
			return ret;
		}
	}

	ret = clk_set_rate(bus->clk, target_rate);
	if (ret) {
		dev_err(dev, "failed to set bus rate %lu\n", target_rate);
		return ret;
	}

	if (bus->cur_rate > target_rate) {
		ret = regulator_set_voltage(bus->regulator, target_volt,
					    INT_MAX);
		if (ret) {
			dev_err(dev, "failed to set voltage %lu\n",
				target_volt);
			return ret;
		}
	}

	dev_dbg(dev, "%luHz %luuV -> %luHz %luuV\n", bus->cur_rate,
		bus->cur_volt, target_rate, target_volt);
	bus->cur_rate = target_rate;
	bus->cur_volt = target_volt;

	return ret;
}

static int rockchip_bus_cpufreq_notifier(struct notifier_block *nb,
					 unsigned long event, void *data)
{
	struct rockchip_bus *bus = to_rockchip_bus_cpufreq_nb(nb);
	struct cpufreq_freqs *freqs = data;
	int id = topology_physical_package_id(freqs->policy->cpu);

	if (id < 0 || id >= MAX_CLUSTERS)
		return NOTIFY_DONE;

	bus->cpu_freq[id] = freqs->new;

	if (!bus->cpu_freq[CLUSTER0] || !bus->cpu_freq[CLUSTER1])
		return NOTIFY_DONE;

	switch (event) {
	case CPUFREQ_PRECHANGE:
		if ((bus->cpu_freq[CLUSTER0] > bus->cpu_high_freq ||
		     bus->cpu_freq[CLUSTER1] > bus->cpu_high_freq) &&
		     bus->cur_rate != bus->high_rate) {
			dev_dbg(bus->dev, "cpu%d freq=%d %d, up cci rate to %lu\n",
				freqs->policy->cpu,
				bus->cpu_freq[CLUSTER0],
				bus->cpu_freq[CLUSTER1],
				bus->high_rate);
			rockchip_bus_cpufreq_target(bus->dev, bus->high_rate,
						    0);
		}
		break;
	case CPUFREQ_POSTCHANGE:
		if (bus->cpu_freq[CLUSTER0] <= bus->cpu_high_freq &&
		    bus->cpu_freq[CLUSTER1] <= bus->cpu_high_freq &&
		    bus->cur_rate != bus->low_rate) {
			dev_dbg(bus->dev, "cpu%d freq=%d %d, down cci rate to %lu\n",
				freqs->policy->cpu,
				bus->cpu_freq[CLUSTER0],
				bus->cpu_freq[CLUSTER1],
				bus->low_rate);
			rockchip_bus_cpufreq_target(bus->dev, bus->low_rate,
						    0);
		}
		break;
	}

	return NOTIFY_OK;
}

static int rockchip_bus_cpufreq(struct rockchip_bus *bus)
{
	struct device *dev = bus->dev;
	struct device_node *np = dev->of_node;
	unsigned int freq;
	int ret = 0;

	if (of_parse_phandle(dev->of_node, "operating-points-v2", 0)) {
		ret = rockchip_bus_power_control_init(bus);
		if (ret) {
			dev_err(dev, "failed to init power control\n");
			return ret;
		}
	} else {
		bus->clk = devm_clk_get(dev, "bus");
		if (IS_ERR(bus->clk)) {
			dev_err(dev, "failed to get bus clock\n");
			return PTR_ERR(bus->clk);
		}
		bus->regulator = NULL;
	}

	ret = of_property_read_u32(np, "cpu-high-freq", &bus->cpu_high_freq);
	if (ret) {
		dev_err(dev, "failed to get cpu-high-freq\n");
		return ret;
	}
	ret = of_property_read_u32(np, "cci-high-freq", &freq);
	if (ret) {
		dev_err(dev, "failed to get cci-high-freq\n");
		return ret;
	}
	bus->high_rate = freq * 1000;
	ret = of_property_read_u32(np, "cci-low-freq", &freq);
	if (ret) {
		dev_err(dev, "failed to get cci-low-freq\n");
		return ret;
	}
	bus->low_rate = freq * 1000;

	bus->cpufreq_nb.notifier_call = rockchip_bus_cpufreq_notifier;
	ret = cpufreq_register_notifier(&bus->cpufreq_nb,
					CPUFREQ_TRANSITION_NOTIFIER);
	if (ret) {
		dev_err(dev, "failed to register cpufreq notifier\n");
		return ret;
	}

	return 0;
}

static const struct of_device_id rockchip_busfreq_of_match[] = {
	{ .compatible = "rockchip,px30-bus", },
	{ .compatible = "rockchip,rk1808-bus", },
	{ .compatible = "rockchip,rk3288-bus", },
	{ .compatible = "rockchip,rk3368-bus", },
	{ .compatible = "rockchip,rk3399-bus", },
	{ .compatible = "rockchip,rk3568-bus", },
	{ .compatible = "rockchip,rk3588-bus", },
	{ .compatible = "rockchip,rv1126-bus", },
	{ },
};

MODULE_DEVICE_TABLE(of, rockchip_busfreq_of_match);

static int rockchip_busfreq_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct rockchip_bus *bus;
	const char *policy_name;
	int ret = 0;

	bus = devm_kzalloc(dev, sizeof(*bus), GFP_KERNEL);
	if (!bus)
		return -ENOMEM;
	bus->dev = dev;
	platform_set_drvdata(pdev, bus);

	ret = of_property_read_string(np, "rockchip,busfreq-policy",
				      &policy_name);
	if (ret) {
		dev_info(dev, "failed to get busfreq policy\n");
		return ret;
	}

	if (!strcmp(policy_name, "smc"))
		ret = rockchip_bus_smc_config(bus);
	else if (!strcmp(policy_name, "clkfreq"))
		ret = rockchip_bus_clkfreq(bus);
	else if (!strcmp(policy_name, "cpufreq"))
		ret = rockchip_bus_cpufreq(bus);

	return ret;
}

static struct platform_driver rockchip_busfreq_driver = {
	.probe	= rockchip_busfreq_probe,
	.driver = {
		.name	= "rockchip,bus",
		.of_match_table = rockchip_busfreq_of_match,
	},
};

module_platform_driver(rockchip_busfreq_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Tony Xie <tony.xie@rock-chips.com>");
MODULE_DESCRIPTION("rockchip busfreq driver with devfreq framework");
