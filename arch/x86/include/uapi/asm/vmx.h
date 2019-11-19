/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * vmx.h: VMX Architecture related definitions
 * Copyright (c) 2004, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 * A few random additions are:
 * Copyright (C) 2006 Qumranet
 *    Avi Kivity <avi@qumranet.com>
 *    Yaniv Kamay <yaniv@qumranet.com>
 *
 */
#ifndef _UAPIVMX_H
#define _UAPIVMX_H


#define VMX_EXIT_REASONS_FAILED_VMENTRY         0x80000000

#define EXIT_REASON_EXCEPTION_NMI       0
#define EXIT_REASON_EXTERNAL_INTERRUPT  1
#define EXIT_REASON_TRIPLE_FAULT        2
#define EXIT_REASON_INIT_SIGNAL			3

#define EXIT_REASON_PENDING_INTERRUPT   7
#define EXIT_REASON_NMI_WINDOW          8
#define EXIT_REASON_TASK_SWITCH         9
#define EXIT_REASON_CPUID               10
#define EXIT_REASON_HLT                 12
#define EXIT_REASON_INVD                13
#define EXIT_REASON_INVLPG              14
#define EXIT_REASON_RDPMC               15
#define EXIT_REASON_RDTSC               16
#define EXIT_REASON_VMCALL              18
#define EXIT_REASON_VMCLEAR             19
#define EXIT_REASON_VMLAUNCH            20
#define EXIT_REASON_VMPTRLD             21
#define EXIT_REASON_VMPTRST             22
#define EXIT_REASON_VMREAD              23
#define EXIT_REASON_VMRESUME            24
#define EXIT_REASON_VMWRITE             25
#define EXIT_REASON_VMOFF               26
#define EXIT_REASON_VMON                27
#define EXIT_REASON_CR_ACCESS           28
#define EXIT_REASON_DR_ACCESS           29
#define EXIT_REASON_IO_INSTRUCTION      30
#define EXIT_REASON_MSR_READ            31
#define EXIT_REASON_MSR_WRITE           32
#define EXIT_REASON_INVALID_STATE       33
#define EXIT_REASON_MSR_LOAD_FAIL       34
#define EXIT_REASON_MWAIT_INSTRUCTION   36
#define EXIT_REASON_MONITOR_TRAP_FLAG   37
#define EXIT_REASON_MONITOR_INSTRUCTION 39
#define EXIT_REASON_PAUSE_INSTRUCTION   40
#define EXIT_REASON_MCE_DURING_VMENTRY  41
#define EXIT_REASON_TPR_BELOW_THRESHOLD 43
#define EXIT_REASON_APIC_ACCESS         44
#define EXIT_REASON_EOI_INDUCED         45
#define EXIT_REASON_GDTR_IDTR           46
#define EXIT_REASON_LDTR_TR             47
#define EXIT_REASON_EPT_VIOLATION       48
#define EXIT_REASON_EPT_MISCONFIG       49
#define EXIT_REASON_INVEPT              50
#define EXIT_REASON_RDTSCP              51
#define EXIT_REASON_PREEMPTION_TIMER    52
#define EXIT_REASON_INVVPID             53
#define EXIT_REASON_WBINVD              54
#define EXIT_REASON_XSETBV              55
#define EXIT_REASON_APIC_WRITE          56
#define EXIT_REASON_RDRAND              57
#define EXIT_REASON_INVPCID             58
#define EXIT_REASON_VMFUNC              59
#define EXIT_REASON_ENCLS               60
#define EXIT_REASON_RDSEED              61
#define EXIT_REASON_PML_FULL            62
#define EXIT_REASON_XSAVES              63
#define EXIT_REASON_XRSTORS             64
#define EXIT_REASON_UMWAIT              67
#define EXIT_REASON_TPAUSE              68

#define VMX_EXIT_REASONS \
	{ EXIT_REASON_EXCEPTION_NMI,         "EXCEPTION_NMI" }, \
	{ EXIT_REASON_EXTERNAL_INTERRUPT,    "EXTERNAL_INTERRUPT" }, \
	{ EXIT_REASON_TRIPLE_FAULT,          "TRIPLE_FAULT" }, \
	{ EXIT_REASON_INIT_SIGNAL,           "INIT_SIGNAL" }, \
	{ EXIT_REASON_PENDING_INTERRUPT,     "PENDING_INTERRUPT" }, \
	{ EXIT_REASON_NMI_WINDOW,            "NMI_WINDOW" }, \
	{ EXIT_REASON_TASK_SWITCH,           "TASK_SWITCH" }, \
	{ EXIT_REASON_CPUID,                 "CPUID" }, \
	{ EXIT_REASON_HLT,                   "HLT" }, \
	{ EXIT_REASON_INVD,                  "INVD" }, \
	{ EXIT_REASON_INVLPG,                "INVLPG" }, \
	{ EXIT_REASON_RDPMC,                 "RDPMC" }, \
	{ EXIT_REASON_RDTSC,                 "RDTSC" }, \
	{ EXIT_REASON_VMCALL,                "VMCALL" }, \
	{ EXIT_REASON_VMCLEAR,               "VMCLEAR" }, \
	{ EXIT_REASON_VMLAUNCH,              "VMLAUNCH" }, \
	{ EXIT_REASON_VMPTRLD,               "VMPTRLD" }, \
	{ EXIT_REASON_VMPTRST,               "VMPTRST" }, \
	{ EXIT_REASON_VMREAD,                "VMREAD" }, \
	{ EXIT_REASON_VMRESUME,              "VMRESUME" }, \
	{ EXIT_REASON_VMWRITE,               "VMWRITE" }, \
	{ EXIT_REASON_VMOFF,                 "VMOFF" }, \
	{ EXIT_REASON_VMON,                  "VMON" }, \
	{ EXIT_REASON_CR_ACCESS,             "CR_ACCESS" }, \
	{ EXIT_REASON_DR_ACCESS,             "DR_ACCESS" }, \
	{ EXIT_REASON_IO_INSTRUCTION,        "IO_INSTRUCTION" }, \
	{ EXIT_REASON_MSR_READ,              "MSR_READ" }, \
	{ EXIT_REASON_MSR_WRITE,             "MSR_WRITE" }, \
	{ EXIT_REASON_INVALID_STATE,         "INVALID_STATE" }, \
	{ EXIT_REASON_MSR_LOAD_FAIL,         "MSR_LOAD_FAIL" }, \
	{ EXIT_REASON_MWAIT_INSTRUCTION,     "MWAIT_INSTRUCTION" }, \
	{ EXIT_REASON_MONITOR_TRAP_FLAG,     "MONITOR_TRAP_FLAG" }, \
	{ EXIT_REASON_MONITOR_INSTRUCTION,   "MONITOR_INSTRUCTION" }, \
	{ EXIT_REASON_PAUSE_INSTRUCTION,     "PAUSE_INSTRUCTION" }, \
	{ EXIT_REASON_MCE_DURING_VMENTRY,    "MCE_DURING_VMENTRY" }, \
	{ EXIT_REASON_TPR_BELOW_THRESHOLD,   "TPR_BELOW_THRESHOLD" }, \
	{ EXIT_REASON_APIC_ACCESS,           "APIC_ACCESS" }, \
	{ EXIT_REASON_EOI_INDUCED,           "EOI_INDUCED" }, \
	{ EXIT_REASON_GDTR_IDTR,             "GDTR_IDTR" }, \
	{ EXIT_REASON_LDTR_TR,               "LDTR_TR" }, \
	{ EXIT_REASON_EPT_VIOLATION,         "EPT_VIOLATION" }, \
	{ EXIT_REASON_EPT_MISCONFIG,         "EPT_MISCONFIG" }, \
	{ EXIT_REASON_INVEPT,                "INVEPT" }, \
	{ EXIT_REASON_RDTSCP,                "RDTSCP" }, \
	{ EXIT_REASON_PREEMPTION_TIMER,      "PREEMPTION_TIMER" }, \
	{ EXIT_REASON_INVVPID,               "INVVPID" }, \
	{ EXIT_REASON_WBINVD,                "WBINVD" }, \
	{ EXIT_REASON_XSETBV,                "XSETBV" }, \
	{ EXIT_REASON_APIC_WRITE,            "APIC_WRITE" }, \
	{ EXIT_REASON_RDRAND,                "RDRAND" }, \
	{ EXIT_REASON_INVPCID,               "INVPCID" }, \
	{ EXIT_REASON_VMFUNC,                "VMFUNC" }, \
	{ EXIT_REASON_ENCLS,                 "ENCLS" }, \
	{ EXIT_REASON_RDSEED,                "RDSEED" }, \
	{ EXIT_REASON_PML_FULL,              "PML_FULL" }, \
	{ EXIT_REASON_XSAVES,                "XSAVES" }, \
	{ EXIT_REASON_XRSTORS,               "XRSTORS" }, \
	{ EXIT_REASON_UMWAIT,                "UMWAIT" }, \
	{ EXIT_REASON_TPAUSE,                "TPAUSE" }

#define VMX_ABORT_SAVE_GUEST_MSR_FAIL        1
#define VMX_ABORT_LOAD_HOST_PDPTE_FAIL       2
#define VMX_ABORT_LOAD_HOST_MSR_FAIL         4

#endif /* _UAPIVMX_H */
