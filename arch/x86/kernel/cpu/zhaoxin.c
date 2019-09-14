// SPDX-License-Identifier: GPL-2.0
#include <linux/sched.h>
#include <linux/sched/clock.h>

#include <asm/cpufeature.h>

#include "cpu.h"

#define MSR_ZHAOXIN_FCR57 0x00001257

#define ACE_PRESENT	(1 << 6)
#define ACE_ENABLED	(1 << 7)
#define ACE_FCR		(1 << 7)	/* MSR_ZHAOXIN_FCR */

#define RNG_PRESENT	(1 << 2)
#define RNG_ENABLED	(1 << 3)
#define RNG_ENABLE	(1 << 8)	/* MSR_ZHAOXIN_RNG */

#define X86_VMX_FEATURE_PROC_CTLS_TPR_SHADOW	0x00200000
#define X86_VMX_FEATURE_PROC_CTLS_VNMI		0x00400000
#define X86_VMX_FEATURE_PROC_CTLS_2ND_CTLS	0x80000000
#define X86_VMX_FEATURE_PROC_CTLS2_VIRT_APIC	0x00000001
#define X86_VMX_FEATURE_PROC_CTLS2_EPT		0x00000002
#define X86_VMX_FEATURE_PROC_CTLS2_VPID		0x00000020

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

	cpu_detect_cache_sizes(c);
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

	if (c->cpuid_level >= 0x00000001) {
		u32 eax, ebx, ecx, edx;

		cpuid(0x00000001, &eax, &ebx, &ecx, &edx);
		/*
		 * If HTT (EDX[28]) is set EBX[16:23] contain the number of
		 * apicids which are reserved per package. Store the resulting
		 * shift value for the package management code.
		 */
		if (edx & (1U << 28))
			c->x86_coreid_bits = get_count_order((ebx >> 16) & 0xff);
	}

}

static void zhaoxin_detect_vmx_virtcap(struct cpuinfo_x86 *c)
{
	u32 vmx_msr_low, vmx_msr_high, msr_ctl, msr_ctl2;

	rdmsr(MSR_IA32_VMX_PROCBASED_CTLS, vmx_msr_low, vmx_msr_high);
	msr_ctl = vmx_msr_high | vmx_msr_low;

	if (msr_ctl & X86_VMX_FEATURE_PROC_CTLS_TPR_SHADOW)
		set_cpu_cap(c, X86_FEATURE_TPR_SHADOW);
	if (msr_ctl & X86_VMX_FEATURE_PROC_CTLS_VNMI)
		set_cpu_cap(c, X86_FEATURE_VNMI);
	if (msr_ctl & X86_VMX_FEATURE_PROC_CTLS_2ND_CTLS) {
		rdmsr(MSR_IA32_VMX_PROCBASED_CTLS2,
		      vmx_msr_low, vmx_msr_high);
		msr_ctl2 = vmx_msr_high | vmx_msr_low;
		if ((msr_ctl2 & X86_VMX_FEATURE_PROC_CTLS2_VIRT_APIC) &&
		    (msr_ctl & X86_VMX_FEATURE_PROC_CTLS_TPR_SHADOW))
			set_cpu_cap(c, X86_FEATURE_FLEXPRIORITY);
		if (msr_ctl2 & X86_VMX_FEATURE_PROC_CTLS2_EPT)
			set_cpu_cap(c, X86_FEATURE_EPT);
		if (msr_ctl2 & X86_VMX_FEATURE_PROC_CTLS2_VPID)
			set_cpu_cap(c, X86_FEATURE_VPID);
	}
}

static void init_zhaoxin(struct cpuinfo_x86 *c)
{
	early_init_zhaoxin(c);
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

	if (c->x86 >= 0x6)
		init_zhaoxin_cap(c);
#ifdef CONFIG_X86_64
	set_cpu_cap(c, X86_FEATURE_LFENCE_RDTSC);
#endif

	if (cpu_has(c, X86_FEATURE_VMX))
		zhaoxin_detect_vmx_virtcap(c);
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
