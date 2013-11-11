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
#include <asm/types.h>
#include <asm/mmu.h>

/*
 * We only have to have statically allocated lppaca structs on
 * legacy iSeries, which supports at most 64 cpus.
 */
#define NR_LPPACAS	1

/*
 * The Hypervisor barfs if the lppaca crosses a page boundary.  A 1k
 * alignment is sufficient to prevent this
 */
struct lppaca {
	/* cacheline 1 contains read-only data */

	u32	desc;			/* Eye catcher 0xD397D781 */
	u16	size;			/* Size of this struct */
	u16	reserved1;
	u16	reserved2:14;
	u8	shared_proc:1;		/* Shared processor indicator */
	u8	secondary_thread:1;	/* Secondary thread indicator */
	u8	reserved3[14];
	volatile u32 dyn_hw_node_id;	/* Dynamic hardware node id */
	volatile u32 dyn_hw_proc_id;	/* Dynamic hardware proc id */
	u8	reserved4[56];
	volatile u8 vphn_assoc_counts[8]; /* Virtual processor home node */
					  /* associativity change counters */
	u8	reserved5[32];

	/* cacheline 2 contains local read-write data */

	u8	reserved6[48];
	u8	cede_latency_hint;
	u8	reserved7[7];
	u8	dtl_enable_mask;	/* Dispatch Trace Log mask */
	u8	donate_dedicated_cpu;	/* Donate dedicated CPU cycles */
	u8	fpregs_in_use;
	u8	pmcregs_in_use;
	u8	reserved8[28];
	u64	wait_state_cycles;	/* Wait cycles for this proc */
	u8	reserved9[28];
	u16	slb_count;		/* # of SLBs to maintain */
	u8	idle;			/* Indicate OS is idle */
	u8	vmxregs_in_use;

	/* cacheline 3 is shared with other processors */

	/*
	 * This is the yield_count.  An "odd" value (low bit on) means that
	 * the processor is yielded (either because of an OS yield or a
	 * hypervisor preempt).  An even value implies that the processor is
	 * currently executing.
	 * NOTE: This value will ALWAYS be zero for dedicated processors and
	 * will NEVER be zero for shared processors (ie, initialized to a 1).
	 */
	volatile u32 yield_count;
	volatile u32 dispersion_count;	/* dispatch changed physical cpu */
	volatile u64 cmo_faults;	/* CMO page fault count */
	volatile u64 cmo_fault_time;	/* CMO page fault time */
	u8	reserved10[104];

	/* cacheline 4-5 */

	u32	page_ins;		/* CMO Hint - # page ins by OS */
	u8	reserved11[148];
	volatile u64 dtl_idx;		/* Dispatch Trace Log head index */
	u8	reserved12[96];
} __attribute__((__aligned__(0x400)));

extern struct lppaca lppaca[];

#define lppaca_of(cpu)	(*paca[cpu].lppaca_ptr)

/*
 * SLB shadow buffer structure as defined in the PAPR.  The save_area
 * contains adjacent ESID and VSID pairs for each shadowed SLB.  The
 * ESID is stored in the lower 64bits, then the VSID.
 */
struct slb_shadow {
	u32	persistent;		/* Number of persistent SLBs */
	u32	buffer_length;		/* Total shadow buffer length */
	u64	reserved;
	struct	{
		u64     esid;
		u64	vsid;
	} save_area[SLB_NUM_BOLTED];
} ____cacheline_aligned;

extern struct slb_shadow slb_shadow[];

/*
 * Layout of entries in the hypervisor's dispatch trace log buffer.
 */
struct dtl_entry {
	u8	dispatch_reason;
	u8	preempt_reason;
	u16	processor_id;
	u32	enqueue_to_dispatch_time;
	u32	ready_to_enqueue_time;
	u32	waiting_to_ready_time;
	u64	timebase;
	u64	fault_addr;
	u64	srr0;
	u64	srr1;
};

#define DISPATCH_LOG_BYTES	4096	/* bytes per cpu */
#define N_DISPATCH_LOG		(DISPATCH_LOG_BYTES / sizeof(struct dtl_entry))

extern struct kmem_cache *dtl_cache;

/*
 * When CONFIG_VIRT_CPU_ACCOUNTING_NATIVE = y, the cpu accounting code controls
 * reading from the dispatch trace log.  If other code wants to consume
 * DTL entries, it can set this pointer to a function that will get
 * called once for each DTL entry that gets processed.
 */
extern void (*dtl_consumer)(struct dtl_entry *entry, u64 index);

#endif /* CONFIG_PPC_BOOK3S */
#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_LPPACA_H */
