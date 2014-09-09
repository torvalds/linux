/*
 * Copyright (C) 2012 Freescale Semiconductor, Inc.
 *
 * Copyright (C) 2014 Linaro.
 * Viresh Kumar <viresh.kumar@linaro.org>
 *
 * The OPP code in function set_target() is reused from
 * drivers/cpufreq/omap-cpufreq.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/clk.h>
#include <linux/cpu.h>
#include <linux/cpu_cooling.h>
#include <linux/cpufreq.h>
#include <linux/cpumask.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pm_opp.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/thermal.h>

struct private_data {
	struct device *cpu_dev;
	struct regulator *cpu_reg;
	struct thermal_cooling_device *cdev;
	unsigned int voltage_tolerance; /* in percentage */
};

static int set_target(struct cpufreq_policy *policy, unsigned int index)
{
	struct dev_pm_opp *opp;
	struct cpufreq_frequency_table *freq_table = policy->freq_table;
	struct clk *cpu_clk = policy->clk;
	struct private_data *priv = policy->driver_data;
	struct device *cpu_dev = priv->cpu_dev;
	struct regulator *cpu_reg = priv->cpu_reg;
	unsigned long volt = 0, volt_old = 0, tol = 0;
	unsigned int old_freq, new_freq;
	long freq_Hz, freq_exact;
	int ret;

	freq_Hz = clk_round_rate(cpu_clk, freq_table[index].frequency * 1000);
	if (freq_Hz <= 0)
		freq_Hz = freq_table[index].frequency * 1000;

	freq_exact = freq_Hz;
	new_freq = freq_Hz / 1000;
	old_freq = clk_get_rate(cpu_clk) / 1000;

	if (!IS_ERR(cpu_reg)) {
		rcu_read_lock();
		opp = dev_pm_opp_find_freq_ceil(cpu_dev, &freq_Hz);
		if (IS_ERR(opp)) {
			rcu_read_unlock();
			dev_err(cpu_dev, "failed to find OPP for %ld\n",
				freq_Hz);
			return PTR_ERR(opp);
		}
		volt = dev_pm_opp_get_voltage(opp);
		rcu_read_unlock();
		tol = volt * priv->voltage_tolerance / 100;
		volt_old = regulator_get_voltage(cpu_reg);
	}

	dev_dbg(cpu_dev, "%u MHz, %ld mV --> %u MHz, %ld mV\n",
		old_freq / 1000, volt_old ? volt_old / 1000 : -1,
		new_freq / 1000, volt ? volt / 1000 : -1);

	/* scaling up?  scale voltage before frequency */
	if (!IS_ERR(cpu_reg) && new_freq > old_freq) {
		ret = regulator_set_voltage_tol(cpu_reg, volt, tol);
		if (ret) {
			dev_err(cpu_dev, "failed to scale voltage up: %d\n",
				ret);
			return ret;
		}
	}

	ret = clk_set_rate(cpu_clk, freq_exact);
	if (ret) {
		dev_err(cpu_dev, "failed to set clock rate: %d\n", ret);
		if (!IS_ERR(cpu_reg))
			regulator_set_voltage_tol(cpu_reg, volt_old, tol);
		return ret;
	}

	/* scaling down?  scale voltage after frequency */
	if (!IS_ERR(cpu_reg) && new_freq < old_freq) {
		ret = regulator_set_voltage_tol(cpu_reg, volt, tol);
		if (ret) {
			dev_err(cpu_dev, "failed to scale voltage down: %d\n",
				ret);
			clk_set_rate(cpu_clk, old_freq * 1000);
		}
	}

	return ret;
}

static int allocate_resources(int cpu, struct device **cdev,
			      struct regulator **creg, struct clk **cclk)
{
	struct device *cpu_dev;
	struct regulator *cpu_reg;
	struct clk *cpu_clk;
	int ret = 0;
	char *reg_cpu0 = "cpu0", *reg_cpu = "cpu", *reg;

	cpu_dev = get_cpu_device(cpu);
	if (!cpu_dev) {
		pr_err("failed to get cpu%d device\n", cpu);
		return -ENODEV;
	}

	/* Try "cpu0" for older DTs */
	if (!cpu)
		reg = reg_cpu0;
	else
		reg = reg_cpu;

try_again:
	cpu_reg = regulator_get_optional(cpu_dev, reg);
	if (IS_ERR(cpu_reg)) {
		/*
		 * If cpu's regulator supply node is present, but regulator is
		 * not yet registered, we should try defering probe.
		 */
		if (PTR_ERR(cpu_reg) == -EPROBE_DEFER) {
			dev_dbg(cpu_dev, "cpu%d regulator not ready, retry\n",
				cpu);
			return -EPROBE_DEFER;
		}

		/* Try with "cpu-supply" */
		if (reg == reg_cpu0) {
			reg = reg_cpu;
			goto try_again;
		}

		dev_warn(cpu_dev, "failed to get cpu%d regulator: %ld\n",
			 cpu, PTR_ERR(cpu_reg));
	}

	cpu_clk = clk_get(cpu_dev, NULL);
	if (IS_ERR(cpu_clk)) {
		/* put regulator */
		if (!IS_ERR(cpu_reg))
			regulator_put(cpu_reg);

		ret = PTR_ERR(cpu_clk);

		/*
		 * If cpu's clk node is present, but clock is not yet
		 * registered, we should try defering probe.
		 */
		if (ret == -EPROBE_DEFER)
			dev_dbg(cpu_dev, "cpu%d clock not ready, retry\n", cpu);
		else
			dev_err(cpu_dev, "failed to get cpu%d clock: %d\n", ret,
				cpu);
	} else {
		*cdev = cpu_dev;
		*creg = cpu_reg;
		*cclk = cpu_clk;
	}

	return ret;
}

static int cpufreq_init(struct cpufreq_policy *policy)
{
	struct cpufreq_frequency_table *freq_table;
	struct thermal_cooling_device *cdev;
	struct device_node *np;
	struct private_data *priv;
	struct device *cpu_dev;
	struct regulator *cpu_reg;
	struct clk *cpu_clk;
	unsigned int transition_latency;
	int ret;

	ret = allocate_resources(policy->cpu, &cpu_dev, &cpu_reg, &cpu_clk);
	if (ret) {
		pr_err("%s: Failed to allocate resources\n: %d", __func__, ret);
		return ret;
	}

	np = of_node_get(cpu_dev->of_node);
	if (!np) {
		dev_err(cpu_dev, "failed to find cpu%d node\n", policy->cpu);
		ret = -ENOENT;
		goto out_put_reg_clk;
	}

	/* OPPs might be populated at runtime, don't check for error here */
	of_init_opp_table(cpu_dev);

	ret = dev_pm_opp_init_cpufreq_table(cpu_dev, &freq_table);
	if (ret) {
		dev_err(cpu_dev, "failed to init cpufreq table: %d\n", ret);
		goto out_put_node;
	}

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		ret = -ENOMEM;
		goto out_free_table;
	}

	of_property_read_u32(np, "voltage-tolerance", &priv->voltage_tolerance);

	if (of_property_read_u32(np, "clock-latency", &transition_latency))
		transition_latency = CPUFREQ_ETERNAL;

	if (!IS_ERR(cpu_reg)) {
		struct dev_pm_opp *opp;
		unsigned long min_uV, max_uV;
		int i;

		/*
		 * OPP is maintained in order of increasing frequency, and
		 * freq_table initialised from OPP is therefore sorted in the
		 * same order.
		 */
		for (i = 0; freq_table[i].frequency != CPUFREQ_TABLE_END; i++)
			;
		rcu_read_lock();
		opp = dev_pm_opp_find_freq_exact(cpu_dev,
				freq_table[0].frequency * 1000, true);
		min_uV = dev_pm_opp_get_voltage(opp);
		opp = dev_pm_opp_find_freq_exact(cpu_dev,
				freq_table[i-1].frequency * 1000, true);
		max_uV = dev_pm_opp_get_voltage(opp);
		rcu_read_unlock();
		ret = regulator_set_voltage_time(cpu_reg, min_uV, max_uV);
		if (ret > 0)
			transition_latency += ret * 1000;
	}

	/*
	 * For now, just loading the cooling device;
	 * thermal DT code takes care of matching them.
	 */
	if (of_find_property(np, "#cooling-cells", NULL)) {
		cdev = of_cpufreq_cooling_register(np, cpu_present_mask);
		if (IS_ERR(cdev))
			dev_err(cpu_dev,
				"running cpufreq without cooling device: %ld\n",
				PTR_ERR(cdev));
		else
			priv->cdev = cdev;
	}
	of_node_put(np);

	priv->cpu_dev = cpu_dev;
	priv->cpu_reg = cpu_reg;
	policy->driver_data = priv;

	policy->clk = cpu_clk;
	ret = cpufreq_generic_init(policy, freq_table, transition_latency);
	if (ret)
		goto out_cooling_unregister;

	return 0;

out_cooling_unregister:
	cpufreq_cooling_unregister(priv->cdev);
	kfree(priv);
out_free_table:
	dev_pm_opp_free_cpufreq_table(cpu_dev, &freq_table);
out_put_node:
	of_node_put(np);
out_put_reg_clk:
	clk_put(cpu_clk);
	if (!IS_ERR(cpu_reg))
		regulator_put(cpu_reg);

	return ret;
}

static int cpufreq_exit(struct cpufreq_policy *policy)
{
	struct private_data *priv = policy->driver_data;

	cpufreq_cooling_unregister(priv->cdev);
	dev_pm_opp_free_cpufreq_table(priv->cpu_dev, &policy->freq_table);
	clk_put(policy->clk);
	if (!IS_ERR(priv->cpu_reg))
		regulator_put(priv->cpu_reg);
	kfree(priv);

	return 0;
}

static struct cpufreq_driver dt_cpufreq_driver = {
	.flags = CPUFREQ_STICKY | CPUFREQ_NEED_INITIAL_FREQ_CHECK,
	.verify = cpufreq_generic_frequency_table_verify,
	.target_index = set_target,
	.get = cpufreq_generic_get,
	.init = cpufreq_init,
	.exit = cpufreq_exit,
	.name = "cpufreq-dt",
	.attr = cpufreq_generic_attr,
};

static int dt_cpufreq_probe(struct platform_device *pdev)
{
	struct device *cpu_dev;
	struct regulator *cpu_reg;
	struct clk *cpu_clk;
	int ret;

	/*
	 * All per-cluster (CPUs sharing clock/voltages) initialization is done
	 * from ->init(). In probe(), we just need to make sure that clk and
	 * regulators are available. Else defer probe and retry.
	 *
	 * FIXME: Is checking this only for CPU0 sufficient ?
	 */
	ret = allocate_resources(0, &cpu_dev, &cpu_reg, &cpu_clk);
	if (ret)
		return ret;

	clk_put(cpu_clk);
	if (!IS_ERR(cpu_reg))
		regulator_put(cpu_reg);

	ret = cpufreq_register_driver(&dt_cpufreq_driver);
	if (ret)
		dev_err(cpu_dev, "failed register driver: %d\n", ret);

	return ret;
}

static int dt_cpufreq_remove(struct platform_device *pdev)
{
	cpufreq_unregister_driver(&dt_cpufreq_driver);
	return 0;
}

static struct platform_driver dt_cpufreq_platdrv = {
	.driver = {
		.name	= "cpufreq-dt",
		.owner	= THIS_MODULE,
	},
	.probe		= dt_cpufreq_probe,
	.remove		= dt_cpufreq_remove,
};
module_platform_driver(dt_cpufreq_platdrv);

MODULE_AUTHOR("Viresh Kumar <viresh.kumar@linaro.org>");
MODULE_AUTHOR("Shawn Guo <shawn.guo@linaro.org>");
MODULE_DESCRIPTION("Generic cpufreq driver");
MODULE_LICENSE("GPL");
