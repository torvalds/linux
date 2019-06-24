// SPDX-License-Identifier: GPL-2.0-only

/*
 *  linux/drivers/cpufreq/cpufreq_userspace.c
 *
 *  Copyright (C)  2001 Russell King
 *            (C)  2002 - 2004 Dominik Brodowski <linux@brodo.de>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/cpufreq.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>

static DEFINE_PER_CPU(unsigned int, cpu_is_managed);
static DEFINE_MUTEX(userspace_mutex);

/**
 * cpufreq_set - set the CPU frequency
 * @policy: pointer to policy struct where freq is being set
 * @freq: target frequency in kHz
 *
 * Sets the CPU frequency to freq.
 */
static int cpufreq_set(struct cpufreq_policy *policy, unsigned int freq)
{
	int ret = -EINVAL;
	unsigned int *setspeed = policy->governor_data;

	pr_debug("cpufreq_set for cpu %u, freq %u kHz\n", policy->cpu, freq);

	mutex_lock(&userspace_mutex);
	if (!per_cpu(cpu_is_managed, policy->cpu))
		goto err;

	*setspeed = freq;

	ret = __cpufreq_driver_target(policy, freq, CPUFREQ_RELATION_L);
 err:
	mutex_unlock(&userspace_mutex);
	return ret;
}

static ssize_t show_speed(struct cpufreq_policy *policy, char *buf)
{
	return sprintf(buf, "%u\n", policy->cur);
}

static int cpufreq_userspace_policy_init(struct cpufreq_policy *policy)
{
	unsigned int *setspeed;

	setspeed = kzalloc(sizeof(*setspeed), GFP_KERNEL);
	if (!setspeed)
		return -ENOMEM;

	policy->governor_data = setspeed;
	return 0;
}

static void cpufreq_userspace_policy_exit(struct cpufreq_policy *policy)
{
	mutex_lock(&userspace_mutex);
	kfree(policy->governor_data);
	policy->governor_data = NULL;
	mutex_unlock(&userspace_mutex);
}

static int cpufreq_userspace_policy_start(struct cpufreq_policy *policy)
{
	unsigned int *setspeed = policy->governor_data;

	BUG_ON(!policy->cur);
	pr_debug("started managing cpu %u\n", policy->cpu);

	mutex_lock(&userspace_mutex);
	per_cpu(cpu_is_managed, policy->cpu) = 1;
	*setspeed = policy->cur;
	mutex_unlock(&userspace_mutex);
	return 0;
}

static void cpufreq_userspace_policy_stop(struct cpufreq_policy *policy)
{
	unsigned int *setspeed = policy->governor_data;

	pr_debug("managing cpu %u stopped\n", policy->cpu);

	mutex_lock(&userspace_mutex);
	per_cpu(cpu_is_managed, policy->cpu) = 0;
	*setspeed = 0;
	mutex_unlock(&userspace_mutex);
}

static void cpufreq_userspace_policy_limits(struct cpufreq_policy *policy)
{
	unsigned int *setspeed = policy->governor_data;

	mutex_lock(&userspace_mutex);

	pr_debug("limit event for cpu %u: %u - %u kHz, currently %u kHz, last set to %u kHz\n",
		 policy->cpu, policy->min, policy->max, policy->cur, *setspeed);

	if (policy->max < *setspeed)
		__cpufreq_driver_target(policy, policy->max, CPUFREQ_RELATION_H);
	else if (policy->min > *setspeed)
		__cpufreq_driver_target(policy, policy->min, CPUFREQ_RELATION_L);
	else
		__cpufreq_driver_target(policy, *setspeed, CPUFREQ_RELATION_L);

	mutex_unlock(&userspace_mutex);
}

static struct cpufreq_governor cpufreq_gov_userspace = {
	.name		= "userspace",
	.init		= cpufreq_userspace_policy_init,
	.exit		= cpufreq_userspace_policy_exit,
	.start		= cpufreq_userspace_policy_start,
	.stop		= cpufreq_userspace_policy_stop,
	.limits		= cpufreq_userspace_policy_limits,
	.store_setspeed	= cpufreq_set,
	.show_setspeed	= show_speed,
	.owner		= THIS_MODULE,
};

static int __init cpufreq_gov_userspace_init(void)
{
	return cpufreq_register_governor(&cpufreq_gov_userspace);
}

static void __exit cpufreq_gov_userspace_exit(void)
{
	cpufreq_unregister_governor(&cpufreq_gov_userspace);
}

MODULE_AUTHOR("Dominik Brodowski <linux@brodo.de>, "
		"Russell King <rmk@arm.linux.org.uk>");
MODULE_DESCRIPTION("CPUfreq policy governor 'userspace'");
MODULE_LICENSE("GPL");

#ifdef CONFIG_CPU_FREQ_DEFAULT_GOV_USERSPACE
struct cpufreq_governor *cpufreq_default_governor(void)
{
	return &cpufreq_gov_userspace;
}

fs_initcall(cpufreq_gov_userspace_init);
#else
module_init(cpufreq_gov_userspace_init);
#endif
module_exit(cpufreq_gov_userspace_exit);
