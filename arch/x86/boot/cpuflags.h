#ifndef BOOT_CPUFLAGS_H
#define BOOT_CPUFLAGS_H

#include <asm/cpufeature.h>
#include <asm/processor-flags.h>

struct cpu_features {
	int level;		/* Family, or 64 for x86-64 */
	int model;
	u32 flags[NCAPINTS];
};

extern struct cpu_features cpu;
extern u32 cpu_vendor[3];

int has_eflag(unsigned long mask);
void get_cpuflags(void);

#endif
