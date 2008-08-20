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

static inline cpumask_t node_to_cpumask(int node)
{
	cpumask_t node_cpu_mask = CPU_MASK_NONE;
	int cpu;

	for_each_online_cpu(cpu) {
		if (cpu_to_node(cpu) == node)
			cpu_set(cpu, node_cpu_mask);
	}

#ifdef DEBUG_NUMA
	printk("node %d: cpu_mask: %016lx\n", node, node_cpu_mask);
#endif

	return node_cpu_mask;
}

#define pcibus_to_cpumask(bus)	(cpu_online_map)

#endif /* !CONFIG_NUMA */
# include <asm-generic/topology.h>

#endif /* _ASM_ALPHA_TOPOLOGY_H */
