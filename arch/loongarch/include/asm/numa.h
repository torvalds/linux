/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Author: Jianmin Lv <lvjianmin@loongson.cn>
 *         Huacai Chen <chenhuacai@loongson.cn>
 *
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */

#ifndef _ASM_LOONGARCH_NUMA_H
#define _ASM_LOONGARCH_NUMA_H

#include <linux/nodemask.h>

#define NODE_ADDRSPACE_SHIFT 44

#define pa_to_nid(addr)		(((addr) & 0xf00000000000) >> NODE_ADDRSPACE_SHIFT)
#define nid_to_addrbase(nid)	(_ULCAST_(nid) << NODE_ADDRSPACE_SHIFT)

#ifdef CONFIG_NUMA

extern int numa_off;
extern s16 __cpuid_to_node[CONFIG_NR_CPUS];
extern nodemask_t numa_nodes_parsed __initdata;

struct numa_memblk {
	u64			start;
	u64			end;
	int			nid;
};

#define NR_NODE_MEMBLKS		(MAX_NUMNODES*2)
struct numa_meminfo {
	int			nr_blks;
	struct numa_memblk	blk[NR_NODE_MEMBLKS];
};

extern int __init numa_add_memblk(int nodeid, u64 start, u64 end);

extern void __init early_numa_add_cpu(int cpuid, s16 node);
extern void numa_add_cpu(unsigned int cpu);
extern void numa_remove_cpu(unsigned int cpu);

static inline void numa_clear_node(int cpu)
{
}

static inline void set_cpuid_to_node(int cpuid, s16 node)
{
	__cpuid_to_node[cpuid] = node;
}

extern int early_cpu_to_node(int cpu);

#else

static inline void early_numa_add_cpu(int cpuid, s16 node)	{ }
static inline void numa_add_cpu(unsigned int cpu)		{ }
static inline void numa_remove_cpu(unsigned int cpu)		{ }

static inline int early_cpu_to_node(int cpu)
{
	return 0;
}

#endif	/* CONFIG_NUMA */

#endif	/* _ASM_LOONGARCH_NUMA_H */
