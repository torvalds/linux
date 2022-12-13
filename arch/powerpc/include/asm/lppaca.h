/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * lppaca.h
 * Copyright (C) 2001  Mike Corrigan IBM Corporation
 */
#ifndef _ASM_POWERPC_LPPACA_H
#define _ASM_POWERPC_LPPACA_H

/*
 * The below VPHN macros are outside the __KERNEL__ check since these are
 * used for compiling the vphn selftest in userspace
 */

/* The H_HOME_NODE_ASSOCIATIVITY h_call returns 6 64-bit registers. */
#define VPHN_REGISTER_COUNT 6

/*
 * 6 64-bit registers unpacked into up to 24 be32 associativity values. To
 * form the complete property we have to add the length in the first cell.
 */
#define VPHN_ASSOC_BUFSIZE (VPHN_REGISTER_COUNT*sizeof(u64)/sizeof(u16) + 1)

/*
 * The H_HOME_NODE_ASSOCIATIVITY hcall takes two values for flags:
 * 1 for retrieving associativity information for a guest cpu
 * 2 for retrieving associativity information for a host/hypervisor cpu
 */
#define VPHN_FLAG_VCPU	1
#define VPHN_FLAG_PCPU	2

#ifdef __KERNEL__

/*
 * These definitions relate to hypervisors that only exist when using
 * a server type processor
 */
#ifdef CONFIG_PPC_BOOK3S

/*
 * This control block contains the data that is shared between the
 * hypervisor and the OS.
 */
#include <linux/cache.h>
#include <linux/threads.h>
#include <asm/types.h>
#include <asm/mmu.h>
#include <asm/firmware.h>

/*
 * The lppaca is the "virtual processor area" registered with the hypervisor,
 * H_REGISTER_VPA etc.
 *
 * According to PAPR, the structure is 640 bytes long, must be L1 cache line
 * aligned, and must not cross a 4kB boundary. Its size field must be at
 * least 640 bytes (but may be more).
 *
 * Pre-v4.14 KVM hypervisors reject the VPA if its size field is smaller than
 * 1kB, so we dynamically allocate 1kB and advertise size as 1kB, but keep
 * this structure as the canonical 640 byte size.
 */
struct lppaca {
	/* cacheline 1 contains read-only data */

	__be32	desc;			/* Eye catcher 0xD397D781 */
	__be16	size;			/* Size of this struct */
	u8	reserved1[3];
	u8	__old_status;		/* Old status, including shared proc */
	u8	reserved3[14];
	volatile __be32 dyn_hw_node_id;	/* Dynamic hardware node id */
	volatile __be32 dyn_hw_proc_id;	/* Dynamic hardware proc id */
	u8	reserved4[56];
	volatile u8 vphn_assoc_counts[8]; /* Virtual processor home node */
					  /* associativity change counters */
	u8	reserved5[32];

	/* cacheline 2 contains local read-write data */

	u8	reserved6[48];
	u8	cede_latency_hint;
	u8	ebb_regs_in_use;
	u8	reserved7[6];
	u8	dtl_enable_mask;	/* Dispatch Trace Log mask */
	u8	donate_dedicated_cpu;	/* Donate dedicated CPU cycles */
	u8	fpregs_in_use;
	u8	pmcregs_in_use;
	u8	reserved8[28];
	__be64	wait_state_cycles;	/* Wait cycles for this proc */
	u8	reserved9[28];
	__be16	slb_count;		/* # of SLBs to maintain */
	u8	idle;			/* Indicate OS is idle */
	u8	vmxregs_in_use;

	/* cacheline 3 is shared with other processors */

	/*
	 * This is the yield_count.  An "odd" value (low bit on) means that
	 * the processor is yielded (either because of an OS yield or a
	 * hypervisor preempt).  An even value implies that the processor is
	 * currently executing.
	 * NOTE: Even dedicated processor partitions can yield so this
	 * field cannot be used to determine if we are shared or dedicated.
	 */
	volatile __be32 yield_count;
	volatile __be32 dispersion_count; /* dispatch changed physical cpu */
	volatile __be64 cmo_faults;	/* CMO page fault count */
	volatile __be64 cmo_fault_time;	/* CMO page fault time */
	u8	reserved10[64];		/* [S]PURR expropriated/donated */
	volatile __be64 enqueue_dispatch_tb; /* Total TB enqueue->dispatch */
	volatile __be64 ready_enqueue_tb; /* Total TB ready->enqueue */
	volatile __be64 wait_ready_tb;	/* Total TB wait->ready */
	u8	reserved11[16];

	/* cacheline 4-5 */

	__be32	page_ins;		/* CMO Hint - # page ins by OS */
	u8	reserved12[148];
	volatile __be64 dtl_idx;	/* Dispatch Trace Log head index */
	u8	reserved13[96];
} ____cacheline_aligned;

#define lppaca_of(cpu)	(*paca_ptrs[cpu]->lppaca_ptr)

/*
 * We are using a non architected field to determine if a partition is
 * shared or dedicated. This currently works on both KVM and PHYP, but
 * we will have to transition to something better.
 */
#define LPPACA_OLD_SHARED_PROC		2

static inline bool lppaca_shared_proc(struct lppaca *l)
{
	if (!firmware_has_feature(FW_FEATURE_SPLPAR))
		return false;
	return !!(l->__old_status & LPPACA_OLD_SHARED_PROC);
}

/*
 * SLB shadow buffer structure as defined in the PAPR.  The save_area
 * contains adjacent ESID and VSID pairs for each shadowed SLB.  The
 * ESID is stored in the lower 64bits, then the VSID.
 */
struct slb_shadow {
	__be32	persistent;		/* Number of persistent SLBs */
	__be32	buffer_length;		/* Total shadow buffer length */
	__be64	reserved;
	struct	{
		__be64     esid;
		__be64	vsid;
	} save_area[SLB_NUM_BOLTED];
} ____cacheline_aligned;

extern long hcall_vphn(unsigned long cpu, u64 flags, __be32 *associativity);

#endif /* CONFIG_PPC_BOOK3S */
#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_LPPACA_H */
