// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/cpu.h>
#include <linux/cpuidle.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/pm_qos.h>
#include <linux/smp.h>
#include <linux/string.h>
#include <linux/suspend.h>
#include <linux/tick.h>
#include <trace/events/power.h>

#include "qcom-simple-lpm.h"

static bool cluster_gov_registered;
bool simple_sleep_disabled = true;
u64 cur_div = 100;
static bool suspend_in_progress;
static struct simple_cluster_governor *cluster_simple_gov_ops;

DEFINE_PER_CPU(struct simple_lpm_cpu, simple_lpm_cpu_data);

static inline bool check_cpu_isactive(int cpu)
{
	return cpu_active(cpu);
}

static int simple_lpm_cpu_qos_notify(struct notifier_block *nfb,
			      unsigned long val, void *ptr)
{
	struct simple_lpm_cpu *cpu_gov = container_of(nfb, struct simple_lpm_cpu, nb);
	int cpu = cpu_gov->cpu;

	if (!cpu_gov->enable)
		return NOTIFY_OK;

	preempt_disable();
	if (cpu != smp_processor_id() && cpu_online(cpu) &&
	    check_cpu_isactive(cpu))
		wake_up_if_idle(cpu);
	preempt_enable();

	return NOTIFY_OK;
}

static int lpm_offline_cpu(unsigned int cpu)
{
	struct simple_lpm_cpu *cpu_gov = per_cpu_ptr(&simple_lpm_cpu_data, cpu);
	struct device *dev = get_cpu_device(cpu);

	if (!dev || !cpu_gov)
		return 0;

	dev_pm_qos_remove_notifier(dev, &cpu_gov->nb, DEV_PM_QOS_RESUME_LATENCY);

	return 0;
}

static int lpm_online_cpu(unsigned int cpu)
{
	struct simple_lpm_cpu *cpu_gov = per_cpu_ptr(&simple_lpm_cpu_data, cpu);
	struct device *dev = get_cpu_device(cpu);

	if (!dev || !cpu_gov)
		return 0;

	cpu_gov->nb.notifier_call = simple_lpm_cpu_qos_notify;
	dev_pm_qos_add_notifier(dev, &cpu_gov->nb, DEV_PM_QOS_RESUME_LATENCY);

	return 0;
}

/**
 * get_cpus_qos() - Returns the aggrigated PM QoS request.
 * @mask: cpumask of the cpus
 */
static inline s64 get_cpus_qos(const struct cpumask *mask)
{
	int cpu;
	s64 n, latency = PM_QOS_CPU_LATENCY_DEFAULT_VALUE;

	for_each_cpu(cpu, mask) {
		if (!check_cpu_isactive(cpu))
			continue;
		n = cpuidle_governor_latency_req(cpu);
		do_div(n, NSEC_PER_USEC);
		if (n < latency)
			latency = n;
	}

	return latency;
}

void register_cluster_simple_governor_ops(struct simple_cluster_governor *ops)
{
	if (!ops)
		return;

	cluster_simple_gov_ops = ops;
}

void unregister_cluster_simple_governor_ops(struct simple_cluster_governor *ops)
{
	if (ops != cluster_simple_gov_ops)
		return;

	cluster_simple_gov_ops = NULL;
}

/**
 * lpm_select() - Find the best idle state for the cpu device
 * @dev:       Target cpu
 * @state:     Entered state
 * @stop_tick: Is the tick device stopped
 *
 * Return: Best cpu LPM mode to enter
 */
static int lpm_select(struct cpuidle_driver *drv, struct cpuidle_device *dev,
		      bool *stop_tick)
{
	struct simple_lpm_cpu *cpu_gov = this_cpu_ptr(&simple_lpm_cpu_data);
	u64 latency_req = get_cpus_qos(cpumask_of(dev->cpu));
	ktime_t delta_tick;
	s64 duration_ns;
	int i = 0;

	if (!cpu_gov)
		return 0;

	if (simple_sleep_disabled)
		return 0;

	duration_ns = tick_nohz_get_sleep_length(&delta_tick);
	if (duration_ns < 0)
		return 0;

	for (i = drv->state_count - 1; i > 0; i--) {
		struct cpuidle_state *s = &drv->states[i];
		u64 target_latency = s->exit_latency;
		s64 target_residency = s->target_residency_ns;

		do_div(target_latency, cur_div);
		do_div(target_residency, cur_div);

		if (dev->states_usage[i].disable)
			continue;

		if (latency_req < target_latency)
			continue;

		if (target_residency > duration_ns)
			continue;

		break;
	}

	cpu_gov->last_idx = i;

	return i;
}

/**
 * lpm_reflect() - Update the state entered by the cpu device
 * @dev:       Target CPU
 * @state:     Entered state
 */
static void lpm_reflect(struct cpuidle_device *dev, int state)
{

}

/**
 * lpm_enable_device() - Initialize the governor's data for the CPU
 * @drv:      cpuidle driver
 * @dev:      Target CPU
 */
static int lpm_enable_device(struct cpuidle_driver *drv,
			     struct cpuidle_device *dev)
{
	struct simple_lpm_cpu *cpu_gov = per_cpu_ptr(&simple_lpm_cpu_data, dev->cpu);

	cpu_gov->cpu = dev->cpu;
	cpu_gov->enable = true;
	cpu_gov->drv = drv;
	cpu_gov->dev = dev;
	cpu_gov->last_idx = -1;

	if (!cluster_gov_registered) {
		if (cluster_simple_gov_ops && cluster_simple_gov_ops->enable)
			cluster_simple_gov_ops->enable();
		cluster_gov_registered = true;
	}
	return 0;
}

/**
 * lpm_disable_device() - Clean up the governor's data for the CPU
 * @drv:      cpuidle driver
 * @dev:      Target CPU
 */
static void lpm_disable_device(struct cpuidle_driver *drv,
			       struct cpuidle_device *dev)
{
	struct simple_lpm_cpu *cpu_gov = per_cpu_ptr(&simple_lpm_cpu_data, dev->cpu);
	int cpu;

	cpu_gov->enable = false;
	cpu_gov->last_idx = -1;
	for_each_possible_cpu(cpu) {
		struct simple_lpm_cpu *cpu_gov = per_cpu_ptr(&simple_lpm_cpu_data, cpu);

		if (cpu_gov->enable)
			return;
	}

	if (cluster_gov_registered) {
		if (cluster_simple_gov_ops && cluster_simple_gov_ops->disable)
			cluster_simple_gov_ops->disable();
		cluster_gov_registered = false;
	}
}

static void qcom_lpm_suspend_trace(void *unused, const char *action,
				   int event, bool start)
{
	int cpu;

	if (start && !strcmp("dpm_suspend_late", action)) {
		suspend_in_progress = true;

		for_each_online_cpu(cpu)
			wake_up_if_idle(cpu);
		return;
	}

	if (!start && !strcmp("dpm_resume_early", action)) {
		suspend_in_progress = false;

		for_each_online_cpu(cpu)
			wake_up_if_idle(cpu);
	}
}

static struct cpuidle_governor lpm_simple_governor = {
	.name =		"qcom-simple-lpm",
	.rating =	40,
	.enable =	lpm_enable_device,
	.disable =	lpm_disable_device,
	.select =	lpm_select,
	.reflect =	lpm_reflect,
};

static int __init qcom_lpm_simple_governor_init(void)
{
	int ret;

	ret = create_simple_gov_global_sysfs_nodes();
	if (ret)
		goto sysfs_fail;

	ret = qcom_cluster_lpm_simple_governor_init();
	if (ret)
		goto cluster_init_fail;

	ret = cpuidle_register_governor(&lpm_simple_governor);
	if (ret)
		goto cpuidle_reg_fail;

	ret = register_trace_suspend_resume(qcom_lpm_suspend_trace, NULL);
	if (ret)
		goto cpuidle_reg_fail;

	ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "qcom-simple-lpm",
				lpm_online_cpu, lpm_offline_cpu);
	if (ret < 0)
		goto cpuhp_setup_fail;

	return 0;

cpuhp_setup_fail:
	unregister_trace_suspend_resume(qcom_lpm_suspend_trace, NULL);
cpuidle_reg_fail:
	qcom_cluster_lpm_simple_governor_deinit();
cluster_init_fail:
	remove_simple_gov_global_sysfs_nodes();
sysfs_fail:
	return ret;
}
module_init(qcom_lpm_simple_governor_init);

MODULE_DESCRIPTION("Qualcomm Technologies, Inc. simple LPM governor");
MODULE_LICENSE("GPL");
