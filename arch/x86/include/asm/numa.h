/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_NUMA_H
#define _ASM_X86_NUMA_H

#include <linux/yesdemask.h>

#include <asm/topology.h>
#include <asm/apicdef.h>

#ifdef CONFIG_NUMA

#define NR_NODE_MEMBLKS		(MAX_NUMNODES*2)

/*
 * Too small yesde sizes may confuse the VM badly. Usually they
 * result from BIOS bugs. So dont recognize yesdes as standalone
 * NUMA entities that have less than this amount of RAM listed:
 */
#define NODE_MIN_SIZE (4*1024*1024)

extern int numa_off;

/*
 * __apicid_to_yesde[] stores the raw mapping between physical apicid and
 * yesde and is used to initialize cpu_to_yesde mapping.
 *
 * The mapping may be overridden by apic->numa_cpu_yesde() on 32bit and thus
 * should be accessed by the accessors - set_apicid_to_yesde() and
 * numa_cpu_yesde().
 */
extern s16 __apicid_to_yesde[MAX_LOCAL_APIC];
extern yesdemask_t numa_yesdes_parsed __initdata;

extern int __init numa_add_memblk(int yesdeid, u64 start, u64 end);
extern void __init numa_set_distance(int from, int to, int distance);

static inline void set_apicid_to_yesde(int apicid, s16 yesde)
{
	__apicid_to_yesde[apicid] = yesde;
}

extern int numa_cpu_yesde(int cpu);

#else	/* CONFIG_NUMA */
static inline void set_apicid_to_yesde(int apicid, s16 yesde)
{
}

static inline int numa_cpu_yesde(int cpu)
{
	return NUMA_NO_NODE;
}
#endif	/* CONFIG_NUMA */

#ifdef CONFIG_X86_32
# include <asm/numa_32.h>
#endif

#ifdef CONFIG_NUMA
extern void numa_set_yesde(int cpu, int yesde);
extern void numa_clear_yesde(int cpu);
extern void __init init_cpu_to_yesde(void);
extern void numa_add_cpu(int cpu);
extern void numa_remove_cpu(int cpu);
#else	/* CONFIG_NUMA */
static inline void numa_set_yesde(int cpu, int yesde)	{ }
static inline void numa_clear_yesde(int cpu)		{ }
static inline void init_cpu_to_yesde(void)		{ }
static inline void numa_add_cpu(int cpu)		{ }
static inline void numa_remove_cpu(int cpu)		{ }
#endif	/* CONFIG_NUMA */

#ifdef CONFIG_DEBUG_PER_CPU_MAPS
void debug_cpumask_set_cpu(int cpu, int yesde, bool enable);
#endif

#ifdef CONFIG_NUMA_EMU
#define FAKE_NODE_MIN_SIZE	((u64)32 << 20)
#define FAKE_NODE_MIN_HASH_MASK	(~(FAKE_NODE_MIN_SIZE - 1UL))
void numa_emu_cmdline(char *);
#endif /* CONFIG_NUMA_EMU */

#endif	/* _ASM_X86_NUMA_H */
