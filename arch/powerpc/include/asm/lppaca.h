/*
 * lppaca.h
 * Copyright (C) 2001  Mike Corrigan IBM Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */
#ifndef _ASM_POWERPC_LPPACA_H
#define _ASM_POWERPC_LPPACA_H
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
#include <linux/spinlock_types.h>
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
	u8	reserved10[104];

	/* cacheline 4-5 */

	__be32	page_ins;		/* CMO Hint - # page ins by OS */
	u8	reserved11[148];
	volatile __be64 dtl_idx;	/* Dispatch Trace Log head index */
	u8	reserved12[96];
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

/*
 * Layout of entries in the hypervisor's dispatch trace log buffer.
 */
struct dtl_entry {
	u8	dispatch_reason;
	u8	preempt_reason;
	__be16	processor_id;
	__be32	enqueue_to_dispatch_time;
	__be32	ready_to_enqueue_time;
	__be32	waiting_to_ready_time;
	__be64	timebase;
	__be64	fault_addr;
	__be64	srr0;
	__be64	srr1;
};

#define DISPATCH_LOG_BYTES	4096	/* bytes per cpu */
#define N_DISPATCH_LOG		(DISPATCH_LOG_BYTES / sizeof(struct dtl_entry))

/*
 * Dispatch trace log event enable mask:
 *   0x1: voluntary virtual processor waits
 *   0x2: time-slice preempts
 *   0x4: virtual partition memory page faults
 */
#define DTL_LOG_CEDE		0x1
#define DTL_LOG_PREEMPT		0x2
#define DTL_LOG_FAULT		0x4
#define DTL_LOG_ALL		(DTL_LOG_CEDE | DTL_LOG_PREEMPT | DTL_LOG_FAULT)

extern struct kmem_cache *dtl_cache;
extern rwlock_t dtl_access_lock;

/*
 * When CONFIG_VIRT_CPU_ACCOUNTING_NATIVE = y, the cpu accounting code controls
 * reading from the dispatch trace log.  If other code wants to consume
 * DTL entries, it can set this pointer to a function that will get
 * called once for each DTL entry that gets processed.
 */
extern void (*dtl_consumer)(struct dtl_entry *entry, u64 index);

extern void register_dtl_buffer(int cpu);
extern void alloc_dtl_buffers(void);

#endif /* CONFIG_PPC_BOOK3S */
#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_LPPACA_H */
