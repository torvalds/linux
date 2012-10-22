#ifndef _ASM_X86_NUMA_H
#define _ASM_X86_NUMA_H

#include <linux/nodemask.h>

#include <asm/topology.h>
#include <asm/apicdef.h>

#ifdef CONFIG_NUMA

#define NR_NODE_MEMBLKS		(MAX_NUMNODES*2)
#define ZONE_ALIGN (1UL << (MAX_ORDER+PAGE_SHIFT))

/*
 * Too small node sizes may confuse the VM badly. Usually they
 * result from BIOS bugs. So dont recognize nodes as standalone
 * NUMA entities that have less than this amount of RAM listed:
 */
#define NODE_MIN_SIZE (4*1024*1024)

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

extern int __init numa_add_memblk(int nodeid, u64 start, u64 end);
extern void __init numa_set_distance(int from, int to, int distance);

static inline void set_apicid_to_node(int apicid, s16 node)
{
	__apicid_to_node[apicid] = node;
}

extern int __cpuinit numa_cpu_node(int cpu);

#else	/* CONFIG_NUMA */
static inline void set_apicid_to_node(int apicid, s16 node)
{
}

static inline int numa_cpu_node(int cpu)
{
	return NUMA_NO_NODE;
}
#endif	/* CONFIG_NUMA */

#ifdef CONFIG_X86_32
# include <asm/numa_32.h>
#else
# include <asm/numa_64.h>
#endif

#ifdef CONFIG_NUMA
extern void __cpuinit numa_set_node(int cpu, int node);
extern void __cpuinit numa_clear_node(int cpu);
extern void __init init_cpu_to_node(void);
extern void __cpuinit numa_add_cpu(int cpu);
extern void __cpuinit numa_remove_cpu(int cpu);
#else	/* CONFIG_NUMA */
static inline void numa_set_node(int cpu, int node)	{ }
static inline void numa_clear_node(int cpu)		{ }
static inline void init_cpu_to_node(void)		{ }
static inline void numa_add_cpu(int cpu)		{ }
static inline void numa_remove_cpu(int cpu)		{ }
#endif	/* CONFIG_NUMA */

#ifdef CONFIG_DEBUG_PER_CPU_MAPS
void debug_cpumask_set_cpu(int cpu, int node, bool enable);
#endif

#ifdef CONFIG_NUMA_EMU
#define FAKE_NODE_MIN_SIZE	((u64)32 << 20)
#define FAKE_NODE_MIN_HASH_MASK	(~(FAKE_NODE_MIN_SIZE - 1UL))
void numa_emu_cmdline(char *);
#endif /* CONFIG_NUMA_EMU */

#endif	/* _ASM_X86_NUMA_H */
