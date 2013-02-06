/*
 *  linux/drivers/thermal/cpu_cooling.c
 *
 *  Copyright (C) 2011	Samsung Electronics Co., Ltd(http://www.samsung.com)
 *  Copyright (C) 2011  Amit Daniel <amit.kachhap at linaro.org>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/thermal.h>
#include <linux/platform_device.h>
#include <linux/cpufreq.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/cpu.h>
#include <linux/cpu_cooling.h>

#ifdef CONFIG_CPU_FREQ
struct cpufreq_cooling_device {
	struct thermal_cooling_device *cool_dev;
	struct freq_pctg_table *tab_ptr;
	unsigned int tab_size;
	unsigned int cpufreq_state;
	const struct cpumask *allowed_cpus;
};

static struct cpufreq_cooling_device *cpufreq_device;

/*Below codes defines functions to be used for cpufreq as cooling device*/
static bool is_cpufreq_valid(int cpu)
{
	struct cpufreq_policy policy;
	if (!cpufreq_get_policy(&policy, cpu))
		return true;
	return false;
}

static int cpufreq_apply_cooling(int cooling_state)
{
	int cpuid, this_cpu = smp_processor_id();

	if (!is_cpufreq_valid(this_cpu))
		return 0;

	if (cooling_state > cpufreq_device->tab_size)
		return -EINVAL;

	/*Check if last cooling level is same as current cooling level*/
	if (cpufreq_device->cpufreq_state == cooling_state)
		return 0;

	cpufreq_device->cpufreq_state = cooling_state;

	for_each_cpu(cpuid, cpufreq_device->allowed_cpus) {
		if (is_cpufreq_valid(cpuid))
			cpufreq_update_policy(cpuid);
	}

	return 0;
}

static int thermal_cpufreq_notifier(struct notifier_block *nb,
					unsigned long event, void *data)
{
	struct cpufreq_policy *policy = data;
	struct freq_pctg_table *th_table;
	unsigned long max_freq = 0;
	unsigned int cpu = policy->cpu, th_pctg = 0, level;

	if (event != CPUFREQ_ADJUST)
		return 0;

	level = cpufreq_device->cpufreq_state;

	if (level > 0) {
		th_table =
			&(cpufreq_device->tab_ptr[level - 1]);
		th_pctg = th_table->freq_clip_pctg[cpu];
	}

	max_freq =
		(policy->cpuinfo.max_freq * (100 - th_pctg)) / 100;

	cpufreq_verify_within_limits(policy, 0, max_freq);

	return 0;
}

/*
 * cpufreq cooling device callback functions
 */
static int cpufreq_get_max_state(struct thermal_cooling_device *cdev,
				 unsigned long *state)
{
	*state = cpufreq_device->tab_size;
	return 0;
}

static int cpufreq_get_cur_state(struct thermal_cooling_device *cdev,
				 unsigned long *state)
{
	*state = cpufreq_device->cpufreq_state;
	return 0;
}

/*This cooling may be as PASSIVE/STATE_ACTIVE type*/
static int cpufreq_set_cur_state(struct thermal_cooling_device *cdev,
				 unsigned long state)
{
	cpufreq_apply_cooling(state);
	return 0;
}

/* bind cpufreq callbacks to cpufreq cooling device */
static struct thermal_cooling_device_ops cpufreq_cooling_ops = {
	.get_max_state = cpufreq_get_max_state,
	.get_cur_state = cpufreq_get_cur_state,
	.set_cur_state = cpufreq_set_cur_state,
};

static struct notifier_block thermal_cpufreq_notifier_block = {
	.notifier_call = thermal_cpufreq_notifier,
};

struct thermal_cooling_device *cpufreq_cooling_register(
	struct freq_pctg_table *tab_ptr, unsigned int tab_size,
	const struct cpumask *mask_val)
{
	struct thermal_cooling_device *cool_dev;

	if (tab_ptr == NULL || tab_size == 0)
		return ERR_PTR(-EINVAL);

	cpufreq_device =
		kzalloc(sizeof(struct cpufreq_cooling_device), GFP_KERNEL);

	if (!cpufreq_device)
		return ERR_PTR(-ENOMEM);

	cool_dev = thermal_cooling_device_register("thermal-cpufreq", NULL,
						&cpufreq_cooling_ops);
	if (!cool_dev) {
		kfree(cpufreq_device);
		return ERR_PTR(-EINVAL);
	}

	cpufreq_device->tab_ptr = tab_ptr;
	cpufreq_device->tab_size = tab_size;
	cpufreq_device->cool_dev = cool_dev;
	cpufreq_device->allowed_cpus = mask_val;

	cpufreq_register_notifier(&thermal_cpufreq_notifier_block,
						CPUFREQ_POLICY_NOTIFIER);
	return cool_dev;
}
EXPORT_SYMBOL(cpufreq_cooling_register);

void cpufreq_cooling_unregister(void)
{
	cpufreq_unregister_notifier(&thermal_cpufreq_notifier_block,
						CPUFREQ_POLICY_NOTIFIER);
	thermal_cooling_device_unregister(cpufreq_device->cool_dev);
	kfree(cpufreq_device);
}
EXPORT_SYMBOL(cpufreq_cooling_unregister);
#else /*!CONFIG_CPU_FREQ*/
struct thermal_cooling_device *cpufreq_cooling_register(
	struct freq_pctg_table *tab_ptr, unsigned int tab_size)
{
	return NULL;
}
EXPORT_SYMBOL(cpufreq_cooling_register);
void cpufreq_cooling_unregister(void)
{
	return;
}
EXPORT_SYMBOL(cpufreq_cooling_unregister);
#endif /*CONFIG_CPU_FREQ*/

#ifdef CONFIG_HOTPLUG_CPU

struct hotplug_cooling_device {
	struct thermal_cooling_device *cool_dev;
	unsigned int hotplug_state;
	const struct cpumask *allowed_cpus;
};
static struct hotplug_cooling_device *hotplug_device;

/*
 * cpu hotplug cooling device callback functions
 */
static int cpuhotplug_get_max_state(struct thermal_cooling_device *cdev,
				 unsigned long *state)
{
	*state = 1;
	return 0;
}

static int cpuhotplug_get_cur_state(struct thermal_cooling_device *cdev,
				 unsigned long *state)
{
	/*This cooling device may be of type ACTIVE, so state field
	can be 0 or 1*/
	*state = hotplug_device->hotplug_state;
	return 0;
}

/*This cooling may be as PASSIVE/STATE_ACTIVE type*/
static int cpuhotplug_set_cur_state(struct thermal_cooling_device *cdev,
				 unsigned long state)
{
	int cpuid, this_cpu = smp_processor_id();

	if (hotplug_device->hotplug_state == state)
		return 0;

	/*This cooling device may be of type ACTIVE, so state field
	can be 0 or 1*/
	if (state == 1) {
		for_each_cpu(cpuid, hotplug_device->allowed_cpus) {
			if (cpu_online(cpuid) && (cpuid != this_cpu))
				cpu_down(cpuid);
		}
	} else if (state == 0) {
		for_each_cpu(cpuid, hotplug_device->allowed_cpus) {
			if (!cpu_online(cpuid) && (cpuid != this_cpu))
				cpu_up(cpuid);
		}
	} else
		return -EINVAL;

	hotplug_device->hotplug_state = state;

	return 0;
}
/* bind hotplug callbacks to cpu hotplug cooling device */
static struct thermal_cooling_device_ops cpuhotplug_cooling_ops = {
	.get_max_state = cpuhotplug_get_max_state,
	.get_cur_state = cpuhotplug_get_cur_state,
	.set_cur_state = cpuhotplug_set_cur_state,
};

struct thermal_cooling_device *cpuhotplug_cooling_register(
	const struct cpumask *mask_val)
{
	struct thermal_cooling_device *cool_dev;

	hotplug_device =
		kzalloc(sizeof(struct hotplug_cooling_device), GFP_KERNEL);

	if (!hotplug_device)
		return ERR_PTR(-ENOMEM);

	cool_dev = thermal_cooling_device_register("thermal-cpuhotplug", NULL,
						&cpuhotplug_cooling_ops);
	if (!cool_dev) {
		kfree(hotplug_device);
		return ERR_PTR(-EINVAL);
	}

	hotplug_device->cool_dev = cool_dev;
	hotplug_device->hotplug_state = 0;
	hotplug_device->allowed_cpus = mask_val;

	return cool_dev;
}
EXPORT_SYMBOL(cpuhotplug_cooling_register);

void cpuhotplug_cooling_unregister(void)
{
	thermal_cooling_device_unregister(hotplug_device->cool_dev);
	kfree(hotplug_device);
}
EXPORT_SYMBOL(cpuhotplug_cooling_unregister);
#else /*!CONFIG_HOTPLUG_CPU*/
struct thermal_cooling_device *cpuhotplug_cooling_register(
	const struct cpumask *mask_val)
{
	return NULL;
}
EXPORT_SYMBOL(cpuhotplug_cooling_register);
void cpuhotplug_cooling_unregister(void)
{
	return;
}
EXPORT_SYMBOL(cpuhotplug_cooling_unregister);
#endif /*CONFIG_HOTPLUG_CPU*/
