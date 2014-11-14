/*
 * Copyright (C) 2014 Linaro Ltd. <ard.biesheuvel@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_CPUFEATURE_H
#define __ASM_CPUFEATURE_H

#include <asm/hwcap.h>

/*
 * In the arm64 world (as in the ARM world), elf_hwcap is used both internally
 * in the kernel and for user space to keep track of which optional features
 * are supported by the current system. So let's map feature 'x' to HWCAP_x.
 * Note that HWCAP_x constants are bit fields so we need to take the log.
 */

#define MAX_CPU_FEATURES	(8 * sizeof(elf_hwcap))
#define cpu_feature(x)		ilog2(HWCAP_ ## x)

#define NCAPS			0

extern DECLARE_BITMAP(cpu_hwcaps, NCAPS);

static inline bool cpu_have_feature(unsigned int num)
{
	return elf_hwcap & (1UL << num);
}

static inline bool cpus_have_cap(unsigned int num)
{
	if (num >= NCAPS)
		return false;
	return test_bit(num, cpu_hwcaps);
}

static inline void cpus_set_cap(unsigned int num)
{
	if (num >= NCAPS)
		pr_warn("Attempt to set an illegal CPU capability (%d >= %d)\n",
			num, NCAPS);
	else
		__set_bit(num, cpu_hwcaps);
}

#endif
