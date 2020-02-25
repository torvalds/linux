/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_VMXFEATURES_H
#define _ASM_X86_VMXFEATURES_H

/*
 * Defines VMX CPU feature bits
 */
#define NVMXINTS			3 /* N 32-bit words worth of info */

/*
 * Note: If the comment begins with a quoted string, that string is used
 * in /proc/cpuinfo instead of the macro name.  If the string is "",
 * this feature bit is not displayed in /proc/cpuinfo at all.
 */

/* Pin-Based VM-Execution Controls, EPT/VPID, APIC and VM-Functions, word 0 */
#define VMX_FEATURE_INTR_EXITING	( 0*32+  0) /* "" VM-Exit on vectored interrupts */
#define VMX_FEATURE_NMI_EXITING		( 0*32+  3) /* "" VM-Exit on NMIs */
#define VMX_FEATURE_VIRTUAL_NMIS	( 0*32+  5) /* "vnmi" NMI virtualization */
#define VMX_FEATURE_PREEMPTION_TIMER	( 0*32+  6) /* VMX Preemption Timer */
#define VMX_FEATURE_POSTED_INTR		( 0*32+  7) /* Posted Interrupts */

/* EPT/VPID features, scattered to bits 16-23 */
#define VMX_FEATURE_INVVPID		( 0*32+ 16) /* INVVPID is supported */
#define VMX_FEATURE_EPT_EXECUTE_ONLY	( 0*32+ 17) /* "ept_x_only" EPT entries can be execute only */
#define VMX_FEATURE_EPT_AD		( 0*32+ 18) /* EPT Accessed/Dirty bits */
#define VMX_FEATURE_EPT_1GB		( 0*32+ 19) /* 1GB EPT pages */

/* Aggregated APIC features 24-27 */
#define VMX_FEATURE_FLEXPRIORITY	( 0*32+ 24) /* TPR shadow + virt APIC */
#define VMX_FEATURE_APICV	        ( 0*32+ 25) /* TPR shadow + APIC reg virt + virt intr delivery + posted interrupts */

/* VM-Functions, shifted to bits 28-31 */
#define VMX_FEATURE_EPTP_SWITCHING	( 0*32+ 28) /* EPTP switching (in guest) */

/* Primary Processor-Based VM-Execution Controls, word 1 */
#define VMX_FEATURE_INTR_WINDOW_EXITING ( 1*32+  2) /* "" VM-Exit if INTRs are unblocked in guest */
#define VMX_FEATURE_USE_TSC_OFFSETTING	( 1*32+  3) /* "tsc_offset" Offset hardware TSC when read in guest */
#define VMX_FEATURE_HLT_EXITING		( 1*32+  7) /* "" VM-Exit on HLT */
#define VMX_FEATURE_INVLPG_EXITING	( 1*32+  9) /* "" VM-Exit on INVLPG */
#define VMX_FEATURE_MWAIT_EXITING	( 1*32+ 10) /* "" VM-Exit on MWAIT */
#define VMX_FEATURE_RDPMC_EXITING	( 1*32+ 11) /* "" VM-Exit on RDPMC */
#define VMX_FEATURE_RDTSC_EXITING	( 1*32+ 12) /* "" VM-Exit on RDTSC */
#define VMX_FEATURE_CR3_LOAD_EXITING	( 1*32+ 15) /* "" VM-Exit on writes to CR3 */
#define VMX_FEATURE_CR3_STORE_EXITING	( 1*32+ 16) /* "" VM-Exit on reads from CR3 */
#define VMX_FEATURE_CR8_LOAD_EXITING	( 1*32+ 19) /* "" VM-Exit on writes to CR8 */
#define VMX_FEATURE_CR8_STORE_EXITING	( 1*32+ 20) /* "" VM-Exit on reads from CR8 */
#define VMX_FEATURE_VIRTUAL_TPR		( 1*32+ 21) /* "vtpr" TPR virtualization, a.k.a. TPR shadow */
#define VMX_FEATURE_NMI_WINDOW_EXITING	( 1*32+ 22) /* "" VM-Exit if NMIs are unblocked in guest */
#define VMX_FEATURE_MOV_DR_EXITING	( 1*32+ 23) /* "" VM-Exit on accesses to debug registers */
#define VMX_FEATURE_UNCOND_IO_EXITING	( 1*32+ 24) /* "" VM-Exit on *all* IN{S} and OUT{S}*/
#define VMX_FEATURE_USE_IO_BITMAPS	( 1*32+ 25) /* "" VM-Exit based on I/O port */
#define VMX_FEATURE_MONITOR_TRAP_FLAG	( 1*32+ 27) /* "mtf" VMX single-step VM-Exits */
#define VMX_FEATURE_USE_MSR_BITMAPS	( 1*32+ 28) /* "" VM-Exit based on MSR index */
#define VMX_FEATURE_MONITOR_EXITING	( 1*32+ 29) /* "" VM-Exit on MONITOR (MWAIT's accomplice) */
#define VMX_FEATURE_PAUSE_EXITING	( 1*32+ 30) /* "" VM-Exit on PAUSE (unconditionally) */
#define VMX_FEATURE_SEC_CONTROLS	( 1*32+ 31) /* "" Enable Secondary VM-Execution Controls */

/* Secondary Processor-Based VM-Execution Controls, word 2 */
#define VMX_FEATURE_VIRT_APIC_ACCESSES	( 2*32+  0) /* "vapic" Virtualize memory mapped APIC accesses */
#define VMX_FEATURE_EPT			( 2*32+  1) /* Extended Page Tables, a.k.a. Two-Dimensional Paging */
#define VMX_FEATURE_DESC_EXITING	( 2*32+  2) /* "" VM-Exit on {S,L}*DT instructions */
#define VMX_FEATURE_RDTSCP		( 2*32+  3) /* "" Enable RDTSCP in guest */
#define VMX_FEATURE_VIRTUAL_X2APIC	( 2*32+  4) /* "" Virtualize X2APIC for the guest */
#define VMX_FEATURE_VPID		( 2*32+  5) /* Virtual Processor ID (TLB ASID modifier) */
#define VMX_FEATURE_WBINVD_EXITING	( 2*32+  6) /* "" VM-Exit on WBINVD */
#define VMX_FEATURE_UNRESTRICTED_GUEST	( 2*32+  7) /* Allow Big Real Mode and other "invalid" states */
#define VMX_FEATURE_APIC_REGISTER_VIRT	( 2*32+  8) /* "vapic_reg" Hardware emulation of reads to the virtual-APIC */
#define VMX_FEATURE_VIRT_INTR_DELIVERY	( 2*32+  9) /* "vid" Evaluation and delivery of pending virtual interrupts */
#define VMX_FEATURE_PAUSE_LOOP_EXITING	( 2*32+ 10) /* "ple" Conditionally VM-Exit on PAUSE at CPL0 */
#define VMX_FEATURE_RDRAND_EXITING	( 2*32+ 11) /* "" VM-Exit on RDRAND*/
#define VMX_FEATURE_INVPCID		( 2*32+ 12) /* "" Enable INVPCID in guest */
#define VMX_FEATURE_VMFUNC		( 2*32+ 13) /* "" Enable VM-Functions (leaf dependent) */
#define VMX_FEATURE_SHADOW_VMCS		( 2*32+ 14) /* VMREAD/VMWRITE in guest can access shadow VMCS */
#define VMX_FEATURE_ENCLS_EXITING	( 2*32+ 15) /* "" VM-Exit on ENCLS (leaf dependent) */
#define VMX_FEATURE_RDSEED_EXITING	( 2*32+ 16) /* "" VM-Exit on RDSEED */
#define VMX_FEATURE_PAGE_MOD_LOGGING	( 2*32+ 17) /* "pml" Log dirty pages into buffer */
#define VMX_FEATURE_EPT_VIOLATION_VE	( 2*32+ 18) /* "" Conditionally reflect EPT violations as #VE exceptions */
#define VMX_FEATURE_PT_CONCEAL_VMX	( 2*32+ 19) /* "" Suppress VMX indicators in Processor Trace */
#define VMX_FEATURE_XSAVES		( 2*32+ 20) /* "" Enable XSAVES and XRSTORS in guest */
#define VMX_FEATURE_MODE_BASED_EPT_EXEC	( 2*32+ 22) /* "ept_mode_based_exec" Enable separate EPT EXEC bits for supervisor vs. user */
#define VMX_FEATURE_PT_USE_GPA		( 2*32+ 24) /* "" Processor Trace logs GPAs */
#define VMX_FEATURE_TSC_SCALING		( 2*32+ 25) /* Scale hardware TSC when read in guest */
#define VMX_FEATURE_ENCLV_EXITING	( 2*32+ 28) /* "" VM-Exit on ENCLV (leaf dependent) */

#endif /* _ASM_X86_VMXFEATURES_H */
