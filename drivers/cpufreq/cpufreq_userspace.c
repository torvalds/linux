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

struct userspace_policy {
	unsigned int is_managed;
	unsigned int setspeed;
	struct mutex mutex;
};

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
	struct userspace_policy *userspace = policy->governor_data;

	pr_debug("cpufreq_set for cpu %u, freq %u kHz\n", policy->cpu, freq);

	mutex_lock(&userspace->mutex);
	if (!userspace->is_managed)
		goto err;

	userspace->setspeed = freq;

	ret = __cpufreq_driver_target(policy, freq, CPUFREQ_RELATION_L);
 err:
	mutex_unlock(&userspace->mutex);
	return ret;
}

static ssize_t show_speed(struct cpufreq_policy *policy, char *buf)
{
	return sprintf(buf, "%u\n", policy->cur);
}

static int cpufreq_userspace_policy_init(struct cpufreq_policy *policy)
{
	struct userspace_policy *userspace;

	userspace = kzalloc(sizeof(*userspace), GFP_KERNEL);
	if (!userspace)
		return -ENOMEM;

	mutex_init(&userspace->mutex);

	policy->governor_data = userspace;
	return 0;
}

/*
 * Any routine that writes to the policy struct will hold the "rwsem" of
 * policy struct that means it is free to free "governor_data" here.
 */
static void cpufreq_userspace_policy_exit(struct cpufreq_policy *policy)
{
	kfree(policy->governor_data);
	policy->governor_data = NULL;
}

static int cpufreq_userspace_policy_start(struct cpufreq_policy *policy)
{
	struct userspace_policy *userspace = policy->governor_data;

	BUG_ON(!policy->cur);
	pr_debug("started managing cpu %u\n", policy->cpu);

	mutex_lock(&userspace->mutex);
	userspace->is_managed = 1;
	userspace->setspeed = policy->cur;
	mutex_unlock(&userspace->mutex);
	return 0;
}

static void cpufreq_userspace_policy_stop(struct cpufreq_policy *policy)
{
	struct userspace_policy *userspace = policy->governor_data;

	pr_debug("managing cpu %u stopped\n", policy->cpu);

	mutex_lock(&userspace->mutex);
	userspace->is_managed = 0;
	userspace->setspeed = 0;
	mutex_unlock(&userspace->mutex);
}

static void cpufreq_userspace_policy_limits(struct cpufreq_policy *policy)
{
	struct userspace_policy *userspace = policy->governor_data;

	mutex_lock(&userspace->mutex);

	pr_debug("limit event for cpu %u: %u - %u kHz, currently %u kHz, last set to %u kHz\n",
		 policy->cpu, policy->min, policy->max, policy->cur, userspace->setspeed);

	if (policy->max < userspace->setspeed)
		__cpufreq_driver_target(policy, policy->max,
					CPUFREQ_RELATION_H);
	else if (policy->min > userspace->setspeed)
		__cpufreq_driver_target(policy, policy->min,
					CPUFREQ_RELATION_L);
	else
		__cpufreq_driver_target(policy, userspace->setspeed,
					CPUFREQ_RELATION_L);

	mutex_unlock(&userspace->mutex);
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

MODULE_AUTHOR("Dominik Brodowski <linux@brodo.de>, "
		"Russell King <rmk@arm.linux.org.uk>");
MODULE_DESCRIPTION("CPUfreq policy governor 'userspace'");
MODULE_LICENSE("GPL");

#ifdef CONFIG_CPU_FREQ_DEFAULT_GOV_USERSPACE
struct cpufreq_governor *cpufreq_default_governor(void)
{
	return &cpufreq_gov_userspace;
}
#endif

cpufreq_governor_init(cpufreq_gov_userspace);
cpufreq_governor_exit(cpufreq_gov_userspace);
