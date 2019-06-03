// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *
 * ia64 kernel NUMA specific stuff
 *
 * Copyright (C) 2002 Erich Focht <efocht@ess.nec.de>
 * Copyright (C) 2004 Silicon Graphics, Inc.
 *   Jesse Barnes <jbarnes@sgi.com>
 */
#include <linux/topology.h>
#include <linux/module.h>
#include <asm/processor.h>
#include <asm/smp.h>

u16 cpu_to_node_map[NR_CPUS] __cacheline_aligned;
EXPORT_SYMBOL(cpu_to_node_map);

cpumask_t node_to_cpu_mask[MAX_NUMNODES] __cacheline_aligned;
EXPORT_SYMBOL(node_to_cpu_mask);

void map_cpu_to_node(int cpu, int nid)
{
	int oldnid;
	if (nid < 0) { /* just initialize by zero */
		cpu_to_node_map[cpu] = 0;
		return;
	}
	/* sanity check first */
	oldnid = cpu_to_node_map[cpu];
	if (cpumask_test_cpu(cpu, &node_to_cpu_mask[oldnid])) {
		return; /* nothing to do */
	}
	/* we don't have cpu-driven node hot add yet...
	   In usual case, node is created from SRAT at boot time. */
	if (!node_online(nid))
		nid = first_online_node;
	cpu_to_node_map[cpu] = nid;
	cpumask_set_cpu(cpu, &node_to_cpu_mask[nid]);
	return;
}

void unmap_cpu_from_node(int cpu, int nid)
{
	WARN_ON(!cpumask_test_cpu(cpu, &node_to_cpu_mask[nid]));
	WARN_ON(cpu_to_node_map[cpu] != nid);
	cpu_to_node_map[cpu] = 0;
	cpumask_clear_cpu(cpu, &node_to_cpu_mask[nid]);
}


/**
 * build_cpu_to_node_map - setup cpu to node and node to cpumask arrays
 *
 * Build cpu to node mapping and initialize the per node cpu masks using
 * info from the node_cpuid array handed to us by ACPI.
 */
void __init build_cpu_to_node_map(void)
{
	int cpu, i, node;

	for(node=0; node < MAX_NUMNODES; node++)
		cpumask_clear(&node_to_cpu_mask[node]);

	for_each_possible_early_cpu(cpu) {
		node = NUMA_NO_NODE;
		for (i = 0; i < NR_CPUS; ++i)
			if (cpu_physical_id(cpu) == node_cpuid[i].phys_id) {
				node = node_cpuid[i].nid;
				break;
			}
		map_cpu_to_node(cpu, node);
	}
}
