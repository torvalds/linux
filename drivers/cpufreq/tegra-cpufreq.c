/*
 * Copyright (C) 2010 Google, Inc.
 *
 * Author:
 *	Colin Cross <ccross@google.com>
 *	Based on arch/arm/plat-omap/cpu-omap.c, (C) 2005 Nokia Corporation
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/cpufreq.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>

static struct cpufreq_frequency_table freq_table[] = {
	{ .frequency = 216000 },
	{ .frequency = 312000 },
	{ .frequency = 456000 },
	{ .frequency = 608000 },
	{ .frequency = 760000 },
	{ .frequency = 816000 },
	{ .frequency = 912000 },
	{ .frequency = 1000000 },
	{ .frequency = CPUFREQ_TABLE_END },
};

#define NUM_CPUS	2

static struct clk *cpu_clk;
static struct clk *pll_x_clk;
static struct clk *pll_p_clk;
static struct clk *emc_clk;

static int tegra_cpu_clk_set_rate(unsigned long rate)
{
	int ret;

	/*
	 * Take an extra reference to the main pll so it doesn't turn
	 * off when we move the cpu off of it
	 */
	clk_prepare_enable(pll_x_clk);

	ret = clk_set_parent(cpu_clk, pll_p_clk);
	if (ret) {
		pr_err("Failed to switch cpu to clock pll_p\n");
		goto out;
	}

	if (rate == clk_get_rate(pll_p_clk))
		goto out;

	ret = clk_set_rate(pll_x_clk, rate);
	if (ret) {
		pr_err("Failed to change pll_x to %lu\n", rate);
		goto out;
	}

	ret = clk_set_parent(cpu_clk, pll_x_clk);
	if (ret) {
		pr_err("Failed to switch cpu to clock pll_x\n");
		goto out;
	}

out:
	clk_disable_unprepare(pll_x_clk);
	return ret;
}

static int tegra_target(struct cpufreq_policy *policy, unsigned int index)
{
	unsigned long rate = freq_table[index].frequency;
	int ret = 0;

	/*
	 * Vote on memory bus frequency based on cpu frequency
	 * This sets the minimum frequency, display or avp may request higher
	 */
	if (rate >= 816000)
		clk_set_rate(emc_clk, 600000000); /* cpu 816 MHz, emc max */
	else if (rate >= 456000)
		clk_set_rate(emc_clk, 300000000); /* cpu 456 MHz, emc 150Mhz */
	else
		clk_set_rate(emc_clk, 100000000);  /* emc 50Mhz */

	ret = tegra_cpu_clk_set_rate(rate * 1000);
	if (ret)
		pr_err("cpu-tegra: Failed to set cpu frequency to %lu kHz\n",
			rate);

	return ret;
}

static int tegra_cpu_init(struct cpufreq_policy *policy)
{
	int ret;

	if (policy->cpu >= NUM_CPUS)
		return -EINVAL;

	clk_prepare_enable(emc_clk);
	clk_prepare_enable(cpu_clk);

	/* FIXME: what's the actual transition time? */
	ret = cpufreq_generic_init(policy, freq_table, 300 * 1000);
	if (ret) {
		clk_disable_unprepare(cpu_clk);
		clk_disable_unprepare(emc_clk);
		return ret;
	}

	policy->clk = cpu_clk;
	policy->suspend_freq = freq_table[0].frequency;
	return 0;
}

static int tegra_cpu_exit(struct cpufreq_policy *policy)
{
	clk_disable_unprepare(cpu_clk);
	clk_disable_unprepare(emc_clk);
	return 0;
}

static struct cpufreq_driver tegra_cpufreq_driver = {
	.flags		= CPUFREQ_NEED_INITIAL_FREQ_CHECK,
	.verify		= cpufreq_generic_frequency_table_verify,
	.target_index	= tegra_target,
	.get		= cpufreq_generic_get,
	.init		= tegra_cpu_init,
	.exit		= tegra_cpu_exit,
	.name		= "tegra",
	.attr		= cpufreq_generic_attr,
#ifdef CONFIG_PM
	.suspend	= cpufreq_generic_suspend,
#endif
};

static int __init tegra_cpufreq_init(void)
{
	cpu_clk = clk_get_sys(NULL, "cclk");
	if (IS_ERR(cpu_clk))
		return PTR_ERR(cpu_clk);

	pll_x_clk = clk_get_sys(NULL, "pll_x");
	if (IS_ERR(pll_x_clk))
		return PTR_ERR(pll_x_clk);

	pll_p_clk = clk_get_sys(NULL, "pll_p");
	if (IS_ERR(pll_p_clk))
		return PTR_ERR(pll_p_clk);

	emc_clk = clk_get_sys("cpu", "emc");
	if (IS_ERR(emc_clk)) {
		clk_put(cpu_clk);
		return PTR_ERR(emc_clk);
	}

	return cpufreq_register_driver(&tegra_cpufreq_driver);
}

static void __exit tegra_cpufreq_exit(void)
{
        cpufreq_unregister_driver(&tegra_cpufreq_driver);
	clk_put(emc_clk);
	clk_put(cpu_clk);
}


MODULE_AUTHOR("Colin Cross <ccross@android.com>");
MODULE_DESCRIPTION("cpufreq driver for Nvidia Tegra2");
MODULE_LICENSE("GPL");
module_init(tegra_cpufreq_init);
module_exit(tegra_cpufreq_exit);
