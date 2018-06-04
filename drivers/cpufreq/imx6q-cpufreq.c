/*
 * Copyright (C) 2013 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/pm_opp.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>

#define PU_SOC_VOLTAGE_NORMAL	1250000
#define PU_SOC_VOLTAGE_HIGH	1275000
#define FREQ_1P2_GHZ		1200000000

static struct regulator *arm_reg;
static struct regulator *pu_reg;
static struct regulator *soc_reg;

enum IMX6_CPUFREQ_CLKS {
	ARM,
	PLL1_SYS,
	STEP,
	PLL1_SW,
	PLL2_PFD2_396M,
	/* MX6UL requires two more clks */
	PLL2_BUS,
	SECONDARY_SEL,
};
#define IMX6Q_CPUFREQ_CLK_NUM		5
#define IMX6UL_CPUFREQ_CLK_NUM		7

static int num_clks;
static struct clk_bulk_data clks[] = {
	{ .id = "arm" },
	{ .id = "pll1_sys" },
	{ .id = "step" },
	{ .id = "pll1_sw" },
	{ .id = "pll2_pfd2_396m" },
	{ .id = "pll2_bus" },
	{ .id = "secondary_sel" },
};

static struct device *cpu_dev;
static bool free_opp;
static struct cpufreq_frequency_table *freq_table;
static unsigned int max_freq;
static unsigned int transition_latency;

static u32 *imx6_soc_volt;
static u32 soc_opp_count;

static int imx6q_set_target(struct cpufreq_policy *policy, unsigned int index)
{
	struct dev_pm_opp *opp;
	unsigned long freq_hz, volt, volt_old;
	unsigned int old_freq, new_freq;
	bool pll1_sys_temp_enabled = false;
	int ret;

	new_freq = freq_table[index].frequency;
	freq_hz = new_freq * 1000;
	old_freq = clk_get_rate(clks[ARM].clk) / 1000;

	opp = dev_pm_opp_find_freq_ceil(cpu_dev, &freq_hz);
	if (IS_ERR(opp)) {
		dev_err(cpu_dev, "failed to find OPP for %ld\n", freq_hz);
		return PTR_ERR(opp);
	}

	volt = dev_pm_opp_get_voltage(opp);
	dev_pm_opp_put(opp);

	volt_old = regulator_get_voltage(arm_reg);

	dev_dbg(cpu_dev, "%u MHz, %ld mV --> %u MHz, %ld mV\n",
		old_freq / 1000, volt_old / 1000,
		new_freq / 1000, volt / 1000);

	/* scaling up?  scale voltage before frequency */
	if (new_freq > old_freq) {
		if (!IS_ERR(pu_reg)) {
			ret = regulator_set_voltage_tol(pu_reg, imx6_soc_volt[index], 0);
			if (ret) {
				dev_err(cpu_dev, "failed to scale vddpu up: %d\n", ret);
				return ret;
			}
		}
		ret = regulator_set_voltage_tol(soc_reg, imx6_soc_volt[index], 0);
		if (ret) {
			dev_err(cpu_dev, "failed to scale vddsoc up: %d\n", ret);
			return ret;
		}
		ret = regulator_set_voltage_tol(arm_reg, volt, 0);
		if (ret) {
			dev_err(cpu_dev,
				"failed to scale vddarm up: %d\n", ret);
			return ret;
		}
	}

	/*
	 * The setpoints are selected per PLL/PDF frequencies, so we need to
	 * reprogram PLL for frequency scaling.  The procedure of reprogramming
	 * PLL1 is as below.
	 * For i.MX6UL, it has a secondary clk mux, the cpu frequency change
	 * flow is slightly different from other i.MX6 OSC.
	 * The cpu frequeny change flow for i.MX6(except i.MX6UL) is as below:
	 *  - Enable pll2_pfd2_396m_clk and reparent pll1_sw_clk to it
	 *  - Reprogram pll1_sys_clk and reparent pll1_sw_clk back to it
	 *  - Disable pll2_pfd2_396m_clk
	 */
	if (of_machine_is_compatible("fsl,imx6ul") ||
	    of_machine_is_compatible("fsl,imx6ull")) {
		/*
		 * When changing pll1_sw_clk's parent to pll1_sys_clk,
		 * CPU may run at higher than 528MHz, this will lead to
		 * the system unstable if the voltage is lower than the
		 * voltage of 528MHz, so lower the CPU frequency to one
		 * half before changing CPU frequency.
		 */
		clk_set_rate(clks[ARM].clk, (old_freq >> 1) * 1000);
		clk_set_parent(clks[PLL1_SW].clk, clks[PLL1_SYS].clk);
		if (freq_hz > clk_get_rate(clks[PLL2_PFD2_396M].clk))
			clk_set_parent(clks[SECONDARY_SEL].clk,
				       clks[PLL2_BUS].clk);
		else
			clk_set_parent(clks[SECONDARY_SEL].clk,
				       clks[PLL2_PFD2_396M].clk);
		clk_set_parent(clks[STEP].clk, clks[SECONDARY_SEL].clk);
		clk_set_parent(clks[PLL1_SW].clk, clks[STEP].clk);
		if (freq_hz > clk_get_rate(clks[PLL2_BUS].clk)) {
			clk_set_rate(clks[PLL1_SYS].clk, new_freq * 1000);
			clk_set_parent(clks[PLL1_SW].clk, clks[PLL1_SYS].clk);
		}
	} else {
		clk_set_parent(clks[STEP].clk, clks[PLL2_PFD2_396M].clk);
		clk_set_parent(clks[PLL1_SW].clk, clks[STEP].clk);
		if (freq_hz > clk_get_rate(clks[PLL2_PFD2_396M].clk)) {
			clk_set_rate(clks[PLL1_SYS].clk, new_freq * 1000);
			clk_set_parent(clks[PLL1_SW].clk, clks[PLL1_SYS].clk);
		} else {
			/* pll1_sys needs to be enabled for divider rate change to work. */
			pll1_sys_temp_enabled = true;
			clk_prepare_enable(clks[PLL1_SYS].clk);
		}
	}

	/* Ensure the arm clock divider is what we expect */
	ret = clk_set_rate(clks[ARM].clk, new_freq * 1000);
	if (ret) {
		dev_err(cpu_dev, "failed to set clock rate: %d\n", ret);
		regulator_set_voltage_tol(arm_reg, volt_old, 0);
		return ret;
	}

	/* PLL1 is only needed until after ARM-PODF is set. */
	if (pll1_sys_temp_enabled)
		clk_disable_unprepare(clks[PLL1_SYS].clk);

	/* scaling down?  scale voltage after frequency */
	if (new_freq < old_freq) {
		ret = regulator_set_voltage_tol(arm_reg, volt, 0);
		if (ret) {
			dev_warn(cpu_dev,
				 "failed to scale vddarm down: %d\n", ret);
			ret = 0;
		}
		ret = regulator_set_voltage_tol(soc_reg, imx6_soc_volt[index], 0);
		if (ret) {
			dev_warn(cpu_dev, "failed to scale vddsoc down: %d\n", ret);
			ret = 0;
		}
		if (!IS_ERR(pu_reg)) {
			ret = regulator_set_voltage_tol(pu_reg, imx6_soc_volt[index], 0);
			if (ret) {
				dev_warn(cpu_dev, "failed to scale vddpu down: %d\n", ret);
				ret = 0;
			}
		}
	}

	return 0;
}

static int imx6q_cpufreq_init(struct cpufreq_policy *policy)
{
	int ret;

	policy->clk = clks[ARM].clk;
	ret = cpufreq_generic_init(policy, freq_table, transition_latency);
	policy->suspend_freq = max_freq;

	return ret;
}

static struct cpufreq_driver imx6q_cpufreq_driver = {
	.flags = CPUFREQ_NEED_INITIAL_FREQ_CHECK,
	.verify = cpufreq_generic_frequency_table_verify,
	.target_index = imx6q_set_target,
	.get = cpufreq_generic_get,
	.init = imx6q_cpufreq_init,
	.name = "imx6q-cpufreq",
	.attr = cpufreq_generic_attr,
	.suspend = cpufreq_generic_suspend,
};

#define OCOTP_CFG3			0x440
#define OCOTP_CFG3_SPEED_SHIFT		16
#define OCOTP_CFG3_SPEED_1P2GHZ		0x3
#define OCOTP_CFG3_SPEED_996MHZ		0x2
#define OCOTP_CFG3_SPEED_852MHZ		0x1

static void imx6q_opp_check_speed_grading(struct device *dev)
{
	struct device_node *np;
	void __iomem *base;
	u32 val;

	np = of_find_compatible_node(NULL, NULL, "fsl,imx6q-ocotp");
	if (!np)
		return;

	base = of_iomap(np, 0);
	if (!base) {
		dev_err(dev, "failed to map ocotp\n");
		goto put_node;
	}

	/*
	 * SPEED_GRADING[1:0] defines the max speed of ARM:
	 * 2b'11: 1200000000Hz;
	 * 2b'10: 996000000Hz;
	 * 2b'01: 852000000Hz; -- i.MX6Q Only, exclusive with 996MHz.
	 * 2b'00: 792000000Hz;
	 * We need to set the max speed of ARM according to fuse map.
	 */
	val = readl_relaxed(base + OCOTP_CFG3);
	val >>= OCOTP_CFG3_SPEED_SHIFT;
	val &= 0x3;

	if (val < OCOTP_CFG3_SPEED_996MHZ)
		if (dev_pm_opp_disable(dev, 996000000))
			dev_warn(dev, "failed to disable 996MHz OPP\n");

	if (of_machine_is_compatible("fsl,imx6q") ||
	    of_machine_is_compatible("fsl,imx6qp")) {
		if (val != OCOTP_CFG3_SPEED_852MHZ)
			if (dev_pm_opp_disable(dev, 852000000))
				dev_warn(dev, "failed to disable 852MHz OPP\n");
		if (val != OCOTP_CFG3_SPEED_1P2GHZ)
			if (dev_pm_opp_disable(dev, 1200000000))
				dev_warn(dev, "failed to disable 1.2GHz OPP\n");
	}
	iounmap(base);
put_node:
	of_node_put(np);
}

#define OCOTP_CFG3_6UL_SPEED_696MHZ	0x2

static void imx6ul_opp_check_speed_grading(struct device *dev)
{
	struct device_node *np;
	void __iomem *base;
	u32 val;

	np = of_find_compatible_node(NULL, NULL, "fsl,imx6ul-ocotp");
	if (!np)
		return;

	base = of_iomap(np, 0);
	if (!base) {
		dev_err(dev, "failed to map ocotp\n");
		goto put_node;
	}

	/*
	 * Speed GRADING[1:0] defines the max speed of ARM:
	 * 2b'00: Reserved;
	 * 2b'01: 528000000Hz;
	 * 2b'10: 696000000Hz;
	 * 2b'11: Reserved;
	 * We need to set the max speed of ARM according to fuse map.
	 */
	val = readl_relaxed(base + OCOTP_CFG3);
	val >>= OCOTP_CFG3_SPEED_SHIFT;
	val &= 0x3;
	if (val != OCOTP_CFG3_6UL_SPEED_696MHZ)
		if (dev_pm_opp_disable(dev, 696000000))
			dev_warn(dev, "failed to disable 696MHz OPP\n");
	iounmap(base);
put_node:
	of_node_put(np);
}

static int imx6q_cpufreq_probe(struct platform_device *pdev)
{
	struct device_node *np;
	struct dev_pm_opp *opp;
	unsigned long min_volt, max_volt;
	int num, ret;
	const struct property *prop;
	const __be32 *val;
	u32 nr, i, j;

	cpu_dev = get_cpu_device(0);
	if (!cpu_dev) {
		pr_err("failed to get cpu0 device\n");
		return -ENODEV;
	}

	np = of_node_get(cpu_dev->of_node);
	if (!np) {
		dev_err(cpu_dev, "failed to find cpu0 node\n");
		return -ENOENT;
	}

	if (of_machine_is_compatible("fsl,imx6ul") ||
	    of_machine_is_compatible("fsl,imx6ull"))
		num_clks = IMX6UL_CPUFREQ_CLK_NUM;
	else
		num_clks = IMX6Q_CPUFREQ_CLK_NUM;

	ret = clk_bulk_get(cpu_dev, num_clks, clks);
	if (ret)
		goto put_node;

	arm_reg = regulator_get(cpu_dev, "arm");
	pu_reg = regulator_get_optional(cpu_dev, "pu");
	soc_reg = regulator_get(cpu_dev, "soc");
	if (PTR_ERR(arm_reg) == -EPROBE_DEFER ||
			PTR_ERR(soc_reg) == -EPROBE_DEFER ||
			PTR_ERR(pu_reg) == -EPROBE_DEFER) {
		ret = -EPROBE_DEFER;
		dev_dbg(cpu_dev, "regulators not ready, defer\n");
		goto put_reg;
	}
	if (IS_ERR(arm_reg) || IS_ERR(soc_reg)) {
		dev_err(cpu_dev, "failed to get regulators\n");
		ret = -ENOENT;
		goto put_reg;
	}

	ret = dev_pm_opp_of_add_table(cpu_dev);
	if (ret < 0) {
		dev_err(cpu_dev, "failed to init OPP table: %d\n", ret);
		goto put_reg;
	}

	if (of_machine_is_compatible("fsl,imx6ul"))
		imx6ul_opp_check_speed_grading(cpu_dev);
	else
		imx6q_opp_check_speed_grading(cpu_dev);

	/* Because we have added the OPPs here, we must free them */
	free_opp = true;
	num = dev_pm_opp_get_opp_count(cpu_dev);
	if (num < 0) {
		ret = num;
		dev_err(cpu_dev, "no OPP table is found: %d\n", ret);
		goto out_free_opp;
	}

	ret = dev_pm_opp_init_cpufreq_table(cpu_dev, &freq_table);
	if (ret) {
		dev_err(cpu_dev, "failed to init cpufreq table: %d\n", ret);
		goto out_free_opp;
	}

	/* Make imx6_soc_volt array's size same as arm opp number */
	imx6_soc_volt = devm_kzalloc(cpu_dev, sizeof(*imx6_soc_volt) * num, GFP_KERNEL);
	if (imx6_soc_volt == NULL) {
		ret = -ENOMEM;
		goto free_freq_table;
	}

	prop = of_find_property(np, "fsl,soc-operating-points", NULL);
	if (!prop || !prop->value)
		goto soc_opp_out;

	/*
	 * Each OPP is a set of tuples consisting of frequency and
	 * voltage like <freq-kHz vol-uV>.
	 */
	nr = prop->length / sizeof(u32);
	if (nr % 2 || (nr / 2) < num)
		goto soc_opp_out;

	for (j = 0; j < num; j++) {
		val = prop->value;
		for (i = 0; i < nr / 2; i++) {
			unsigned long freq = be32_to_cpup(val++);
			unsigned long volt = be32_to_cpup(val++);
			if (freq_table[j].frequency == freq) {
				imx6_soc_volt[soc_opp_count++] = volt;
				break;
			}
		}
	}

soc_opp_out:
	/* use fixed soc opp volt if no valid soc opp info found in dtb */
	if (soc_opp_count != num) {
		dev_warn(cpu_dev, "can NOT find valid fsl,soc-operating-points property in dtb, use default value!\n");
		for (j = 0; j < num; j++)
			imx6_soc_volt[j] = PU_SOC_VOLTAGE_NORMAL;
		if (freq_table[num - 1].frequency * 1000 == FREQ_1P2_GHZ)
			imx6_soc_volt[num - 1] = PU_SOC_VOLTAGE_HIGH;
	}

	if (of_property_read_u32(np, "clock-latency", &transition_latency))
		transition_latency = CPUFREQ_ETERNAL;

	/*
	 * Calculate the ramp time for max voltage change in the
	 * VDDSOC and VDDPU regulators.
	 */
	ret = regulator_set_voltage_time(soc_reg, imx6_soc_volt[0], imx6_soc_volt[num - 1]);
	if (ret > 0)
		transition_latency += ret * 1000;
	if (!IS_ERR(pu_reg)) {
		ret = regulator_set_voltage_time(pu_reg, imx6_soc_volt[0], imx6_soc_volt[num - 1]);
		if (ret > 0)
			transition_latency += ret * 1000;
	}

	/*
	 * OPP is maintained in order of increasing frequency, and
	 * freq_table initialised from OPP is therefore sorted in the
	 * same order.
	 */
	max_freq = freq_table[--num].frequency;
	opp = dev_pm_opp_find_freq_exact(cpu_dev,
				  freq_table[0].frequency * 1000, true);
	min_volt = dev_pm_opp_get_voltage(opp);
	dev_pm_opp_put(opp);
	opp = dev_pm_opp_find_freq_exact(cpu_dev, max_freq * 1000, true);
	max_volt = dev_pm_opp_get_voltage(opp);
	dev_pm_opp_put(opp);

	ret = regulator_set_voltage_time(arm_reg, min_volt, max_volt);
	if (ret > 0)
		transition_latency += ret * 1000;

	ret = cpufreq_register_driver(&imx6q_cpufreq_driver);
	if (ret) {
		dev_err(cpu_dev, "failed register driver: %d\n", ret);
		goto free_freq_table;
	}

	of_node_put(np);
	return 0;

free_freq_table:
	dev_pm_opp_free_cpufreq_table(cpu_dev, &freq_table);
out_free_opp:
	if (free_opp)
		dev_pm_opp_of_remove_table(cpu_dev);
put_reg:
	if (!IS_ERR(arm_reg))
		regulator_put(arm_reg);
	if (!IS_ERR(pu_reg))
		regulator_put(pu_reg);
	if (!IS_ERR(soc_reg))
		regulator_put(soc_reg);

	clk_bulk_put(num_clks, clks);
put_node:
	of_node_put(np);

	return ret;
}

static int imx6q_cpufreq_remove(struct platform_device *pdev)
{
	cpufreq_unregister_driver(&imx6q_cpufreq_driver);
	dev_pm_opp_free_cpufreq_table(cpu_dev, &freq_table);
	if (free_opp)
		dev_pm_opp_of_remove_table(cpu_dev);
	regulator_put(arm_reg);
	if (!IS_ERR(pu_reg))
		regulator_put(pu_reg);
	regulator_put(soc_reg);

	clk_bulk_put(num_clks, clks);

	return 0;
}

static struct platform_driver imx6q_cpufreq_platdrv = {
	.driver = {
		.name	= "imx6q-cpufreq",
	},
	.probe		= imx6q_cpufreq_probe,
	.remove		= imx6q_cpufreq_remove,
};
module_platform_driver(imx6q_cpufreq_platdrv);

MODULE_ALIAS("platform:imx6q-cpufreq");
MODULE_AUTHOR("Shawn Guo <shawn.guo@linaro.org>");
MODULE_DESCRIPTION("Freescale i.MX6Q cpufreq driver");
MODULE_LICENSE("GPL");
