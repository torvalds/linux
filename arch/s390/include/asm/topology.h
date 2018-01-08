/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_S390_TOPOLOGY_H
#define _ASM_S390_TOPOLOGY_H

#include <linux/cpumask.h>
#include <asm/numa.h>

struct sysinfo_15_1_x;
struct cpu;

#ifdef CONFIG_SCHED_TOPOLOGY

struct cpu_topology_s390 {
	unsigned short thread_id;
	unsigned short core_id;
	unsigned short socket_id;
	unsigned short book_id;
	unsigned short drawer_id;
	unsigned short node_id;
	unsigned short dedicated : 1;
	cpumask_t thread_mask;
	cpumask_t core_mask;
	cpumask_t book_mask;
	cpumask_t drawer_mask;
};

extern struct cpu_topology_s390 cpu_topology[NR_CPUS];
extern cpumask_t cpus_with_topology;

#define topology_physical_package_id(cpu) (cpu_topology[cpu].socket_id)
#define topology_thread_id(cpu)		  (cpu_topology[cpu].thread_id)
#define topology_sibling_cpumask(cpu)	  (&cpu_topology[cpu].thread_mask)
#define topology_core_id(cpu)		  (cpu_topology[cpu].core_id)
#define topology_core_cpumask(cpu)	  (&cpu_topology[cpu].core_mask)
#define topology_book_id(cpu)		  (cpu_topology[cpu].book_id)
#define topology_book_cpumask(cpu)	  (&cpu_topology[cpu].book_mask)
#define topology_drawer_id(cpu)		  (cpu_topology[cpu].drawer_id)
#define topology_drawer_cpumask(cpu)	  (&cpu_topology[cpu].drawer_mask)
#define topology_cpu_dedicated(cpu)	  (cpu_topology[cpu].dedicated)

#define mc_capable() 1

void topology_init_early(void);
int topology_cpu_init(struct cpu *);
int topology_set_cpu_management(int fc);
void topology_schedule_update(void);
void store_topology(struct sysinfo_15_1_x *info);
void topology_expect_change(void);
const struct cpumask *cpu_coregroup_mask(int cpu);

#else /* CONFIG_SCHED_TOPOLOGY */

static inline void topology_init_early(void) { }
static inline void topology_schedule_update(void) { }
static inline int topology_cpu_init(struct cpu *cpu) { return 0; }
static inline void topology_expect_change(void) { }

#endif /* CONFIG_SCHED_TOPOLOGY */

#define POLARIZATION_UNKNOWN	(-1)
#define POLARIZATION_HRZ	(0)
#define POLARIZATION_VL		(1)
#define POLARIZATION_VM		(2)
#define POLARIZATION_VH		(3)

#define SD_BOOK_INIT	SD_CPU_INIT

#ifdef CONFIG_NUMA

#define cpu_to_node cpu_to_node
static inline int cpu_to_node(int cpu)
{
	return cpu_topology[cpu].node_id;
}

/* Returns a pointer to the cpumask of CPUs on node 'node'. */
#define cpumask_of_node cpumask_of_node
static inline const struct cpumask *cpumask_of_node(int node)
{
	return &node_to_cpumask_map[node];
}

#define pcibus_to_node(bus) __pcibus_to_node(bus)

#define node_distance(a, b) __node_distance(a, b)

#else /* !CONFIG_NUMA */

#define numa_node_id numa_node_id
static inline int numa_node_id(void)
{
	return 0;
}

#endif /* CONFIG_NUMA */

#include <asm-generic/topology.h>

#endif /* _ASM_S390_TOPOLOGY_H */
