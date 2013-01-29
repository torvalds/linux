/*
 * Copyright (C) STMicroelectronics 2009
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License Terms: GNU General Public License v2
 * Author: Sundar Iyer <sundar.iyer@stericsson.com>
 * Author: Martin Persson <martin.persson@stericsson.com>
 * Author: Jonas Aaberg <jonas.aberg@stericsson.com>
 *
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/cpufreq.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <mach/id.h>

static struct cpufreq_frequency_table *freq_table;
static struct clk *armss_clk;

static struct freq_attr *db8500_cpufreq_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
	NULL,
};

static int db8500_cpufreq_verify_speed(struct cpufreq_policy *policy)
{
	return cpufreq_frequency_table_verify(policy, freq_table);
}

static int db8500_cpufreq_target(struct cpufreq_policy *policy,
				unsigned int target_freq,
				unsigned int relation)
{
	struct cpufreq_freqs freqs;
	unsigned int idx;

	/* scale the target frequency to one of the extremes supported */
	if (target_freq < policy->cpuinfo.min_freq)
		target_freq = policy->cpuinfo.min_freq;
	if (target_freq > policy->cpuinfo.max_freq)
		target_freq = policy->cpuinfo.max_freq;

	/* Lookup the next frequency */
	if (cpufreq_frequency_table_target
	    (policy, freq_table, target_freq, relation, &idx)) {
		return -EINVAL;
	}

	freqs.old = policy->cur;
	freqs.new = freq_table[idx].frequency;

	if (freqs.old == freqs.new)
		return 0;

	/* pre-change notification */
	for_each_cpu(freqs.cpu, policy->cpus)
		cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);

	/* update armss clk frequency */
	if (clk_set_rate(armss_clk, freq_table[idx].frequency * 1000)) {
		pr_err("db8500-cpufreq: Failed to update armss clk\n");
		return -EINVAL;
	}

	/* post change notification */
	for_each_cpu(freqs.cpu, policy->cpus)
		cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);

	return 0;
}

static unsigned int db8500_cpufreq_getspeed(unsigned int cpu)
{
	int i = 0;
	unsigned long freq = clk_get_rate(armss_clk) / 1000;

	while (freq_table[i].frequency != CPUFREQ_TABLE_END) {
		if (freq <= freq_table[i].frequency)
			return freq_table[i].frequency;
		i++;
	}

	/* We could not find a corresponding frequency. */
	pr_err("db8500-cpufreq: Failed to find cpufreq speed\n");
	return 0;
}

static int __cpuinit db8500_cpufreq_init(struct cpufreq_policy *policy)
{
	int i = 0;
	int res;

	armss_clk = clk_get(NULL, "armss");
	if (IS_ERR(armss_clk)) {
		pr_err("db8500-cpufreq : Failed to get armss clk\n");
		return PTR_ERR(armss_clk);
	}

	pr_info("db8500-cpufreq : Available frequencies:\n");
	while (freq_table[i].frequency != CPUFREQ_TABLE_END) {
		pr_info("  %d Mhz\n", freq_table[i].frequency/1000);
		i++;
	}

	/* get policy fields based on the table */
	res = cpufreq_frequency_table_cpuinfo(policy, freq_table);
	if (!res)
		cpufreq_frequency_table_get_attr(freq_table, policy->cpu);
	else {
		pr_err("db8500-cpufreq : Failed to read policy table\n");
		clk_put(armss_clk);
		return res;
	}

	policy->min = policy->cpuinfo.min_freq;
	policy->max = policy->cpuinfo.max_freq;
	policy->cur = db8500_cpufreq_getspeed(policy->cpu);
	policy->governor = CPUFREQ_DEFAULT_GOVERNOR;

	/*
	 * FIXME : Need to take time measurement across the target()
	 *	   function with no/some/all drivers in the notification
	 *	   list.
	 */
	policy->cpuinfo.transition_latency = 20 * 1000; /* in ns */

	/* policy sharing between dual CPUs */
	cpumask_copy(policy->cpus, cpu_present_mask);

	policy->shared_type = CPUFREQ_SHARED_TYPE_ALL;

	return 0;
}

static struct cpufreq_driver db8500_cpufreq_driver = {
	.flags  = CPUFREQ_STICKY,
	.verify = db8500_cpufreq_verify_speed,
	.target = db8500_cpufreq_target,
	.get    = db8500_cpufreq_getspeed,
	.init   = db8500_cpufreq_init,
	.name   = "DB8500",
	.attr   = db8500_cpufreq_attr,
};

static int db8500_cpufreq_probe(struct platform_device *pdev)
{
	freq_table = dev_get_platdata(&pdev->dev);

	if (!freq_table) {
		pr_err("db8500-cpufreq: Failed to fetch cpufreq table\n");
		return -ENODEV;
	}

	return cpufreq_register_driver(&db8500_cpufreq_driver);
}

static struct platform_driver db8500_cpufreq_plat_driver = {
	.driver = {
		.name = "cpufreq-u8500",
		.owner = THIS_MODULE,
	},
	.probe = db8500_cpufreq_probe,
};

static int __init db8500_cpufreq_register(void)
{
	if (!cpu_is_u8500_family())
		return -ENODEV;

	pr_info("cpufreq for DB8500 started\n");
	return platform_driver_register(&db8500_cpufreq_plat_driver);
}
device_initcall(db8500_cpufreq_register);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("cpufreq driver for DB8500");
