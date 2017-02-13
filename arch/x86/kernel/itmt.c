/*
 * itmt.c: Support Intel Turbo Boost Max Technology 3.0
 *
 * (C) Copyright 2016 Intel Corporation
 * Author: Tim Chen <tim.c.chen@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 *
 * On platforms supporting Intel Turbo Boost Max Technology 3.0, (ITMT),
 * the maximum turbo frequencies of some cores in a CPU package may be
 * higher than for the other cores in the same package.  In that case,
 * better performance can be achieved by making the scheduler prefer
 * to run tasks on the CPUs with higher max turbo frequencies.
 *
 * This file provides functions and data structures for enabling the
 * scheduler to favor scheduling on cores can be boosted to a higher
 * frequency under ITMT.
 */

#include <linux/sched.h>
#include <linux/cpumask.h>
#include <linux/cpuset.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/sysctl.h>
#include <linux/nodemask.h>

static DEFINE_MUTEX(itmt_update_mutex);
DEFINE_PER_CPU_READ_MOSTLY(int, sched_core_priority);

/* Boolean to track if system has ITMT capabilities */
static bool __read_mostly sched_itmt_capable;

/*
 * Boolean to control whether we want to move processes to cpu capable
 * of higher turbo frequency for cpus supporting Intel Turbo Boost Max
 * Technology 3.0.
 *
 * It can be set via /proc/sys/kernel/sched_itmt_enabled
 */
unsigned int __read_mostly sysctl_sched_itmt_enabled;

static int sched_itmt_update_handler(struct ctl_table *table, int write,
				     void __user *buffer, size_t *lenp,
				     loff_t *ppos)
{
	unsigned int old_sysctl;
	int ret;

	mutex_lock(&itmt_update_mutex);

	if (!sched_itmt_capable) {
		mutex_unlock(&itmt_update_mutex);
		return -EINVAL;
	}

	old_sysctl = sysctl_sched_itmt_enabled;
	ret = proc_dointvec_minmax(table, write, buffer, lenp, ppos);

	if (!ret && write && old_sysctl != sysctl_sched_itmt_enabled) {
		x86_topology_update = true;
		rebuild_sched_domains();
	}

	mutex_unlock(&itmt_update_mutex);

	return ret;
}

static unsigned int zero;
static unsigned int one = 1;
static struct ctl_table itmt_kern_table[] = {
	{
		.procname	= "sched_itmt_enabled",
		.data		= &sysctl_sched_itmt_enabled,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= sched_itmt_update_handler,
		.extra1		= &zero,
		.extra2		= &one,
	},
	{}
};

static struct ctl_table itmt_root_table[] = {
	{
		.procname	= "kernel",
		.mode		= 0555,
		.child		= itmt_kern_table,
	},
	{}
};

static struct ctl_table_header *itmt_sysctl_header;

/**
 * sched_set_itmt_support() - Indicate platform supports ITMT
 *
 * This function is used by the OS to indicate to scheduler that the platform
 * is capable of supporting the ITMT feature.
 *
 * The current scheme has the pstate driver detects if the system
 * is ITMT capable and call sched_set_itmt_support.
 *
 * This must be done only after sched_set_itmt_core_prio
 * has been called to set the cpus' priorities.
 * It must not be called with cpu hot plug lock
 * held as we need to acquire the lock to rebuild sched domains
 * later.
 *
 * Return: 0 on success
 */
int sched_set_itmt_support(void)
{
	mutex_lock(&itmt_update_mutex);

	if (sched_itmt_capable) {
		mutex_unlock(&itmt_update_mutex);
		return 0;
	}

	itmt_sysctl_header = register_sysctl_table(itmt_root_table);
	if (!itmt_sysctl_header) {
		mutex_unlock(&itmt_update_mutex);
		return -ENOMEM;
	}

	sched_itmt_capable = true;

	sysctl_sched_itmt_enabled = 1;

	if (sysctl_sched_itmt_enabled) {
		x86_topology_update = true;
		rebuild_sched_domains();
	}

	mutex_unlock(&itmt_update_mutex);

	return 0;
}

/**
 * sched_clear_itmt_support() - Revoke platform's support of ITMT
 *
 * This function is used by the OS to indicate that it has
 * revoked the platform's support of ITMT feature.
 *
 * It must not be called with cpu hot plug lock
 * held as we need to acquire the lock to rebuild sched domains
 * later.
 */
void sched_clear_itmt_support(void)
{
	mutex_lock(&itmt_update_mutex);

	if (!sched_itmt_capable) {
		mutex_unlock(&itmt_update_mutex);
		return;
	}
	sched_itmt_capable = false;

	if (itmt_sysctl_header) {
		unregister_sysctl_table(itmt_sysctl_header);
		itmt_sysctl_header = NULL;
	}

	if (sysctl_sched_itmt_enabled) {
		/* disable sched_itmt if we are no longer ITMT capable */
		sysctl_sched_itmt_enabled = 0;
		x86_topology_update = true;
		rebuild_sched_domains();
	}

	mutex_unlock(&itmt_update_mutex);
}

int arch_asym_cpu_priority(int cpu)
{
	return per_cpu(sched_core_priority, cpu);
}

/**
 * sched_set_itmt_core_prio() - Set CPU priority based on ITMT
 * @prio:	Priority of cpu core
 * @core_cpu:	The cpu number associated with the core
 *
 * The pstate driver will find out the max boost frequency
 * and call this function to set a priority proportional
 * to the max boost frequency. CPU with higher boost
 * frequency will receive higher priority.
 *
 * No need to rebuild sched domain after updating
 * the CPU priorities. The sched domains have no
 * dependency on CPU priorities.
 */
void sched_set_itmt_core_prio(int prio, int core_cpu)
{
	int cpu, i = 1;

	for_each_cpu(cpu, topology_sibling_cpumask(core_cpu)) {
		int smt_prio;

		/*
		 * Ensure that the siblings are moved to the end
		 * of the priority chain and only used when
		 * all other high priority cpus are out of capacity.
		 */
		smt_prio = prio * smp_num_siblings / i;
		per_cpu(sched_core_priority, cpu) = smt_prio;
		i++;
	}
}
