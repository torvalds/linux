// SPDX-License-Identifier: GPL-2.0
/*
 * Intel PCONFIG instruction support.
 *
 * Copyright (C) 2017 Intel Corporation
 *
 * Author:
 *	Kirill A. Shutemov <kirill.shutemov@linux.intel.com>
 */
#include <linux/bug.h>
#include <linux/limits.h>

#include <asm/cpufeature.h>
#include <asm/intel_pconfig.h>

#define	PCONFIG_CPUID			0x1b

#define PCONFIG_CPUID_SUBLEAF_MASK	((1 << 12) - 1)

/* Subleaf type (EAX) for PCONFIG CPUID leaf (0x1B) */
enum {
	PCONFIG_CPUID_SUBLEAF_INVALID	= 0,
	PCONFIG_CPUID_SUBLEAF_TARGETID	= 1,
};

/* Bitmask of supported targets */
static u64 targets_supported __read_mostly;

int pconfig_target_supported(enum pconfig_target target)
{
	/*
	 * We would need to re-think the implementation once we get > 64
	 * PCONFIG targets. Spec allows up to 2^32 targets.
	 */
	BUILD_BUG_ON(PCONFIG_TARGET_NR >= 64);

	if (WARN_ON_ONCE(target >= 64))
		return 0;
	return targets_supported & (1ULL << target);
}

static int __init intel_pconfig_init(void)
{
	int subleaf;

	if (!boot_cpu_has(X86_FEATURE_PCONFIG))
		return 0;

	/*
	 * Scan subleafs of PCONFIG CPUID leaf.
	 *
	 * Subleafs of the same type need not to be consecutive.
	 *
	 * Stop on the first invalid subleaf type. All subleafs after the first
	 * invalid are invalid too.
	 */
	for (subleaf = 0; subleaf < INT_MAX; subleaf++) {
		struct cpuid_regs regs;

		cpuid_count(PCONFIG_CPUID, subleaf,
				&regs.eax, &regs.ebx, &regs.ecx, &regs.edx);

		switch (regs.eax & PCONFIG_CPUID_SUBLEAF_MASK) {
		case PCONFIG_CPUID_SUBLEAF_INVALID:
			/* Stop on the first invalid subleaf */
			goto out;
		case PCONFIG_CPUID_SUBLEAF_TARGETID:
			/* Mark supported PCONFIG targets */
			if (regs.ebx < 64)
				targets_supported |= (1ULL << regs.ebx);
			if (regs.ecx < 64)
				targets_supported |= (1ULL << regs.ecx);
			if (regs.edx < 64)
				targets_supported |= (1ULL << regs.edx);
			break;
		default:
			/* Unknown CPUID.PCONFIG subleaf: ignore */
			break;
		}
	}
out:
	return 0;
}
arch_initcall(intel_pconfig_init);
