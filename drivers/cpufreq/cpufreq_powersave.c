/*
 *  linux/drivers/cpufreq/cpufreq_powersave.c
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
	cpufreq_debug_printk(CPUFREQ_DEBUG_GOVERNOR, "powersave", msg)

static int cpufreq_governor_powersave(struct cpufreq_policy *policy,
					unsigned int event)
{
	switch (event) {
	case CPUFREQ_GOV_START:
	case CPUFREQ_GOV_LIMITS:
		dprintk("setting to %u kHz because of event %u\n",
							policy->min, event);
		__cpufreq_driver_target(policy, policy->min,
						CPUFREQ_RELATION_L);
		break;
	default:
		break;
	}
	return 0;
}

static struct cpufreq_governor cpufreq_gov_powersave = {
	.name		= "powersave",
	.governor	= cpufreq_governor_powersave,
	.owner		= THIS_MODULE,
};


static int __init cpufreq_gov_powersave_init(void)
{
	return cpufreq_register_governor(&cpufreq_gov_powersave);
}


static void __exit cpufreq_gov_powersave_exit(void)
{
	cpufreq_unregister_governor(&cpufreq_gov_powersave);
}


MODULE_AUTHOR("Dominik Brodowski <linux@brodo.de>");
MODULE_DESCRIPTION("CPUfreq policy governor 'powersave'");
MODULE_LICENSE("GPL");

module_init(cpufreq_gov_powersave_init);
module_exit(cpufreq_gov_powersave_exit);
