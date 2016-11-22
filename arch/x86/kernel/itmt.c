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
#include <asm/mutex.h>
#include <linux/sched.h>
#include <linux/sysctl.h>
#include <linux/nodemask.h>

static DEFINE_MUTEX(itmt_update_mutex);
DEFINE_PER_CPU_READ_MOSTLY(int, sched_core_priority);

/* Boolean to track if system has ITMT capabilities */
static bool __read_mostly sched_itmt_capable;

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
 */
void sched_set_itmt_support(void)
{
	mutex_lock(&itmt_update_mutex);

	sched_itmt_capable = true;

	mutex_unlock(&itmt_update_mutex);
}

/**
 * sched_clear_itmt_support() - Revoke platform's support of ITMT
 *
 * This function is used by the OS to indicate that it has
 * revoked the platform's support of ITMT feature.
 *
 */
void sched_clear_itmt_support(void)
{
	mutex_lock(&itmt_update_mutex);

	sched_itmt_capable = false;

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
