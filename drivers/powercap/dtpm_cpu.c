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
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/cpumask.h>
#include <linux/cpufreq.h>
#include <linux/cpuhotplug.h>
#include <linux/dtpm.h>
#include <linux/energy_model.h>
#include <linux/pm_qos.h>
#include <linux/slab.h>
#include <linux/units.h>

struct dtpm_cpu {
	struct dtpm dtpm;
	struct freq_qos_request qos_req;
	int cpu;
};

static DEFINE_PER_CPU(struct dtpm_cpu *, dtpm_per_cpu);

static struct dtpm_cpu *to_dtpm_cpu(struct dtpm *dtpm)
{
	return container_of(dtpm, struct dtpm_cpu, dtpm);
}

static u64 set_pd_power_limit(struct dtpm *dtpm, u64 power_limit)
{
	struct dtpm_cpu *dtpm_cpu = to_dtpm_cpu(dtpm);
	struct em_perf_domain *pd = em_cpu_get(dtpm_cpu->cpu);
	struct cpumask cpus;
	unsigned long freq;
	u64 power;
	int i, nr_cpus;

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

static u64 scale_pd_power_uw(struct cpumask *pd_mask, u64 power)
{
	unsigned long max = 0, sum_util = 0;
	int cpu;

	for_each_cpu_and(cpu, pd_mask, cpu_online_mask) {

		/*
		 * The capacity is the same for all CPUs belonging to
		 * the same perf domain, so a single call to
		 * arch_scale_cpu_capacity() is enough. However, we
		 * need the CPU parameter to be initialized by the
		 * loop, so the call ends up in this block.
		 *
		 * We can initialize 'max' with a cpumask_first() call
		 * before the loop but the bits computation is not
		 * worth given the arch_scale_cpu_capacity() just
		 * returns a value where the resulting assembly code
		 * will be optimized by the compiler.
		 */
		max = arch_scale_cpu_capacity(cpu);
		sum_util += sched_cpu_util(cpu, max);
	}

	/*
	 * In the improbable case where all the CPUs of the perf
	 * domain are offline, 'max' will be zero and will lead to an
	 * illegal operation with a zero division.
	 */
	return max ? (power * ((sum_util << 10) / max)) >> 10 : 0;
}

static u64 get_pd_power_uw(struct dtpm *dtpm)
{
	struct dtpm_cpu *dtpm_cpu = to_dtpm_cpu(dtpm);
	struct em_perf_domain *pd;
	struct cpumask *pd_mask;
	unsigned long freq;
	int i;

	pd = em_cpu_get(dtpm_cpu->cpu);

	pd_mask = em_span_cpus(pd);

	freq = cpufreq_quick_get(dtpm_cpu->cpu);

	for (i = 0; i < pd->nr_perf_states; i++) {

		if (pd->table[i].frequency < freq)
			continue;

		return scale_pd_power_uw(pd_mask, pd->table[i].power *
					 MICROWATT_PER_MILLIWATT);
	}

	return 0;
}

static int update_pd_power_uw(struct dtpm *dtpm)
{
	struct dtpm_cpu *dtpm_cpu = to_dtpm_cpu(dtpm);
	struct em_perf_domain *em = em_cpu_get(dtpm_cpu->cpu);
	struct cpumask cpus;
	int nr_cpus;

	cpumask_and(&cpus, cpu_online_mask, to_cpumask(em->cpus));
	nr_cpus = cpumask_weight(&cpus);

	dtpm->power_min = em->table[0].power;
	dtpm->power_min *= MICROWATT_PER_MILLIWATT;
	dtpm->power_min *= nr_cpus;

	dtpm->power_max = em->table[em->nr_perf_states - 1].power;
	dtpm->power_max *= MICROWATT_PER_MILLIWATT;
	dtpm->power_max *= nr_cpus;

	return 0;
}

static void pd_release(struct dtpm *dtpm)
{
	struct dtpm_cpu *dtpm_cpu = to_dtpm_cpu(dtpm);

	if (freq_qos_request_active(&dtpm_cpu->qos_req))
		freq_qos_remove_request(&dtpm_cpu->qos_req);

	kfree(dtpm_cpu);
}

static struct dtpm_ops dtpm_ops = {
	.set_power_uw	 = set_pd_power_limit,
	.get_power_uw	 = get_pd_power_uw,
	.update_power_uw = update_pd_power_uw,
	.release	 = pd_release,
};

static int cpuhp_dtpm_cpu_offline(unsigned int cpu)
{
	struct dtpm_cpu *dtpm_cpu;

	dtpm_cpu = per_cpu(dtpm_per_cpu, cpu);
	if (dtpm_cpu)
		dtpm_update_power(&dtpm_cpu->dtpm);

	return 0;
}

static int cpuhp_dtpm_cpu_online(unsigned int cpu)
{
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

	dtpm_cpu = per_cpu(dtpm_per_cpu, cpu);
	if (dtpm_cpu)
		return dtpm_update_power(&dtpm_cpu->dtpm);

	dtpm_cpu = kzalloc(sizeof(*dtpm_cpu), GFP_KERNEL);
	if (!dtpm_cpu)
		return -ENOMEM;

	dtpm_init(&dtpm_cpu->dtpm, &dtpm_ops);
	dtpm_cpu->cpu = cpu;

	for_each_cpu(cpu, policy->related_cpus)
		per_cpu(dtpm_per_cpu, cpu) = dtpm_cpu;

	snprintf(name, sizeof(name), "cpu%d-cpufreq", dtpm_cpu->cpu);

	ret = dtpm_register(name, &dtpm_cpu->dtpm, NULL);
	if (ret)
		goto out_kfree_dtpm_cpu;

	ret = freq_qos_add_request(&policy->constraints,
				   &dtpm_cpu->qos_req, FREQ_QOS_MAX,
				   pd->table[pd->nr_perf_states - 1].frequency);
	if (ret)
		goto out_dtpm_unregister;

	return 0;

out_dtpm_unregister:
	dtpm_unregister(&dtpm_cpu->dtpm);
	dtpm_cpu = NULL;

out_kfree_dtpm_cpu:
	for_each_cpu(cpu, policy->related_cpus)
		per_cpu(dtpm_per_cpu, cpu) = NULL;
	kfree(dtpm_cpu);

	return ret;
}

static int __init dtpm_cpu_init(void)
{
	int ret;

	/*
	 * The callbacks at CPU hotplug time are calling
	 * dtpm_update_power() which in turns calls update_pd_power().
	 *
	 * The function update_pd_power() uses the online mask to
	 * figure out the power consumption limits.
	 *
	 * At CPUHP_AP_ONLINE_DYN, the CPU is present in the CPU
	 * online mask when the cpuhp_dtpm_cpu_online function is
	 * called, but the CPU is still in the online mask for the
	 * tear down callback. So the power can not be updated when
	 * the CPU is unplugged.
	 *
	 * At CPUHP_AP_DTPM_CPU_DEAD, the situation is the opposite as
	 * above. The CPU online mask is not up to date when the CPU
	 * is plugged in.
	 *
	 * For this reason, we need to call the online and offline
	 * callbacks at different moments when the CPU online mask is
	 * consistent with the power numbers we want to update.
	 */
	ret = cpuhp_setup_state(CPUHP_AP_DTPM_CPU_DEAD, "dtpm_cpu:offline",
				NULL, cpuhp_dtpm_cpu_offline);
	if (ret < 0)
		return ret;

	ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "dtpm_cpu:online",
				cpuhp_dtpm_cpu_online, NULL);
	if (ret < 0)
		return ret;

	return 0;
}

struct dtpm_subsys_ops dtpm_cpu_ops = {
	.name = KBUILD_MODNAME,
	.init = dtpm_cpu_init,
};
