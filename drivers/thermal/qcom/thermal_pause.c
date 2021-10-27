// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */

#define pr_fmt(fmt) "%s:%s " fmt, KBUILD_MODNAME, __func__

#include <linux/module.h>
#include <linux/thermal.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/cpu.h>
#include <linux/of_device.h>
#include <linux/suspend.h>
#include <linux/cpumask.h>
#include <linux/sched/walt.h>

enum thermal_pause_levels {
	THERMAL_NO_CPU_PAUSE,
	THERMAL_GROUP_CPU_PAUSE,

	/* define new pause levels above this line */
	MAX_THERMAL_PAUSE_LEVEL
};

struct thermal_pause_cdev {
	struct list_head		node;
	cpumask_t			cpu_mask;
	bool				thermal_pause_level;
	struct thermal_cooling_device	*cdev;
	struct device_node		*np;
	char				cdev_name[THERMAL_NAME_LENGTH];
	struct work_struct		reg_work;
};

static DEFINE_MUTEX(cpus_pause_lock);
static LIST_HEAD(thermal_pause_cdev_list);

static struct cpumask cpus_in_max_cooling_level;
static enum cpuhp_state cpu_hp_online;

static BLOCKING_NOTIFIER_HEAD(multi_max_cooling_level_notifer);

void cpu_cooling_multi_max_level_notifier_register(struct notifier_block *n)
{
	blocking_notifier_chain_register(&multi_max_cooling_level_notifer, n);
}

void cpu_cooling_multi_max_level_notifier_unregister(struct notifier_block *n)
{
	blocking_notifier_chain_unregister(&multi_max_cooling_level_notifer, n);
}

const struct cpumask *cpu_cooling_multi_get_max_level_cpumask(void)
{
	return &cpus_in_max_cooling_level;
}

static int thermal_pause_hp_online(unsigned int online_cpu)
{
	struct thermal_pause_cdev *thermal_pause_cdev;
	int ret = 0;

	pr_debug("online entry CPU:%d\n", online_cpu);

	mutex_lock(&cpus_pause_lock);
	list_for_each_entry(thermal_pause_cdev, &thermal_pause_cdev_list, node) {
		if (cpumask_test_cpu(online_cpu, &thermal_pause_cdev->cpu_mask)
			&& !thermal_pause_cdev->cdev)
			queue_work(system_highpri_wq,
				   &thermal_pause_cdev->reg_work);
	}
	mutex_unlock(&cpus_pause_lock);
	pr_debug("online exit CPU:%d\n", online_cpu);
	return ret;
}

/**
 * thermal_pause_cpus_pause - function to pause a group of cpus at
 *                         the specified level.
 *
 * @thermal_pause_cdev: the pause device
 *
 * function to handle setting the current cpus paused by
 * this driver for the mask specified in the device.
 * it assumes the mutex is locked.
 *
 * Returns 0 if CPUs were paused, error otherwise
 */
static int thermal_pause_cpus_pause(struct thermal_pause_cdev *thermal_pause_cdev)
{
	int cpu = 0;
	int ret = -EBUSY;
	cpumask_t cpus_to_pause, cpus_to_notify;

	if (thermal_pause_cdev->thermal_pause_level)
		return ret;

	cpumask_copy(&cpus_to_pause, &thermal_pause_cdev->cpu_mask);
	pr_debug("Pause:%*pbl\n", cpumask_pr_args(&thermal_pause_cdev->cpu_mask));

	mutex_unlock(&cpus_pause_lock);
	ret = walt_pause_cpus(&cpus_to_pause);
	mutex_lock(&cpus_pause_lock);

	if (ret == 0) {
		/* remove CPUs that have already been notified */
		cpumask_andnot(&cpus_to_notify, &thermal_pause_cdev->cpu_mask,
			       &cpus_in_max_cooling_level);

		for_each_cpu(cpu, &cpus_to_notify)
			blocking_notifier_call_chain(&thermal_pause_notifier, 1,
						     (void *)(long)cpu);

		/* track CPUs currently in cooling level */
		cpumask_or(&cpus_in_max_cooling_level,
			   &cpus_in_max_cooling_level,
			   &thermal_pause_cdev->cpu_mask);
	} else {
		/* Failure. These cpus not paused by thermal */
		pr_err("Error pausing CPU:%*pbl. err:%d\n",
		       cpumask_pr_args(&thermal_pause_cdev->cpu_mask), ret);
		return ret;
	}

	return ret;
}

/**
 * thermal_pause_cpus_unpause - function to unpause a
 *       group of cpus in the mask for this cdev
 *
 * @thermal_pause_cdev: the pause device
 *
 * function to handle enabling the group of cpus in the cdev
 *
 * Returns 0 if CPUs were unpaused,
 */
static int thermal_pause_cpus_unpause(struct thermal_pause_cdev *thermal_pause_cdev)
{
	int cpu = 0;
	int ret = -ENODEV;
	cpumask_t cpus_to_unpause, new_paused_cpus, cpus_to_notify;
	struct thermal_pause_cdev *cdev;

	/* do not unpause a cooling device not paused */
	if (!thermal_pause_cdev->thermal_pause_level)
		return ret;

	cpumask_copy(&cpus_to_unpause, &thermal_pause_cdev->cpu_mask);
	pr_debug("Unpause:%*pbl\n", cpumask_pr_args(&cpus_to_unpause));

	mutex_unlock(&cpus_pause_lock);
	ret = walt_resume_cpus(&cpus_to_unpause);
	mutex_lock(&cpus_pause_lock);

	if (ret == 0) {
		/* gather up the cpus paused state from all the cdevs */
		cpumask_clear(&new_paused_cpus);
		list_for_each_entry(cdev, &thermal_pause_cdev_list, node) {
			if (!cdev->thermal_pause_level || cdev == thermal_pause_cdev)
				continue;
			cpumask_or(&new_paused_cpus, &new_paused_cpus, &cdev->cpu_mask);
		}

		/* remove CPUs that will remain paused */
		cpumask_andnot(&cpus_to_notify, &cpus_in_max_cooling_level, &new_paused_cpus);

		/* Notify for each CPU that we intended to resume */
		for_each_cpu(cpu, &cpus_to_notify)
			blocking_notifier_call_chain(&thermal_pause_notifier, 0,
						     (void *)(long)cpu);

		/* update the cpus cooling mask */
		cpumask_copy(&cpus_in_max_cooling_level, &new_paused_cpus);
	} else {
		/* Failure. Ref-count for cpus controlled by thermal still set */
		pr_err("Error resuming CPU:%*pbl. err:%d\n",
		       cpumask_pr_args(&thermal_pause_cdev->cpu_mask), ret);
		return ret;
	}

	return ret;
}

/**
 * thermal_pause_set_cur_state - callback function to set the current cooling
 *				level.
 * @cdev: thermal cooling device pointer.
 * @level: set this variable to the current cooling level.
 *
 * Callback for the thermal cooling device to change the cpu pause
 * current cooling level.
 *
 * Return: 0 on success, an error code otherwise.
 */
static int thermal_pause_set_cur_state(struct thermal_cooling_device *cdev,
				   unsigned long level)
{
	struct thermal_pause_cdev *thermal_pause_cdev = cdev->devdata;
	int ret = 0;

	if (level >= MAX_THERMAL_PAUSE_LEVEL)
		return -EINVAL;

	if (thermal_pause_cdev->thermal_pause_level == level)
		return 0;

	mutex_lock(&cpus_pause_lock);
	if (level == THERMAL_GROUP_CPU_PAUSE)
		ret = thermal_pause_cpus_pause(thermal_pause_cdev);
	else
		ret = thermal_pause_cpus_unpause(thermal_pause_cdev);
	/*
	 * only change the pause level if things were successful.  Otherwise
	 * an unsuccessful pause operation can be followed by a resume
	 * operation, resuming cpus not paused by this cooling device.
	 */
	if (ret == 0)
		thermal_pause_cdev->thermal_pause_level = level;

	mutex_unlock(&cpus_pause_lock);
	return ret;
}

/**
 * thermal_pause_get_cur_state - callback function to get the current cooling
 *				state.
 * @cdev: thermal cooling device pointer.
 * @state: fill this variable with the current cooling state.
 *
 * Callback for the thermal cooling device to return the cpu pause
 * current cooling level
 *
 * Return: 0 on success, an error code otherwise.
 */
static int thermal_pause_get_cur_state(struct thermal_cooling_device *cdev,
				   unsigned long *level)
{
	struct thermal_pause_cdev *thermal_pause_cdev = cdev->devdata;

	*level = thermal_pause_cdev->thermal_pause_level;

	return 0;
}

/**
 * thermal_pause_get_max_state - callback function to get the max cooling state.
 * @cdev: thermal cooling device pointer.
 * @level: fill this variable with the max cooling level
 *
 * Callback for the thermal cooling device to return the cpu
 * pause max cooling state.
 *
 * Return: 0 on success, an error code otherwise.
 */
static int thermal_pause_get_max_state(struct thermal_cooling_device *cdev,
				   unsigned long *level)
{
	*level = MAX_THERMAL_PAUSE_LEVEL - 1;
	return 0;
}

static struct thermal_cooling_device_ops thermal_pause_cooling_ops = {
	.get_max_state = thermal_pause_get_max_state,
	.get_cur_state = thermal_pause_get_cur_state,
	.set_cur_state = thermal_pause_set_cur_state,
};

static void thermal_pause_register_cdev(struct work_struct *work)
{
	struct thermal_pause_cdev *thermal_pause_cdev =
			container_of(work, struct thermal_pause_cdev, reg_work);
	int ret = 0;
	cpumask_t cpus_online;

	cpumask_and(&cpus_online,
			&thermal_pause_cdev->cpu_mask,
			cpu_online_mask);
	if (!cpumask_equal(&thermal_pause_cdev->cpu_mask, &cpus_online))
		return;

	thermal_pause_cdev->cdev = thermal_of_cooling_device_register(
					thermal_pause_cdev->np,
					thermal_pause_cdev->cdev_name,
					thermal_pause_cdev,
					&thermal_pause_cooling_ops);

	if (IS_ERR(thermal_pause_cdev->cdev)) {
		ret = PTR_ERR(thermal_pause_cdev->cdev);
		pr_err("Cooling register failed for %s, ret:%d\n",
			thermal_pause_cdev->cdev_name, ret);
		thermal_pause_cdev->cdev = NULL;
		return;
	}
	pr_debug("Cooling device [%s] registered.\n",
			thermal_pause_cdev->cdev_name);
}

static int thermal_pause_probe(struct platform_device *pdev)
{
	int ret = 0, cpu = 0;
	struct device_node *subsys_np = NULL, *cpu_phandle = NULL;
	struct device *cpu_dev;
	struct thermal_pause_cdev *thermal_pause_cdev = NULL;
	struct device_node *np = pdev->dev.of_node;
	struct of_phandle_iterator it;
	cpumask_t cpu_mask;
	unsigned long mask = 0;

	INIT_LIST_HEAD(&thermal_pause_cdev_list);
	cpumask_clear(&cpus_in_max_cooling_level);

	for_each_available_child_of_node(np, subsys_np) {

		cpumask_clear(&cpu_mask);
		mask = 0;
		of_phandle_iterator_init(&it, subsys_np, "qcom,cpus", NULL, 0);
		while (of_phandle_iterator_next(&it) == 0) {
			cpu_phandle = it.node;
			for_each_possible_cpu(cpu) {
				cpu_dev = get_cpu_device(cpu);
				if (cpu_dev && cpu_dev->of_node
						== cpu_phandle) {
					cpumask_set_cpu(cpu, &cpu_mask);
					mask = mask | BIT(cpu);
					break;
				}
			}
		}

		if (cpumask_empty(&cpu_mask))
			continue;

		thermal_pause_cdev = devm_kzalloc(&pdev->dev,
				sizeof(*thermal_pause_cdev), GFP_KERNEL);

		if (!thermal_pause_cdev) {
			of_node_put(subsys_np);
			return -ENOMEM;
		}

		snprintf(thermal_pause_cdev->cdev_name, THERMAL_NAME_LENGTH,
				"thermal-pause-%X", mask);

		thermal_pause_cdev->thermal_pause_level = false;
		thermal_pause_cdev->cdev = NULL;
		thermal_pause_cdev->np = subsys_np;
		cpumask_copy(&thermal_pause_cdev->cpu_mask, &cpu_mask);

		INIT_WORK(&thermal_pause_cdev->reg_work,
				thermal_pause_register_cdev);
		list_add(&thermal_pause_cdev->node, &thermal_pause_cdev_list);
	}

	ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "thermal-pause/cdev:online",
				thermal_pause_hp_online, NULL);
	if (ret < 0)
		return ret;
	cpu_hp_online = ret;

	return 0;
}

static int thermal_pause_remove(struct platform_device *pdev)
{
	struct thermal_pause_cdev *thermal_pause_cdev = NULL, *next = NULL;
	int ret = 0;

	if (cpu_hp_online) {
		cpuhp_remove_state_nocalls(cpu_hp_online);
		cpu_hp_online = 0;
	}

	mutex_lock(&cpus_pause_lock);
	list_for_each_entry_safe(thermal_pause_cdev, next,
			&thermal_pause_cdev_list, node) {

		/* for each asserted cooling device, resume the CPUs */
		if (thermal_pause_cdev->thermal_pause_level) {
			mutex_unlock(&cpus_pause_lock);
			ret = walt_resume_cpus(&thermal_pause_cdev->cpu_mask);

			if (ret < 0)
				pr_err("Error resuming CPU:%*pbl. err:%d\n",
				       cpumask_pr_args(&thermal_pause_cdev->cpu_mask), ret);
			mutex_lock(&cpus_pause_lock);
		}

		if (thermal_pause_cdev->cdev)
			thermal_cooling_device_unregister(
					thermal_pause_cdev->cdev);
		list_del(&thermal_pause_cdev->node);
	}

	mutex_unlock(&cpus_pause_lock);

	/* if the resume failed, thermal still controls the CPUs.
	 * ensure that the error is passed to the caller.
	 */
	return ret;
}

static const struct of_device_id thermal_pause_match[] = {
	{ .compatible = "qcom,thermal-pause", },
	{},
};

static struct platform_driver thermal_pause_driver = {
	.probe		= thermal_pause_probe,
	.remove         = thermal_pause_remove,
	.driver		= {
		.name		= KBUILD_MODNAME,
		.of_match_table = thermal_pause_match,
	},
};

module_platform_driver(thermal_pause_driver);
MODULE_LICENSE("GPL v2");
