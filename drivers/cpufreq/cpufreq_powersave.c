// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/drivers/cpufreq/cpufreq_powersave.c
 *
 * Copyright (C) 2002 - 2003 Dominik Brodowski <linux@brodo.de>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/cpufreq.h>
#include <linux/init.h>
#include <linux/module.h>

static void cpufreq_gov_powersave_limits(struct cpufreq_policy *policy)
{
	pr_debug("setting to %u kHz\n", policy->min);
	__cpufreq_driver_target(policy, policy->min, CPUFREQ_RELATION_L);
}

static struct cpufreq_governor cpufreq_gov_powersave = {
	.name		= "powersave",
	.limits		= cpufreq_gov_powersave_limits,
	.owner		= THIS_MODULE,
	.flags		= CPUFREQ_GOV_STRICT_TARGET,
};

MODULE_AUTHOR("Dominik Brodowski <linux@brodo.de>");
MODULE_DESCRIPTION("CPUfreq policy governor 'powersave'");
MODULE_LICENSE("GPL");

#ifdef CONFIG_CPU_FREQ_DEFAULT_GOV_POWERSAVE
struct cpufreq_governor *cpufreq_default_governor(void)
{
	return &cpufreq_gov_powersave;
}
#endif

cpufreq_governor_init(cpufreq_gov_powersave);
cpufreq_governor_exit(cpufreq_gov_powersave);
