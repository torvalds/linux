#ifndef _ASM_X86_NUMA_64_H
#define _ASM_X86_NUMA_64_H

#include <linux/nodemask.h>

struct bootnode {
	u64 start;
	u64 end;
};

#define ZONE_ALIGN (1UL << (MAX_ORDER+PAGE_SHIFT))

extern int numa_off;

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

extern nodemask_t numa_nodes_parsed __initdata;

extern int __cpuinit numa_cpu_node(int cpu);
extern int __init numa_add_memblk(int nodeid, u64 start, u64 end);
extern void __init numa_set_distance(int from, int to, int distance);

#ifdef CONFIG_NUMA_EMU
#define FAKE_NODE_MIN_SIZE	((u64)32 << 20)
#define FAKE_NODE_MIN_HASH_MASK	(~(FAKE_NODE_MIN_SIZE - 1UL))
void numa_emu_cmdline(char *);
#endif /* CONFIG_NUMA_EMU */
#else
static inline int numa_cpu_node(int cpu)		{ return NUMA_NO_NODE; }
#endif

#endif /* _ASM_X86_NUMA_64_H */
