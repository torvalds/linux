/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_ASM_X86_MCE_H
#define _UAPI_ASM_X86_MCE_H

#include <linux/types.h>
#include <linux/ioctl.h>

/*
 * Fields are zero when not available. Also, this struct is shared with
 * userspace mcelog and thus must keep existing fields at current offsets.
 * Only add new fields to the end of the structure
 */
struct mce {
	__u64 status;		/* Bank's MCi_STATUS MSR */
	__u64 misc;		/* Bank's MCi_MISC MSR */
	__u64 addr;		/* Bank's MCi_ADDR MSR */
	__u64 mcgstatus;	/* Machine Check Global Status MSR */
	__u64 ip;		/* Instruction Pointer when the error happened */
	__u64 tsc;		/* CPU time stamp counter */
	__u64 time;		/* Wall time_t when error was detected */
	__u8  cpuvendor;	/* Kernel's X86_VENDOR enum */
	__u8  inject_flags;	/* Software inject flags */
	__u8  severity;		/* Error severity */
	__u8  pad;
	__u32 cpuid;		/* CPUID 1 EAX */
	__u8  cs;		/* Code segment */
	__u8  bank;		/* Machine check bank reporting the error */
	__u8  cpu;		/* CPU number; obsoleted by extcpu */
	__u8  finished;		/* Entry is valid */
	__u32 extcpu;		/* Linux CPU number that detected the error */
	__u32 socketid;		/* CPU socket ID */
	__u32 apicid;		/* CPU initial APIC ID */
	__u64 mcgcap;		/* MCGCAP MSR: machine check capabilities of CPU */
	__u64 synd;		/* MCA_SYND MSR: only valid on SMCA systems */
	__u64 ipid;		/* MCA_IPID MSR: only valid on SMCA systems */
	__u64 ppin;		/* Protected Processor Inventory Number */
	__u32 microcode;	/* Microcode revision */
	__u64 kflags;		/* Internal kernel use. See below */
};

#define MCE_GET_RECORD_LEN   _IOR('M', 1, int)
#define MCE_GET_LOG_LEN      _IOR('M', 2, int)
#define MCE_GETCLEAR_FLAGS   _IOR('M', 3, int)

#endif /* _UAPI_ASM_X86_MCE_H */
