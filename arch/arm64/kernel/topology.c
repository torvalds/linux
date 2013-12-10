/*
 * arch/arm64/kernel/topology.c
 *
 * Copyright (C) 2011,2013 Linaro Limited.
 * Written by: Vincent Guittot
 *
 * based on arch/sh/kernel/topology.c
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/percpu.h>
#include <linux/node.h>
#include <linux/nodemask.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include <asm/cputype.h>
#include <asm/smp_plat.h>
#include <asm/topology.h>

/*
 * cpu topology table
 */
struct cputopo_arm cpu_topology[NR_CPUS];
EXPORT_SYMBOL_GPL(cpu_topology);

const struct cpumask *cpu_coregroup_mask(int cpu)
{
	return &cpu_topology[cpu].core_sibling;
}

static void update_siblings_masks(unsigned int cpuid)
{
	struct cputopo_arm *cpu_topo, *cpuid_topo = &cpu_topology[cpuid];
	int cpu;

	/* update core and thread sibling masks */
	for_each_possible_cpu(cpu) {
		cpu_topo = &cpu_topology[cpu];

		if (cpuid_topo->socket_id != cpu_topo->socket_id)
			continue;

		cpumask_set_cpu(cpuid, &cpu_topo->core_sibling);
		if (cpu != cpuid)
			cpumask_set_cpu(cpu, &cpuid_topo->core_sibling);

		if (cpuid_topo->core_id != cpu_topo->core_id)
			continue;

		cpumask_set_cpu(cpuid, &cpu_topo->thread_sibling);
		if (cpu != cpuid)
			cpumask_set_cpu(cpu, &cpuid_topo->thread_sibling);
	}
	smp_wmb();
}

void store_cpu_topology(unsigned int cpuid)
{
	struct cputopo_arm *cpuid_topo = &cpu_topology[cpuid];

	/* DT should have been parsed by the time we get here */
	if (cpuid_topo->core_id == -1)
		pr_info("CPU%u: No topology information configured\n", cpuid);
	else
		update_siblings_masks(cpuid);
}


/*
 * cluster_to_logical_mask - return cpu logical mask of CPUs in a cluster
 * @socket_id:		cluster HW identifier
 * @cluster_mask:	the cpumask location to be initialized, modified by the
 *			function only if return value == 0
 *
 * Return:
 *
 * 0 on success
 * -EINVAL if cluster_mask is NULL or there is no record matching socket_id
 */
int cluster_to_logical_mask(unsigned int socket_id, cpumask_t *cluster_mask)
{
	int cpu;

	if (!cluster_mask)
		return -EINVAL;

	for_each_online_cpu(cpu) {
		if (socket_id == topology_physical_package_id(cpu)) {
			cpumask_copy(cluster_mask, topology_core_cpumask(cpu));
			return 0;
		}
	}

	return -EINVAL;
}

/*
 * init_cpu_topology is called at boot when only one cpu is running
 * which prevent simultaneous write access to cpu_topology array
 */
void __init init_cpu_topology(void)
{
	unsigned int cpu;

	/* init core mask and power*/
	for_each_possible_cpu(cpu) {
		struct cputopo_arm *cpu_topo = &(cpu_topology[cpu]);

		cpu_topo->thread_id = -1;
		cpu_topo->core_id =  -1;
		cpu_topo->socket_id = -1;
		cpumask_clear(&cpu_topo->core_sibling);
		cpumask_clear(&cpu_topo->thread_sibling);
	}
	smp_wmb();
}
