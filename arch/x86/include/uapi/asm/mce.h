/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_ASM_X86_MCE_H
#define _UAPI_ASM_X86_MCE_H

#include <linux/types.h>
#include <linux/ioctl.h>

/* Fields are zero when not available */
struct mce {
	__u64 status;
	__u64 misc;
	__u64 addr;
	__u64 mcgstatus;
	__u64 ip;
	__u64 tsc;	/* cpu time stamp counter */
	__u64 time;	/* wall time_t when error was detected */
	__u8  cpuvendor;	/* cpu vendor as encoded in system.h */
	__u8  inject_flags;	/* software inject flags */
	__u8  severity;
	__u8  pad;
	__u32 cpuid;	/* CPUID 1 EAX */
	__u8  cs;		/* code segment */
	__u8  bank;	/* machine check bank */
	__u8  cpu;	/* cpu number; obsolete; use extcpu now */
	__u8  finished;   /* entry is valid */
	__u32 extcpu;	/* linux cpu number that detected the error */
	__u32 socketid;	/* CPU socket ID */
	__u32 apicid;	/* CPU initial apic ID */
	__u64 mcgcap;	/* MCGCAP MSR: machine check capabilities of CPU */
	__u64 synd;	/* MCA_SYND MSR: only valid on SMCA systems */
	__u64 ipid;	/* MCA_IPID MSR: only valid on SMCA systems */
	__u64 ppin;	/* Protected Processor Inventory Number */
};

#define MCE_GET_RECORD_LEN   _IOR('M', 1, int)
#define MCE_GET_LOG_LEN      _IOR('M', 2, int)
#define MCE_GETCLEAR_FLAGS   _IOR('M', 3, int)

#endif /* _UAPI_ASM_X86_MCE_H */
