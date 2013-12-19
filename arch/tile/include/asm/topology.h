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

/*
 * TILE architecture has many cores integrated in one processor, so we need
 * setup bigger balance_interval for both CPU/NODE scheduling domains to
 * reduce process scheduling costs.
 */

/* sched_domains SD_CPU_INIT for TILE architecture */
#define SD_CPU_INIT (struct sched_domain) {				\
	.min_interval		= 4,					\
	.max_interval		= 128,					\
	.busy_factor		= 64,					\
	.imbalance_pct		= 125,					\
	.cache_nice_tries	= 1,					\
	.busy_idx		= 2,					\
	.idle_idx		= 1,					\
	.newidle_idx		= 0,					\
	.wake_idx		= 0,					\
	.forkexec_idx		= 0,					\
									\
	.flags			= 1*SD_LOAD_BALANCE			\
				| 1*SD_BALANCE_NEWIDLE			\
				| 1*SD_BALANCE_EXEC			\
				| 1*SD_BALANCE_FORK			\
				| 0*SD_BALANCE_WAKE			\
				| 0*SD_WAKE_AFFINE			\
				| 0*SD_SHARE_CPUPOWER			\
				| 0*SD_SHARE_PKG_RESOURCES		\
				| 0*SD_SERIALIZE			\
				,					\
	.last_balance		= jiffies,				\
	.balance_interval	= 32,					\
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
#endif

#endif /* _ASM_TILE_TOPOLOGY_H */
