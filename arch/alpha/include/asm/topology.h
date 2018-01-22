/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_ALPHA_TOPOLOGY_H
#define _ASM_ALPHA_TOPOLOGY_H

#include <linux/smp.h>
#include <linux/threads.h>
#include <asm/machvec.h>

#ifdef CONFIG_NUMA
static inline int cpu_to_node(int cpu)
{
	int node;
	
	if (!alpha_mv.cpuid_to_nid)
		return 0;

	node = alpha_mv.cpuid_to_nid(cpu);

#ifdef DEBUG_NUMA
	BUG_ON(node < 0);
#endif

	return node;
}

extern struct cpumask node_to_cpumask_map[];
/* FIXME: This is dumb, recalculating every time.  But simple. */
static const struct cpumask *cpumask_of_node(int node)
{
	int cpu;

	if (node == -1)
		return cpu_all_mask;

	cpumask_clear(&node_to_cpumask_map[node]);

	for_each_online_cpu(cpu) {
		if (cpu_to_node(cpu) == node)
			cpumask_set_cpu(cpu, node_to_cpumask_map[node]);
	}

	return &node_to_cpumask_map[node];
}

#define cpumask_of_pcibus(bus)	(cpu_online_mask)

#endif /* !CONFIG_NUMA */
# include <asm-generic/topology.h>

#endif /* _ASM_ALPHA_TOPOLOGY_H */
