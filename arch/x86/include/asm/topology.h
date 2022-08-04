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

/*
 * to preserve the visibility of NUMA_NO_NODE definition,
 * moved to there from here.  May be used independent of
 * CONFIG_NUMA.
 */
#include <linux/numa.h>

#ifdef CONFIG_NUMA
#include <linux/cpumask.h>

#include <asm/mpspec.h>
#include <asm/percpu.h>

/* Mappings between logical cpu number and node number */
DECLARE_EARLY_PER_CPU(int, x86_cpu_to_node_map);

#ifdef CONFIG_DEBUG_PER_CPU_MAPS
/*
 * override generic percpu implementation of cpu_to_node
 */
extern int __cpu_to_node(int cpu);
#define cpu_to_node __cpu_to_node

extern int early_cpu_to_node(int cpu);

#else	/* !CONFIG_DEBUG_PER_CPU_MAPS */

/* Same function but used if called before per_cpu areas are setup */
static inline int early_cpu_to_node(int cpu)
{
	return early_per_cpu(x86_cpu_to_node_map, cpu);
}

#endif /* !CONFIG_DEBUG_PER_CPU_MAPS */

/* Mappings between node number and cpus on that node. */
extern cpumask_var_t node_to_cpumask_map[MAX_NUMNODES];

#ifdef CONFIG_DEBUG_PER_CPU_MAPS
extern const struct cpumask *cpumask_of_node(int node);
#else
/* Returns a pointer to the cpumask of CPUs on Node 'node'. */
static inline const struct cpumask *cpumask_of_node(int node)
{
	return node_to_cpumask_map[node];
}
#endif

extern void setup_node_to_cpumask_map(void);

#define pcibus_to_node(bus) __pcibus_to_node(bus)

extern int __node_distance(int, int);
#define node_distance(a, b) __node_distance(a, b)

#else /* !CONFIG_NUMA */

static inline int numa_node_id(void)
{
	return 0;
}
/*
 * indicate override:
 */
#define numa_node_id numa_node_id

static inline int early_cpu_to_node(int cpu)
{
	return 0;
}

static inline void setup_node_to_cpumask_map(void) { }

#endif

#include <asm-generic/topology.h>

extern const struct cpumask *cpu_coregroup_mask(int cpu);
extern const struct cpumask *cpu_clustergroup_mask(int cpu);

#define topology_logical_package_id(cpu)	(cpu_data(cpu).logical_proc_id)
#define topology_physical_package_id(cpu)	(cpu_data(cpu).phys_proc_id)
#define topology_logical_die_id(cpu)		(cpu_data(cpu).logical_die_id)
#define topology_die_id(cpu)			(cpu_data(cpu).cpu_die_id)
#define topology_core_id(cpu)			(cpu_data(cpu).cpu_core_id)
#define topology_ppin(cpu)			(cpu_data(cpu).ppin)

extern unsigned int __max_die_per_package;

#ifdef CONFIG_SMP
#define topology_cluster_id(cpu)		(per_cpu(cpu_l2c_id, cpu))
#define topology_die_cpumask(cpu)		(per_cpu(cpu_die_map, cpu))
#define topology_cluster_cpumask(cpu)		(cpu_clustergroup_mask(cpu))
#define topology_core_cpumask(cpu)		(per_cpu(cpu_core_map, cpu))
#define topology_sibling_cpumask(cpu)		(per_cpu(cpu_sibling_map, cpu))

extern unsigned int __max_logical_packages;
#define topology_max_packages()			(__max_logical_packages)

static inline int topology_max_die_per_package(void)
{
	return __max_die_per_package;
}

extern int __max_smt_threads;

static inline int topology_max_smt_threads(void)
{
	return __max_smt_threads;
}

int topology_update_package_map(unsigned int apicid, unsigned int cpu);
int topology_update_die_map(unsigned int dieid, unsigned int cpu);
int topology_phys_to_logical_pkg(unsigned int pkg);
int topology_phys_to_logical_die(unsigned int die, unsigned int cpu);
bool topology_is_primary_thread(unsigned int cpu);
bool topology_smt_supported(void);
#else
#define topology_max_packages()			(1)
static inline int
topology_update_package_map(unsigned int apicid, unsigned int cpu) { return 0; }
static inline int
topology_update_die_map(unsigned int dieid, unsigned int cpu) { return 0; }
static inline int topology_phys_to_logical_pkg(unsigned int pkg) { return 0; }
static inline int topology_phys_to_logical_die(unsigned int die,
		unsigned int cpu) { return 0; }
static inline int topology_max_die_per_package(void) { return 1; }
static inline int topology_max_smt_threads(void) { return 1; }
static inline bool topology_is_primary_thread(unsigned int cpu) { return true; }
static inline bool topology_smt_supported(void) { return false; }
#endif

static inline void arch_fix_phys_package_id(int num, u32 slot)
{
}

struct pci_bus;
int x86_pci_root_bus_node(int bus);
void x86_pci_root_bus_resources(int bus, struct list_head *resources);

extern bool x86_topology_update;

#ifdef CONFIG_SCHED_MC_PRIO
#include <asm/percpu.h>

DECLARE_PER_CPU_READ_MOSTLY(int, sched_core_priority);
extern unsigned int __read_mostly sysctl_sched_itmt_enabled;

/* Interface to set priority of a cpu */
void sched_set_itmt_core_prio(int prio, int core_cpu);

/* Interface to notify scheduler that system supports ITMT */
int sched_set_itmt_support(void);

/* Interface to notify scheduler that system revokes ITMT support */
void sched_clear_itmt_support(void);

#else /* CONFIG_SCHED_MC_PRIO */

#define sysctl_sched_itmt_enabled	0
static inline void sched_set_itmt_core_prio(int prio, int core_cpu)
{
}
static inline int sched_set_itmt_support(void)
{
	return 0;
}
static inline void sched_clear_itmt_support(void)
{
}
#endif /* CONFIG_SCHED_MC_PRIO */

#if defined(CONFIG_SMP) && defined(CONFIG_X86_64)
#include <asm/cpufeature.h>

DECLARE_STATIC_KEY_FALSE(arch_scale_freq_key);

#define arch_scale_freq_invariant() static_branch_likely(&arch_scale_freq_key)

DECLARE_PER_CPU(unsigned long, arch_freq_scale);

static inline long arch_scale_freq_capacity(int cpu)
{
	return per_cpu(arch_freq_scale, cpu);
}
#define arch_scale_freq_capacity arch_scale_freq_capacity

extern void arch_set_max_freq_ratio(bool turbo_disabled);
extern void freq_invariance_set_perf_ratio(u64 ratio, bool turbo_disabled);
#else
static inline void arch_set_max_freq_ratio(bool turbo_disabled) { }
static inline void freq_invariance_set_perf_ratio(u64 ratio, bool turbo_disabled) { }
#endif

extern void arch_scale_freq_tick(void);
#define arch_scale_freq_tick arch_scale_freq_tick

#ifdef CONFIG_ACPI_CPPC_LIB
void init_freq_invariance_cppc(void);
#define arch_init_invariance_cppc init_freq_invariance_cppc
#endif

#endif /* _ASM_X86_TOPOLOGY_H */
