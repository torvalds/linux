/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_NUMA_H
#define _ASM_X86_NUMA_H

#include <linux/nodemask.h>
#include <linux/errno.h>

#include <asm/topology.h>
#include <asm/apicdef.h>

#ifdef CONFIG_NUMA

extern int numa_off;

/*
 * __apicid_to_node[] stores the raw mapping between physical apicid and
 * node and is used to initialize cpu_to_node mapping.
 *
 * The mapping may be overridden by apic->numa_cpu_node() on 32bit and thus
 * should be accessed by the accessors - set_apicid_to_node() and
 * numa_cpu_node().
 */
extern s16 __apicid_to_node[MAX_LOCAL_APIC];
extern nodemask_t numa_nodes_parsed __initdata;

static inline void set_apicid_to_node(int apicid, s16 node)
{
	__apicid_to_node[apicid] = node;
}

extern int numa_cpu_node(int cpu);

#else	/* CONFIG_NUMA */
static inline void set_apicid_to_node(int apicid, s16 node)
{
}

static inline int numa_cpu_node(int cpu)
{
	return NUMA_NO_NODE;
}
#endif	/* CONFIG_NUMA */

#ifdef CONFIG_NUMA
extern void numa_set_node(int cpu, int node);
extern void numa_clear_node(int cpu);
extern void __init init_cpu_to_node(void);
extern void numa_add_cpu(unsigned int cpu);
extern void numa_remove_cpu(unsigned int cpu);
extern void init_gi_nodes(void);
#else	/* CONFIG_NUMA */
static inline void numa_set_node(int cpu, int node)	{ }
static inline void numa_clear_node(int cpu)		{ }
static inline void init_cpu_to_node(void)		{ }
static inline void numa_add_cpu(unsigned int cpu)	{ }
static inline void numa_remove_cpu(unsigned int cpu)	{ }
static inline void init_gi_nodes(void)			{ }
#endif	/* CONFIG_NUMA */

#ifdef CONFIG_DEBUG_PER_CPU_MAPS
void debug_cpumask_set_cpu(unsigned int cpu, int node, bool enable);
#endif

#endif	/* _ASM_X86_NUMA_H */
