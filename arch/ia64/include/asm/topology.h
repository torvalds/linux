/*
 * Copyright (C) 2002, Erich Focht, NEC
 *
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef _ASM_IA64_TOPOLOGY_H
#define _ASM_IA64_TOPOLOGY_H

#include <asm/acpi.h>
#include <asm/numa.h>
#include <asm/smp.h>

#ifdef CONFIG_NUMA

/* Nodes w/o CPUs are preferred for memory allocations, see build_zonelists */
#define PENALTY_FOR_NODE_WITH_CPUS 255

/*
 * Distance above which we begin to use zone reclaim
 */
#define RECLAIM_DISTANCE 15

/*
 * Returns the number of the node containing CPU 'cpu'
 */
#define cpu_to_node(cpu) (int)(cpu_to_node_map[cpu])

/*
 * Returns a bitmask of CPUs on Node 'node'.
 */
#define node_to_cpumask(node) (node_to_cpu_mask[node])

/*
 * Returns the number of the node containing Node 'nid'.
 * Not implemented here. Multi-level hierarchies detected with
 * the help of node_distance().
 */
#define parent_node(nid) (nid)

/*
 * Returns the number of the first CPU on Node 'node'.
 */
#define node_to_first_cpu(node) (first_cpu(node_to_cpumask(node)))

/*
 * Determines the node for a given pci bus
 */
#define pcibus_to_node(bus) PCI_CONTROLLER(bus)->node

void build_cpu_to_node_map(void);

#define SD_CPU_INIT (struct sched_domain) {		\
	.span			= CPU_MASK_NONE,	\
	.parent			= NULL,			\
	.child			= NULL,			\
	.groups			= NULL,			\
	.min_interval		= 1,			\
	.max_interval		= 4,			\
	.busy_factor		= 64,			\
	.imbalance_pct		= 125,			\
	.cache_nice_tries	= 2,			\
	.busy_idx		= 2,			\
	.idle_idx		= 1,			\
	.newidle_idx		= 2,			\
	.wake_idx		= 1,			\
	.forkexec_idx		= 1,			\
	.flags			= SD_LOAD_BALANCE	\
				| SD_BALANCE_NEWIDLE	\
				| SD_BALANCE_EXEC	\
				| SD_WAKE_AFFINE,	\
	.last_balance		= jiffies,		\
	.balance_interval	= 1,			\
	.nr_balance_failed	= 0,			\
}

/* sched_domains SD_NODE_INIT for IA64 NUMA machines */
#define SD_NODE_INIT (struct sched_domain) {		\
	.span			= CPU_MASK_NONE,	\
	.parent			= NULL,			\
	.child			= NULL,			\
	.groups			= NULL,			\
	.min_interval		= 8,			\
	.max_interval		= 8*(min(num_online_cpus(), 32)), \
	.busy_factor		= 64,			\
	.imbalance_pct		= 125,			\
	.cache_nice_tries	= 2,			\
	.busy_idx		= 3,			\
	.idle_idx		= 2,			\
	.newidle_idx		= 2,			\
	.wake_idx		= 1,			\
	.forkexec_idx		= 1,			\
	.flags			= SD_LOAD_BALANCE	\
				| SD_BALANCE_EXEC	\
				| SD_BALANCE_FORK	\
				| SD_SERIALIZE		\
				| SD_WAKE_BALANCE,	\
	.last_balance		= jiffies,		\
	.balance_interval	= 64,			\
	.nr_balance_failed	= 0,			\
}

#endif /* CONFIG_NUMA */

#ifdef CONFIG_SMP
#define topology_physical_package_id(cpu)	(cpu_data(cpu)->socket_id)
#define topology_core_id(cpu)			(cpu_data(cpu)->core_id)
#define topology_core_siblings(cpu)		(cpu_core_map[cpu])
#define topology_thread_siblings(cpu)		(per_cpu(cpu_sibling_map, cpu))
#define smt_capable() 				(smp_num_siblings > 1)
#endif

extern void arch_fix_phys_package_id(int num, u32 slot);

#define pcibus_to_cpumask(bus)	(pcibus_to_node(bus) == -1 ? \
					CPU_MASK_ALL : \
					node_to_cpumask(pcibus_to_node(bus)) \
				)

#include <asm-generic/topology.h>

#endif /* _ASM_IA64_TOPOLOGY_H */
