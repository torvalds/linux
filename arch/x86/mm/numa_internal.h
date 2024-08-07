/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __X86_MM_NUMA_INTERNAL_H
#define __X86_MM_NUMA_INTERNAL_H

#include <linux/types.h>
#include <asm/numa.h>

void __init x86_numa_init(void);

struct numa_meminfo;

#ifdef CONFIG_NUMA_EMU
void __init numa_emulation(struct numa_meminfo *numa_meminfo,
			   int numa_dist_cnt);
#else
static inline void numa_emulation(struct numa_meminfo *numa_meminfo,
				  int numa_dist_cnt)
{ }
#endif

#endif	/* __X86_MM_NUMA_INTERNAL_H */
