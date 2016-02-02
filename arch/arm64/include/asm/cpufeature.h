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
#include <asm/sysreg.h>

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
#define ARM64_WORKAROUND_CAVIUM_23154		6
#define ARM64_WORKAROUND_834220			7
#define ARM64_HAS_NO_HW_PREFETCH		8

#define ARM64_NCAPS				9

#ifndef __ASSEMBLY__

#include <linux/kernel.h>

/* CPU feature register tracking */
enum ftr_type {
	FTR_EXACT,	/* Use a predefined safe value */
	FTR_LOWER_SAFE,	/* Smaller value is safe */
	FTR_HIGHER_SAFE,/* Bigger value is safe */
};

#define FTR_STRICT	true	/* SANITY check strict matching required */
#define FTR_NONSTRICT	false	/* SANITY check ignored */

#define FTR_SIGNED	true	/* Value should be treated as signed */
#define FTR_UNSIGNED	false	/* Value should be treated as unsigned */

struct arm64_ftr_bits {
	bool		sign;	/* Value is signed ? */
	bool		strict;	/* CPU Sanity check: strict matching required ? */
	enum ftr_type	type;
	u8		shift;
	u8		width;
	s64		safe_val; /* safe value for discrete features */
};

/*
 * @arm64_ftr_reg - Feature register
 * @strict_mask		Bits which should match across all CPUs for sanity.
 * @sys_val		Safe value across the CPUs (system view)
 */
struct arm64_ftr_reg {
	u32			sys_id;
	const char		*name;
	u64			strict_mask;
	u64			sys_val;
	struct arm64_ftr_bits	*ftr_bits;
};

struct arm64_cpu_capabilities {
	const char *desc;
	u16 capability;
	bool (*matches)(const struct arm64_cpu_capabilities *);
	void (*enable)(void *);		/* Called on all active CPUs */
	union {
		struct {	/* To be used for erratum handling only */
			u32 midr_model;
			u32 midr_range_min, midr_range_max;
		};

		struct {	/* Feature register checking */
			u32 sys_reg;
			int field_pos;
			int min_field_value;
			int hwcap_type;
			unsigned long hwcap;
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

static inline int __attribute_const__
cpuid_feature_extract_field_width(u64 features, int field, int width)
{
	return (s64)(features << (64 - width - field)) >> (64 - width);
}

static inline int __attribute_const__
cpuid_feature_extract_field(u64 features, int field)
{
	return cpuid_feature_extract_field_width(features, field, 4);
}

static inline unsigned int __attribute_const__
cpuid_feature_extract_unsigned_field_width(u64 features, int field, int width)
{
	return (u64)(features << (64 - width - field)) >> (64 - width);
}

static inline unsigned int __attribute_const__
cpuid_feature_extract_unsigned_field(u64 features, int field)
{
	return cpuid_feature_extract_unsigned_field_width(features, field, 4);
}

static inline u64 arm64_ftr_mask(struct arm64_ftr_bits *ftrp)
{
	return (u64)GENMASK(ftrp->shift + ftrp->width - 1, ftrp->shift);
}

static inline s64 arm64_ftr_value(struct arm64_ftr_bits *ftrp, u64 val)
{
	return ftrp->sign ?
		cpuid_feature_extract_field_width(val, ftrp->shift, ftrp->width) :
		cpuid_feature_extract_unsigned_field_width(val, ftrp->shift, ftrp->width);
}

static inline bool id_aa64mmfr0_mixed_endian_el0(u64 mmfr0)
{
	return cpuid_feature_extract_field(mmfr0, ID_AA64MMFR0_BIGENDEL_SHIFT) == 0x1 ||
		cpuid_feature_extract_field(mmfr0, ID_AA64MMFR0_BIGENDEL0_SHIFT) == 0x1;
}

void __init setup_cpu_features(void);

void update_cpu_capabilities(const struct arm64_cpu_capabilities *caps,
			    const char *info);
void check_local_cpu_errata(void);

#ifdef CONFIG_HOTPLUG_CPU
void verify_local_cpu_capabilities(void);
#else
static inline void verify_local_cpu_capabilities(void)
{
}
#endif

u64 read_system_reg(u32 id);

static inline bool cpu_supports_mixed_endian_el0(void)
{
	return id_aa64mmfr0_mixed_endian_el0(read_cpuid(ID_AA64MMFR0_EL1));
}

static inline bool system_supports_mixed_endian_el0(void)
{
	return id_aa64mmfr0_mixed_endian_el0(read_system_reg(SYS_ID_AA64MMFR0_EL1));
}

#endif /* __ASSEMBLY__ */

#endif
