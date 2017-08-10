/*
 * CPU feature definitions for module loading, used by
 * module_cpu_feature_match(), see uapi/asm/hwcap.h for MIPS CPU features.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef __ASM_CPUFEATURE_H
#define __ASM_CPUFEATURE_H

#include <uapi/asm/hwcap.h>
#include <asm/elf.h>

#define MAX_CPU_FEATURES (8 * sizeof(elf_hwcap))

#define cpu_feature(x)		ilog2(HWCAP_ ## x)

static inline bool cpu_have_feature(unsigned int num)
{
	return elf_hwcap & (1UL << num);
}

#endif /* __ASM_CPUFEATURE_H */
