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

#define ARM64_WORKAROUND_CLEAN_CACHE		0
#define ARM64_WORKAROUND_DEVICE_LOAD_ACQUIRE	1
#define ARM64_WORKAROUND_845719			2
#define ARM64_HAS_SYSREG_GIC_CPUIF		3
#define ARM64_HAS_PAN				4
#define ARM64_HAS_LSE_ATOMICS			5

#define ARM64_NCAPS				6

#ifndef __ASSEMBLY__

#include <linux/kernel.h>

struct arm64_cpu_capabilities {
	const char *desc;
	u16 capability;
	bool (*matches)(const struct arm64_cpu_capabilities *);
	void (*enable)(void);
	union {
		struct {	/* To be used for erratum handling only */
			u32 midr_model;
			u32 midr_range_min, midr_range_max;
		};

		struct {	/* Feature register checking */
			int field_pos;
			int min_field_value;
		};
	};
};

extern DECLARE_BITMAP(cpu_hwcaps, ARM64_NCAPS);

static inline bool cpu_have_feature(unsigned int num)
{
	return elf_hwcap & (1UL << num);
}

static inline bool cpus_have_cap(unsigned int num)
{
	if (num >= ARM64_NCAPS)
		return false;
	return test_bit(num, cpu_hwcaps);
}

static inline void cpus_set_cap(unsigned int num)
{
	if (num >= ARM64_NCAPS)
		pr_warn("Attempt to set an illegal CPU capability (%d >= %d)\n",
			num, ARM64_NCAPS);
	else
		__set_bit(num, cpu_hwcaps);
}

static inline int __attribute_const__ cpuid_feature_extract_field(u64 features,
								  int field)
{
	return (s64)(features << (64 - 4 - field)) >> (64 - 4);
}


void check_cpu_capabilities(const struct arm64_cpu_capabilities *caps,
			    const char *info);
void check_local_cpu_errata(void);
void check_local_cpu_features(void);
bool cpu_supports_mixed_endian_el0(void);
bool system_supports_mixed_endian_el0(void);

#endif /* __ASSEMBLY__ */

#endif
