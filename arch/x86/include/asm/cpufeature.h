/*
 * Defines x86 CPU feature bits
 */
#ifndef _ASM_X86_CPUFEATURE_H
#define _ASM_X86_CPUFEATURE_H

#include <asm/required-features.h>

#define NCAPINTS	9	/* N 32-bit words worth of info */

/*
 * Note: If the comment begins with a quoted string, that string is used
 * in /proc/cpuinfo instead of the macro name.  If the string is "",
 * this feature bit is not displayed in /proc/cpuinfo at all.
 */

/* Intel-defined CPU features, CPUID level 0x00000001 (edx), word 0 */
#define X86_FEATURE_FPU		(0*32+ 0) /* Onboard FPU */
#define X86_FEATURE_VME		(0*32+ 1) /* Virtual Mode Extensions */
#define X86_FEATURE_DE		(0*32+ 2) /* Debugging Extensions */
#define X86_FEATURE_PSE		(0*32+ 3) /* Page Size Extensions */
#define X86_FEATURE_TSC		(0*32+ 4) /* Time Stamp Counter */
#define X86_FEATURE_MSR		(0*32+ 5) /* Model-Specific Registers */
#define X86_FEATURE_PAE		(0*32+ 6) /* Physical Address Extensions */
#define X86_FEATURE_MCE		(0*32+ 7) /* Machine Check Exception */
#define X86_FEATURE_CX8		(0*32+ 8) /* CMPXCHG8 instruction */
#define X86_FEATURE_APIC	(0*32+ 9) /* Onboard APIC */
#define X86_FEATURE_SEP		(0*32+11) /* SYSENTER/SYSEXIT */
#define X86_FEATURE_MTRR	(0*32+12) /* Memory Type Range Registers */
#define X86_FEATURE_PGE		(0*32+13) /* Page Global Enable */
#define X86_FEATURE_MCA		(0*32+14) /* Machine Check Architecture */
#define X86_FEATURE_CMOV	(0*32+15) /* CMOV instructions */
					  /* (plus FCMOVcc, FCOMI with FPU) */
#define X86_FEATURE_PAT		(0*32+16) /* Page Attribute Table */
#define X86_FEATURE_PSE36	(0*32+17) /* 36-bit PSEs */
#define X86_FEATURE_PN		(0*32+18) /* Processor serial number */
#define X86_FEATURE_CLFLSH	(0*32+19) /* "clflush" CLFLUSH instruction */
#define X86_FEATURE_DS		(0*32+21) /* "dts" Debug Store */
#define X86_FEATURE_ACPI	(0*32+22) /* ACPI via MSR */
#define X86_FEATURE_MMX		(0*32+23) /* Multimedia Extensions */
#define X86_FEATURE_FXSR	(0*32+24) /* FXSAVE/FXRSTOR, CR4.OSFXSR */
#define X86_FEATURE_XMM		(0*32+25) /* "sse" */
#define X86_FEATURE_XMM2	(0*32+26) /* "sse2" */
#define X86_FEATURE_SELFSNOOP	(0*32+27) /* "ss" CPU self snoop */
#define X86_FEATURE_HT		(0*32+28) /* Hyper-Threading */
#define X86_FEATURE_ACC		(0*32+29) /* "tm" Automatic clock control */
#define X86_FEATURE_IA64	(0*32+30) /* IA-64 processor */
#define X86_FEATURE_PBE		(0*32+31) /* Pending Break Enable */

/* AMD-defined CPU features, CPUID level 0x80000001, word 1 */
/* Don't duplicate feature flags which are redundant with Intel! */
#define X86_FEATURE_SYSCALL	(1*32+11) /* SYSCALL/SYSRET */
#define X86_FEATURE_MP		(1*32+19) /* MP Capable. */
#define X86_FEATURE_NX		(1*32+20) /* Execute Disable */
#define X86_FEATURE_MMXEXT	(1*32+22) /* AMD MMX extensions */
#define X86_FEATURE_FXSR_OPT	(1*32+25) /* FXSAVE/FXRSTOR optimizations */
#define X86_FEATURE_GBPAGES	(1*32+26) /* "pdpe1gb" GB pages */
#define X86_FEATURE_RDTSCP	(1*32+27) /* RDTSCP */
#define X86_FEATURE_LM		(1*32+29) /* Long Mode (x86-64) */
#define X86_FEATURE_3DNOWEXT	(1*32+30) /* AMD 3DNow! extensions */
#define X86_FEATURE_3DNOW	(1*32+31) /* 3DNow! */

/* Transmeta-defined CPU features, CPUID level 0x80860001, word 2 */
#define X86_FEATURE_RECOVERY	(2*32+ 0) /* CPU in recovery mode */
#define X86_FEATURE_LONGRUN	(2*32+ 1) /* Longrun power control */
#define X86_FEATURE_LRTI	(2*32+ 3) /* LongRun table interface */

/* Other features, Linux-defined mapping, word 3 */
/* This range is used for feature bits which conflict or are synthesized */
#define X86_FEATURE_CXMMX	(3*32+ 0) /* Cyrix MMX extensions */
#define X86_FEATURE_K6_MTRR	(3*32+ 1) /* AMD K6 nonstandard MTRRs */
#define X86_FEATURE_CYRIX_ARR	(3*32+ 2) /* Cyrix ARRs (= MTRRs) */
#define X86_FEATURE_CENTAUR_MCR	(3*32+ 3) /* Centaur MCRs (= MTRRs) */
/* cpu types for specific tunings: */
#define X86_FEATURE_K8		(3*32+ 4) /* "" Opteron, Athlon64 */
#define X86_FEATURE_K7		(3*32+ 5) /* "" Athlon */
#define X86_FEATURE_P3		(3*32+ 6) /* "" P3 */
#define X86_FEATURE_P4		(3*32+ 7) /* "" P4 */
#define X86_FEATURE_CONSTANT_TSC (3*32+ 8) /* TSC ticks at a constant rate */
#define X86_FEATURE_UP		(3*32+ 9) /* smp kernel running on up */
#define X86_FEATURE_FXSAVE_LEAK (3*32+10) /* "" FXSAVE leaks FOP/FIP/FOP */
#define X86_FEATURE_ARCH_PERFMON (3*32+11) /* Intel Architectural PerfMon */
#define X86_FEATURE_PEBS	(3*32+12) /* Precise-Event Based Sampling */
#define X86_FEATURE_BTS		(3*32+13) /* Branch Trace Store */
#define X86_FEATURE_SYSCALL32	(3*32+14) /* "" syscall in ia32 userspace */
#define X86_FEATURE_SYSENTER32	(3*32+15) /* "" sysenter in ia32 userspace */
#define X86_FEATURE_REP_GOOD	(3*32+16) /* rep microcode works well */
#define X86_FEATURE_MFENCE_RDTSC (3*32+17) /* "" Mfence synchronizes RDTSC */
#define X86_FEATURE_LFENCE_RDTSC (3*32+18) /* "" Lfence synchronizes RDTSC */
#define X86_FEATURE_11AP	(3*32+19) /* "" Bad local APIC aka 11AP */
#define X86_FEATURE_NOPL	(3*32+20) /* The NOPL (0F 1F) instructions */
#define X86_FEATURE_AMDC1E	(3*32+21) /* AMD C1E detected */
#define X86_FEATURE_XTOPOLOGY	(3*32+22) /* cpu topology enum extensions */
#define X86_FEATURE_TSC_RELIABLE (3*32+23) /* TSC is known to be reliable */
#define X86_FEATURE_NONSTOP_TSC	(3*32+24) /* TSC does not stop in C states */
#define X86_FEATURE_CLFLUSH_MONITOR (3*32+25) /* "" clflush reqd with monitor */
#define X86_FEATURE_EXTD_APICID	(3*32+26) /* has extended APICID (8 bits) */
#define X86_FEATURE_AMD_DCM     (3*32+27) /* multi-node processor */
#define X86_FEATURE_APERFMPERF	(3*32+28) /* APERFMPERF */

/* Intel-defined CPU features, CPUID level 0x00000001 (ecx), word 4 */
#define X86_FEATURE_XMM3	(4*32+ 0) /* "pni" SSE-3 */
#define X86_FEATURE_PCLMULQDQ	(4*32+ 1) /* PCLMULQDQ instruction */
#define X86_FEATURE_DTES64	(4*32+ 2) /* 64-bit Debug Store */
#define X86_FEATURE_MWAIT	(4*32+ 3) /* "monitor" Monitor/Mwait support */
#define X86_FEATURE_DSCPL	(4*32+ 4) /* "ds_cpl" CPL Qual. Debug Store */
#define X86_FEATURE_VMX		(4*32+ 5) /* Hardware virtualization */
#define X86_FEATURE_SMX		(4*32+ 6) /* Safer mode */
#define X86_FEATURE_EST		(4*32+ 7) /* Enhanced SpeedStep */
#define X86_FEATURE_TM2		(4*32+ 8) /* Thermal Monitor 2 */
#define X86_FEATURE_SSSE3	(4*32+ 9) /* Supplemental SSE-3 */
#define X86_FEATURE_CID		(4*32+10) /* Context ID */
#define X86_FEATURE_FMA		(4*32+12) /* Fused multiply-add */
#define X86_FEATURE_CX16	(4*32+13) /* CMPXCHG16B */
#define X86_FEATURE_XTPR	(4*32+14) /* Send Task Priority Messages */
#define X86_FEATURE_PDCM	(4*32+15) /* Performance Capabilities */
#define X86_FEATURE_DCA		(4*32+18) /* Direct Cache Access */
#define X86_FEATURE_XMM4_1	(4*32+19) /* "sse4_1" SSE-4.1 */
#define X86_FEATURE_XMM4_2	(4*32+20) /* "sse4_2" SSE-4.2 */
#define X86_FEATURE_X2APIC	(4*32+21) /* x2APIC */
#define X86_FEATURE_MOVBE	(4*32+22) /* MOVBE instruction */
#define X86_FEATURE_POPCNT      (4*32+23) /* POPCNT instruction */
#define X86_FEATURE_AES		(4*32+25) /* AES instructions */
#define X86_FEATURE_XSAVE	(4*32+26) /* XSAVE/XRSTOR/XSETBV/XGETBV */
#define X86_FEATURE_OSXSAVE	(4*32+27) /* "" XSAVE enabled in the OS */
#define X86_FEATURE_AVX		(4*32+28) /* Advanced Vector Extensions */
#define X86_FEATURE_HYPERVISOR	(4*32+31) /* Running on a hypervisor */

/* VIA/Cyrix/Centaur-defined CPU features, CPUID level 0xC0000001, word 5 */
#define X86_FEATURE_XSTORE	(5*32+ 2) /* "rng" RNG present (xstore) */
#define X86_FEATURE_XSTORE_EN	(5*32+ 3) /* "rng_en" RNG enabled */
#define X86_FEATURE_XCRYPT	(5*32+ 6) /* "ace" on-CPU crypto (xcrypt) */
#define X86_FEATURE_XCRYPT_EN	(5*32+ 7) /* "ace_en" on-CPU crypto enabled */
#define X86_FEATURE_ACE2	(5*32+ 8) /* Advanced Cryptography Engine v2 */
#define X86_FEATURE_ACE2_EN	(5*32+ 9) /* ACE v2 enabled */
#define X86_FEATURE_PHE		(5*32+10) /* PadLock Hash Engine */
#define X86_FEATURE_PHE_EN	(5*32+11) /* PHE enabled */
#define X86_FEATURE_PMM		(5*32+12) /* PadLock Montgomery Multiplier */
#define X86_FEATURE_PMM_EN	(5*32+13) /* PMM enabled */

/* More extended AMD flags: CPUID level 0x80000001, ecx, word 6 */
#define X86_FEATURE_LAHF_LM	(6*32+ 0) /* LAHF/SAHF in long mode */
#define X86_FEATURE_CMP_LEGACY	(6*32+ 1) /* If yes HyperThreading not valid */
#define X86_FEATURE_SVM		(6*32+ 2) /* Secure virtual machine */
#define X86_FEATURE_EXTAPIC	(6*32+ 3) /* Extended APIC space */
#define X86_FEATURE_CR8_LEGACY	(6*32+ 4) /* CR8 in 32-bit mode */
#define X86_FEATURE_ABM		(6*32+ 5) /* Advanced bit manipulation */
#define X86_FEATURE_SSE4A	(6*32+ 6) /* SSE-4A */
#define X86_FEATURE_MISALIGNSSE (6*32+ 7) /* Misaligned SSE mode */
#define X86_FEATURE_3DNOWPREFETCH (6*32+ 8) /* 3DNow prefetch instructions */
#define X86_FEATURE_OSVW	(6*32+ 9) /* OS Visible Workaround */
#define X86_FEATURE_IBS		(6*32+10) /* Instruction Based Sampling */
#define X86_FEATURE_SSE5	(6*32+11) /* SSE-5 */
#define X86_FEATURE_SKINIT	(6*32+12) /* SKINIT/STGI instructions */
#define X86_FEATURE_WDT		(6*32+13) /* Watchdog timer */
#define X86_FEATURE_NODEID_MSR	(6*32+19) /* NodeId MSR */

/*
 * Auxiliary flags: Linux defined - For features scattered in various
 * CPUID levels like 0x6, 0xA etc
 */
#define X86_FEATURE_IDA		(7*32+ 0) /* Intel Dynamic Acceleration */
#define X86_FEATURE_ARAT	(7*32+ 1) /* Always Running APIC Timer */

/* Virtualization flags: Linux defined */
#define X86_FEATURE_TPR_SHADOW  (8*32+ 0) /* Intel TPR Shadow */
#define X86_FEATURE_VNMI        (8*32+ 1) /* Intel Virtual NMI */
#define X86_FEATURE_FLEXPRIORITY (8*32+ 2) /* Intel FlexPriority */
#define X86_FEATURE_EPT         (8*32+ 3) /* Intel Extended Page Table */
#define X86_FEATURE_VPID        (8*32+ 4) /* Intel Virtual Processor ID */

#if defined(__KERNEL__) && !defined(__ASSEMBLY__)

#include <linux/bitops.h>

extern const char * const x86_cap_flags[NCAPINTS*32];
extern const char * const x86_power_flags[32];

#define test_cpu_cap(c, bit)						\
	 test_bit(bit, (unsigned long *)((c)->x86_capability))

#define cpu_has(c, bit)							\
	(__builtin_constant_p(bit) &&					\
	 ( (((bit)>>5)==0 && (1UL<<((bit)&31) & REQUIRED_MASK0)) ||	\
	   (((bit)>>5)==1 && (1UL<<((bit)&31) & REQUIRED_MASK1)) ||	\
	   (((bit)>>5)==2 && (1UL<<((bit)&31) & REQUIRED_MASK2)) ||	\
	   (((bit)>>5)==3 && (1UL<<((bit)&31) & REQUIRED_MASK3)) ||	\
	   (((bit)>>5)==4 && (1UL<<((bit)&31) & REQUIRED_MASK4)) ||	\
	   (((bit)>>5)==5 && (1UL<<((bit)&31) & REQUIRED_MASK5)) ||	\
	   (((bit)>>5)==6 && (1UL<<((bit)&31) & REQUIRED_MASK6)) ||	\
	   (((bit)>>5)==7 && (1UL<<((bit)&31) & REQUIRED_MASK7)) )	\
	  ? 1 :								\
	 test_cpu_cap(c, bit))

#define boot_cpu_has(bit)	cpu_has(&boot_cpu_data, bit)

#define set_cpu_cap(c, bit)	set_bit(bit, (unsigned long *)((c)->x86_capability))
#define clear_cpu_cap(c, bit)	clear_bit(bit, (unsigned long *)((c)->x86_capability))
#define setup_clear_cpu_cap(bit) do { \
	clear_cpu_cap(&boot_cpu_data, bit);	\
	set_bit(bit, (unsigned long *)cpu_caps_cleared); \
} while (0)
#define setup_force_cpu_cap(bit) do { \
	set_cpu_cap(&boot_cpu_data, bit);	\
	set_bit(bit, (unsigned long *)cpu_caps_set);	\
} while (0)

#define cpu_has_fpu		boot_cpu_has(X86_FEATURE_FPU)
#define cpu_has_vme		boot_cpu_has(X86_FEATURE_VME)
#define cpu_has_de		boot_cpu_has(X86_FEATURE_DE)
#define cpu_has_pse		boot_cpu_has(X86_FEATURE_PSE)
#define cpu_has_tsc		boot_cpu_has(X86_FEATURE_TSC)
#define cpu_has_pae		boot_cpu_has(X86_FEATURE_PAE)
#define cpu_has_pge		boot_cpu_has(X86_FEATURE_PGE)
#define cpu_has_apic		boot_cpu_has(X86_FEATURE_APIC)
#define cpu_has_sep		boot_cpu_has(X86_FEATURE_SEP)
#define cpu_has_mtrr		boot_cpu_has(X86_FEATURE_MTRR)
#define cpu_has_mmx		boot_cpu_has(X86_FEATURE_MMX)
#define cpu_has_fxsr		boot_cpu_has(X86_FEATURE_FXSR)
#define cpu_has_xmm		boot_cpu_has(X86_FEATURE_XMM)
#define cpu_has_xmm2		boot_cpu_has(X86_FEATURE_XMM2)
#define cpu_has_xmm3		boot_cpu_has(X86_FEATURE_XMM3)
#define cpu_has_aes		boot_cpu_has(X86_FEATURE_AES)
#define cpu_has_ht		boot_cpu_has(X86_FEATURE_HT)
#define cpu_has_mp		boot_cpu_has(X86_FEATURE_MP)
#define cpu_has_nx		boot_cpu_has(X86_FEATURE_NX)
#define cpu_has_k6_mtrr		boot_cpu_has(X86_FEATURE_K6_MTRR)
#define cpu_has_cyrix_arr	boot_cpu_has(X86_FEATURE_CYRIX_ARR)
#define cpu_has_centaur_mcr	boot_cpu_has(X86_FEATURE_CENTAUR_MCR)
#define cpu_has_xstore		boot_cpu_has(X86_FEATURE_XSTORE)
#define cpu_has_xstore_enabled	boot_cpu_has(X86_FEATURE_XSTORE_EN)
#define cpu_has_xcrypt		boot_cpu_has(X86_FEATURE_XCRYPT)
#define cpu_has_xcrypt_enabled	boot_cpu_has(X86_FEATURE_XCRYPT_EN)
#define cpu_has_ace2		boot_cpu_has(X86_FEATURE_ACE2)
#define cpu_has_ace2_enabled	boot_cpu_has(X86_FEATURE_ACE2_EN)
#define cpu_has_phe		boot_cpu_has(X86_FEATURE_PHE)
#define cpu_has_phe_enabled	boot_cpu_has(X86_FEATURE_PHE_EN)
#define cpu_has_pmm		boot_cpu_has(X86_FEATURE_PMM)
#define cpu_has_pmm_enabled	boot_cpu_has(X86_FEATURE_PMM_EN)
#define cpu_has_ds		boot_cpu_has(X86_FEATURE_DS)
#define cpu_has_pebs		boot_cpu_has(X86_FEATURE_PEBS)
#define cpu_has_clflush		boot_cpu_has(X86_FEATURE_CLFLSH)
#define cpu_has_bts		boot_cpu_has(X86_FEATURE_BTS)
#define cpu_has_gbpages		boot_cpu_has(X86_FEATURE_GBPAGES)
#define cpu_has_arch_perfmon	boot_cpu_has(X86_FEATURE_ARCH_PERFMON)
#define cpu_has_pat		boot_cpu_has(X86_FEATURE_PAT)
#define cpu_has_xmm4_1		boot_cpu_has(X86_FEATURE_XMM4_1)
#define cpu_has_xmm4_2		boot_cpu_has(X86_FEATURE_XMM4_2)
#define cpu_has_x2apic		boot_cpu_has(X86_FEATURE_X2APIC)
#define cpu_has_xsave		boot_cpu_has(X86_FEATURE_XSAVE)
#define cpu_has_hypervisor	boot_cpu_has(X86_FEATURE_HYPERVISOR)

#if defined(CONFIG_X86_INVLPG) || defined(CONFIG_X86_64)
# define cpu_has_invlpg		1
#else
# define cpu_has_invlpg		(boot_cpu_data.x86 > 3)
#endif

#ifdef CONFIG_X86_64

#undef  cpu_has_vme
#define cpu_has_vme		0

#undef  cpu_has_pae
#define cpu_has_pae		___BUG___

#undef  cpu_has_mp
#define cpu_has_mp		1

#undef  cpu_has_k6_mtrr
#define cpu_has_k6_mtrr		0

#undef  cpu_has_cyrix_arr
#define cpu_has_cyrix_arr	0

#undef  cpu_has_centaur_mcr
#define cpu_has_centaur_mcr	0

#endif /* CONFIG_X86_64 */

#endif /* defined(__KERNEL__) && !defined(__ASSEMBLY__) */

#endif /* _ASM_X86_CPUFEATURE_H */
