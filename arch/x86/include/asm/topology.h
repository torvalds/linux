/*
 * Written by: Matthew Dobson, IBM Corporation
 *
 * Copyright (C) 2002, IBM Corp.
 *
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Send feedback to <colpatch@us.ibm.com>
 */
#ifndef _ASM_X86_TOPOLOGY_H
#define _ASM_X86_TOPOLOGY_H

#ifdef CONFIG_X86_32
# ifdef CONFIG_X86_HT
#  define ENABLE_TOPO_DEFINES
# endif
#else
# ifdef CONFIG_SMP
#  define ENABLE_TOPO_DEFINES
# endif
#endif

/* Node not present */
#define NUMA_NO_NODE	(-1)

#ifdef CONFIG_NUMA
#include <linux/cpumask.h>
#include <asm/mpspec.h>

#ifdef CONFIG_X86_32

/* Mappings between node number and cpus on that node. */
extern cpumask_t node_to_cpumask_map[];

/* Mappings between logical cpu number and node number */
extern int cpu_to_node_map[];

/* Returns the number of the node containing CPU 'cpu' */
static inline int cpu_to_node(int cpu)
{
	return cpu_to_node_map[cpu];
}
#define early_cpu_to_node(cpu)	cpu_to_node(cpu)

/* Returns a bitmask of CPUs on Node 'node'.
 *
 * Side note: this function creates the returned cpumask on the stack
 * so with a high NR_CPUS count, excessive stack space is used.  The
 * cpumask_of_node function should be used whenever possible.
 */
static inline cpumask_t node_to_cpumask(int node)
{
	return node_to_cpumask_map[node];
}

/* Returns a bitmask of CPUs on Node 'node'. */
static inline const struct cpumask *cpumask_of_node(int node)
{
	return &node_to_cpumask_map[node];
}

#else /* CONFIG_X86_64 */

/* Mappings between node number and cpus on that node. */
extern cpumask_t *node_to_cpumask_map;

/* Mappings between logical cpu number and node number */
DECLARE_EARLY_PER_CPU(int, x86_cpu_to_node_map);

/* Returns the number of the current Node. */
#define numa_node_id()		read_pda(nodenumber)

#ifdef CONFIG_DEBUG_PER_CPU_MAPS
extern int cpu_to_node(int cpu);
extern int early_cpu_to_node(int cpu);
extern const cpumask_t *cpumask_of_node(int node);
extern cpumask_t node_to_cpumask(int node);

#else	/* !CONFIG_DEBUG_PER_CPU_MAPS */

/* Returns the number of the node containing CPU 'cpu' */
static inline int cpu_to_node(int cpu)
{
	return per_cpu(x86_cpu_to_node_map, cpu);
}

/* Same function but used if called before per_cpu areas are setup */
static inline int early_cpu_to_node(int cpu)
{
	if (early_per_cpu_ptr(x86_cpu_to_node_map))
		return early_per_cpu_ptr(x86_cpu_to_node_map)[cpu];

	return per_cpu(x86_cpu_to_node_map, cpu);
}

/* Returns a pointer to the cpumask of CPUs on Node 'node'. */
static inline const cpumask_t *cpumask_of_node(int node)
{
	return &node_to_cpumask_map[node];
}

/* Returns a bitmask of CPUs on Node 'node'. */
static inline cpumask_t node_to_cpumask(int node)
{
	return node_to_cpumask_map[node];
}

#endif /* !CONFIG_DEBUG_PER_CPU_MAPS */

/*
 * Replace default node_to_cpumask_ptr with optimized version
 * Deprecated: use "const struct cpumask *mask = cpumask_of_node(node)"
 */
#define node_to_cpumask_ptr(v, node)		\
		const cpumask_t *v = cpumask_of_node(node)

#define node_to_cpumask_ptr_next(v, node)	\
			   v = cpumask_of_node(node)

#endif /* CONFIG_X86_64 */

/*
 * Returns the number of the node containing Node 'node'. This
 * architecture is flat, so it is a pretty simple function!
 */
#define parent_node(node) (node)

#define pcibus_to_node(bus) __pcibus_to_node(bus)
#define pcibus_to_cpumask(bus) __pcibus_to_cpumask(bus)

#ifdef CONFIG_X86_32
extern unsigned long node_start_pfn[];
extern unsigned long node_end_pfn[];
extern unsigned long node_remap_size[];
#define node_has_online_mem(nid) (node_start_pfn[nid] != node_end_pfn[nid])

# define SD_CACHE_NICE_TRIES	1
# define SD_IDLE_IDX		1
# define SD_NEWIDLE_IDX		2
# define SD_FORKEXEC_IDX	0

#else

# define SD_CACHE_NICE_TRIES	2
# define SD_IDLE_IDX		2
# define SD_NEWIDLE_IDX		2
# define SD_FORKEXEC_IDX	1

#endif

/* sched_domains SD_NODE_INIT for NUMA machines */
#define SD_NODE_INIT (struct sched_domain) {		\
	.min_interval		= 8,			\
	.max_interval		= 32,			\
	.busy_factor		= 32,			\
	.imbalance_pct		= 125,			\
	.cache_nice_tries	= SD_CACHE_NICE_TRIES,	\
	.busy_idx		= 3,			\
	.idle_idx		= SD_IDLE_IDX,		\
	.newidle_idx		= SD_NEWIDLE_IDX,	\
	.wake_idx		= 1,			\
	.forkexec_idx		= SD_FORKEXEC_IDX,	\
	.flags			= SD_LOAD_BALANCE	\
				| SD_BALANCE_EXEC	\
				| SD_BALANCE_FORK	\
				| SD_WAKE_AFFINE	\
				| SD_WAKE_BALANCE	\
				| SD_SERIALIZE,		\
	.last_balance		= jiffies,		\
	.balance_interval	= 1,			\
}

#ifdef CONFIG_X86_64_ACPI_NUMA
extern int __node_distance(int, int);
#define node_distance(a, b) __node_distance(a, b)
#endif

#else /* !CONFIG_NUMA */

#define numa_node_id()		0
#define	cpu_to_node(cpu)	0
#define	early_cpu_to_node(cpu)	0

static inline const cpumask_t *cpumask_of_node(int node)
{
	return &cpu_online_map;
}
static inline cpumask_t node_to_cpumask(int node)
{
	return cpu_online_map;
}
static inline int node_to_first_cpu(int node)
{
	return first_cpu(cpu_online_map);
}

/*
 * Replace default node_to_cpumask_ptr with optimized version
 * Deprecated: use "const struct cpumask *mask = cpumask_of_node(node)"
 */
#define node_to_cpumask_ptr(v, node)		\
		const cpumask_t *v = cpumask_of_node(node)

#define node_to_cpumask_ptr_next(v, node)	\
			   v = cpumask_of_node(node)
#endif

#include <asm-generic/topology.h>

#ifdef CONFIG_NUMA
/* Returns the number of the first CPU on Node 'node'. */
static inline int node_to_first_cpu(int node)
{
	return cpumask_first(cpumask_of_node(node));
}
#endif

extern cpumask_t cpu_coregroup_map(int cpu);
extern const struct cpumask *cpu_coregroup_mask(int cpu);

#ifdef ENABLE_TOPO_DEFINES
#define topology_physical_package_id(cpu)	(cpu_data(cpu).phys_proc_id)
#define topology_core_id(cpu)			(cpu_data(cpu).cpu_core_id)
#define topology_core_siblings(cpu)		(per_cpu(cpu_core_map, cpu))
#define topology_thread_siblings(cpu)		(per_cpu(cpu_sibling_map, cpu))
#define topology_core_cpumask(cpu)		(&per_cpu(cpu_core_map, cpu))
#define topology_thread_cpumask(cpu)		(&per_cpu(cpu_sibling_map, cpu))

/* indicates that pointers to the topology cpumask_t maps are valid */
#define arch_provides_topology_pointers		yes
#endif

static inline void arch_fix_phys_package_id(int num, u32 slot)
{
}

struct pci_bus;
void set_pci_bus_resources_arch_default(struct pci_bus *b);

#ifdef CONFIG_SMP
#define mc_capable()	(cpus_weight(per_cpu(cpu_core_map, 0)) != nr_cpu_ids)
#define smt_capable()			(smp_num_siblings > 1)
#endif

#ifdef CONFIG_NUMA
extern int get_mp_bus_to_node(int busnum);
extern void set_mp_bus_to_node(int busnum, int node);
#else
static inline int get_mp_bus_to_node(int busnum)
{
	return 0;
}
static inline void set_mp_bus_to_node(int busnum, int node)
{
}
#endif

#endif /* _ASM_X86_TOPOLOGY_H */
