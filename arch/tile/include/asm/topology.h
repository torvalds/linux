/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#ifndef _ASM_TILE_TOPOLOGY_H
#define _ASM_TILE_TOPOLOGY_H

#ifdef CONFIG_NUMA

#include <linux/cpumask.h>

/* Mappings between logical cpu number and node number. */
extern struct cpumask node_2_cpu_mask[];
extern char cpu_2_node[];

/* Returns the number of the node containing CPU 'cpu'. */
static inline int cpu_to_node(int cpu)
{
	return cpu_2_node[cpu];
}

/*
 * Returns the number of the node containing Node 'node'.
 * This architecture is flat, so it is a pretty simple function!
 */
#define parent_node(node) (node)

/* Returns a bitmask of CPUs on Node 'node'. */
static inline const struct cpumask *cpumask_of_node(int node)
{
	return &node_2_cpu_mask[node];
}

/* For now, use numa node -1 for global allocation. */
#define pcibus_to_node(bus)		((void)(bus), -1)

/* sched_domains SD_NODE_INIT for TILE architecture */
#define SD_NODE_INIT (struct sched_domain) {		\
	.min_interval		= 8,			\
	.max_interval		= 32,			\
	.busy_factor		= 32,			\
	.imbalance_pct		= 125,			\
	.cache_nice_tries	= 1,			\
	.busy_idx		= 3,			\
	.idle_idx		= 1,			\
	.newidle_idx		= 2,			\
	.wake_idx		= 1,			\
	.flags			= SD_LOAD_BALANCE	\
				| SD_BALANCE_NEWIDLE	\
				| SD_BALANCE_EXEC	\
				| SD_BALANCE_FORK	\
				| SD_WAKE_AFFINE	\
				| SD_SERIALIZE,		\
	.last_balance		= jiffies,		\
	.balance_interval	= 1,			\
}

/* By definition, we create nodes based on online memory. */
#define node_has_online_mem(nid) 1

#endif /* CONFIG_NUMA */

#include <asm-generic/topology.h>

#ifdef CONFIG_SMP
#define topology_physical_package_id(cpu)       ((void)(cpu), 0)
#define topology_core_id(cpu)                   (cpu)
#define topology_core_cpumask(cpu)              ((void)(cpu), cpu_online_mask)
#define topology_thread_cpumask(cpu)            cpumask_of(cpu)

/* indicates that pointers to the topology struct cpumask maps are valid */
#define arch_provides_topology_pointers         yes
#endif

#endif /* _ASM_TILE_TOPOLOGY_H */
