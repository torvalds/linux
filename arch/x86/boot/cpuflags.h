/* SPDX-License-Identifier: GPL-2.0 */
#ifndef BOOT_CPUFLAGS_H
#define BOOT_CPUFLAGS_H

#include <asm/cpufeatures.h>
#include <asm/processor-flags.h>

struct cpu_features {
	int level;		/* Family, or 64 for x86-64 */
	int family;		/* Family, always */
	int model;
	u32 flags[NCAPINTS];
};

extern struct cpu_features cpu;
extern u32 cpu_vendor[3];

#ifdef CONFIG_X86_32
bool has_eflag(unsigned long mask);
#else
static inline bool has_eflag(unsigned long mask) { return true; }
#endif
void get_cpuflags(void);
void cpuid_count(u32 id, u32 count, u32 *a, u32 *b, u32 *c, u32 *d);
bool has_cpuflag(int flag);

#endif
