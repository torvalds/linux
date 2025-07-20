// SPDX-License-Identifier: GPL-2.0
#include <linux/sched.h>
#include <linux/sched/clock.h>

#include <asm/cpu.h>
#include <asm/cpufeature.h>
#include <asm/msr.h>

#include "cpu.h"

#define MSR_ZHAOXIN_FCR57 0x00001257

#define ACE_PRESENT	(1 << 6)
#define ACE_ENABLED	(1 << 7)
#define ACE_FCR		(1 << 7)	/* MSR_ZHAOXIN_FCR */

#define RNG_PRESENT	(1 << 2)
#define RNG_ENABLED	(1 << 3)
#define RNG_ENABLE	(1 << 8)	/* MSR_ZHAOXIN_RNG */

static void init_zhaoxin_cap(struct cpuinfo_x86 *c)
{
	u32  lo, hi;

	/* Test for Extended Feature Flags presence */
	if (cpuid_eax(0xC0000000) >= 0xC0000001) {
		u32 tmp = cpuid_edx(0xC0000001);

		/* Enable ACE unit, if present and disabled */
		if ((tmp & (ACE_PRESENT | ACE_ENABLED)) == ACE_PRESENT) {
			rdmsr(MSR_ZHAOXIN_FCR57, lo, hi);
			/* Enable ACE unit */
			lo |= ACE_FCR;
			wrmsr(MSR_ZHAOXIN_FCR57, lo, hi);
			pr_info("CPU: Enabled ACE h/w crypto\n");
		}

		/* Enable RNG unit, if present and disabled */
		if ((tmp & (RNG_PRESENT | RNG_ENABLED)) == RNG_PRESENT) {
			rdmsr(MSR_ZHAOXIN_FCR57, lo, hi);
			/* Enable RNG unit */
			lo |= RNG_ENABLE;
			wrmsr(MSR_ZHAOXIN_FCR57, lo, hi);
			pr_info("CPU: Enabled h/w RNG\n");
		}

		/*
		 * Store Extended Feature Flags as word 5 of the CPU
		 * capability bit array
		 */
		c->x86_capability[CPUID_C000_0001_EDX] = cpuid_edx(0xC0000001);
	}

	if (c->x86 >= 0x6)
		set_cpu_cap(c, X86_FEATURE_REP_GOOD);
}

static void early_init_zhaoxin(struct cpuinfo_x86 *c)
{
	if (c->x86 >= 0x6)
		set_cpu_cap(c, X86_FEATURE_CONSTANT_TSC);
#ifdef CONFIG_X86_64
	set_cpu_cap(c, X86_FEATURE_SYSENTER32);
#endif
	if (c->x86_power & (1 << 8)) {
		set_cpu_cap(c, X86_FEATURE_CONSTANT_TSC);
		set_cpu_cap(c, X86_FEATURE_NONSTOP_TSC);
	}
}

static void init_zhaoxin(struct cpuinfo_x86 *c)
{
	early_init_zhaoxin(c);
	init_intel_cacheinfo(c);

	if (c->cpuid_level > 9) {
		unsigned int eax = cpuid_eax(10);

		/*
		 * Check for version and the number of counters
		 * Version(eax[7:0]) can't be 0;
		 * Counters(eax[15:8]) should be greater than 1;
		 */
		if ((eax & 0xff) && (((eax >> 8) & 0xff) > 1))
			set_cpu_cap(c, X86_FEATURE_ARCH_PERFMON);
	}

	if (c->x86 >= 0x6)
		init_zhaoxin_cap(c);
#ifdef CONFIG_X86_64
	set_cpu_cap(c, X86_FEATURE_LFENCE_RDTSC);
#endif

	init_ia32_feat_ctl(c);
}

#ifdef CONFIG_X86_32
static unsigned int
zhaoxin_size_cache(struct cpuinfo_x86 *c, unsigned int size)
{
	return size;
}
#endif

static const struct cpu_dev zhaoxin_cpu_dev = {
	.c_vendor	= "zhaoxin",
	.c_ident	= { "  Shanghai  " },
	.c_early_init	= early_init_zhaoxin,
	.c_init		= init_zhaoxin,
#ifdef CONFIG_X86_32
	.legacy_cache_size = zhaoxin_size_cache,
#endif
	.c_x86_vendor	= X86_VENDOR_ZHAOXIN,
};

cpu_dev_register(zhaoxin_cpu_dev);
