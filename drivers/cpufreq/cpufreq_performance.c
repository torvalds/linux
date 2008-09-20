/*
 *  linux/drivers/cpufreq/cpufreq_performance.c
 *
 *  Copyright (C) 2002 - 2003 Dominik Brodowski <linux@brodo.de>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/cpufreq.h>
#include <linux/init.h>

#define dprintk(msg...) \
	cpufreq_debug_printk(CPUFREQ_DEBUG_GOVERNOR, "performance", msg)


static int cpufreq_governor_performance(struct cpufreq_policy *policy,
					unsigned int event)
{
	switch (event) {
	case CPUFREQ_GOV_START:
	case CPUFREQ_GOV_LIMITS:
		dprintk("setting to %u kHz because of event %u\n",
						policy->max, event);
		__cpufreq_driver_target(policy, policy->max,
						CPUFREQ_RELATION_H);
		break;
	default:
		break;
	}
	return 0;
}

#ifdef CONFIG_CPU_FREQ_GOV_PERFORMANCE_MODULE
static
#endif
struct cpufreq_governor cpufreq_gov_performance = {
	.name		= "performance",
	.governor	= cpufreq_governor_performance,
	.owner		= THIS_MODULE,
};


static int __init cpufreq_gov_performance_init(void)
{
	return cpufreq_register_governor(&cpufreq_gov_performance);
}


static void __exit cpufreq_gov_performance_exit(void)
{
	cpufreq_unregister_governor(&cpufreq_gov_performance);
}


MODULE_AUTHOR("Dominik Brodowski <linux@brodo.de>");
MODULE_DESCRIPTION("CPUfreq policy governor 'performance'");
MODULE_LICENSE("GPL");

fs_initcall(cpufreq_gov_performance_init);
module_exit(cpufreq_gov_performance_exit);
