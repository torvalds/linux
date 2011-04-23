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

/* These definitions relate to hypervisors that only exist when using
 * a server type processor
 */
#ifdef CONFIG_PPC_BOOK3S

//=============================================================================
//
//	This control block contains the data that is shared between the
//	hypervisor (PLIC) and the OS.
//
//
//----------------------------------------------------------------------------
#include <linux/cache.h>
#include <linux/threads.h>
#include <asm/types.h>
#include <asm/mmu.h>

/*
 * We only have to have statically allocated lppaca structs on
 * legacy iSeries, which supports at most 64 cpus.
 */
#ifdef CONFIG_PPC_ISERIES
#if NR_CPUS < 64
#define NR_LPPACAS	NR_CPUS
#else
#define NR_LPPACAS	64
#endif
#else /* not iSeries */
#define NR_LPPACAS	1
#endif


/* The Hypervisor barfs if the lppaca crosses a page boundary.  A 1k
 * alignment is sufficient to prevent this */
struct lppaca {
//=============================================================================
// CACHE_LINE_1 0x0000 - 0x007F Contains read-only data
// NOTE: The xDynXyz fields are fields that will be dynamically changed by
// PLIC when preparing to bring a processor online or when dispatching a
// virtual processor!
//=============================================================================
	u32	desc;			// Eye catcher 0xD397D781	x00-x03
	u16	size;			// Size of this struct		x04-x05
	u16	reserved1;		// Reserved			x06-x07
	u16	reserved2:14;		// Reserved			x08-x09
	u8	shared_proc:1;		// Shared processor indicator	...
	u8	secondary_thread:1;	// Secondary thread indicator	...
	volatile u8 dyn_proc_status:8;	// Dynamic Status of this proc	x0A-x0A
	u8	secondary_thread_count;	// Secondary thread count	x0B-x0B
	volatile u16 dyn_hv_phys_proc_index;// Dynamic HV Physical Proc Index0C-x0D
	volatile u16 dyn_hv_log_proc_index;// Dynamic HV Logical Proc Indexx0E-x0F
	u32	decr_val;   		// Value for Decr programming 	x10-x13
	u32	pmc_val;       		// Value for PMC regs         	x14-x17
	volatile u32 dyn_hw_node_id;	// Dynamic Hardware Node id	x18-x1B
	volatile u32 dyn_hw_proc_id;	// Dynamic Hardware Proc Id	x1C-x1F
	volatile u32 dyn_pir;		// Dynamic ProcIdReg value	x20-x23
	u32	dsei_data;           	// DSEI data                  	x24-x27
	u64	sprg3;               	// SPRG3 value                	x28-x2F
	u8	reserved3[40];		// Reserved			x30-x57
	volatile u8 vphn_assoc_counts[8]; // Virtual processor home node
					// associativity change counters x58-x5F
	u8	reserved4[32];		// Reserved			x60-x7F

//=============================================================================
// CACHE_LINE_2 0x0080 - 0x00FF Contains local read-write data
//=============================================================================
	// This Dword contains a byte for each type of interrupt that can occur.
	// The IPI is a count while the others are just a binary 1 or 0.
	union {
		u64	any_int;
		struct {
			u16	reserved;	// Reserved - cleared by #mpasmbl
			u8	xirr_int;	// Indicates xXirrValue is valid or Immed IO
			u8	ipi_cnt;	// IPI Count
			u8	decr_int;	// DECR interrupt occurred
			u8	pdc_int;	// PDC interrupt occurred
			u8	quantum_int;	// Interrupt quantum reached
			u8	old_plic_deferred_ext_int;	// Old PLIC has a deferred XIRR pending
		} fields;
	} int_dword;

	// Whenever any fields in this Dword are set then PLIC will defer the
	// processing of external interrupts.  Note that PLIC will store the
	// XIRR directly into the xXirrValue field so that another XIRR will
	// not be presented until this one clears.  The layout of the low
	// 4-bytes of this Dword is up to SLIC - PLIC just checks whether the
	// entire Dword is zero or not.  A non-zero value in the low order
	// 2-bytes will result in SLIC being granted the highest thread
	// priority upon return.  A 0 will return to SLIC as medium priority.
	u64	plic_defer_ints_area;	// Entire Dword

	// Used to pass the real SRR0/1 from PLIC to SLIC as well as to
	// pass the target SRR0/1 from SLIC to PLIC on a SetAsrAndRfid.
	u64	saved_srr0;		// Saved SRR0                   x10-x17
	u64	saved_srr1;		// Saved SRR1                   x18-x1F

	// Used to pass parms from the OS to PLIC for SetAsrAndRfid
	u64	saved_gpr3;		// Saved GPR3                   x20-x27
	u64	saved_gpr4;		// Saved GPR4                   x28-x2F
	union {
		u64	saved_gpr5;	/* Saved GPR5               x30-x37 */
		struct {
			u8	cede_latency_hint;  /*			x30 */
			u8	reserved[7];        /*		    x31-x36 */
		} fields;
	} gpr5_dword;


	u8	dtl_enable_mask;	// Dispatch Trace Log mask	x38-x38
	u8	donate_dedicated_cpu;	// Donate dedicated CPU cycles  x39-x39
	u8	fpregs_in_use;		// FP regs in use               x3A-x3A
	u8	pmcregs_in_use;		// PMC regs in use              x3B-x3B
	volatile u32 saved_decr;	// Saved Decr Value             x3C-x3F
	volatile u64 emulated_time_base;// Emulated TB for this thread  x40-x47
	volatile u64 cur_plic_latency;	// Unaccounted PLIC latency     x48-x4F
	u64	tot_plic_latency;	// Accumulated PLIC latency     x50-x57
	u64	wait_state_cycles;	// Wait cycles for this proc    x58-x5F
	u64	end_of_quantum;		// TB at end of quantum         x60-x67
	u64	pdc_saved_sprg1;	// Saved SPRG1 for PMC int      x68-x6F
	u64	pdc_saved_srr0;		// Saved SRR0 for PMC int       x70-x77
	volatile u32 virtual_decr;	// Virtual DECR for shared procsx78-x7B
	u16	slb_count;		// # of SLBs to maintain        x7C-x7D
	u8	idle;			// Indicate OS is idle          x7E
	u8	vmxregs_in_use;		// VMX registers in use         x7F


//=============================================================================
// CACHE_LINE_3 0x0100 - 0x017F: This line is shared with other processors
//=============================================================================
	// This is the yield_count.  An "odd" value (low bit on) means that
	// the processor is yielded (either because of an OS yield or a PLIC
	// preempt).  An even value implies that the processor is currently
	// executing.
	// NOTE: This value will ALWAYS be zero for dedicated processors and
	// will NEVER be zero for shared processors (ie, initialized to a 1).
	volatile u32 yield_count;	// PLIC increments each dispatchx00-x03
	volatile u32 dispersion_count;	// dispatch changed phys cpu    x04-x07
	volatile u64 cmo_faults;	// CMO page fault count         x08-x0F
	volatile u64 cmo_fault_time;	// CMO page fault time          x10-x17
	u8	reserved7[104];		// Reserved                     x18-x7F

//=============================================================================
// CACHE_LINE_4-5 0x0180 - 0x027F Contains PMC interrupt data
//=============================================================================
	u32	page_ins;		// CMO Hint - # page ins by OS  x00-x03
	u8	reserved8[148];		// Reserved                     x04-x97
	volatile u64 dtl_idx;		// Dispatch Trace Log head idx	x98-x9F
	u8	reserved9[96];		// Reserved                     xA0-xFF
} __attribute__((__aligned__(0x400)));

extern struct lppaca lppaca[];

#define lppaca_of(cpu)	(*paca[cpu].lppaca_ptr)

/*
 * SLB shadow buffer structure as defined in the PAPR.  The save_area
 * contains adjacent ESID and VSID pairs for each shadowed SLB.  The
 * ESID is stored in the lower 64bits, then the VSID.
 */
struct slb_shadow {
	u32	persistent;		// Number of persistent SLBs	x00-x03
	u32	buffer_length;		// Total shadow buffer length	x04-x07
	u64	reserved;		// Alignment			x08-x0f
	struct	{
		u64     esid;
		u64	vsid;
	} save_area[SLB_NUM_BOLTED];	//				x10-x40
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

/*
 * When CONFIG_VIRT_CPU_ACCOUNTING = y, the cpu accounting code controls
 * reading from the dispatch trace log.  If other code wants to consume
 * DTL entries, it can set this pointer to a function that will get
 * called once for each DTL entry that gets processed.
 */
extern void (*dtl_consumer)(struct dtl_entry *entry, u64 index);

#endif /* CONFIG_PPC_BOOK3S */
#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_LPPACA_H */
