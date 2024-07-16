/* SPDX-License-Identifier: GPL-2.0 */
/*
 * CPU feature definitions for module loading, used by
 * module_cpu_feature_match(), see uapi/asm/hwcap.h for LoongArch CPU features.
 *
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
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
