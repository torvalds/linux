#ifndef _ASM_X86_NUMA_64_H
#define _ASM_X86_NUMA_64_H

#include <linux/nodemask.h>
#include <asm/apicdef.h>

struct bootnode {
	u64 start;
	u64 end;
};

extern int compute_hash_shift(struct bootnode *nodes, int numblks,
			      int *nodeids);

#define ZONE_ALIGN (1UL << (MAX_ORDER+PAGE_SHIFT))

extern void numa_init_array(void);
extern int numa_off;

extern s16 apicid_to_node[MAX_LOCAL_APIC];

extern unsigned long numa_free_all_bootmem(void);
extern void setup_node_bootmem(int nodeid, unsigned long start,
			       unsigned long end);

#ifdef CONFIG_NUMA
/*
 * Too small node sizes may confuse the VM badly. Usually they
 * result from BIOS bugs. So dont recognize nodes as standalone
 * NUMA entities that have less than this amount of RAM listed:
 */
#define NODE_MIN_SIZE (4*1024*1024)

extern void __init init_cpu_to_node(void);
extern void __cpuinit numa_set_node(int cpu, int node);
extern void __cpuinit numa_clear_node(int cpu);
extern void __cpuinit numa_add_cpu(int cpu);
extern void __cpuinit numa_remove_cpu(int cpu);

#ifdef CONFIG_NUMA_EMU
#define FAKE_NODE_MIN_SIZE	((u64)32 << 20)
#define FAKE_NODE_MIN_HASH_MASK	(~(FAKE_NODE_MIN_SIZE - 1UL))
#endif /* CONFIG_NUMA_EMU */
#else
static inline void init_cpu_to_node(void)		{ }
static inline void numa_set_node(int cpu, int node)	{ }
static inline void numa_clear_node(int cpu)		{ }
static inline void numa_add_cpu(int cpu, int node)	{ }
static inline void numa_remove_cpu(int cpu)		{ }
#endif

#endif /* _ASM_X86_NUMA_64_H */
