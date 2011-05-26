#ifndef _ASM_X86_NUMA_H
#define _ASM_X86_NUMA_H

#include <asm/topology.h>
#include <asm/apicdef.h>

#ifdef CONFIG_NUMA

#define NR_NODE_MEMBLKS		(MAX_NUMNODES*2)

/*
 * __apicid_to_node[] stores the raw mapping between physical apicid and
 * node and is used to initialize cpu_to_node mapping.
 *
 * The mapping may be overridden by apic->numa_cpu_node() on 32bit and thus
 * should be accessed by the accessors - set_apicid_to_node() and
 * numa_cpu_node().
 */
extern s16 __apicid_to_node[MAX_LOCAL_APIC];

static inline void set_apicid_to_node(int apicid, s16 node)
{
	__apicid_to_node[apicid] = node;
}
#else	/* CONFIG_NUMA */
static inline void set_apicid_to_node(int apicid, s16 node)
{
}
#endif	/* CONFIG_NUMA */

#ifdef CONFIG_X86_32
# include "numa_32.h"
#else
# include "numa_64.h"
#endif

#ifdef CONFIG_NUMA
extern void __cpuinit numa_set_node(int cpu, int node);
extern void __cpuinit numa_clear_node(int cpu);
extern void __init numa_init_array(void);
extern void __init init_cpu_to_node(void);
extern void __cpuinit numa_add_cpu(int cpu);
extern void __cpuinit numa_remove_cpu(int cpu);
#else	/* CONFIG_NUMA */
static inline void numa_set_node(int cpu, int node)	{ }
static inline void numa_clear_node(int cpu)		{ }
static inline void numa_init_array(void)		{ }
static inline void init_cpu_to_node(void)		{ }
static inline void numa_add_cpu(int cpu)		{ }
static inline void numa_remove_cpu(int cpu)		{ }
#endif	/* CONFIG_NUMA */

#ifdef CONFIG_DEBUG_PER_CPU_MAPS
void debug_cpumask_set_cpu(int cpu, int node, bool enable);
#endif

#endif	/* _ASM_X86_NUMA_H */
