/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_CPUFEATURES_H
#define _ASM_X86_CPUFEATURES_H

#ifndef _ASM_X86_REQUIRED_FEATURES_H
#include <asm/required-features.h>
#endif

#ifndef _ASM_X86_DISABLED_FEATURES_H
#include <asm/disabled-features.h>
#endif

/*
 * Defines x86 CPU feature bits
 */
#define NCAPINTS			22	   /* N 32-bit words worth of info */
#define NBUGINTS			2	   /* N 32-bit bug flags */

/*
 * Note: If the comment begins with a quoted string, that string is used
 * in /proc/cpuinfo instead of the macro name.  Otherwise, this feature
 * bit is not displayed in /proc/cpuinfo at all.
 *
 * When adding new features here that depend on other features,
 * please update the table in kernel/cpu/cpuid-deps.c as well.
 */

/* Intel-defined CPU features, CPUID level 0x00000001 (EDX), word 0 */
#define X86_FEATURE_FPU			( 0*32+ 0) /* "fpu" Onboard FPU */
#define X86_FEATURE_VME			( 0*32+ 1) /* "vme" Virtual Mode Extensions */
#define X86_FEATURE_DE			( 0*32+ 2) /* "de" Debugging Extensions */
#define X86_FEATURE_PSE			( 0*32+ 3) /* "pse" Page Size Extensions */
#define X86_FEATURE_TSC			( 0*32+ 4) /* "tsc" Time Stamp Counter */
#define X86_FEATURE_MSR			( 0*32+ 5) /* "msr" Model-Specific Registers */
#define X86_FEATURE_PAE			( 0*32+ 6) /* "pae" Physical Address Extensions */
#define X86_FEATURE_MCE			( 0*32+ 7) /* "mce" Machine Check Exception */
#define X86_FEATURE_CX8			( 0*32+ 8) /* "cx8" CMPXCHG8 instruction */
#define X86_FEATURE_APIC		( 0*32+ 9) /* "apic" Onboard APIC */
#define X86_FEATURE_SEP			( 0*32+11) /* "sep" SYSENTER/SYSEXIT */
#define X86_FEATURE_MTRR		( 0*32+12) /* "mtrr" Memory Type Range Registers */
#define X86_FEATURE_PGE			( 0*32+13) /* "pge" Page Global Enable */
#define X86_FEATURE_MCA			( 0*32+14) /* "mca" Machine Check Architecture */
#define X86_FEATURE_CMOV		( 0*32+15) /* "cmov" CMOV instructions (plus FCMOVcc, FCOMI with FPU) */
#define X86_FEATURE_PAT			( 0*32+16) /* "pat" Page Attribute Table */
#define X86_FEATURE_PSE36		( 0*32+17) /* "pse36" 36-bit PSEs */
#define X86_FEATURE_PN			( 0*32+18) /* "pn" Processor serial number */
#define X86_FEATURE_CLFLUSH		( 0*32+19) /* "clflush" CLFLUSH instruction */
#define X86_FEATURE_DS			( 0*32+21) /* "dts" Debug Store */
#define X86_FEATURE_ACPI		( 0*32+22) /* "acpi" ACPI via MSR */
#define X86_FEATURE_MMX			( 0*32+23) /* "mmx" Multimedia Extensions */
#define X86_FEATURE_FXSR		( 0*32+24) /* "fxsr" FXSAVE/FXRSTOR, CR4.OSFXSR */
#define X86_FEATURE_XMM			( 0*32+25) /* "sse" */
#define X86_FEATURE_XMM2		( 0*32+26) /* "sse2" */
#define X86_FEATURE_SELFSNOOP		( 0*32+27) /* "ss" CPU self snoop */
#define X86_FEATURE_HT			( 0*32+28) /* "ht" Hyper-Threading */
#define X86_FEATURE_ACC			( 0*32+29) /* "tm" Automatic clock control */
#define X86_FEATURE_IA64		( 0*32+30) /* "ia64" IA-64 processor */
#define X86_FEATURE_PBE			( 0*32+31) /* "pbe" Pending Break Enable */

/* AMD-defined CPU features, CPUID level 0x80000001, word 1 */
/* Don't duplicate feature flags which are redundant with Intel! */
#define X86_FEATURE_SYSCALL		( 1*32+11) /* "syscall" SYSCALL/SYSRET */
#define X86_FEATURE_MP			( 1*32+19) /* "mp" MP Capable */
#define X86_FEATURE_NX			( 1*32+20) /* "nx" Execute Disable */
#define X86_FEATURE_MMXEXT		( 1*32+22) /* "mmxext" AMD MMX extensions */
#define X86_FEATURE_FXSR_OPT		( 1*32+25) /* "fxsr_opt" FXSAVE/FXRSTOR optimizations */
#define X86_FEATURE_GBPAGES		( 1*32+26) /* "pdpe1gb" GB pages */
#define X86_FEATURE_RDTSCP		( 1*32+27) /* "rdtscp" RDTSCP */
#define X86_FEATURE_LM			( 1*32+29) /* "lm" Long Mode (x86-64, 64-bit support) */
#define X86_FEATURE_3DNOWEXT		( 1*32+30) /* "3dnowext" AMD 3DNow extensions */
#define X86_FEATURE_3DNOW		( 1*32+31) /* "3dnow" 3DNow */

/* Transmeta-defined CPU features, CPUID level 0x80860001, word 2 */
#define X86_FEATURE_RECOVERY		( 2*32+ 0) /* "recovery" CPU in recovery mode */
#define X86_FEATURE_LONGRUN		( 2*32+ 1) /* "longrun" Longrun power control */
#define X86_FEATURE_LRTI		( 2*32+ 3) /* "lrti" LongRun table interface */

/* Other features, Linux-defined mapping, word 3 */
/* This range is used for feature bits which conflict or are synthesized */
#define X86_FEATURE_CXMMX		( 3*32+ 0) /* "cxmmx" Cyrix MMX extensions */
#define X86_FEATURE_K6_MTRR		( 3*32+ 1) /* "k6_mtrr" AMD K6 nonstandard MTRRs */
#define X86_FEATURE_CYRIX_ARR		( 3*32+ 2) /* "cyrix_arr" Cyrix ARRs (= MTRRs) */
#define X86_FEATURE_CENTAUR_MCR		( 3*32+ 3) /* "centaur_mcr" Centaur MCRs (= MTRRs) */
#define X86_FEATURE_K8			( 3*32+ 4) /* Opteron, Athlon64 */
#define X86_FEATURE_ZEN5		( 3*32+ 5) /* CPU based on Zen5 microarchitecture */
/* Free                                 ( 3*32+ 6) */
/* Free                                 ( 3*32+ 7) */
#define X86_FEATURE_CONSTANT_TSC	( 3*32+ 8) /* "constant_tsc" TSC ticks at a constant rate */
#define X86_FEATURE_UP			( 3*32+ 9) /* "up" SMP kernel running on UP */
#define X86_FEATURE_ART			( 3*32+10) /* "art" Always running timer (ART) */
#define X86_FEATURE_ARCH_PERFMON	( 3*32+11) /* "arch_perfmon" Intel Architectural PerfMon */
#define X86_FEATURE_PEBS		( 3*32+12) /* "pebs" Precise-Event Based Sampling */
#define X86_FEATURE_BTS			( 3*32+13) /* "bts" Branch Trace Store */
#define X86_FEATURE_SYSCALL32		( 3*32+14) /* syscall in IA32 userspace */
#define X86_FEATURE_SYSENTER32		( 3*32+15) /* sysenter in IA32 userspace */
#define X86_FEATURE_REP_GOOD		( 3*32+16) /* "rep_good" REP microcode works well */
#define X86_FEATURE_AMD_LBR_V2		( 3*32+17) /* "amd_lbr_v2" AMD Last Branch Record Extension Version 2 */
#define X86_FEATURE_CLEAR_CPU_BUF	( 3*32+18) /* Clear CPU buffers using VERW */
#define X86_FEATURE_ACC_POWER		( 3*32+19) /* "acc_power" AMD Accumulated Power Mechanism */
#define X86_FEATURE_NOPL		( 3*32+20) /* "nopl" The NOPL (0F 1F) instructions */
#define X86_FEATURE_ALWAYS		( 3*32+21) /* Always-present feature */
#define X86_FEATURE_XTOPOLOGY		( 3*32+22) /* "xtopology" CPU topology enum extensions */
#define X86_FEATURE_TSC_RELIABLE	( 3*32+23) /* "tsc_reliable" TSC is known to be reliable */
#define X86_FEATURE_NONSTOP_TSC		( 3*32+24) /* "nonstop_tsc" TSC does not stop in C states */
#define X86_FEATURE_CPUID		( 3*32+25) /* "cpuid" CPU has CPUID instruction itself */
#define X86_FEATURE_EXTD_APICID		( 3*32+26) /* "extd_apicid" Extended APICID (8 bits) */
#define X86_FEATURE_AMD_DCM		( 3*32+27) /* "amd_dcm" AMD multi-node processor */
#define X86_FEATURE_APERFMPERF		( 3*32+28) /* "aperfmperf" P-State hardware coordination feedback capability (APERF/MPERF MSRs) */
#define X86_FEATURE_RAPL		( 3*32+29) /* "rapl" AMD/Hygon RAPL interface */
#define X86_FEATURE_NONSTOP_TSC_S3	( 3*32+30) /* "nonstop_tsc_s3" TSC doesn't stop in S3 state */
#define X86_FEATURE_TSC_KNOWN_FREQ	( 3*32+31) /* "tsc_known_freq" TSC has known frequency */

/* Intel-defined CPU features, CPUID level 0x00000001 (ECX), word 4 */
#define X86_FEATURE_XMM3		( 4*32+ 0) /* "pni" SSE-3 */
#define X86_FEATURE_PCLMULQDQ		( 4*32+ 1) /* "pclmulqdq" PCLMULQDQ instruction */
#define X86_FEATURE_DTES64		( 4*32+ 2) /* "dtes64" 64-bit Debug Store */
#define X86_FEATURE_MWAIT		( 4*32+ 3) /* "monitor" MONITOR/MWAIT support */
#define X86_FEATURE_DSCPL		( 4*32+ 4) /* "ds_cpl" CPL-qualified (filtered) Debug Store */
#define X86_FEATURE_VMX			( 4*32+ 5) /* "vmx" Hardware virtualization */
#define X86_FEATURE_SMX			( 4*32+ 6) /* "smx" Safer Mode eXtensions */
#define X86_FEATURE_EST			( 4*32+ 7) /* "est" Enhanced SpeedStep */
#define X86_FEATURE_TM2			( 4*32+ 8) /* "tm2" Thermal Monitor 2 */
#define X86_FEATURE_SSSE3		( 4*32+ 9) /* "ssse3" Supplemental SSE-3 */
#define X86_FEATURE_CID			( 4*32+10) /* "cid" Context ID */
#define X86_FEATURE_SDBG		( 4*32+11) /* "sdbg" Silicon Debug */
#define X86_FEATURE_FMA			( 4*32+12) /* "fma" Fused multiply-add */
#define X86_FEATURE_CX16		( 4*32+13) /* "cx16" CMPXCHG16B instruction */
#define X86_FEATURE_XTPR		( 4*32+14) /* "xtpr" Send Task Priority Messages */
#define X86_FEATURE_PDCM		( 4*32+15) /* "pdcm" Perf/Debug Capabilities MSR */
#define X86_FEATURE_PCID		( 4*32+17) /* "pcid" Process Context Identifiers */
#define X86_FEATURE_DCA			( 4*32+18) /* "dca" Direct Cache Access */
#define X86_FEATURE_XMM4_1		( 4*32+19) /* "sse4_1" SSE-4.1 */
#define X86_FEATURE_XMM4_2		( 4*32+20) /* "sse4_2" SSE-4.2 */
#define X86_FEATURE_X2APIC		( 4*32+21) /* "x2apic" X2APIC */
#define X86_FEATURE_MOVBE		( 4*32+22) /* "movbe" MOVBE instruction */
#define X86_FEATURE_POPCNT		( 4*32+23) /* "popcnt" POPCNT instruction */
#define X86_FEATURE_TSC_DEADLINE_TIMER	( 4*32+24) /* "tsc_deadline_timer" TSC deadline timer */
#define X86_FEATURE_AES			( 4*32+25) /* "aes" AES instructions */
#define X86_FEATURE_XSAVE		( 4*32+26) /* "xsave" XSAVE/XRSTOR/XSETBV/XGETBV instructions */
#define X86_FEATURE_OSXSAVE		( 4*32+27) /* XSAVE instruction enabled in the OS */
#define X86_FEATURE_AVX			( 4*32+28) /* "avx" Advanced Vector Extensions */
#define X86_FEATURE_F16C		( 4*32+29) /* "f16c" 16-bit FP conversions */
#define X86_FEATURE_RDRAND		( 4*32+30) /* "rdrand" RDRAND instruction */
#define X86_FEATURE_HYPERVISOR		( 4*32+31) /* "hypervisor" Running on a hypervisor */

/* VIA/Cyrix/Centaur-defined CPU features, CPUID level 0xC0000001, word 5 */
#define X86_FEATURE_XSTORE		( 5*32+ 2) /* "rng" RNG present (xstore) */
#define X86_FEATURE_XSTORE_EN		( 5*32+ 3) /* "rng_en" RNG enabled */
#define X86_FEATURE_XCRYPT		( 5*32+ 6) /* "ace" on-CPU crypto (xcrypt) */
#define X86_FEATURE_XCRYPT_EN		( 5*32+ 7) /* "ace_en" on-CPU crypto enabled */
#define X86_FEATURE_ACE2		( 5*32+ 8) /* "ace2" Advanced Cryptography Engine v2 */
#define X86_FEATURE_ACE2_EN		( 5*32+ 9) /* "ace2_en" ACE v2 enabled */
#define X86_FEATURE_PHE			( 5*32+10) /* "phe" PadLock Hash Engine */
#define X86_FEATURE_PHE_EN		( 5*32+11) /* "phe_en" PHE enabled */
#define X86_FEATURE_PMM			( 5*32+12) /* "pmm" PadLock Montgomery Multiplier */
#define X86_FEATURE_PMM_EN		( 5*32+13) /* "pmm_en" PMM enabled */

/* More extended AMD flags: CPUID level 0x80000001, ECX, word 6 */
#define X86_FEATURE_LAHF_LM		( 6*32+ 0) /* "lahf_lm" LAHF/SAHF in long mode */
#define X86_FEATURE_CMP_LEGACY		( 6*32+ 1) /* "cmp_legacy" If yes HyperThreading not valid */
#define X86_FEATURE_SVM			( 6*32+ 2) /* "svm" Secure Virtual Machine */
#define X86_FEATURE_EXTAPIC		( 6*32+ 3) /* "extapic" Extended APIC space */
#define X86_FEATURE_CR8_LEGACY		( 6*32+ 4) /* "cr8_legacy" CR8 in 32-bit mode */
#define X86_FEATURE_ABM			( 6*32+ 5) /* "abm" Advanced bit manipulation */
#define X86_FEATURE_SSE4A		( 6*32+ 6) /* "sse4a" SSE-4A */
#define X86_FEATURE_MISALIGNSSE		( 6*32+ 7) /* "misalignsse" Misaligned SSE mode */
#define X86_FEATURE_3DNOWPREFETCH	( 6*32+ 8) /* "3dnowprefetch" 3DNow prefetch instructions */
#define X86_FEATURE_OSVW		( 6*32+ 9) /* "osvw" OS Visible Workaround */
#define X86_FEATURE_IBS			( 6*32+10) /* "ibs" Instruction Based Sampling */
#define X86_FEATURE_XOP			( 6*32+11) /* "xop" Extended AVX instructions */
#define X86_FEATURE_SKINIT		( 6*32+12) /* "skinit" SKINIT/STGI instructions */
#define X86_FEATURE_WDT			( 6*32+13) /* "wdt" Watchdog timer */
#define X86_FEATURE_LWP			( 6*32+15) /* "lwp" Light Weight Profiling */
#define X86_FEATURE_FMA4		( 6*32+16) /* "fma4" 4 operands MAC instructions */
#define X86_FEATURE_TCE			( 6*32+17) /* "tce" Translation Cache Extension */
#define X86_FEATURE_NODEID_MSR		( 6*32+19) /* "nodeid_msr" NodeId MSR */
#define X86_FEATURE_TBM			( 6*32+21) /* "tbm" Trailing Bit Manipulations */
#define X86_FEATURE_TOPOEXT		( 6*32+22) /* "topoext" Topology extensions CPUID leafs */
#define X86_FEATURE_PERFCTR_CORE	( 6*32+23) /* "perfctr_core" Core performance counter extensions */
#define X86_FEATURE_PERFCTR_NB		( 6*32+24) /* "perfctr_nb" NB performance counter extensions */
#define X86_FEATURE_BPEXT		( 6*32+26) /* "bpext" Data breakpoint extension */
#define X86_FEATURE_PTSC		( 6*32+27) /* "ptsc" Performance time-stamp counter */
#define X86_FEATURE_PERFCTR_LLC		( 6*32+28) /* "perfctr_llc" Last Level Cache performance counter extensions */
#define X86_FEATURE_MWAITX		( 6*32+29) /* "mwaitx" MWAIT extension (MONITORX/MWAITX instructions) */

/*
 * Auxiliary flags: Linux defined - For features scattered in various
 * CPUID levels like 0x6, 0xA etc, word 7.
 *
 * Reuse free bits when adding new feature flags!
 */
#define X86_FEATURE_RING3MWAIT		( 7*32+ 0) /* "ring3mwait" Ring 3 MONITOR/MWAIT instructions */
#define X86_FEATURE_CPUID_FAULT		( 7*32+ 1) /* "cpuid_fault" Intel CPUID faulting */
#define X86_FEATURE_CPB			( 7*32+ 2) /* "cpb" AMD Core Performance Boost */
#define X86_FEATURE_EPB			( 7*32+ 3) /* "epb" IA32_ENERGY_PERF_BIAS support */
#define X86_FEATURE_CAT_L3		( 7*32+ 4) /* "cat_l3" Cache Allocation Technology L3 */
#define X86_FEATURE_CAT_L2		( 7*32+ 5) /* "cat_l2" Cache Allocation Technology L2 */
#define X86_FEATURE_CDP_L3		( 7*32+ 6) /* "cdp_l3" Code and Data Prioritization L3 */
#define X86_FEATURE_TDX_HOST_PLATFORM	( 7*32+ 7) /* "tdx_host_platform" Platform supports being a TDX host */
#define X86_FEATURE_HW_PSTATE		( 7*32+ 8) /* "hw_pstate" AMD HW-PState */
#define X86_FEATURE_PROC_FEEDBACK	( 7*32+ 9) /* "proc_feedback" AMD ProcFeedbackInterface */
#define X86_FEATURE_XCOMPACTED		( 7*32+10) /* Use compacted XSTATE (XSAVES or XSAVEC) */
#define X86_FEATURE_PTI			( 7*32+11) /* "pti" Kernel Page Table Isolation enabled */
#define X86_FEATURE_KERNEL_IBRS		( 7*32+12) /* Set/clear IBRS on kernel entry/exit */
#define X86_FEATURE_RSB_VMEXIT		( 7*32+13) /* Fill RSB on VM-Exit */
#define X86_FEATURE_INTEL_PPIN		( 7*32+14) /* "intel_ppin" Intel Processor Inventory Number */
#define X86_FEATURE_CDP_L2		( 7*32+15) /* "cdp_l2" Code and Data Prioritization L2 */
#define X86_FEATURE_MSR_SPEC_CTRL	( 7*32+16) /* MSR SPEC_CTRL is implemented */
#define X86_FEATURE_SSBD		( 7*32+17) /* "ssbd" Speculative Store Bypass Disable */
#define X86_FEATURE_MBA			( 7*32+18) /* "mba" Memory Bandwidth Allocation */
#define X86_FEATURE_RSB_CTXSW		( 7*32+19) /* Fill RSB on context switches */
#define X86_FEATURE_PERFMON_V2		( 7*32+20) /* "perfmon_v2" AMD Performance Monitoring Version 2 */
#define X86_FEATURE_USE_IBPB		( 7*32+21) /* Indirect Branch Prediction Barrier enabled */
#define X86_FEATURE_USE_IBRS_FW		( 7*32+22) /* Use IBRS during runtime firmware calls */
#define X86_FEATURE_SPEC_STORE_BYPASS_DISABLE	( 7*32+23) /* Disable Speculative Store Bypass. */
#define X86_FEATURE_LS_CFG_SSBD		( 7*32+24)  /* AMD SSBD implementation via LS_CFG MSR */
#define X86_FEATURE_IBRS		( 7*32+25) /* "ibrs" Indirect Branch Restricted Speculation */
#define X86_FEATURE_IBPB		( 7*32+26) /* "ibpb" Indirect Branch Prediction Barrier without a guaranteed RSB flush */
#define X86_FEATURE_STIBP		( 7*32+27) /* "stibp" Single Thread Indirect Branch Predictors */
#define X86_FEATURE_ZEN			( 7*32+28) /* Generic flag for all Zen and newer */
#define X86_FEATURE_L1TF_PTEINV		( 7*32+29) /* L1TF workaround PTE inversion */
#define X86_FEATURE_IBRS_ENHANCED	( 7*32+30) /* "ibrs_enhanced" Enhanced IBRS */
#define X86_FEATURE_MSR_IA32_FEAT_CTL	( 7*32+31) /* MSR IA32_FEAT_CTL configured */

/* Virtualization flags: Linux defined, word 8 */
#define X86_FEATURE_TPR_SHADOW		( 8*32+ 0) /* "tpr_shadow" Intel TPR Shadow */
#define X86_FEATURE_FLEXPRIORITY	( 8*32+ 1) /* "flexpriority" Intel FlexPriority */
#define X86_FEATURE_EPT			( 8*32+ 2) /* "ept" Intel Extended Page Table */
#define X86_FEATURE_VPID		( 8*32+ 3) /* "vpid" Intel Virtual Processor ID */

#define X86_FEATURE_VMMCALL		( 8*32+15) /* "vmmcall" Prefer VMMCALL to VMCALL */
#define X86_FEATURE_XENPV		( 8*32+16) /* Xen paravirtual guest */
#define X86_FEATURE_EPT_AD		( 8*32+17) /* "ept_ad" Intel Extended Page Table access-dirty bit */
#define X86_FEATURE_VMCALL		( 8*32+18) /* Hypervisor supports the VMCALL instruction */
#define X86_FEATURE_VMW_VMMCALL		( 8*32+19) /* VMware prefers VMMCALL hypercall instruction */
#define X86_FEATURE_PVUNLOCK		( 8*32+20) /* PV unlock function */
#define X86_FEATURE_VCPUPREEMPT		( 8*32+21) /* PV vcpu_is_preempted function */
#define X86_FEATURE_TDX_GUEST		( 8*32+22) /* "tdx_guest" Intel Trust Domain Extensions Guest */

/* Intel-defined CPU features, CPUID level 0x00000007:0 (EBX), word 9 */
#define X86_FEATURE_FSGSBASE		( 9*32+ 0) /* "fsgsbase" RDFSBASE, WRFSBASE, RDGSBASE, WRGSBASE instructions*/
#define X86_FEATURE_TSC_ADJUST		( 9*32+ 1) /* "tsc_adjust" TSC adjustment MSR 0x3B */
#define X86_FEATURE_SGX			( 9*32+ 2) /* "sgx" Software Guard Extensions */
#define X86_FEATURE_BMI1		( 9*32+ 3) /* "bmi1" 1st group bit manipulation extensions */
#define X86_FEATURE_HLE			( 9*32+ 4) /* "hle" Hardware Lock Elision */
#define X86_FEATURE_AVX2		( 9*32+ 5) /* "avx2" AVX2 instructions */
#define X86_FEATURE_FDP_EXCPTN_ONLY	( 9*32+ 6) /* FPU data pointer updated only on x87 exceptions */
#define X86_FEATURE_SMEP		( 9*32+ 7) /* "smep" Supervisor Mode Execution Protection */
#define X86_FEATURE_BMI2		( 9*32+ 8) /* "bmi2" 2nd group bit manipulation extensions */
#define X86_FEATURE_ERMS		( 9*32+ 9) /* "erms" Enhanced REP MOVSB/STOSB instructions */
#define X86_FEATURE_INVPCID		( 9*32+10) /* "invpcid" Invalidate Processor Context ID */
#define X86_FEATURE_RTM			( 9*32+11) /* "rtm" Restricted Transactional Memory */
#define X86_FEATURE_CQM			( 9*32+12) /* "cqm" Cache QoS Monitoring */
#define X86_FEATURE_ZERO_FCS_FDS	( 9*32+13) /* Zero out FPU CS and FPU DS */
#define X86_FEATURE_MPX			( 9*32+14) /* "mpx" Memory Protection Extension */
#define X86_FEATURE_RDT_A		( 9*32+15) /* "rdt_a" Resource Director Technology Allocation */
#define X86_FEATURE_AVX512F		( 9*32+16) /* "avx512f" AVX-512 Foundation */
#define X86_FEATURE_AVX512DQ		( 9*32+17) /* "avx512dq" AVX-512 DQ (Double/Quad granular) Instructions */
#define X86_FEATURE_RDSEED		( 9*32+18) /* "rdseed" RDSEED instruction */
#define X86_FEATURE_ADX			( 9*32+19) /* "adx" ADCX and ADOX instructions */
#define X86_FEATURE_SMAP		( 9*32+20) /* "smap" Supervisor Mode Access Prevention */
#define X86_FEATURE_AVX512IFMA		( 9*32+21) /* "avx512ifma" AVX-512 Integer Fused Multiply-Add instructions */
#define X86_FEATURE_CLFLUSHOPT		( 9*32+23) /* "clflushopt" CLFLUSHOPT instruction */
#define X86_FEATURE_CLWB		( 9*32+24) /* "clwb" CLWB instruction */
#define X86_FEATURE_INTEL_PT		( 9*32+25) /* "intel_pt" Intel Processor Trace */
#define X86_FEATURE_AVX512PF		( 9*32+26) /* "avx512pf" AVX-512 Prefetch */
#define X86_FEATURE_AVX512ER		( 9*32+27) /* "avx512er" AVX-512 Exponential and Reciprocal */
#define X86_FEATURE_AVX512CD		( 9*32+28) /* "avx512cd" AVX-512 Conflict Detection */
#define X86_FEATURE_SHA_NI		( 9*32+29) /* "sha_ni" SHA1/SHA256 Instruction Extensions */
#define X86_FEATURE_AVX512BW		( 9*32+30) /* "avx512bw" AVX-512 BW (Byte/Word granular) Instructions */
#define X86_FEATURE_AVX512VL		( 9*32+31) /* "avx512vl" AVX-512 VL (128/256 Vector Length) Extensions */

/* Extended state features, CPUID level 0x0000000d:1 (EAX), word 10 */
#define X86_FEATURE_XSAVEOPT		(10*32+ 0) /* "xsaveopt" XSAVEOPT instruction */
#define X86_FEATURE_XSAVEC		(10*32+ 1) /* "xsavec" XSAVEC instruction */
#define X86_FEATURE_XGETBV1		(10*32+ 2) /* "xgetbv1" XGETBV with ECX = 1 instruction */
#define X86_FEATURE_XSAVES		(10*32+ 3) /* "xsaves" XSAVES/XRSTORS instructions */
#define X86_FEATURE_XFD			(10*32+ 4) /* eXtended Feature Disabling */

/*
 * Extended auxiliary flags: Linux defined - for features scattered in various
 * CPUID levels like 0xf, etc.
 *
 * Reuse free bits when adding new feature flags!
 */
#define X86_FEATURE_CQM_LLC		(11*32+ 0) /* "cqm_llc" LLC QoS if 1 */
#define X86_FEATURE_CQM_OCCUP_LLC	(11*32+ 1) /* "cqm_occup_llc" LLC occupancy monitoring */
#define X86_FEATURE_CQM_MBM_TOTAL	(11*32+ 2) /* "cqm_mbm_total" LLC Total MBM monitoring */
#define X86_FEATURE_CQM_MBM_LOCAL	(11*32+ 3) /* "cqm_mbm_local" LLC Local MBM monitoring */
#define X86_FEATURE_FENCE_SWAPGS_USER	(11*32+ 4) /* LFENCE in user entry SWAPGS path */
#define X86_FEATURE_FENCE_SWAPGS_KERNEL	(11*32+ 5) /* LFENCE in kernel entry SWAPGS path */
#define X86_FEATURE_SPLIT_LOCK_DETECT	(11*32+ 6) /* "split_lock_detect" #AC for split lock */
#define X86_FEATURE_PER_THREAD_MBA	(11*32+ 7) /* Per-thread Memory Bandwidth Allocation */
#define X86_FEATURE_SGX1		(11*32+ 8) /* Basic SGX */
#define X86_FEATURE_SGX2		(11*32+ 9) /* SGX Enclave Dynamic Memory Management (EDMM) */
#define X86_FEATURE_ENTRY_IBPB		(11*32+10) /* Issue an IBPB on kernel entry */
#define X86_FEATURE_RRSBA_CTRL		(11*32+11) /* RET prediction control */
#define X86_FEATURE_RETPOLINE		(11*32+12) /* Generic Retpoline mitigation for Spectre variant 2 */
#define X86_FEATURE_RETPOLINE_LFENCE	(11*32+13) /* Use LFENCE for Spectre variant 2 */
#define X86_FEATURE_RETHUNK		(11*32+14) /* Use REturn THUNK */
#define X86_FEATURE_UNRET		(11*32+15) /* AMD BTB untrain return */
#define X86_FEATURE_USE_IBPB_FW		(11*32+16) /* Use IBPB during runtime firmware calls */
#define X86_FEATURE_RSB_VMEXIT_LITE	(11*32+17) /* Fill RSB on VM exit when EIBRS is enabled */
#define X86_FEATURE_SGX_EDECCSSA	(11*32+18) /* SGX EDECCSSA user leaf function */
#define X86_FEATURE_CALL_DEPTH		(11*32+19) /* Call depth tracking for RSB stuffing */
#define X86_FEATURE_MSR_TSX_CTRL	(11*32+20) /* MSR IA32_TSX_CTRL (Intel) implemented */
#define X86_FEATURE_SMBA		(11*32+21) /* Slow Memory Bandwidth Allocation */
#define X86_FEATURE_BMEC		(11*32+22) /* Bandwidth Monitoring Event Configuration */
#define X86_FEATURE_USER_SHSTK		(11*32+23) /* "user_shstk" Shadow stack support for user mode applications */
#define X86_FEATURE_SRSO		(11*32+24) /* AMD BTB untrain RETs */
#define X86_FEATURE_SRSO_ALIAS		(11*32+25) /* AMD BTB untrain RETs through aliasing */
#define X86_FEATURE_IBPB_ON_VMEXIT	(11*32+26) /* Issue an IBPB only on VMEXIT */
#define X86_FEATURE_APIC_MSRS_FENCE	(11*32+27) /* IA32_TSC_DEADLINE and X2APIC MSRs need fencing */
#define X86_FEATURE_ZEN2		(11*32+28) /* CPU based on Zen2 microarchitecture */
#define X86_FEATURE_ZEN3		(11*32+29) /* CPU based on Zen3 microarchitecture */
#define X86_FEATURE_ZEN4		(11*32+30) /* CPU based on Zen4 microarchitecture */
#define X86_FEATURE_ZEN1		(11*32+31) /* CPU based on Zen1 microarchitecture */

/* Intel-defined CPU features, CPUID level 0x00000007:1 (EAX), word 12 */
#define X86_FEATURE_SHA512		(12*32+ 0) /* SHA512 instructions */
#define X86_FEATURE_SM3			(12*32+ 1) /* SM3 instructions */
#define X86_FEATURE_SM4			(12*32+ 2) /* SM4 instructions */
#define X86_FEATURE_AVX_VNNI		(12*32+ 4) /* "avx_vnni" AVX VNNI instructions */
#define X86_FEATURE_AVX512_BF16		(12*32+ 5) /* "avx512_bf16" AVX512 BFLOAT16 instructions */
#define X86_FEATURE_CMPCCXADD           (12*32+ 7) /* CMPccXADD instructions */
#define X86_FEATURE_ARCH_PERFMON_EXT	(12*32+ 8) /* Intel Architectural PerfMon Extension */
#define X86_FEATURE_FZRM		(12*32+10) /* Fast zero-length REP MOVSB */
#define X86_FEATURE_FSRS		(12*32+11) /* Fast short REP STOSB */
#define X86_FEATURE_FSRC		(12*32+12) /* Fast short REP {CMPSB,SCASB} */
#define X86_FEATURE_FRED		(12*32+17) /* "fred" Flexible Return and Event Delivery */
#define X86_FEATURE_LKGS		(12*32+18) /* Load "kernel" (userspace) GS */
#define X86_FEATURE_WRMSRNS		(12*32+19) /* Non-serializing WRMSR */
#define X86_FEATURE_AMX_FP16		(12*32+21) /* AMX fp16 Support */
#define X86_FEATURE_AVX_IFMA            (12*32+23) /* Support for VPMADD52[H,L]UQ */
#define X86_FEATURE_LAM			(12*32+26) /* "lam" Linear Address Masking */

/* AMD-defined CPU features, CPUID level 0x80000008 (EBX), word 13 */
#define X86_FEATURE_CLZERO		(13*32+ 0) /* "clzero" CLZERO instruction */
#define X86_FEATURE_IRPERF		(13*32+ 1) /* "irperf" Instructions Retired Count */
#define X86_FEATURE_XSAVEERPTR		(13*32+ 2) /* "xsaveerptr" Always save/restore FP error pointers */
#define X86_FEATURE_RDPRU		(13*32+ 4) /* "rdpru" Read processor register at user level */
#define X86_FEATURE_WBNOINVD		(13*32+ 9) /* "wbnoinvd" WBNOINVD instruction */
#define X86_FEATURE_AMD_IBPB		(13*32+12) /* Indirect Branch Prediction Barrier */
#define X86_FEATURE_AMD_IBRS		(13*32+14) /* Indirect Branch Restricted Speculation */
#define X86_FEATURE_AMD_STIBP		(13*32+15) /* Single Thread Indirect Branch Predictors */
#define X86_FEATURE_AMD_STIBP_ALWAYS_ON	(13*32+17) /* Single Thread Indirect Branch Predictors always-on preferred */
#define X86_FEATURE_AMD_PPIN		(13*32+23) /* "amd_ppin" Protected Processor Inventory Number */
#define X86_FEATURE_AMD_SSBD		(13*32+24) /* Speculative Store Bypass Disable */
#define X86_FEATURE_VIRT_SSBD		(13*32+25) /* "virt_ssbd" Virtualized Speculative Store Bypass Disable */
#define X86_FEATURE_AMD_SSB_NO		(13*32+26) /* Speculative Store Bypass is fixed in hardware. */
#define X86_FEATURE_CPPC		(13*32+27) /* "cppc" Collaborative Processor Performance Control */
#define X86_FEATURE_AMD_PSFD            (13*32+28) /* Predictive Store Forwarding Disable */
#define X86_FEATURE_BTC_NO		(13*32+29) /* Not vulnerable to Branch Type Confusion */
#define X86_FEATURE_AMD_IBPB_RET	(13*32+30) /* IBPB clears return address predictor */
#define X86_FEATURE_BRS			(13*32+31) /* "brs" Branch Sampling available */

/* Thermal and Power Management Leaf, CPUID level 0x00000006 (EAX), word 14 */
#define X86_FEATURE_DTHERM		(14*32+ 0) /* "dtherm" Digital Thermal Sensor */
#define X86_FEATURE_IDA			(14*32+ 1) /* "ida" Intel Dynamic Acceleration */
#define X86_FEATURE_ARAT		(14*32+ 2) /* "arat" Always Running APIC Timer */
#define X86_FEATURE_PLN			(14*32+ 4) /* "pln" Intel Power Limit Notification */
#define X86_FEATURE_PTS			(14*32+ 6) /* "pts" Intel Package Thermal Status */
#define X86_FEATURE_HWP			(14*32+ 7) /* "hwp" Intel Hardware P-states */
#define X86_FEATURE_HWP_NOTIFY		(14*32+ 8) /* "hwp_notify" HWP Notification */
#define X86_FEATURE_HWP_ACT_WINDOW	(14*32+ 9) /* "hwp_act_window" HWP Activity Window */
#define X86_FEATURE_HWP_EPP		(14*32+10) /* "hwp_epp" HWP Energy Perf. Preference */
#define X86_FEATURE_HWP_PKG_REQ		(14*32+11) /* "hwp_pkg_req" HWP Package Level Request */
#define X86_FEATURE_HWP_HIGHEST_PERF_CHANGE (14*32+15) /* HWP Highest perf change */
#define X86_FEATURE_HFI			(14*32+19) /* "hfi" Hardware Feedback Interface */

/* AMD SVM Feature Identification, CPUID level 0x8000000a (EDX), word 15 */
#define X86_FEATURE_NPT			(15*32+ 0) /* "npt" Nested Page Table support */
#define X86_FEATURE_LBRV		(15*32+ 1) /* "lbrv" LBR Virtualization support */
#define X86_FEATURE_SVML		(15*32+ 2) /* "svm_lock" SVM locking MSR */
#define X86_FEATURE_NRIPS		(15*32+ 3) /* "nrip_save" SVM next_rip save */
#define X86_FEATURE_TSCRATEMSR		(15*32+ 4) /* "tsc_scale" TSC scaling support */
#define X86_FEATURE_VMCBCLEAN		(15*32+ 5) /* "vmcb_clean" VMCB clean bits support */
#define X86_FEATURE_FLUSHBYASID		(15*32+ 6) /* "flushbyasid" Flush-by-ASID support */
#define X86_FEATURE_DECODEASSISTS	(15*32+ 7) /* "decodeassists" Decode Assists support */
#define X86_FEATURE_PAUSEFILTER		(15*32+10) /* "pausefilter" Filtered pause intercept */
#define X86_FEATURE_PFTHRESHOLD		(15*32+12) /* "pfthreshold" Pause filter threshold */
#define X86_FEATURE_AVIC		(15*32+13) /* "avic" Virtual Interrupt Controller */
#define X86_FEATURE_V_VMSAVE_VMLOAD	(15*32+15) /* "v_vmsave_vmload" Virtual VMSAVE VMLOAD */
#define X86_FEATURE_VGIF		(15*32+16) /* "vgif" Virtual GIF */
#define X86_FEATURE_X2AVIC		(15*32+18) /* "x2avic" Virtual x2apic */
#define X86_FEATURE_V_SPEC_CTRL		(15*32+20) /* "v_spec_ctrl" Virtual SPEC_CTRL */
#define X86_FEATURE_VNMI		(15*32+25) /* "vnmi" Virtual NMI */
#define X86_FEATURE_SVME_ADDR_CHK	(15*32+28) /* SVME addr check */

/* Intel-defined CPU features, CPUID level 0x00000007:0 (ECX), word 16 */
#define X86_FEATURE_AVX512VBMI		(16*32+ 1) /* "avx512vbmi" AVX512 Vector Bit Manipulation instructions*/
#define X86_FEATURE_UMIP		(16*32+ 2) /* "umip" User Mode Instruction Protection */
#define X86_FEATURE_PKU			(16*32+ 3) /* "pku" Protection Keys for Userspace */
#define X86_FEATURE_OSPKE		(16*32+ 4) /* "ospke" OS Protection Keys Enable */
#define X86_FEATURE_WAITPKG		(16*32+ 5) /* "waitpkg" UMONITOR/UMWAIT/TPAUSE Instructions */
#define X86_FEATURE_AVX512_VBMI2	(16*32+ 6) /* "avx512_vbmi2" Additional AVX512 Vector Bit Manipulation Instructions */
#define X86_FEATURE_SHSTK		(16*32+ 7) /* Shadow stack */
#define X86_FEATURE_GFNI		(16*32+ 8) /* "gfni" Galois Field New Instructions */
#define X86_FEATURE_VAES		(16*32+ 9) /* "vaes" Vector AES */
#define X86_FEATURE_VPCLMULQDQ		(16*32+10) /* "vpclmulqdq" Carry-Less Multiplication Double Quadword */
#define X86_FEATURE_AVX512_VNNI		(16*32+11) /* "avx512_vnni" Vector Neural Network Instructions */
#define X86_FEATURE_AVX512_BITALG	(16*32+12) /* "avx512_bitalg" Support for VPOPCNT[B,W] and VPSHUF-BITQMB instructions */
#define X86_FEATURE_TME			(16*32+13) /* "tme" Intel Total Memory Encryption */
#define X86_FEATURE_AVX512_VPOPCNTDQ	(16*32+14) /* "avx512_vpopcntdq" POPCNT for vectors of DW/QW */
#define X86_FEATURE_LA57		(16*32+16) /* "la57" 5-level page tables */
#define X86_FEATURE_RDPID		(16*32+22) /* "rdpid" RDPID instruction */
#define X86_FEATURE_BUS_LOCK_DETECT	(16*32+24) /* "bus_lock_detect" Bus Lock detect */
#define X86_FEATURE_CLDEMOTE		(16*32+25) /* "cldemote" CLDEMOTE instruction */
#define X86_FEATURE_MOVDIRI		(16*32+27) /* "movdiri" MOVDIRI instruction */
#define X86_FEATURE_MOVDIR64B		(16*32+28) /* "movdir64b" MOVDIR64B instruction */
#define X86_FEATURE_ENQCMD		(16*32+29) /* "enqcmd" ENQCMD and ENQCMDS instructions */
#define X86_FEATURE_SGX_LC		(16*32+30) /* "sgx_lc" Software Guard Extensions Launch Control */

/* AMD-defined CPU features, CPUID level 0x80000007 (EBX), word 17 */
#define X86_FEATURE_OVERFLOW_RECOV	(17*32+ 0) /* "overflow_recov" MCA overflow recovery support */
#define X86_FEATURE_SUCCOR		(17*32+ 1) /* "succor" Uncorrectable error containment and recovery */
#define X86_FEATURE_SMCA		(17*32+ 3) /* "smca" Scalable MCA */

/* Intel-defined CPU features, CPUID level 0x00000007:0 (EDX), word 18 */
#define X86_FEATURE_AVX512_4VNNIW	(18*32+ 2) /* "avx512_4vnniw" AVX-512 Neural Network Instructions */
#define X86_FEATURE_AVX512_4FMAPS	(18*32+ 3) /* "avx512_4fmaps" AVX-512 Multiply Accumulation Single precision */
#define X86_FEATURE_FSRM		(18*32+ 4) /* "fsrm" Fast Short Rep Mov */
#define X86_FEATURE_AVX512_VP2INTERSECT (18*32+ 8) /* "avx512_vp2intersect" AVX-512 Intersect for D/Q */
#define X86_FEATURE_SRBDS_CTRL		(18*32+ 9) /* SRBDS mitigation MSR available */
#define X86_FEATURE_MD_CLEAR		(18*32+10) /* "md_clear" VERW clears CPU buffers */
#define X86_FEATURE_RTM_ALWAYS_ABORT	(18*32+11) /* RTM transaction always aborts */
#define X86_FEATURE_TSX_FORCE_ABORT	(18*32+13) /* TSX_FORCE_ABORT */
#define X86_FEATURE_SERIALIZE		(18*32+14) /* "serialize" SERIALIZE instruction */
#define X86_FEATURE_HYBRID_CPU		(18*32+15) /* This part has CPUs of more than one type */
#define X86_FEATURE_TSXLDTRK		(18*32+16) /* "tsxldtrk" TSX Suspend Load Address Tracking */
#define X86_FEATURE_PCONFIG		(18*32+18) /* "pconfig" Intel PCONFIG */
#define X86_FEATURE_ARCH_LBR		(18*32+19) /* "arch_lbr" Intel ARCH LBR */
#define X86_FEATURE_IBT			(18*32+20) /* "ibt" Indirect Branch Tracking */
#define X86_FEATURE_AMX_BF16		(18*32+22) /* "amx_bf16" AMX bf16 Support */
#define X86_FEATURE_AVX512_FP16		(18*32+23) /* "avx512_fp16" AVX512 FP16 */
#define X86_FEATURE_AMX_TILE		(18*32+24) /* "amx_tile" AMX tile Support */
#define X86_FEATURE_AMX_INT8		(18*32+25) /* "amx_int8" AMX int8 Support */
#define X86_FEATURE_SPEC_CTRL		(18*32+26) /* Speculation Control (IBRS + IBPB) */
#define X86_FEATURE_INTEL_STIBP		(18*32+27) /* Single Thread Indirect Branch Predictors */
#define X86_FEATURE_FLUSH_L1D		(18*32+28) /* "flush_l1d" Flush L1D cache */
#define X86_FEATURE_ARCH_CAPABILITIES	(18*32+29) /* "arch_capabilities" IA32_ARCH_CAPABILITIES MSR (Intel) */
#define X86_FEATURE_CORE_CAPABILITIES	(18*32+30) /* IA32_CORE_CAPABILITIES MSR */
#define X86_FEATURE_SPEC_CTRL_SSBD	(18*32+31) /* Speculative Store Bypass Disable */

/* AMD-defined memory encryption features, CPUID level 0x8000001f (EAX), word 19 */
#define X86_FEATURE_SME			(19*32+ 0) /* "sme" AMD Secure Memory Encryption */
#define X86_FEATURE_SEV			(19*32+ 1) /* "sev" AMD Secure Encrypted Virtualization */
#define X86_FEATURE_VM_PAGE_FLUSH	(19*32+ 2) /* VM Page Flush MSR is supported */
#define X86_FEATURE_SEV_ES		(19*32+ 3) /* "sev_es" AMD Secure Encrypted Virtualization - Encrypted State */
#define X86_FEATURE_SEV_SNP		(19*32+ 4) /* "sev_snp" AMD Secure Encrypted Virtualization - Secure Nested Paging */
#define X86_FEATURE_V_TSC_AUX		(19*32+ 9) /* Virtual TSC_AUX */
#define X86_FEATURE_SME_COHERENT	(19*32+10) /* AMD hardware-enforced cache coherency */
#define X86_FEATURE_DEBUG_SWAP		(19*32+14) /* "debug_swap" AMD SEV-ES full debug state swap support */
#define X86_FEATURE_SVSM		(19*32+28) /* "svsm" SVSM present */

/* AMD-defined Extended Feature 2 EAX, CPUID level 0x80000021 (EAX), word 20 */
#define X86_FEATURE_NO_NESTED_DATA_BP	(20*32+ 0) /* No Nested Data Breakpoints */
#define X86_FEATURE_WRMSR_XX_BASE_NS	(20*32+ 1) /* WRMSR to {FS,GS,KERNEL_GS}_BASE is non-serializing */
#define X86_FEATURE_LFENCE_RDTSC	(20*32+ 2) /* LFENCE always serializing / synchronizes RDTSC */
#define X86_FEATURE_NULL_SEL_CLR_BASE	(20*32+ 6) /* Null Selector Clears Base */
#define X86_FEATURE_AUTOIBRS		(20*32+ 8) /* Automatic IBRS */
#define X86_FEATURE_NO_SMM_CTL_MSR	(20*32+ 9) /* SMM_CTL MSR is not present */

#define X86_FEATURE_SBPB		(20*32+27) /* Selective Branch Prediction Barrier */
#define X86_FEATURE_IBPB_BRTYPE		(20*32+28) /* MSR_PRED_CMD[IBPB] flushes all branch type predictions */
#define X86_FEATURE_SRSO_NO		(20*32+29) /* CPU is not affected by SRSO */

/*
 * Extended auxiliary flags: Linux defined - for features scattered in various
 * CPUID levels like 0x80000022, etc and Linux defined features.
 *
 * Reuse free bits when adding new feature flags!
 */
#define X86_FEATURE_AMD_LBR_PMC_FREEZE	(21*32+ 0) /* "amd_lbr_pmc_freeze" AMD LBR and PMC Freeze */
#define X86_FEATURE_CLEAR_BHB_LOOP	(21*32+ 1) /* Clear branch history at syscall entry using SW loop */
#define X86_FEATURE_BHI_CTRL		(21*32+ 2) /* BHI_DIS_S HW control available */
#define X86_FEATURE_CLEAR_BHB_HW	(21*32+ 3) /* BHI_DIS_S HW control enabled */
#define X86_FEATURE_CLEAR_BHB_LOOP_ON_VMEXIT (21*32+ 4) /* Clear branch history at vmexit using SW loop */
#define X86_FEATURE_AMD_FAST_CPPC	(21*32 + 5) /* Fast CPPC */
#define X86_FEATURE_AMD_HETEROGENEOUS_CORES (21*32 + 6) /* Heterogeneous Core Topology */
#define X86_FEATURE_AMD_WORKLOAD_CLASS	(21*32 + 7) /* Workload Classification */

/*
 * BUG word(s)
 */
#define X86_BUG(x)			(NCAPINTS*32 + (x))

#define X86_BUG_F00F			X86_BUG(0) /* "f00f" Intel F00F */
#define X86_BUG_FDIV			X86_BUG(1) /* "fdiv" FPU FDIV */
#define X86_BUG_COMA			X86_BUG(2) /* "coma" Cyrix 6x86 coma */
#define X86_BUG_AMD_TLB_MMATCH		X86_BUG(3) /* "tlb_mmatch" AMD Erratum 383 */
#define X86_BUG_AMD_APIC_C1E		X86_BUG(4) /* "apic_c1e" AMD Erratum 400 */
#define X86_BUG_11AP			X86_BUG(5) /* "11ap" Bad local APIC aka 11AP */
#define X86_BUG_FXSAVE_LEAK		X86_BUG(6) /* "fxsave_leak" FXSAVE leaks FOP/FIP/FOP */
#define X86_BUG_CLFLUSH_MONITOR		X86_BUG(7) /* "clflush_monitor" AAI65, CLFLUSH required before MONITOR */
#define X86_BUG_SYSRET_SS_ATTRS		X86_BUG(8) /* "sysret_ss_attrs" SYSRET doesn't fix up SS attrs */
#ifdef CONFIG_X86_32
/*
 * 64-bit kernels don't use X86_BUG_ESPFIX.  Make the define conditional
 * to avoid confusion.
 */
#define X86_BUG_ESPFIX			X86_BUG(9) /* IRET to 16-bit SS corrupts ESP/RSP high bits */
#endif
#define X86_BUG_NULL_SEG		X86_BUG(10) /* "null_seg" Nulling a selector preserves the base */
#define X86_BUG_SWAPGS_FENCE		X86_BUG(11) /* "swapgs_fence" SWAPGS without input dep on GS */
#define X86_BUG_MONITOR			X86_BUG(12) /* "monitor" IPI required to wake up remote CPU */
#define X86_BUG_AMD_E400		X86_BUG(13) /* "amd_e400" CPU is among the affected by Erratum 400 */
#define X86_BUG_CPU_MELTDOWN		X86_BUG(14) /* "cpu_meltdown" CPU is affected by meltdown attack and needs kernel page table isolation */
#define X86_BUG_SPECTRE_V1		X86_BUG(15) /* "spectre_v1" CPU is affected by Spectre variant 1 attack with conditional branches */
#define X86_BUG_SPECTRE_V2		X86_BUG(16) /* "spectre_v2" CPU is affected by Spectre variant 2 attack with indirect branches */
#define X86_BUG_SPEC_STORE_BYPASS	X86_BUG(17) /* "spec_store_bypass" CPU is affected by speculative store bypass attack */
#define X86_BUG_L1TF			X86_BUG(18) /* "l1tf" CPU is affected by L1 Terminal Fault */
#define X86_BUG_MDS			X86_BUG(19) /* "mds" CPU is affected by Microarchitectural data sampling */
#define X86_BUG_MSBDS_ONLY		X86_BUG(20) /* "msbds_only" CPU is only affected by the  MSDBS variant of BUG_MDS */
#define X86_BUG_SWAPGS			X86_BUG(21) /* "swapgs" CPU is affected by speculation through SWAPGS */
#define X86_BUG_TAA			X86_BUG(22) /* "taa" CPU is affected by TSX Async Abort(TAA) */
#define X86_BUG_ITLB_MULTIHIT		X86_BUG(23) /* "itlb_multihit" CPU may incur MCE during certain page attribute changes */
#define X86_BUG_SRBDS			X86_BUG(24) /* "srbds" CPU may leak RNG bits if not mitigated */
#define X86_BUG_MMIO_STALE_DATA		X86_BUG(25) /* "mmio_stale_data" CPU is affected by Processor MMIO Stale Data vulnerabilities */
#define X86_BUG_MMIO_UNKNOWN		X86_BUG(26) /* "mmio_unknown" CPU is too old and its MMIO Stale Data status is unknown */
#define X86_BUG_RETBLEED		X86_BUG(27) /* "retbleed" CPU is affected by RETBleed */
#define X86_BUG_EIBRS_PBRSB		X86_BUG(28) /* "eibrs_pbrsb" EIBRS is vulnerable to Post Barrier RSB Predictions */
#define X86_BUG_SMT_RSB			X86_BUG(29) /* "smt_rsb" CPU is vulnerable to Cross-Thread Return Address Predictions */
#define X86_BUG_GDS			X86_BUG(30) /* "gds" CPU is affected by Gather Data Sampling */
#define X86_BUG_TDX_PW_MCE		X86_BUG(31) /* "tdx_pw_mce" CPU may incur #MC if non-TD software does partial write to TDX private memory */

/* BUG word 2 */
#define X86_BUG_SRSO			X86_BUG(1*32 + 0) /* "srso" AMD SRSO bug */
#define X86_BUG_DIV0			X86_BUG(1*32 + 1) /* "div0" AMD DIV0 speculation bug */
#define X86_BUG_RFDS			X86_BUG(1*32 + 2) /* "rfds" CPU is vulnerable to Register File Data Sampling */
#define X86_BUG_BHI			X86_BUG(1*32 + 3) /* "bhi" CPU is affected by Branch History Injection */
#define X86_BUG_IBPB_NO_RET	   	X86_BUG(1*32 + 4) /* "ibpb_no_ret" IBPB omits return target predictions */
#endif /* _ASM_X86_CPUFEATURES_H */
