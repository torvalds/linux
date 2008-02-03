
/*
 *  linux/drivers/cpufreq/cpufreq_userspace.c
 *
 *  Copyright (C)  2001 Russell King
 *            (C)  2002 - 2004 Dominik Brodowski <linux@brodo.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/smp.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/cpufreq.h>
#include <linux/cpu.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/sysfs.h>
#include <linux/mutex.h>

#include <asm/uaccess.h>


/**
 * A few values needed by the userspace governor
 */
static unsigned int	cpu_max_freq[NR_CPUS];
static unsigned int	cpu_min_freq[NR_CPUS];
static unsigned int	cpu_cur_freq[NR_CPUS]; /* current CPU freq */
static unsigned int	cpu_set_freq[NR_CPUS]; /* CPU freq desired by userspace */
static unsigned int	cpu_is_managed[NR_CPUS];

static DEFINE_MUTEX	(userspace_mutex);
static int cpus_using_userspace_governor;

#define dprintk(msg...) cpufreq_debug_printk(CPUFREQ_DEBUG_GOVERNOR, "userspace", msg)

/* keep track of frequency transitions */
static int
userspace_cpufreq_notifier(struct notifier_block *nb, unsigned long val,
                       void *data)
{
        struct cpufreq_freqs *freq = data;

	if (!cpu_is_managed[freq->cpu])
		return 0;

	dprintk("saving cpu_cur_freq of cpu %u to be %u kHz\n",
			freq->cpu, freq->new);
	cpu_cur_freq[freq->cpu] = freq->new;

        return 0;
}

static struct notifier_block userspace_cpufreq_notifier_block = {
        .notifier_call  = userspace_cpufreq_notifier
};


/**
 * cpufreq_set - set the CPU frequency
 * @freq: target frequency in kHz
 * @cpu: CPU for which the frequency is to be set
 *
 * Sets the CPU frequency to freq.
 */
static int cpufreq_set(unsigned int freq, struct cpufreq_policy *policy)
{
	int ret = -EINVAL;

	dprintk("cpufreq_set for cpu %u, freq %u kHz\n", policy->cpu, freq);

	mutex_lock(&userspace_mutex);
	if (!cpu_is_managed[policy->cpu])
		goto err;

	cpu_set_freq[policy->cpu] = freq;

	if (freq < cpu_min_freq[policy->cpu])
		freq = cpu_min_freq[policy->cpu];
	if (freq > cpu_max_freq[policy->cpu])
		freq = cpu_max_freq[policy->cpu];

	/*
	 * We're safe from concurrent calls to ->target() here
	 * as we hold the userspace_mutex lock. If we were calling
	 * cpufreq_driver_target, a deadlock situation might occur:
	 * A: cpufreq_set (lock userspace_mutex) -> cpufreq_driver_target(lock policy->lock)
	 * B: cpufreq_set_policy(lock policy->lock) -> __cpufreq_governor -> cpufreq_governor_userspace (lock userspace_mutex)
	 */
	ret = __cpufreq_driver_target(policy, freq, CPUFREQ_RELATION_L);

 err:
	mutex_unlock(&userspace_mutex);
	return ret;
}


/************************** sysfs interface ************************/
static ssize_t show_speed (struct cpufreq_policy *policy, char *buf)
{
	return sprintf (buf, "%u\n", cpu_cur_freq[policy->cpu]);
}

static ssize_t
store_speed (struct cpufreq_policy *policy, const char *buf, size_t count)
{
	unsigned int freq = 0;
	unsigned int ret;

	ret = sscanf (buf, "%u", &freq);
	if (ret != 1)
		return -EINVAL;

	cpufreq_set(freq, policy);

	return count;
}

static struct freq_attr freq_attr_scaling_setspeed =
{
	.attr = { .name = "scaling_setspeed", .mode = 0644 },
	.show = show_speed,
	.store = store_speed,
};

static int cpufreq_governor_userspace(struct cpufreq_policy *policy,
				   unsigned int event)
{
	unsigned int cpu = policy->cpu;
	int rc = 0;

	switch (event) {
	case CPUFREQ_GOV_START:
		if (!cpu_online(cpu))
			return -EINVAL;
		BUG_ON(!policy->cur);
		mutex_lock(&userspace_mutex);
		rc = sysfs_create_file (&policy->kobj,
					&freq_attr_scaling_setspeed.attr);
		if (rc)
			goto start_out;

		if (cpus_using_userspace_governor == 0) {
			cpufreq_register_notifier(
					&userspace_cpufreq_notifier_block,
					CPUFREQ_TRANSITION_NOTIFIER);
		}
		cpus_using_userspace_governor++;

		cpu_is_managed[cpu] = 1;
		cpu_min_freq[cpu] = policy->min;
		cpu_max_freq[cpu] = policy->max;
		cpu_cur_freq[cpu] = policy->cur;
		cpu_set_freq[cpu] = policy->cur;
		dprintk("managing cpu %u started (%u - %u kHz, currently %u kHz)\n", cpu, cpu_min_freq[cpu], cpu_max_freq[cpu], cpu_cur_freq[cpu]);
start_out:
		mutex_unlock(&userspace_mutex);
		break;
	case CPUFREQ_GOV_STOP:
		mutex_lock(&userspace_mutex);
		cpus_using_userspace_governor--;
		if (cpus_using_userspace_governor == 0) {
			cpufreq_unregister_notifier(
					&userspace_cpufreq_notifier_block,
					CPUFREQ_TRANSITION_NOTIFIER);
		}

		cpu_is_managed[cpu] = 0;
		cpu_min_freq[cpu] = 0;
		cpu_max_freq[cpu] = 0;
		cpu_set_freq[cpu] = 0;
		sysfs_remove_file (&policy->kobj, &freq_attr_scaling_setspeed.attr);
		dprintk("managing cpu %u stopped\n", cpu);
		mutex_unlock(&userspace_mutex);
		break;
	case CPUFREQ_GOV_LIMITS:
		mutex_lock(&userspace_mutex);
		dprintk("limit event for cpu %u: %u - %u kHz,"
			"currently %u kHz, last set to %u kHz\n",
			cpu, policy->min, policy->max,
			cpu_cur_freq[cpu], cpu_set_freq[cpu]);
		if (policy->max < cpu_set_freq[cpu]) {
			__cpufreq_driver_target(policy, policy->max,
						CPUFREQ_RELATION_H);
		}
		else if (policy->min > cpu_set_freq[cpu]) {
			__cpufreq_driver_target(policy, policy->min,
						CPUFREQ_RELATION_L);
		}
		else {
			__cpufreq_driver_target(policy, cpu_set_freq[cpu],
						CPUFREQ_RELATION_L);
		}
		cpu_min_freq[cpu] = policy->min;
		cpu_max_freq[cpu] = policy->max;
		cpu_cur_freq[cpu] = policy->cur;
		mutex_unlock(&userspace_mutex);
		break;
	}
	return rc;
}


struct cpufreq_governor cpufreq_gov_userspace = {
	.name		= "userspace",
	.governor	= cpufreq_governor_userspace,
	.owner		= THIS_MODULE,
};
EXPORT_SYMBOL(cpufreq_gov_userspace);

static int __init cpufreq_gov_userspace_init(void)
{
	return cpufreq_register_governor(&cpufreq_gov_userspace);
}


static void __exit cpufreq_gov_userspace_exit(void)
{
	cpufreq_unregister_governor(&cpufreq_gov_userspace);
}


MODULE_AUTHOR ("Dominik Brodowski <linux@brodo.de>, Russell King <rmk@arm.linux.org.uk>");
MODULE_DESCRIPTION ("CPUfreq policy governor 'userspace'");
MODULE_LICENSE ("GPL");

#ifdef CONFIG_CPU_FREQ_DEFAULT_GOV_USERSPACE
fs_initcall(cpufreq_gov_userspace_init);
#else
module_init(cpufreq_gov_userspace_init);
#endif
module_exit(cpufreq_gov_userspace_exit);
