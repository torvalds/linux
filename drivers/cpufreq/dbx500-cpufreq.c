/*
 * Copyright (C) STMicroelectronics 2009
 * Copyright (C) ST-Ericsson SA 2010-2012
 *
 * License Terms: GNU General Public License v2
 * Author: Sundar Iyer <sundar.iyer@stericsson.com>
 * Author: Martin Persson <martin.persson@stericsson.com>
 * Author: Jonas Aaberg <jonas.aberg@stericsson.com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/cpufreq.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/clk.h>

static struct cpufreq_frequency_table *freq_table;
static struct clk *armss_clk;

static int dbx500_cpufreq_target(struct cpufreq_policy *policy,
				unsigned int index)
{
	/* update armss clk frequency */
	return clk_set_rate(armss_clk, freq_table[index].frequency * 1000);
}

static unsigned int dbx500_cpufreq_getspeed(unsigned int cpu)
{
	int i = 0;
	unsigned long freq = clk_get_rate(armss_clk) / 1000;

	/* The value is rounded to closest frequency in the defined table. */
	while (freq_table[i + 1].frequency != CPUFREQ_TABLE_END) {
		if (freq < freq_table[i].frequency +
		   (freq_table[i + 1].frequency - freq_table[i].frequency) / 2)
			return freq_table[i].frequency;
		i++;
	}

	return freq_table[i].frequency;
}

static int dbx500_cpufreq_init(struct cpufreq_policy *policy)
{
	return cpufreq_generic_init(policy, freq_table, 20 * 1000);
}

static struct cpufreq_driver dbx500_cpufreq_driver = {
	.flags  = CPUFREQ_STICKY | CPUFREQ_CONST_LOOPS,
	.verify = cpufreq_generic_frequency_table_verify,
	.target_index = dbx500_cpufreq_target,
	.get    = dbx500_cpufreq_getspeed,
	.init   = dbx500_cpufreq_init,
	.name   = "DBX500",
	.attr   = cpufreq_generic_attr,
};

static int dbx500_cpufreq_probe(struct platform_device *pdev)
{
	int i = 0;

	freq_table = dev_get_platdata(&pdev->dev);
	if (!freq_table) {
		pr_err("dbx500-cpufreq: Failed to fetch cpufreq table\n");
		return -ENODEV;
	}

	armss_clk = clk_get(&pdev->dev, "armss");
	if (IS_ERR(armss_clk)) {
		pr_err("dbx500-cpufreq: Failed to get armss clk\n");
		return PTR_ERR(armss_clk);
	}

	pr_info("dbx500-cpufreq: Available frequencies:\n");
	while (freq_table[i].frequency != CPUFREQ_TABLE_END) {
		pr_info("  %d Mhz\n", freq_table[i].frequency/1000);
		i++;
	}

	return cpufreq_register_driver(&dbx500_cpufreq_driver);
}

static struct platform_driver dbx500_cpufreq_plat_driver = {
	.driver = {
		.name = "cpufreq-ux500",
		.owner = THIS_MODULE,
	},
	.probe = dbx500_cpufreq_probe,
};

static int __init dbx500_cpufreq_register(void)
{
	return platform_driver_register(&dbx500_cpufreq_plat_driver);
}
device_initcall(dbx500_cpufreq_register);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("cpufreq driver for DBX500");
