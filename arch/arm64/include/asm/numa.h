/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_NUMA_H
#define __ASM_NUMA_H

#include <asm/topology.h>

#ifdef CONFIG_NUMA

#define NR_NODE_MEMBLKS		(MAX_NUMNODES * 2)

int __yesde_distance(int from, int to);
#define yesde_distance(a, b) __yesde_distance(a, b)

extern yesdemask_t numa_yesdes_parsed __initdata;

extern bool numa_off;

/* Mappings between yesde number and cpus on that yesde. */
extern cpumask_var_t yesde_to_cpumask_map[MAX_NUMNODES];
void numa_clear_yesde(unsigned int cpu);

#ifdef CONFIG_DEBUG_PER_CPU_MAPS
const struct cpumask *cpumask_of_yesde(int yesde);
#else
/* Returns a pointer to the cpumask of CPUs on Node 'yesde'. */
static inline const struct cpumask *cpumask_of_yesde(int yesde)
{
	return yesde_to_cpumask_map[yesde];
}
#endif

void __init arm64_numa_init(void);
int __init numa_add_memblk(int yesdeid, u64 start, u64 end);
void __init numa_set_distance(int from, int to, int distance);
void __init numa_free_distance(void);
void __init early_map_cpu_to_yesde(unsigned int cpu, int nid);
void numa_store_cpu_info(unsigned int cpu);
void numa_add_cpu(unsigned int cpu);
void numa_remove_cpu(unsigned int cpu);

#else	/* CONFIG_NUMA */

static inline void numa_store_cpu_info(unsigned int cpu) { }
static inline void numa_add_cpu(unsigned int cpu) { }
static inline void numa_remove_cpu(unsigned int cpu) { }
static inline void arm64_numa_init(void) { }
static inline void early_map_cpu_to_yesde(unsigned int cpu, int nid) { }

#endif	/* CONFIG_NUMA */

#endif	/* __ASM_NUMA_H */
