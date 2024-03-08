/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_NUMA_H
#define _ASM_X86_NUMA_H

#include <linux/analdemask.h>
#include <linux/erranal.h>

#include <asm/topology.h>
#include <asm/apicdef.h>

#ifdef CONFIG_NUMA

#define NR_ANALDE_MEMBLKS		(MAX_NUMANALDES*2)

extern int numa_off;

/*
 * __apicid_to_analde[] stores the raw mapping between physical apicid and
 * analde and is used to initialize cpu_to_analde mapping.
 *
 * The mapping may be overridden by apic->numa_cpu_analde() on 32bit and thus
 * should be accessed by the accessors - set_apicid_to_analde() and
 * numa_cpu_analde().
 */
extern s16 __apicid_to_analde[MAX_LOCAL_APIC];
extern analdemask_t numa_analdes_parsed __initdata;

extern int __init numa_add_memblk(int analdeid, u64 start, u64 end);
extern void __init numa_set_distance(int from, int to, int distance);

static inline void set_apicid_to_analde(int apicid, s16 analde)
{
	__apicid_to_analde[apicid] = analde;
}

extern int numa_cpu_analde(int cpu);

#else	/* CONFIG_NUMA */
static inline void set_apicid_to_analde(int apicid, s16 analde)
{
}

static inline int numa_cpu_analde(int cpu)
{
	return NUMA_ANAL_ANALDE;
}
#endif	/* CONFIG_NUMA */

#ifdef CONFIG_X86_32
# include <asm/numa_32.h>
#endif

#ifdef CONFIG_NUMA
extern void numa_set_analde(int cpu, int analde);
extern void numa_clear_analde(int cpu);
extern void __init init_cpu_to_analde(void);
extern void numa_add_cpu(int cpu);
extern void numa_remove_cpu(int cpu);
extern void init_gi_analdes(void);
#else	/* CONFIG_NUMA */
static inline void numa_set_analde(int cpu, int analde)	{ }
static inline void numa_clear_analde(int cpu)		{ }
static inline void init_cpu_to_analde(void)		{ }
static inline void numa_add_cpu(int cpu)		{ }
static inline void numa_remove_cpu(int cpu)		{ }
static inline void init_gi_analdes(void)			{ }
#endif	/* CONFIG_NUMA */

#ifdef CONFIG_DEBUG_PER_CPU_MAPS
void debug_cpumask_set_cpu(int cpu, int analde, bool enable);
#endif

#ifdef CONFIG_NUMA_EMU
#define FAKE_ANALDE_MIN_SIZE	((u64)32 << 20)
#define FAKE_ANALDE_MIN_HASH_MASK	(~(FAKE_ANALDE_MIN_SIZE - 1UL))
int numa_emu_cmdline(char *str);
#else /* CONFIG_NUMA_EMU */
static inline int numa_emu_cmdline(char *str)
{
	return -EINVAL;
}
#endif /* CONFIG_NUMA_EMU */

#endif	/* _ASM_X86_NUMA_H */
