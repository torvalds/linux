// SPDX-License-Identifier: GPL-2.0

#include <linux/sched.h>
#include <linux/sched/clock.h>

#include <asm/cpufeature.h>
#include <asm/e820/api.h>
#include <asm/mtrr.h>
#include <asm/msr.h>

#include "cpu.h"

#define ACE_PRESENT	(1 << 6)
#define ACE_ENABLED	(1 << 7)
#define ACE_FCR		(1 << 28)	/* MSR_VIA_FCR */

#define RNG_PRESENT	(1 << 2)
#define RNG_ENABLED	(1 << 3)
#define RNG_ENABLE	(1 << 6)	/* MSR_VIA_RNG */

static void init_c3(struct cpuinfo_x86 *c)
{
	u32  lo, hi;

	/* Test for Centaur Extended Feature Flags presence */
	if (cpuid_eax(0xC0000000) >= 0xC0000001) {
		u32 tmp = cpuid_edx(0xC0000001);

		/* enable ACE unit, if present and disabled */
		if ((tmp & (ACE_PRESENT | ACE_ENABLED)) == ACE_PRESENT) {
			rdmsr(MSR_VIA_FCR, lo, hi);
			lo |= ACE_FCR;		/* enable ACE unit */
			wrmsr(MSR_VIA_FCR, lo, hi);
			pr_info("CPU: Enabled ACE h/w crypto\n");
		}

		/* enable RNG unit, if present and disabled */
		if ((tmp & (RNG_PRESENT | RNG_ENABLED)) == RNG_PRESENT) {
			rdmsr(MSR_VIA_RNG, lo, hi);
			lo |= RNG_ENABLE;	/* enable RNG unit */
			wrmsr(MSR_VIA_RNG, lo, hi);
			pr_info("CPU: Enabled h/w RNG\n");
		}

		/* store Centaur Extended Feature Flags as
		 * word 5 of the CPU capability bit array
		 */
		c->x86_capability[CPUID_C000_0001_EDX] = cpuid_edx(0xC0000001);
	}
#ifdef CONFIG_X86_32
	/* Cyrix III family needs CX8 & PGE explicitly enabled. */
	if (c->x86_model >= 6 && c->x86_model <= 13) {
		rdmsr(MSR_VIA_FCR, lo, hi);
		lo |= (1<<1 | 1<<7);
		wrmsr(MSR_VIA_FCR, lo, hi);
		set_cpu_cap(c, X86_FEATURE_CX8);
	}

	/* Before Nehemiah, the C3's had 3dNOW! */
	if (c->x86_model >= 6 && c->x86_model < 9)
		set_cpu_cap(c, X86_FEATURE_3DNOW);
#endif
	if (c->x86 == 0x6 && c->x86_model >= 0xf) {
		c->x86_cache_alignment = c->x86_clflush_size * 2;
		set_cpu_cap(c, X86_FEATURE_REP_GOOD);
	}
}

enum {
		ECX8		= 1<<1,
		EIERRINT	= 1<<2,
		DPM		= 1<<3,
		DMCE		= 1<<4,
		DSTPCLK		= 1<<5,
		ELINEAR		= 1<<6,
		DSMC		= 1<<7,
		DTLOCK		= 1<<8,
		EDCTLB		= 1<<8,
		EMMX		= 1<<9,
		DPDC		= 1<<11,
		EBRPRED		= 1<<12,
		DIC		= 1<<13,
		DDC		= 1<<14,
		DNA		= 1<<15,
		ERETSTK		= 1<<16,
		E2MMX		= 1<<19,
		EAMD3D		= 1<<20,
};

static void early_init_centaur(struct cpuinfo_x86 *c)
{
	switch (c->x86) {
#ifdef CONFIG_X86_32
	case 5:
		/* Emulate MTRRs using Centaur's MCR. */
		set_cpu_cap(c, X86_FEATURE_CENTAUR_MCR);
		break;
#endif
	case 6:
		if (c->x86_model >= 0xf)
			set_cpu_cap(c, X86_FEATURE_CONSTANT_TSC);
		break;
	}
#ifdef CONFIG_X86_64
	set_cpu_cap(c, X86_FEATURE_SYSENTER32);
#endif
	if (c->x86_power & (1 << 8)) {
		set_cpu_cap(c, X86_FEATURE_CONSTANT_TSC);
		set_cpu_cap(c, X86_FEATURE_NONSTOP_TSC);
	}
}

static void init_centaur(struct cpuinfo_x86 *c)
{
#ifdef CONFIG_X86_32
	char *name;
	u32  fcr_set = 0;
	u32  fcr_clr = 0;
	u32  lo, hi, newlo;
	u32  aa, bb, cc, dd;

	/*
	 * Bit 31 in normal CPUID used for nonstandard 3DNow ID;
	 * 3DNow is IDd by bit 31 in extended CPUID (1*32+31) anyway
	 */
	clear_cpu_cap(c, 0*32+31);
#endif
	early_init_centaur(c);
	init_intel_cacheinfo(c);
	detect_num_cpu_cores(c);
#ifdef CONFIG_X86_32
	detect_ht(c);
#endif

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

	switch (c->x86) {
#ifdef CONFIG_X86_32
	case 5:
		switch (c->x86_model) {
		case 4:
			name = "C6";
			fcr_set = ECX8|DSMC|EDCTLB|EMMX|ERETSTK;
			fcr_clr = DPDC;
			pr_notice("Disabling bugged TSC.\n");
			clear_cpu_cap(c, X86_FEATURE_TSC);
			break;
		case 8:
			switch (c->x86_stepping) {
			default:
			name = "2";
				break;
			case 7 ... 9:
				name = "2A";
				break;
			case 10 ... 15:
				name = "2B";
				break;
			}
			fcr_set = ECX8|DSMC|DTLOCK|EMMX|EBRPRED|ERETSTK|
				  E2MMX|EAMD3D;
			fcr_clr = DPDC;
			break;
		case 9:
			name = "3";
			fcr_set = ECX8|DSMC|DTLOCK|EMMX|EBRPRED|ERETSTK|
				  E2MMX|EAMD3D;
			fcr_clr = DPDC;
			break;
		default:
			name = "??";
		}

		rdmsr(MSR_IDT_FCR1, lo, hi);
		newlo = (lo|fcr_set) & (~fcr_clr);

		if (newlo != lo) {
			pr_info("Centaur FCR was 0x%X now 0x%X\n",
				lo, newlo);
			wrmsr(MSR_IDT_FCR1, newlo, hi);
		} else {
			pr_info("Centaur FCR is 0x%X\n", lo);
		}
		/* Emulate MTRRs using Centaur's MCR. */
		set_cpu_cap(c, X86_FEATURE_CENTAUR_MCR);
		/* Report CX8 */
		set_cpu_cap(c, X86_FEATURE_CX8);
		/* Set 3DNow! on Winchip 2 and above. */
		if (c->x86_model >= 8)
			set_cpu_cap(c, X86_FEATURE_3DNOW);
		/* See if we can find out some more. */
		if (cpuid_eax(0x80000000) >= 0x80000005) {
			/* Yes, we can. */
			cpuid(0x80000005, &aa, &bb, &cc, &dd);
			/* Add L1 data and code cache sizes. */
			c->x86_cache_size = (cc>>24)+(dd>>24);
		}
		sprintf(c->x86_model_id, "WinChip %s", name);
		break;
#endif
	case 6:
		init_c3(c);
		break;
	}
#ifdef CONFIG_X86_64
	set_cpu_cap(c, X86_FEATURE_LFENCE_RDTSC);
#endif

	init_ia32_feat_ctl(c);
}

#ifdef CONFIG_X86_32
static unsigned int
centaur_size_cache(struct cpuinfo_x86 *c, unsigned int size)
{
	/* VIA C3 CPUs (670-68F) need further shifting. */
	if ((c->x86 == 6) && ((c->x86_model == 7) || (c->x86_model == 8)))
		size >>= 8;

	/*
	 * There's also an erratum in Nehemiah stepping 1, which
	 * returns '65KB' instead of '64KB'
	 *  - Note, it seems this may only be in engineering samples.
	 */
	if ((c->x86 == 6) && (c->x86_model == 9) &&
				(c->x86_stepping == 1) && (size == 65))
		size -= 1;
	return size;
}
#endif

static const struct cpu_dev centaur_cpu_dev = {
	.c_vendor	= "Centaur",
	.c_ident	= { "CentaurHauls" },
	.c_early_init	= early_init_centaur,
	.c_init		= init_centaur,
#ifdef CONFIG_X86_32
	.legacy_cache_size = centaur_size_cache,
#endif
	.c_x86_vendor	= X86_VENDOR_CENTAUR,
};

cpu_dev_register(centaur_cpu_dev);
