// SPDX-License-Identifier: GPL-2.0
/*
 * Legacy Energy Model loading driver
 *
 * Copyright (C) 2018, ARM Ltd.
 * Written by: Quentin Perret, ARM Ltd.
 */

#define pr_fmt(fmt) "legacy-dt-em: " fmt

#include <linux/cpufreq.h>
#include <linux/cpumask.h>
#include <linux/cpuset.h>
#include <linux/energy_model.h>
#include <linux/gfp.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/printk.h>
#include <linux/slab.h>

static cpumask_var_t cpus_to_visit;

static DEFINE_PER_CPU(unsigned long, nr_states) = 0;

struct em_state {
	unsigned long frequency;
	unsigned long power;
	unsigned long capacity;
};
static DEFINE_PER_CPU(struct em_state*, cpu_em) = NULL;

static void finish_em_loading_workfn(struct work_struct *work);
static DECLARE_WORK(finish_em_loading_work, finish_em_loading_workfn);

static DEFINE_MUTEX(em_loading_mutex);

/*
 * Callback given to the EM framework. All this does is browse the table
 * created by legacy_em_dt().
 */
static int get_power(unsigned long *mW, unsigned long *KHz, int cpu)
{
	unsigned long nstates = per_cpu(nr_states, cpu);
	struct em_state *em = per_cpu(cpu_em, cpu);
	int i;

	if (!nstates || !em)
		return -ENODEV;

	for (i = 0; i < nstates - 1; i++) {
		if (em[i].frequency > *KHz)
			break;
	}

	*KHz = em[i].frequency;
	*mW = em[i].power;

	return 0;
}

static int init_em_dt_callback(struct notifier_block *nb, unsigned long val,
			       void *data)
{
	struct em_data_callback em_cb = EM_DATA_CB(get_power);
	unsigned long nstates, scale_cpu, max_freq;
	struct cpufreq_policy *policy = data;
	const struct property *prop;
	struct device_node *cn, *cp;
	struct em_state *em;
	int cpu, i, ret = 0;
	const __be32 *tmp;

	if (val != CPUFREQ_NOTIFY)
		return 0;

	mutex_lock(&em_loading_mutex);

	/* Do not register twice an energy model */
	for_each_cpu(cpu, policy->cpus) {
		if (per_cpu(nr_states, cpu) || per_cpu(cpu_em, cpu)) {
			pr_err("EM of CPU%d already loaded\n", cpu);
			ret = -EEXIST;
			goto unlock;
		}
	}

	max_freq = policy->cpuinfo.max_freq;
	if (!max_freq) {
		pr_err("No policy->max for CPU%d\n", cpu);
		ret = -EINVAL;
		goto unlock;
	}

	cpu = cpumask_first(policy->cpus);
	cn = of_get_cpu_node(cpu, NULL);
	if (!cn) {
		pr_err("No device_node for CPU%d\n", cpu);
		ret = -ENODEV;
		goto unlock;
	}

	cp = of_parse_phandle(cn, "sched-energy-costs", 0);
	if (!cp) {
		pr_err("CPU%d node has no sched-energy-costs\n", cpu);
		ret = -ENODEV;
		goto unlock;
	}

	prop = of_find_property(cp, "busy-cost-data", NULL);
	if (!prop || !prop->value) {
		pr_err("No busy-cost-data for CPU%d\n", cpu);
		ret = -ENODEV;
		goto unlock;
	}

	nstates = (prop->length / sizeof(u32)) / 2;
	em = kcalloc(nstates, sizeof(struct em_cap_state), GFP_KERNEL);
	if (!em) {
		ret = -ENOMEM;
		goto unlock;
	}

	/* Copy the capacity and power cost to the table. */
	for (i = 0, tmp = prop->value; i < nstates; i++) {
		em[i].capacity = be32_to_cpup(tmp++);
		em[i].power = be32_to_cpup(tmp++);
	}

	/* Get the CPU capacity (according to the EM) */
	scale_cpu = em[nstates - 1].capacity;
	if (!scale_cpu) {
		pr_err("CPU%d: capacity cannot be 0\n", cpu);
		kfree(em);
		ret = -EINVAL;
		goto unlock;
	}

	/* Re-compute the intermediate frequencies based on the EM. */
	for (i = 0; i < nstates; i++)
		em[i].frequency = em[i].capacity * max_freq / scale_cpu;

	/* Assign the table to all CPUs of this policy. */
	for_each_cpu(i, policy->cpus) {
		per_cpu(nr_states, i) = nstates;
		per_cpu(cpu_em, i) = em;
	}

	pr_info("Registering EM of %*pbl\n", cpumask_pr_args(policy->cpus));
	em_register_perf_domain(policy->cpus, nstates, &em_cb);

	/* Finish the work when all possible CPUs have been registered. */
	cpumask_andnot(cpus_to_visit, cpus_to_visit, policy->cpus);
	if (cpumask_empty(cpus_to_visit))
		schedule_work(&finish_em_loading_work);

unlock:
	mutex_unlock(&em_loading_mutex);

	return ret;
}

static struct notifier_block init_em_dt_notifier = {
	.notifier_call = init_em_dt_callback,
};

static void finish_em_loading_workfn(struct work_struct *work)
{
	cpufreq_unregister_notifier(&init_em_dt_notifier,
				    CPUFREQ_POLICY_NOTIFIER);
	free_cpumask_var(cpus_to_visit);

	/* Let the scheduler know the Energy Model is ready. */
	rebuild_sched_domains();
}

static int __init register_cpufreq_notifier(void)
{
	int ret;

	if (!alloc_cpumask_var(&cpus_to_visit, GFP_KERNEL))
		return -ENOMEM;

	cpumask_copy(cpus_to_visit, cpu_possible_mask);

	ret = cpufreq_register_notifier(&init_em_dt_notifier,
					CPUFREQ_POLICY_NOTIFIER);

	if (ret)
		free_cpumask_var(cpus_to_visit);

	return ret;
}
core_initcall(register_cpufreq_notifier);
