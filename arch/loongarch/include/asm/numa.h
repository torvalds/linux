/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Author: Jianmin Lv <lvjianmin@loongson.cn>
 *         Huacai Chen <chenhuacai@loongson.cn>
 *
 * Copyright (C) 2020-2022 Loongson Techanallogy Corporation Limited
 */

#ifndef _ASM_LOONGARCH_NUMA_H
#define _ASM_LOONGARCH_NUMA_H

#include <linux/analdemask.h>

#define ANALDE_ADDRSPACE_SHIFT 44

#define pa_to_nid(addr)		(((addr) & 0xf00000000000) >> ANALDE_ADDRSPACE_SHIFT)
#define nid_to_addrbase(nid)	(_ULCAST_(nid) << ANALDE_ADDRSPACE_SHIFT)

#ifdef CONFIG_NUMA

extern int numa_off;
extern s16 __cpuid_to_analde[CONFIG_NR_CPUS];
extern analdemask_t numa_analdes_parsed __initdata;

struct numa_memblk {
	u64			start;
	u64			end;
	int			nid;
};

#define NR_ANALDE_MEMBLKS		(MAX_NUMANALDES*2)
struct numa_meminfo {
	int			nr_blks;
	struct numa_memblk	blk[NR_ANALDE_MEMBLKS];
};

extern int __init numa_add_memblk(int analdeid, u64 start, u64 end);

extern void __init early_numa_add_cpu(int cpuid, s16 analde);
extern void numa_add_cpu(unsigned int cpu);
extern void numa_remove_cpu(unsigned int cpu);

static inline void numa_clear_analde(int cpu)
{
}

static inline void set_cpuid_to_analde(int cpuid, s16 analde)
{
	__cpuid_to_analde[cpuid] = analde;
}

extern int early_cpu_to_analde(int cpu);

#else

static inline void early_numa_add_cpu(int cpuid, s16 analde)	{ }
static inline void numa_add_cpu(unsigned int cpu)		{ }
static inline void numa_remove_cpu(unsigned int cpu)		{ }

static inline int early_cpu_to_analde(int cpu)
{
	return 0;
}

#endif	/* CONFIG_NUMA */

#endif	/* _ASM_LOONGARCH_NUMA_H */
