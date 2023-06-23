/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_S390_ABS_LOWCORE_H
#define _ASM_S390_ABS_LOWCORE_H

#include <asm/lowcore.h>

#define ABS_LOWCORE_MAP_SIZE	(NR_CPUS * sizeof(struct lowcore))

extern unsigned long __abs_lowcore;
extern bool abs_lowcore_mapped;

struct lowcore *get_abs_lowcore(unsigned long *flags);
void put_abs_lowcore(struct lowcore *lc, unsigned long flags);
int abs_lowcore_map(int cpu, struct lowcore *lc, bool alloc);
void abs_lowcore_unmap(int cpu);

#endif /* _ASM_S390_ABS_LOWCORE_H */
