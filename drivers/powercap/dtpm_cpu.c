// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2020 Linaro Limited
 *
 * Author: Daniel Lezcano <daniel.lezcano@linaro.org>
 *
 * The DTPM CPU is based on the energy model. It hooks the CPU in the
 * DTPM tree which in turns update the power number by propagating the
 * power number from the CPU energy model information to the parents.
 *
 * The association between the power and the performance state, allows
 * to set the power of the CPU at the OPP granularity.
 *
 * The CPU hotplug is supported and the power numbers will be updated
 * if a CPU is hot plugged / unplugged.
 */
#include <linux/cpumask.h>
#include <linux/cpufreq.h>
#include <linux/cpuhotplug.h>
#include <linux/dtpm.h>
#include <linux/energy_model.h>
#include <linux/pm_qos.h>
#include <linux/slab.h>
#include <linux/units.h>

static struct dtpm *__parent;

static DEFINE_PER_CPU(struct dtpm *, dtpm_per_cpu);

struct dtpm_cpu {
	struct freq_qos_request qos_req;
	int cpu;
};

/*
 * When a new CPU is inserted at hotplug or boot time, add the power
 * contribution and update the dtpm tree.
 */
static int power_add(struct dtpm *dtpm, struct em_perf_domain *em)
{
	u64 power_min, power_max;

	power_min = em->table[0].power;
	power_min *= MICROWATT_PER_MILLIWATT;
	power_min += dtpm->power_min;

	power_max = em->table[em->nr_perf_states - 1].power;
	power_max *= MICROWATT_PER_MILLIWATT;
	power_max += dtpm->power_max;

	return dtpm_update_power(dtpm, power_min, power_max);
}

/*
 * When a CPU is unplugged, remove its power contribution from the
 * dtpm tree.
 */
static int power_sub(struct dtpm *dtpm, struct em_perf_domain *em)
{
	u64 power_min, power_max;

	power_min = em->table[0].power;
	power_min *= MICROWATT_PER_MILLIWATT;
	power_min = dtpm->power_min - power_min;

	power_max = em->table[em->nr_perf_states - 1].power;
	power_max *= MICROWATT_PER_MILLIWATT;
	power_max = dtpm->power_max - power_max;

	return dtpm_update_power(dtpm, power_min, power_max);
}

static u64 set_pd_power_limit(struct dtpm *dtpm, u64 power_limit)
{
	struct dtpm_cpu *dtpm_cpu = dtpm->private;
	struct em_perf_domain *pd;
	struct cpumask cpus;
	unsigned long freq;
	u64 power;
	int i, nr_cpus;

	pd = em_cpu_get(dtpm_cpu->cpu);

	cpumask_and(&cpus, cpu_online_mask, to_cpumask(pd->cpus));

	nr_cpus = cpumask_weight(&cpus);

	for (i = 0; i < pd->nr_perf_states; i++) {

		power = pd->table[i].power * MICROWATT_PER_MILLIWATT * nr_cpus;

		if (power > power_limit)
			break;
	}

	freq = pd->table[i - 1].frequency;

	freq_qos_update_request(&dtpm_cpu->qos_req, freq);

	power_limit = pd->table[i - 1].power *
		MICROWATT_PER_MILLIWATT * nr_cpus;

	return power_limit;
}

static u64 get_pd_power_uw(struct dtpm *dtpm)
{
	struct dtpm_cpu *dtpm_cpu = dtpm->private;
	struct em_perf_domain *pd;
	struct cpumask cpus;
	unsigned long freq;
	int i, nr_cpus;

	pd = em_cpu_get(dtpm_cpu->cpu);
	freq = cpufreq_quick_get(dtpm_cpu->cpu);
	cpumask_and(&cpus, cpu_online_mask, to_cpumask(pd->cpus));
	nr_cpus = cpumask_weight(&cpus);

	for (i = 0; i < pd->nr_perf_states; i++) {

		if (pd->table[i].frequency < freq)
			continue;

		return pd->table[i].power *
			MICROWATT_PER_MILLIWATT * nr_cpus;
	}

	return 0;
}

static void pd_release(struct dtpm *dtpm)
{
	struct dtpm_cpu *dtpm_cpu = dtpm->private;

	if (freq_qos_request_active(&dtpm_cpu->qos_req))
		freq_qos_remove_request(&dtpm_cpu->qos_req);

	kfree(dtpm_cpu);
}

static struct dtpm_ops dtpm_ops = {
	.set_power_uw = set_pd_power_limit,
	.get_power_uw = get_pd_power_uw,
	.release = pd_release,
};

static int cpuhp_dtpm_cpu_offline(unsigned int cpu)
{
	struct cpufreq_policy *policy;
	struct em_perf_domain *pd;
	struct dtpm *dtpm;

	policy = cpufreq_cpu_get(cpu);

	if (!policy)
		return 0;

	pd = em_cpu_get(cpu);
	if (!pd)
		return -EINVAL;

	dtpm = per_cpu(dtpm_per_cpu, cpu);

	power_sub(dtpm, pd);

	if (cpumask_weight(policy->cpus) != 1)
		return 0;

	for_each_cpu(cpu, policy->related_cpus)
		per_cpu(dtpm_per_cpu, cpu) = NULL;

	dtpm_unregister(dtpm);

	return 0;
}

static int cpuhp_dtpm_cpu_online(unsigned int cpu)
{
	struct dtpm *dtpm;
	struct dtpm_cpu *dtpm_cpu;
	struct cpufreq_policy *policy;
	struct em_perf_domain *pd;
	char name[CPUFREQ_NAME_LEN];
	int ret = -ENOMEM;

	policy = cpufreq_cpu_get(cpu);

	if (!policy)
		return 0;

	pd = em_cpu_get(cpu);
	if (!pd)
		return -EINVAL;

	dtpm = per_cpu(dtpm_per_cpu, cpu);
	if (dtpm)
		return power_add(dtpm, pd);

	dtpm = dtpm_alloc(&dtpm_ops);
	if (!dtpm)
		return -EINVAL;

	dtpm_cpu = kzalloc(sizeof(*dtpm_cpu), GFP_KERNEL);
	if (!dtpm_cpu)
		goto out_kfree_dtpm;

	dtpm->private = dtpm_cpu;
	dtpm_cpu->cpu = cpu;

	for_each_cpu(cpu, policy->related_cpus)
		per_cpu(dtpm_per_cpu, cpu) = dtpm;

	sprintf(name, "cpu%d", dtpm_cpu->cpu);

	ret = dtpm_register(name, dtpm, __parent);
	if (ret)
		goto out_kfree_dtpm_cpu;

	ret = power_add(dtpm, pd);
	if (ret)
		goto out_dtpm_unregister;

	ret = freq_qos_add_request(&policy->constraints,
				   &dtpm_cpu->qos_req, FREQ_QOS_MAX,
				   pd->table[pd->nr_perf_states - 1].frequency);
	if (ret)
		goto out_power_sub;

	return 0;

out_power_sub:
	power_sub(dtpm, pd);

out_dtpm_unregister:
	dtpm_unregister(dtpm);
	dtpm_cpu = NULL;
	dtpm = NULL;

out_kfree_dtpm_cpu:
	for_each_cpu(cpu, policy->related_cpus)
		per_cpu(dtpm_per_cpu, cpu) = NULL;
	kfree(dtpm_cpu);

out_kfree_dtpm:
	kfree(dtpm);
	return ret;
}

int dtpm_register_cpu(struct dtpm *parent)
{
	__parent = parent;

	return cpuhp_setup_state(CPUHP_AP_DTPM_CPU_ONLINE,
				 "dtpm_cpu:online",
				 cpuhp_dtpm_cpu_online,
				 cpuhp_dtpm_cpu_offline);
}
