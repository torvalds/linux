// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/drivers/thermal/cpufreq_cooling.c
 *
 *  Copyright (C) 2012	Samsung Electronics Co., Ltd(http://www.samsung.com)
 *
 *  Copyright (C) 2012-2018 Linaro Limited.
 *
 *  Authors:	Amit Daniel <amit.kachhap@linaro.org>
 *		Viresh Kumar <viresh.kumar@linaro.org>
 *
 */
#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/cpu_cooling.h>
#include <linux/device.h>
#include <linux/energy_model.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/pm_opp.h>
#include <linux/pm_qos.h>
#include <linux/slab.h>
#include <linux/thermal.h>
#include <linux/units.h>

#include "thermal_trace.h"

/*
 * Cooling state <-> CPUFreq frequency
 *
 * Cooling states are translated to frequencies throughout this driver and this
 * is the relation between them.
 *
 * Highest cooling state corresponds to lowest possible frequency.
 *
 * i.e.
 *	level 0 --> 1st Max Freq
 *	level 1 --> 2nd Max Freq
 *	...
 */

/**
 * struct time_in_idle - Idle time stats
 * @time: previous reading of the absolute time that this cpu was idle
 * @timestamp: wall time of the last invocation of get_cpu_idle_time_us()
 */
struct time_in_idle {
	u64 time;
	u64 timestamp;
};

/**
 * struct cpufreq_cooling_device - data for cooling device with cpufreq
 * @last_load: load measured by the latest call to cpufreq_get_requested_power()
 * @cpufreq_state: integer value representing the current state of cpufreq
 *	cooling	devices.
 * @max_level: maximum cooling level. One less than total number of valid
 *	cpufreq frequencies.
 * @em: Reference on the Energy Model of the device
 * @cdev: thermal_cooling_device pointer to keep track of the
 *	registered cooling device.
 * @policy: cpufreq policy.
 * @cooling_ops: cpufreq callbacks to thermal cooling device ops
 * @idle_time: idle time stats
 * @qos_req: PM QoS contraint to apply
 *
 * This structure is required for keeping information of each registered
 * cpufreq_cooling_device.
 */
struct cpufreq_cooling_device {
	u32 last_load;
	unsigned int cpufreq_state;
	unsigned int max_level;
	struct em_perf_domain *em;
	struct cpufreq_policy *policy;
	struct thermal_cooling_device_ops cooling_ops;
#ifndef CONFIG_SMP
	struct time_in_idle *idle_time;
#endif
	struct freq_qos_request qos_req;
};

#ifdef CONFIG_THERMAL_GOV_POWER_ALLOCATOR
/**
 * get_level: Find the level for a particular frequency
 * @cpufreq_cdev: cpufreq_cdev for which the property is required
 * @freq: Frequency
 *
 * Return: level corresponding to the frequency.
 */
static unsigned long get_level(struct cpufreq_cooling_device *cpufreq_cdev,
			       unsigned int freq)
{
	struct em_perf_state *table;
	int i;

	rcu_read_lock();
	table = em_perf_state_from_pd(cpufreq_cdev->em);
	for (i = cpufreq_cdev->max_level - 1; i >= 0; i--) {
		if (freq > table[i].frequency)
			break;
	}
	rcu_read_unlock();

	return cpufreq_cdev->max_level - i - 1;
}

static u32 cpu_freq_to_power(struct cpufreq_cooling_device *cpufreq_cdev,
			     u32 freq)
{
	struct em_perf_state *table;
	unsigned long power_mw;
	int i;

	rcu_read_lock();
	table = em_perf_state_from_pd(cpufreq_cdev->em);
	for (i = cpufreq_cdev->max_level - 1; i >= 0; i--) {
		if (freq > table[i].frequency)
			break;
	}

	power_mw = table[i + 1].power;
	power_mw /= MICROWATT_PER_MILLIWATT;
	rcu_read_unlock();

	return power_mw;
}

static u32 cpu_power_to_freq(struct cpufreq_cooling_device *cpufreq_cdev,
			     u32 power)
{
	struct em_perf_state *table;
	unsigned long em_power_mw;
	u32 freq;
	int i;

	rcu_read_lock();
	table = em_perf_state_from_pd(cpufreq_cdev->em);
	for (i = cpufreq_cdev->max_level; i > 0; i--) {
		/* Convert EM power to milli-Watts to make safe comparison */
		em_power_mw = table[i].power;
		em_power_mw /= MICROWATT_PER_MILLIWATT;
		if (power >= em_power_mw)
			break;
	}
	freq = table[i].frequency;
	rcu_read_unlock();

	return freq;
}

/**
 * get_load() - get load for a cpu
 * @cpufreq_cdev: struct cpufreq_cooling_device for the cpu
 * @cpu: cpu number
 * @cpu_idx: index of the cpu in time_in_idle array
 *
 * Return: The average load of cpu @cpu in percentage since this
 * function was last called.
 */
#ifdef CONFIG_SMP
static u32 get_load(struct cpufreq_cooling_device *cpufreq_cdev, int cpu,
		    int cpu_idx)
{
	unsigned long util = sched_cpu_util(cpu);

	return (util * 100) / arch_scale_cpu_capacity(cpu);
}
#else /* !CONFIG_SMP */
static u32 get_load(struct cpufreq_cooling_device *cpufreq_cdev, int cpu,
		    int cpu_idx)
{
	u32 load;
	u64 now, now_idle, delta_time, delta_idle;
	struct time_in_idle *idle_time = &cpufreq_cdev->idle_time[cpu_idx];

	now_idle = get_cpu_idle_time(cpu, &now, 0);
	delta_idle = now_idle - idle_time->time;
	delta_time = now - idle_time->timestamp;

	if (delta_time <= delta_idle)
		load = 0;
	else
		load = div64_u64(100 * (delta_time - delta_idle), delta_time);

	idle_time->time = now_idle;
	idle_time->timestamp = now;

	return load;
}
#endif /* CONFIG_SMP */

/**
 * get_dynamic_power() - calculate the dynamic power
 * @cpufreq_cdev:	&cpufreq_cooling_device for this cdev
 * @freq:	current frequency
 *
 * Return: the dynamic power consumed by the cpus described by
 * @cpufreq_cdev.
 */
static u32 get_dynamic_power(struct cpufreq_cooling_device *cpufreq_cdev,
			     unsigned long freq)
{
	u32 raw_cpu_power;

	raw_cpu_power = cpu_freq_to_power(cpufreq_cdev, freq);
	return (raw_cpu_power * cpufreq_cdev->last_load) / 100;
}

/**
 * cpufreq_get_requested_power() - get the current power
 * @cdev:	&thermal_cooling_device pointer
 * @power:	pointer in which to store the resulting power
 *
 * Calculate the current power consumption of the cpus in milliwatts
 * and store it in @power.  This function should actually calculate
 * the requested power, but it's hard to get the frequency that
 * cpufreq would have assigned if there were no thermal limits.
 * Instead, we calculate the current power on the assumption that the
 * immediate future will look like the immediate past.
 *
 * We use the current frequency and the average load since this
 * function was last called.  In reality, there could have been
 * multiple opps since this function was last called and that affects
 * the load calculation.  While it's not perfectly accurate, this
 * simplification is good enough and works.  REVISIT this, as more
 * complex code may be needed if experiments show that it's not
 * accurate enough.
 *
 * Return: 0 on success, this function doesn't fail.
 */
static int cpufreq_get_requested_power(struct thermal_cooling_device *cdev,
				       u32 *power)
{
	unsigned long freq;
	int i = 0, cpu;
	u32 total_load = 0;
	struct cpufreq_cooling_device *cpufreq_cdev = cdev->devdata;
	struct cpufreq_policy *policy = cpufreq_cdev->policy;

	freq = cpufreq_quick_get(policy->cpu);

	for_each_cpu(cpu, policy->related_cpus) {
		u32 load;

		if (cpu_online(cpu))
			load = get_load(cpufreq_cdev, cpu, i);
		else
			load = 0;

		total_load += load;
	}

	cpufreq_cdev->last_load = total_load;

	*power = get_dynamic_power(cpufreq_cdev, freq);

	trace_thermal_power_cpu_get_power_simple(policy->cpu, *power);

	return 0;
}

/**
 * cpufreq_state2power() - convert a cpu cdev state to power consumed
 * @cdev:	&thermal_cooling_device pointer
 * @state:	cooling device state to be converted
 * @power:	pointer in which to store the resulting power
 *
 * Convert cooling device state @state into power consumption in
 * milliwatts assuming 100% load.  Store the calculated power in
 * @power.
 *
 * Return: 0 on success, -EINVAL if the cooling device state is bigger
 * than maximum allowed.
 */
static int cpufreq_state2power(struct thermal_cooling_device *cdev,
			       unsigned long state, u32 *power)
{
	struct cpufreq_cooling_device *cpufreq_cdev = cdev->devdata;
	unsigned int freq, num_cpus, idx;
	struct em_perf_state *table;

	/* Request state should be less than max_level */
	if (state > cpufreq_cdev->max_level)
		return -EINVAL;

	num_cpus = cpumask_weight(cpufreq_cdev->policy->cpus);

	idx = cpufreq_cdev->max_level - state;

	rcu_read_lock();
	table = em_perf_state_from_pd(cpufreq_cdev->em);
	freq = table[idx].frequency;
	rcu_read_unlock();

	*power = cpu_freq_to_power(cpufreq_cdev, freq) * num_cpus;

	return 0;
}

/**
 * cpufreq_power2state() - convert power to a cooling device state
 * @cdev:	&thermal_cooling_device pointer
 * @power:	power in milliwatts to be converted
 * @state:	pointer in which to store the resulting state
 *
 * Calculate a cooling device state for the cpus described by @cdev
 * that would allow them to consume at most @power mW and store it in
 * @state.  Note that this calculation depends on external factors
 * such as the CPUs load.  Calling this function with the same power
 * as input can yield different cooling device states depending on those
 * external factors.
 *
 * Return: 0 on success, this function doesn't fail.
 */
static int cpufreq_power2state(struct thermal_cooling_device *cdev,
			       u32 power, unsigned long *state)
{
	unsigned int target_freq;
	u32 last_load, normalised_power;
	struct cpufreq_cooling_device *cpufreq_cdev = cdev->devdata;
	struct cpufreq_policy *policy = cpufreq_cdev->policy;

	last_load = cpufreq_cdev->last_load ?: 1;
	normalised_power = (power * 100) / last_load;
	target_freq = cpu_power_to_freq(cpufreq_cdev, normalised_power);

	*state = get_level(cpufreq_cdev, target_freq);
	trace_thermal_power_cpu_limit(policy->related_cpus, target_freq, *state,
				      power);
	return 0;
}

static inline bool em_is_sane(struct cpufreq_cooling_device *cpufreq_cdev,
			      struct em_perf_domain *em) {
	struct cpufreq_policy *policy;
	unsigned int nr_levels;

	if (!em || em_is_artificial(em))
		return false;

	policy = cpufreq_cdev->policy;
	if (!cpumask_equal(policy->related_cpus, em_span_cpus(em))) {
		pr_err("The span of pd %*pbl is misaligned with cpufreq policy %*pbl\n",
			cpumask_pr_args(em_span_cpus(em)),
			cpumask_pr_args(policy->related_cpus));
		return false;
	}

	nr_levels = cpufreq_cdev->max_level + 1;
	if (em_pd_nr_perf_states(em) != nr_levels) {
		pr_err("The number of performance states in pd %*pbl (%u) doesn't match the number of cooling levels (%u)\n",
			cpumask_pr_args(em_span_cpus(em)),
			em_pd_nr_perf_states(em), nr_levels);
		return false;
	}

	return true;
}
#endif /* CONFIG_THERMAL_GOV_POWER_ALLOCATOR */

#ifdef CONFIG_SMP
static inline int allocate_idle_time(struct cpufreq_cooling_device *cpufreq_cdev)
{
	return 0;
}

static inline void free_idle_time(struct cpufreq_cooling_device *cpufreq_cdev)
{
}
#else
static int allocate_idle_time(struct cpufreq_cooling_device *cpufreq_cdev)
{
	unsigned int num_cpus = cpumask_weight(cpufreq_cdev->policy->related_cpus);

	cpufreq_cdev->idle_time = kcalloc(num_cpus,
					  sizeof(*cpufreq_cdev->idle_time),
					  GFP_KERNEL);
	if (!cpufreq_cdev->idle_time)
		return -ENOMEM;

	return 0;
}

static void free_idle_time(struct cpufreq_cooling_device *cpufreq_cdev)
{
	kfree(cpufreq_cdev->idle_time);
	cpufreq_cdev->idle_time = NULL;
}
#endif /* CONFIG_SMP */

static unsigned int get_state_freq(struct cpufreq_cooling_device *cpufreq_cdev,
				   unsigned long state)
{
	struct cpufreq_policy *policy;
	unsigned long idx;

#ifdef CONFIG_THERMAL_GOV_POWER_ALLOCATOR
	/* Use the Energy Model table if available */
	if (cpufreq_cdev->em) {
		struct em_perf_state *table;
		unsigned int freq;

		idx = cpufreq_cdev->max_level - state;

		rcu_read_lock();
		table = em_perf_state_from_pd(cpufreq_cdev->em);
		freq = table[idx].frequency;
		rcu_read_unlock();

		return freq;
	}
#endif

	/* Otherwise, fallback on the CPUFreq table */
	policy = cpufreq_cdev->policy;
	if (policy->freq_table_sorted == CPUFREQ_TABLE_SORTED_ASCENDING)
		idx = cpufreq_cdev->max_level - state;
	else
		idx = state;

	return policy->freq_table[idx].frequency;
}

/* cpufreq cooling device callback functions are defined below */

/**
 * cpufreq_get_max_state - callback function to get the max cooling state.
 * @cdev: thermal cooling device pointer.
 * @state: fill this variable with the max cooling state.
 *
 * Callback for the thermal cooling device to return the cpufreq
 * max cooling state.
 *
 * Return: 0 on success, this function doesn't fail.
 */
static int cpufreq_get_max_state(struct thermal_cooling_device *cdev,
				 unsigned long *state)
{
	struct cpufreq_cooling_device *cpufreq_cdev = cdev->devdata;

	*state = cpufreq_cdev->max_level;
	return 0;
}

/**
 * cpufreq_get_cur_state - callback function to get the current cooling state.
 * @cdev: thermal cooling device pointer.
 * @state: fill this variable with the current cooling state.
 *
 * Callback for the thermal cooling device to return the cpufreq
 * current cooling state.
 *
 * Return: 0 on success, this function doesn't fail.
 */
static int cpufreq_get_cur_state(struct thermal_cooling_device *cdev,
				 unsigned long *state)
{
	struct cpufreq_cooling_device *cpufreq_cdev = cdev->devdata;

	*state = cpufreq_cdev->cpufreq_state;

	return 0;
}

/**
 * cpufreq_set_cur_state - callback function to set the current cooling state.
 * @cdev: thermal cooling device pointer.
 * @state: set this variable to the current cooling state.
 *
 * Callback for the thermal cooling device to change the cpufreq
 * current cooling state.
 *
 * Return: 0 on success, an error code otherwise.
 */
static int cpufreq_set_cur_state(struct thermal_cooling_device *cdev,
				 unsigned long state)
{
	struct cpufreq_cooling_device *cpufreq_cdev = cdev->devdata;
	struct cpumask *cpus;
	unsigned int frequency;
	int ret;

	/* Request state should be less than max_level */
	if (state > cpufreq_cdev->max_level)
		return -EINVAL;

	/* Check if the old cooling action is same as new cooling action */
	if (cpufreq_cdev->cpufreq_state == state)
		return 0;

	frequency = get_state_freq(cpufreq_cdev, state);

	ret = freq_qos_update_request(&cpufreq_cdev->qos_req, frequency);
	if (ret >= 0) {
		cpufreq_cdev->cpufreq_state = state;
		cpus = cpufreq_cdev->policy->related_cpus;
		arch_update_thermal_pressure(cpus, frequency);
		ret = 0;
	}

	return ret;
}

/**
 * __cpufreq_cooling_register - helper function to create cpufreq cooling device
 * @np: a valid struct device_node to the cooling device tree node
 * @policy: cpufreq policy
 * Normally this should be same as cpufreq policy->related_cpus.
 * @em: Energy Model of the cpufreq policy
 *
 * This interface function registers the cpufreq cooling device with the name
 * "cpufreq-%s". This API can support multiple instances of cpufreq
 * cooling devices. It also gives the opportunity to link the cooling device
 * with a device tree node, in order to bind it via the thermal DT code.
 *
 * Return: a valid struct thermal_cooling_device pointer on success,
 * on failure, it returns a corresponding ERR_PTR().
 */
static struct thermal_cooling_device *
__cpufreq_cooling_register(struct device_node *np,
			struct cpufreq_policy *policy,
			struct em_perf_domain *em)
{
	struct thermal_cooling_device *cdev;
	struct cpufreq_cooling_device *cpufreq_cdev;
	unsigned int i;
	struct device *dev;
	int ret;
	struct thermal_cooling_device_ops *cooling_ops;
	char *name;

	if (IS_ERR_OR_NULL(policy)) {
		pr_err("%s: cpufreq policy isn't valid: %p\n", __func__, policy);
		return ERR_PTR(-EINVAL);
	}

	dev = get_cpu_device(policy->cpu);
	if (unlikely(!dev)) {
		pr_warn("No cpu device for cpu %d\n", policy->cpu);
		return ERR_PTR(-ENODEV);
	}

	i = cpufreq_table_count_valid_entries(policy);
	if (!i) {
		pr_debug("%s: CPUFreq table not found or has no valid entries\n",
			 __func__);
		return ERR_PTR(-ENODEV);
	}

	cpufreq_cdev = kzalloc(sizeof(*cpufreq_cdev), GFP_KERNEL);
	if (!cpufreq_cdev)
		return ERR_PTR(-ENOMEM);

	cpufreq_cdev->policy = policy;

	ret = allocate_idle_time(cpufreq_cdev);
	if (ret) {
		cdev = ERR_PTR(ret);
		goto free_cdev;
	}

	/* max_level is an index, not a counter */
	cpufreq_cdev->max_level = i - 1;

	cooling_ops = &cpufreq_cdev->cooling_ops;
	cooling_ops->get_max_state = cpufreq_get_max_state;
	cooling_ops->get_cur_state = cpufreq_get_cur_state;
	cooling_ops->set_cur_state = cpufreq_set_cur_state;

#ifdef CONFIG_THERMAL_GOV_POWER_ALLOCATOR
	if (em_is_sane(cpufreq_cdev, em)) {
		cpufreq_cdev->em = em;
		cooling_ops->get_requested_power = cpufreq_get_requested_power;
		cooling_ops->state2power = cpufreq_state2power;
		cooling_ops->power2state = cpufreq_power2state;
	} else
#endif
	if (policy->freq_table_sorted == CPUFREQ_TABLE_UNSORTED) {
		pr_err("%s: unsorted frequency tables are not supported\n",
		       __func__);
		cdev = ERR_PTR(-EINVAL);
		goto free_idle_time;
	}

	ret = freq_qos_add_request(&policy->constraints,
				   &cpufreq_cdev->qos_req, FREQ_QOS_MAX,
				   get_state_freq(cpufreq_cdev, 0));
	if (ret < 0) {
		pr_err("%s: Failed to add freq constraint (%d)\n", __func__,
		       ret);
		cdev = ERR_PTR(ret);
		goto free_idle_time;
	}

	cdev = ERR_PTR(-ENOMEM);
	name = kasprintf(GFP_KERNEL, "cpufreq-%s", dev_name(dev));
	if (!name)
		goto remove_qos_req;

	cdev = thermal_of_cooling_device_register(np, name, cpufreq_cdev,
						  cooling_ops);
	kfree(name);

	if (IS_ERR(cdev))
		goto remove_qos_req;

	return cdev;

remove_qos_req:
	freq_qos_remove_request(&cpufreq_cdev->qos_req);
free_idle_time:
	free_idle_time(cpufreq_cdev);
free_cdev:
	kfree(cpufreq_cdev);
	return cdev;
}

/**
 * cpufreq_cooling_register - function to create cpufreq cooling device.
 * @policy: cpufreq policy
 *
 * This interface function registers the cpufreq cooling device with the name
 * "cpufreq-%s". This API can support multiple instances of cpufreq cooling
 * devices.
 *
 * Return: a valid struct thermal_cooling_device pointer on success,
 * on failure, it returns a corresponding ERR_PTR().
 */
struct thermal_cooling_device *
cpufreq_cooling_register(struct cpufreq_policy *policy)
{
	return __cpufreq_cooling_register(NULL, policy, NULL);
}
EXPORT_SYMBOL_GPL(cpufreq_cooling_register);

/**
 * of_cpufreq_cooling_register - function to create cpufreq cooling device.
 * @policy: cpufreq policy
 *
 * This interface function registers the cpufreq cooling device with the name
 * "cpufreq-%s". This API can support multiple instances of cpufreq cooling
 * devices. Using this API, the cpufreq cooling device will be linked to the
 * device tree node provided.
 *
 * Using this function, the cooling device will implement the power
 * extensions by using the Energy Model (if present).  The cpus must have
 * registered their OPPs using the OPP library.
 *
 * Return: a valid struct thermal_cooling_device pointer on success,
 * and NULL on failure.
 */
struct thermal_cooling_device *
of_cpufreq_cooling_register(struct cpufreq_policy *policy)
{
	struct device_node *np = of_get_cpu_node(policy->cpu, NULL);
	struct thermal_cooling_device *cdev = NULL;

	if (!np) {
		pr_err("cpufreq_cooling: OF node not available for cpu%d\n",
		       policy->cpu);
		return NULL;
	}

	if (of_property_present(np, "#cooling-cells")) {
		struct em_perf_domain *em = em_cpu_get(policy->cpu);

		cdev = __cpufreq_cooling_register(np, policy, em);
		if (IS_ERR(cdev)) {
			pr_err("cpufreq_cooling: cpu%d failed to register as cooling device: %ld\n",
			       policy->cpu, PTR_ERR(cdev));
			cdev = NULL;
		}
	}

	of_node_put(np);
	return cdev;
}
EXPORT_SYMBOL_GPL(of_cpufreq_cooling_register);

/**
 * cpufreq_cooling_unregister - function to remove cpufreq cooling device.
 * @cdev: thermal cooling device pointer.
 *
 * This interface function unregisters the "cpufreq-%x" cooling device.
 */
void cpufreq_cooling_unregister(struct thermal_cooling_device *cdev)
{
	struct cpufreq_cooling_device *cpufreq_cdev;

	if (!cdev)
		return;

	cpufreq_cdev = cdev->devdata;

	thermal_cooling_device_unregister(cdev);
	freq_qos_remove_request(&cpufreq_cdev->qos_req);
	free_idle_time(cpufreq_cdev);
	kfree(cpufreq_cdev);
}
EXPORT_SYMBOL_GPL(cpufreq_cooling_unregister);
