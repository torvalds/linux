/*
 * Copyright (C) 2013 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/cpufreq.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/opp.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>

#define PU_SOC_VOLTAGE_NORMAL	1250000
#define PU_SOC_VOLTAGE_HIGH	1275000
#define FREQ_1P2_GHZ		1200000000

static struct regulator *arm_reg;
static struct regulator *pu_reg;
static struct regulator *soc_reg;

static struct clk *arm_clk;
static struct clk *pll1_sys_clk;
static struct clk *pll1_sw_clk;
static struct clk *step_clk;
static struct clk *pll2_pfd2_396m_clk;

static struct device *cpu_dev;
static struct cpufreq_frequency_table *freq_table;
static unsigned int transition_latency;

static int imx6q_verify_speed(struct cpufreq_policy *policy)
{
	return cpufreq_frequency_table_verify(policy, freq_table);
}

static unsigned int imx6q_get_speed(unsigned int cpu)
{
	return clk_get_rate(arm_clk) / 1000;
}

static int imx6q_set_target(struct cpufreq_policy *policy,
			    unsigned int target_freq, unsigned int relation)
{
	struct cpufreq_freqs freqs;
	struct opp *opp;
	unsigned long freq_hz, volt, volt_old;
	unsigned int index;
	int ret;

	ret = cpufreq_frequency_table_target(policy, freq_table, target_freq,
					     relation, &index);
	if (ret) {
		dev_err(cpu_dev, "failed to match target frequency %d: %d\n",
			target_freq, ret);
		return ret;
	}

	freqs.new = freq_table[index].frequency;
	freq_hz = freqs.new * 1000;
	freqs.old = clk_get_rate(arm_clk) / 1000;

	if (freqs.old == freqs.new)
		return 0;

	rcu_read_lock();
	opp = opp_find_freq_ceil(cpu_dev, &freq_hz);
	if (IS_ERR(opp)) {
		rcu_read_unlock();
		dev_err(cpu_dev, "failed to find OPP for %ld\n", freq_hz);
		return PTR_ERR(opp);
	}

	volt = opp_get_voltage(opp);
	rcu_read_unlock();
	volt_old = regulator_get_voltage(arm_reg);

	dev_dbg(cpu_dev, "%u MHz, %ld mV --> %u MHz, %ld mV\n",
		freqs.old / 1000, volt_old / 1000,
		freqs.new / 1000, volt / 1000);

	cpufreq_notify_transition(policy, &freqs, CPUFREQ_PRECHANGE);

	/* scaling up?  scale voltage before frequency */
	if (freqs.new > freqs.old) {
		ret = regulator_set_voltage_tol(arm_reg, volt, 0);
		if (ret) {
			dev_err(cpu_dev,
				"failed to scale vddarm up: %d\n", ret);
			freqs.new = freqs.old;
			goto post_notify;
		}

		/*
		 * Need to increase vddpu and vddsoc for safety
		 * if we are about to run at 1.2 GHz.
		 */
		if (freqs.new == FREQ_1P2_GHZ / 1000) {
			regulator_set_voltage_tol(pu_reg,
					PU_SOC_VOLTAGE_HIGH, 0);
			regulator_set_voltage_tol(soc_reg,
					PU_SOC_VOLTAGE_HIGH, 0);
		}
	}

	/*
	 * The setpoints are selected per PLL/PDF frequencies, so we need to
	 * reprogram PLL for frequency scaling.  The procedure of reprogramming
	 * PLL1 is as below.
	 *
	 *  - Enable pll2_pfd2_396m_clk and reparent pll1_sw_clk to it
	 *  - Reprogram pll1_sys_clk and reparent pll1_sw_clk back to it
	 *  - Disable pll2_pfd2_396m_clk
	 */
	clk_prepare_enable(pll2_pfd2_396m_clk);
	clk_set_parent(step_clk, pll2_pfd2_396m_clk);
	clk_set_parent(pll1_sw_clk, step_clk);
	if (freq_hz > clk_get_rate(pll2_pfd2_396m_clk)) {
		clk_set_rate(pll1_sys_clk, freqs.new * 1000);
		/*
		 * If we are leaving 396 MHz set-point, we need to enable
		 * pll1_sys_clk and disable pll2_pfd2_396m_clk to keep
		 * their use count correct.
		 */
		if (freqs.old * 1000 <= clk_get_rate(pll2_pfd2_396m_clk)) {
			clk_prepare_enable(pll1_sys_clk);
			clk_disable_unprepare(pll2_pfd2_396m_clk);
		}
		clk_set_parent(pll1_sw_clk, pll1_sys_clk);
		clk_disable_unprepare(pll2_pfd2_396m_clk);
	} else {
		/*
		 * Disable pll1_sys_clk if pll2_pfd2_396m_clk is sufficient
		 * to provide the frequency.
		 */
		clk_disable_unprepare(pll1_sys_clk);
	}

	/* Ensure the arm clock divider is what we expect */
	ret = clk_set_rate(arm_clk, freqs.new * 1000);
	if (ret) {
		dev_err(cpu_dev, "failed to set clock rate: %d\n", ret);
		regulator_set_voltage_tol(arm_reg, volt_old, 0);
		freqs.new = freqs.old;
		goto post_notify;
	}

	/* scaling down?  scale voltage after frequency */
	if (freqs.new < freqs.old) {
		ret = regulator_set_voltage_tol(arm_reg, volt, 0);
		if (ret) {
			dev_warn(cpu_dev,
				 "failed to scale vddarm down: %d\n", ret);
			ret = 0;
		}

		if (freqs.old == FREQ_1P2_GHZ / 1000) {
			regulator_set_voltage_tol(pu_reg,
					PU_SOC_VOLTAGE_NORMAL, 0);
			regulator_set_voltage_tol(soc_reg,
					PU_SOC_VOLTAGE_NORMAL, 0);
		}
	}

post_notify:
	cpufreq_notify_transition(policy, &freqs, CPUFREQ_POSTCHANGE);

	return ret;
}

static int imx6q_cpufreq_init(struct cpufreq_policy *policy)
{
	int ret;

	ret = cpufreq_frequency_table_cpuinfo(policy, freq_table);
	if (ret) {
		dev_err(cpu_dev, "invalid frequency table: %d\n", ret);
		return ret;
	}

	policy->cpuinfo.transition_latency = transition_latency;
	policy->cur = clk_get_rate(arm_clk) / 1000;
	cpumask_setall(policy->cpus);
	cpufreq_frequency_table_get_attr(freq_table, policy->cpu);

	return 0;
}

static int imx6q_cpufreq_exit(struct cpufreq_policy *policy)
{
	cpufreq_frequency_table_put_attr(policy->cpu);
	return 0;
}

static struct freq_attr *imx6q_cpufreq_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
	NULL,
};

static struct cpufreq_driver imx6q_cpufreq_driver = {
	.verify = imx6q_verify_speed,
	.target = imx6q_set_target,
	.get = imx6q_get_speed,
	.init = imx6q_cpufreq_init,
	.exit = imx6q_cpufreq_exit,
	.name = "imx6q-cpufreq",
	.attr = imx6q_cpufreq_attr,
};

static int imx6q_cpufreq_probe(struct platform_device *pdev)
{
	struct device_node *np;
	struct opp *opp;
	unsigned long min_volt, max_volt;
	int num, ret;

	cpu_dev = &pdev->dev;

	np = of_node_get(cpu_dev->of_node);
	if (!np) {
		dev_err(cpu_dev, "failed to find cpu0 node\n");
		return -ENOENT;
	}

	arm_clk = devm_clk_get(cpu_dev, "arm");
	pll1_sys_clk = devm_clk_get(cpu_dev, "pll1_sys");
	pll1_sw_clk = devm_clk_get(cpu_dev, "pll1_sw");
	step_clk = devm_clk_get(cpu_dev, "step");
	pll2_pfd2_396m_clk = devm_clk_get(cpu_dev, "pll2_pfd2_396m");
	if (IS_ERR(arm_clk) || IS_ERR(pll1_sys_clk) || IS_ERR(pll1_sw_clk) ||
	    IS_ERR(step_clk) || IS_ERR(pll2_pfd2_396m_clk)) {
		dev_err(cpu_dev, "failed to get clocks\n");
		ret = -ENOENT;
		goto put_node;
	}

	arm_reg = devm_regulator_get(cpu_dev, "arm");
	pu_reg = devm_regulator_get(cpu_dev, "pu");
	soc_reg = devm_regulator_get(cpu_dev, "soc");
	if (IS_ERR(arm_reg) || IS_ERR(pu_reg) || IS_ERR(soc_reg)) {
		dev_err(cpu_dev, "failed to get regulators\n");
		ret = -ENOENT;
		goto put_node;
	}

	/* We expect an OPP table supplied by platform */
	num = opp_get_opp_count(cpu_dev);
	if (num < 0) {
		ret = num;
		dev_err(cpu_dev, "no OPP table is found: %d\n", ret);
		goto put_node;
	}

	ret = opp_init_cpufreq_table(cpu_dev, &freq_table);
	if (ret) {
		dev_err(cpu_dev, "failed to init cpufreq table: %d\n", ret);
		goto put_node;
	}

	if (of_property_read_u32(np, "clock-latency", &transition_latency))
		transition_latency = CPUFREQ_ETERNAL;

	/*
	 * OPP is maintained in order of increasing frequency, and
	 * freq_table initialised from OPP is therefore sorted in the
	 * same order.
	 */
	rcu_read_lock();
	opp = opp_find_freq_exact(cpu_dev,
				  freq_table[0].frequency * 1000, true);
	min_volt = opp_get_voltage(opp);
	opp = opp_find_freq_exact(cpu_dev,
				  freq_table[--num].frequency * 1000, true);
	max_volt = opp_get_voltage(opp);
	rcu_read_unlock();
	ret = regulator_set_voltage_time(arm_reg, min_volt, max_volt);
	if (ret > 0)
		transition_latency += ret * 1000;

	/* Count vddpu and vddsoc latency in for 1.2 GHz support */
	if (freq_table[num].frequency == FREQ_1P2_GHZ / 1000) {
		ret = regulator_set_voltage_time(pu_reg, PU_SOC_VOLTAGE_NORMAL,
						 PU_SOC_VOLTAGE_HIGH);
		if (ret > 0)
			transition_latency += ret * 1000;
		ret = regulator_set_voltage_time(soc_reg, PU_SOC_VOLTAGE_NORMAL,
						 PU_SOC_VOLTAGE_HIGH);
		if (ret > 0)
			transition_latency += ret * 1000;
	}

	ret = cpufreq_register_driver(&imx6q_cpufreq_driver);
	if (ret) {
		dev_err(cpu_dev, "failed register driver: %d\n", ret);
		goto free_freq_table;
	}

	of_node_put(np);
	return 0;

free_freq_table:
	opp_free_cpufreq_table(cpu_dev, &freq_table);
put_node:
	of_node_put(np);
	return ret;
}

static int imx6q_cpufreq_remove(struct platform_device *pdev)
{
	cpufreq_unregister_driver(&imx6q_cpufreq_driver);
	opp_free_cpufreq_table(cpu_dev, &freq_table);

	return 0;
}

static struct platform_driver imx6q_cpufreq_platdrv = {
	.driver = {
		.name	= "imx6q-cpufreq",
		.owner	= THIS_MODULE,
	},
	.probe		= imx6q_cpufreq_probe,
	.remove		= imx6q_cpufreq_remove,
};
module_platform_driver(imx6q_cpufreq_platdrv);

MODULE_AUTHOR("Shawn Guo <shawn.guo@linaro.org>");
MODULE_DESCRIPTION("Freescale i.MX6Q cpufreq driver");
MODULE_LICENSE("GPL");
