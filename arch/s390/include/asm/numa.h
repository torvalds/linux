/*
 * NUMA support for s390
 *
 * Declare the NUMA core code structures and functions.
 *
 * Copyright IBM Corp. 2015
 */

#ifndef _ASM_S390_NUMA_H
#define _ASM_S390_NUMA_H

#ifdef CONFIG_NUMA

#include <linux/numa.h>
#include <linux/cpumask.h>

void numa_setup(void);
int numa_pfn_to_nid(unsigned long pfn);
int __node_distance(int a, int b);
void numa_update_cpu_topology(void);

extern cpumask_t node_to_cpumask_map[MAX_NUMNODES];
extern int numa_debug_enabled;

#else

static inline void numa_setup(void) { }
static inline void numa_update_cpu_topology(void) { }
static inline int numa_pfn_to_nid(unsigned long pfn)
{
	return 0;
}

#endif /* CONFIG_NUMA */
#endif /* _ASM_S390_NUMA_H */
