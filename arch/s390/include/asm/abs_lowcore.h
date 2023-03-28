/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_S390_ABS_LOWCORE_H
#define _ASM_S390_ABS_LOWCORE_H

#include <asm/lowcore.h>

#define ABS_LOWCORE_MAP_SIZE	(NR_CPUS * sizeof(struct lowcore))

extern unsigned long __abs_lowcore;

int abs_lowcore_map(int cpu, struct lowcore *lc, bool alloc);
void abs_lowcore_unmap(int cpu);

static inline struct lowcore *get_abs_lowcore(void)
{
	int cpu;

	cpu = get_cpu();
	return ((struct lowcore *)__abs_lowcore) + cpu;
}

static inline void put_abs_lowcore(struct lowcore *lc)
{
	put_cpu();
}

#endif /* _ASM_S390_ABS_LOWCORE_H */
