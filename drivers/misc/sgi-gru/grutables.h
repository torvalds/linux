/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * SN Platform GRU Driver
 *
 *            GRU DRIVER TABLES, MACROS, externs, etc
 *
 *  Copyright (c) 2008 Silicon Graphics, Inc.  All Rights Reserved.
 */

#ifndef __GRUTABLES_H__
#define __GRUTABLES_H__

/*
 * GRU Chiplet:
 *   The GRU is a user addressible memory accelerator. It provides
 *   several forms of load, store, memset, bcopy instructions. In addition, it
 *   contains special instructions for AMOs, sending messages to message
 *   queues, etc.
 *
 *   The GRU is an integral part of the node controller. It connects
 *   directly to the cpu socket. In its current implementation, there are 2
 *   GRU chiplets in the node controller on each blade (~node).
 *
 *   The entire GRU memory space is fully coherent and cacheable by the cpus.
 *
 *   Each GRU chiplet has a physical memory map that looks like the following:
 *
 *   	+-----------------+
 *   	|/////////////////|
 *   	|/////////////////|
 *   	|/////////////////|
 *   	|/////////////////|
 *   	|/////////////////|
 *   	|/////////////////|
 *   	|/////////////////|
 *   	|/////////////////|
 *   	+-----------------+
 *   	|  system control |
 *   	+-----------------+        _______ +-------------+
 *   	|/////////////////|       /        |             |
 *   	|/////////////////|      /         |             |
 *   	|/////////////////|     /          | instructions|
 *   	|/////////////////|    /           |             |
 *   	|/////////////////|   /            |             |
 *   	|/////////////////|  /             |-------------|
 *   	|/////////////////| /              |             |
 *   	+-----------------+                |             |
 *   	|   context 15    |                |  data       |
 *   	+-----------------+                |             |
 *   	|    ......       | \              |             |
 *   	+-----------------+  \____________ +-------------+
 *   	|   context 1     |
 *   	+-----------------+
 *   	|   context 0     |
 *   	+-----------------+
 *
 *   Each of the "contexts" is a chunk of memory that can be mmaped into user
 *   space. The context consists of 2 parts:
 *
 *  	- an instruction space that can be directly accessed by the user
 *  	  to issue GRU instructions and to check instruction status.
 *
 *  	- a data area that acts as normal RAM.
 *
 *   User instructions contain virtual addresses of data to be accessed by the
 *   GRU. The GRU contains a TLB that is used to convert these user virtual
 *   addresses to physical addresses.
 *
 *   The "system control" area of the GRU chiplet is used by the kernel driver
 *   to manage user contexts and to perform functions such as TLB dropin and
 *   purging.
 *
 *   One context may be reserved for the kernel and used for cross-partition
 *   communication. The GRU will also be used to asynchronously zero out
 *   large blocks of memory (not currently implemented).
 *
 *
 * Tables:
 *
 * 	VDATA-VMA Data		- Holds a few parameters. Head of linked list of
 * 				  GTS tables for threads using the GSEG
 * 	GTS - Gru Thread State  - contains info for managing a GSEG context. A
 * 				  GTS is allocated for each thread accessing a
 * 				  GSEG.
 *     	GTD - GRU Thread Data   - contains shadow copy of GRU data when GSEG is
 *     				  not loaded into a GRU
 *	GMS - GRU Memory Struct - Used to manage TLB shootdowns. Tracks GRUs
 *				  where a GSEG has been loaded. Similar to
 *				  an mm_struct but for GRU.
 *
 *	GS  - GRU State 	- Used to manage the state of a GRU chiplet
 *	BS  - Blade State	- Used to manage state of all GRU chiplets
 *				  on a blade
 *
 *
 *  Normal task tables for task using GRU.
 *  		- 2 threads in process
 *  		- 2 GSEGs open in process
 *  		- GSEG1 is being used by both threads
 *  		- GSEG2 is used only by thread 2
 *
 *       task -->|
 *       task ---+---> mm ->------ (notifier) -------+-> gms
 *                     |                             |
 *                     |--> vma -> vdata ---> gts--->|		GSEG1 (thread1)
 *                     |                  |          |
 *                     |                  +-> gts--->|		GSEG1 (thread2)
 *                     |                             |
 *                     |--> vma -> vdata ---> gts--->|		GSEG2 (thread2)
 *                     .
 *                     .
 *
 *  GSEGs are marked DONTCOPY on fork
 *
 * At open
 * 	file.private_data -> NULL
 *
 * At mmap,
 * 	vma -> vdata
 *
 * After gseg reference
 * 	vma -> vdata ->gts
 *
 * After fork
 *   parent
 * 	vma -> vdata -> gts
 *   child
 * 	(vma is not copied)
 *
 */

#include <linux/refcount.h>
#include <linux/rmap.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/mmu_notifier.h>
#include <linux/mm_types.h>
#include "gru.h"
#include "grulib.h"
#include "gruhandles.h"

extern struct gru_stats_s gru_stats;
extern struct gru_blade_state *gru_base[];
extern unsigned long gru_start_paddr, gru_end_paddr;
extern void *gru_start_vaddr;
extern unsigned int gru_max_gids;

#define GRU_MAX_BLADES		MAX_NUMNODES
#define GRU_MAX_GRUS		(GRU_MAX_BLADES * GRU_CHIPLETS_PER_BLADE)

#define GRU_DRIVER_ID_STR	"SGI GRU Device Driver"
#define GRU_DRIVER_VERSION_STR	"0.85"

/*
 * GRU statistics.
 */
struct gru_stats_s {
	atomic_long_t vdata_alloc;
	atomic_long_t vdata_free;
	atomic_long_t gts_alloc;
	atomic_long_t gts_free;
	atomic_long_t gms_alloc;
	atomic_long_t gms_free;
	atomic_long_t gts_double_allocate;
	atomic_long_t assign_context;
	atomic_long_t assign_context_failed;
	atomic_long_t free_context;
	atomic_long_t load_user_context;
	atomic_long_t load_kernel_context;
	atomic_long_t lock_kernel_context;
	atomic_long_t unlock_kernel_context;
	atomic_long_t steal_user_context;
	atomic_long_t steal_kernel_context;
	atomic_long_t steal_context_failed;
	atomic_long_t nopfn;
	atomic_long_t asid_new;
	atomic_long_t asid_next;
	atomic_long_t asid_wrap;
	atomic_long_t asid_reuse;
	atomic_long_t intr;
	atomic_long_t intr_cbr;
	atomic_long_t intr_tfh;
	atomic_long_t intr_spurious;
	atomic_long_t intr_mm_lock_failed;
	atomic_long_t call_os;
	atomic_long_t call_os_wait_queue;
	atomic_long_t user_flush_tlb;
	atomic_long_t user_unload_context;
	atomic_long_t user_exception;
	atomic_long_t set_context_option;
	atomic_long_t check_context_retarget_intr;
	atomic_long_t check_context_unload;
	atomic_long_t tlb_dropin;
	atomic_long_t tlb_preload_page;
	atomic_long_t tlb_dropin_fail_no_asid;
	atomic_long_t tlb_dropin_fail_upm;
	atomic_long_t tlb_dropin_fail_invalid;
	atomic_long_t tlb_dropin_fail_range_active;
	atomic_long_t tlb_dropin_fail_idle;
	atomic_long_t tlb_dropin_fail_fmm;
	atomic_long_t tlb_dropin_fail_no_exception;
	atomic_long_t tfh_stale_on_fault;
	atomic_long_t mmu_invalidate_range;
	atomic_long_t mmu_invalidate_page;
	atomic_long_t flush_tlb;
	atomic_long_t flush_tlb_gru;
	atomic_long_t flush_tlb_gru_tgh;
	atomic_long_t flush_tlb_gru_zero_asid;

	atomic_long_t copy_gpa;
	atomic_long_t read_gpa;

	atomic_long_t mesq_receive;
	atomic_long_t mesq_receive_none;
	atomic_long_t mesq_send;
	atomic_long_t mesq_send_failed;
	atomic_long_t mesq_noop;
	atomic_long_t mesq_send_unexpected_error;
	atomic_long_t mesq_send_lb_overflow;
	atomic_long_t mesq_send_qlimit_reached;
	atomic_long_t mesq_send_amo_nacked;
	atomic_long_t mesq_send_put_nacked;
	atomic_long_t mesq_page_overflow;
	atomic_long_t mesq_qf_locked;
	atomic_long_t mesq_qf_noop_not_full;
	atomic_long_t mesq_qf_switch_head_failed;
	atomic_long_t mesq_qf_unexpected_error;
	atomic_long_t mesq_noop_unexpected_error;
	atomic_long_t mesq_noop_lb_overflow;
	atomic_long_t mesq_noop_qlimit_reached;
	atomic_long_t mesq_noop_amo_nacked;
	atomic_long_t mesq_noop_put_nacked;
	atomic_long_t mesq_noop_page_overflow;

};

enum mcs_op {cchop_allocate, cchop_start, cchop_interrupt, cchop_interrupt_sync,
	cchop_deallocate, tfhop_write_only, tfhop_write_restart,
	tghop_invalidate, mcsop_last};

struct mcs_op_statistic {
	atomic_long_t	count;
	atomic_long_t	total;
	unsigned long	max;
};

extern struct mcs_op_statistic mcs_op_statistics[mcsop_last];

#define OPT_DPRINT		1
#define OPT_STATS		2


#define IRQ_GRU			110	/* Starting IRQ number for interrupts */

/* Delay in jiffies between attempts to assign a GRU context */
#define GRU_ASSIGN_DELAY	((HZ * 20) / 1000)

/*
 * If a process has it's context stolen, min delay in jiffies before trying to
 * steal a context from another process.
 */
#define GRU_STEAL_DELAY		((HZ * 200) / 1000)

#define STAT(id)	do {						\
				if (gru_options & OPT_STATS)		\
					atomic_long_inc(&gru_stats.id);	\
			} while (0)

#ifdef CONFIG_SGI_GRU_DEBUG
#define gru_dbg(dev, fmt, x...)						\
	do {								\
		if (gru_options & OPT_DPRINT)				\
			printk(KERN_DEBUG "GRU:%d %s: " fmt, smp_processor_id(), __func__, x);\
	} while (0)
#else
#define gru_dbg(x...)
#endif

/*-----------------------------------------------------------------------------
 * ASID management
 */
#define MAX_ASID	0xfffff0
#define MIN_ASID	8
#define ASID_INC	8	/* number of regions */

/* Generate a GRU asid value from a GRU base asid & a virtual address. */
#define VADDR_HI_BIT		64
#define GRUREGION(addr)		((addr) >> (VADDR_HI_BIT - 3) & 3)
#define GRUASID(asid, addr)	((asid) + GRUREGION(addr))

/*------------------------------------------------------------------------------
 *  File & VMS Tables
 */

struct gru_state;

/*
 * This structure is pointed to from the mmstruct via the notifier pointer.
 * There is one of these per address space.
 */
struct gru_mm_tracker {				/* pack to reduce size */
	unsigned int		mt_asid_gen:24;	/* ASID wrap count */
	unsigned int		mt_asid:24;	/* current base ASID for gru */
	unsigned short		mt_ctxbitmap:16;/* bitmap of contexts using
						   asid */
} __attribute__ ((packed));

struct gru_mm_struct {
	struct mmu_notifier	ms_notifier;
	spinlock_t		ms_asid_lock;	/* protects ASID assignment */
	atomic_t		ms_range_active;/* num range_invals active */
	wait_queue_head_t	ms_wait_queue;
	DECLARE_BITMAP(ms_asidmap, GRU_MAX_GRUS);
	struct gru_mm_tracker	ms_asids[GRU_MAX_GRUS];
};

/*
 * One of these structures is allocated when a GSEG is mmaped. The
 * structure is pointed to by the vma->vm_private_data field in the vma struct.
 */
struct gru_vma_data {
	spinlock_t		vd_lock;	/* Serialize access to vma */
	struct list_head	vd_head;	/* head of linked list of gts */
	long			vd_user_options;/* misc user option flags */
	int			vd_cbr_au_count;
	int			vd_dsr_au_count;
	unsigned char		vd_tlb_preload_count;
};

/*
 * One of these is allocated for each thread accessing a mmaped GRU. A linked
 * list of these structure is hung off the struct gru_vma_data in the mm_struct.
 */
struct gru_thread_state {
	struct list_head	ts_next;	/* list - head at vma-private */
	struct mutex		ts_ctxlock;	/* load/unload CTX lock */
	struct mm_struct	*ts_mm;		/* mm currently mapped to
						   context */
	struct vm_area_struct	*ts_vma;	/* vma of GRU context */
	struct gru_state	*ts_gru;	/* GRU where the context is
						   loaded */
	struct gru_mm_struct	*ts_gms;	/* asid & ioproc struct */
	unsigned char		ts_tlb_preload_count; /* TLB preload pages */
	unsigned long		ts_cbr_map;	/* map of allocated CBRs */
	unsigned long		ts_dsr_map;	/* map of allocated DATA
						   resources */
	unsigned long		ts_steal_jiffies;/* jiffies when context last
						    stolen */
	long			ts_user_options;/* misc user option flags */
	pid_t			ts_tgid_owner;	/* task that is using the
						   context - for migration */
	short			ts_user_blade_id;/* user selected blade */
	signed char		ts_user_chiplet_id;/* user selected chiplet */
	unsigned short		ts_sizeavail;	/* Pagesizes in use */
	int			ts_tsid;	/* thread that owns the
						   structure */
	int			ts_tlb_int_select;/* target cpu if interrupts
						     enabled */
	int			ts_ctxnum;	/* context number where the
						   context is loaded */
	refcount_t		ts_refcnt;	/* reference count GTS */
	unsigned char		ts_dsr_au_count;/* Number of DSR resources
						   required for contest */
	unsigned char		ts_cbr_au_count;/* Number of CBR resources
						   required for contest */
	signed char		ts_cch_req_slice;/* CCH packet slice */
	signed char		ts_blade;	/* If >= 0, migrate context if
						   ref from different blade */
	signed char		ts_force_cch_reload;
	signed char		ts_cbr_idx[GRU_CBR_AU];/* CBR numbers of each
							  allocated CB */
	int			ts_data_valid;	/* Indicates if ts_gdata has
						   valid data */
	struct gru_gseg_statistics ustats;	/* User statistics */
	unsigned long		ts_gdata[];	/* save area for GRU data (CB,
						   DS, CBE) */
};

/*
 * Threaded programs actually allocate an array of GSEGs when a context is
 * created. Each thread uses a separate GSEG. TSID is the index into the GSEG
 * array.
 */
#define TSID(a, v)		(((a) - (v)->vm_start) / GRU_GSEG_PAGESIZE)
#define UGRUADDR(gts)		((gts)->ts_vma->vm_start +		\
					(gts)->ts_tsid * GRU_GSEG_PAGESIZE)

#define NULLCTX			(-1)	/* if context not loaded into GRU */

/*-----------------------------------------------------------------------------
 *  GRU State Tables
 */

/*
 * One of these exists for each GRU chiplet.
 */
struct gru_state {
	struct gru_blade_state	*gs_blade;		/* GRU state for entire
							   blade */
	unsigned long		gs_gru_base_paddr;	/* Physical address of
							   gru segments (64) */
	void			*gs_gru_base_vaddr;	/* Virtual address of
							   gru segments (64) */
	unsigned short		gs_gid;			/* unique GRU number */
	unsigned short		gs_blade_id;		/* blade of GRU */
	unsigned char		gs_chiplet_id;		/* blade chiplet of GRU */
	unsigned char		gs_tgh_local_shift;	/* used to pick TGH for
							   local flush */
	unsigned char		gs_tgh_first_remote;	/* starting TGH# for
							   remote flush */
	spinlock_t		gs_asid_lock;		/* lock used for
							   assigning asids */
	spinlock_t		gs_lock;		/* lock used for
							   assigning contexts */

	/* -- the following are protected by the gs_asid_lock spinlock ---- */
	unsigned int		gs_asid;		/* Next availe ASID */
	unsigned int		gs_asid_limit;		/* Limit of available
							   ASIDs */
	unsigned int		gs_asid_gen;		/* asid generation.
							   Inc on wrap */

	/* --- the following fields are protected by the gs_lock spinlock --- */
	unsigned long		gs_context_map;		/* bitmap to manage
							   contexts in use */
	unsigned long		gs_cbr_map;		/* bitmap to manage CB
							   resources */
	unsigned long		gs_dsr_map;		/* bitmap used to manage
							   DATA resources */
	unsigned int		gs_reserved_cbrs;	/* Number of kernel-
							   reserved cbrs */
	unsigned int		gs_reserved_dsr_bytes;	/* Bytes of kernel-
							   reserved dsrs */
	unsigned short		gs_active_contexts;	/* number of contexts
							   in use */
	struct gru_thread_state	*gs_gts[GRU_NUM_CCH];	/* GTS currently using
							   the context */
	int			gs_irq[GRU_NUM_TFM];	/* Interrupt irqs */
};

/*
 * This structure contains the GRU state for all the GRUs on a blade.
 */
struct gru_blade_state {
	void			*kernel_cb;		/* First kernel
							   reserved cb */
	void			*kernel_dsr;		/* First kernel
							   reserved DSR */
	struct rw_semaphore	bs_kgts_sema;		/* lock for kgts */
	struct gru_thread_state *bs_kgts;		/* GTS for kernel use */

	/* ---- the following are used for managing kernel async GRU CBRs --- */
	int			bs_async_dsr_bytes;	/* DSRs for async */
	int			bs_async_cbrs;		/* CBRs AU for async */
	struct completion	*bs_async_wq;

	/* ---- the following are protected by the bs_lock spinlock ---- */
	spinlock_t		bs_lock;		/* lock used for
							   stealing contexts */
	int			bs_lru_ctxnum;		/* STEAL - last context
							   stolen */
	struct gru_state	*bs_lru_gru;		/* STEAL - last gru
							   stolen */

	struct gru_state	bs_grus[GRU_CHIPLETS_PER_BLADE];
};

/*-----------------------------------------------------------------------------
 * Address Primitives
 */
#define get_tfm_for_cpu(g, c)						\
	((struct gru_tlb_fault_map *)get_tfm((g)->gs_gru_base_vaddr, (c)))
#define get_tfh_by_index(g, i)						\
	((struct gru_tlb_fault_handle *)get_tfh((g)->gs_gru_base_vaddr, (i)))
#define get_tgh_by_index(g, i)						\
	((struct gru_tlb_global_handle *)get_tgh((g)->gs_gru_base_vaddr, (i)))
#define get_cbe_by_index(g, i)						\
	((struct gru_control_block_extended *)get_cbe((g)->gs_gru_base_vaddr,\
			(i)))

/*-----------------------------------------------------------------------------
 * Useful Macros
 */

/* Given a blade# & chiplet#, get a pointer to the GRU */
#define get_gru(b, c)		(&gru_base[b]->bs_grus[c])

/* Number of bytes to save/restore when unloading/loading GRU contexts */
#define DSR_BYTES(dsr)		((dsr) * GRU_DSR_AU_BYTES)
#define CBR_BYTES(cbr)		((cbr) * GRU_HANDLE_BYTES * GRU_CBR_AU_SIZE * 2)

/* Convert a user CB number to the actual CBRNUM */
#define thread_cbr_number(gts, n) ((gts)->ts_cbr_idx[(n) / GRU_CBR_AU_SIZE] \
				  * GRU_CBR_AU_SIZE + (n) % GRU_CBR_AU_SIZE)

/* Convert a gid to a pointer to the GRU */
#define GID_TO_GRU(gid)							\
	(gru_base[(gid) / GRU_CHIPLETS_PER_BLADE] ?			\
		(&gru_base[(gid) / GRU_CHIPLETS_PER_BLADE]->		\
			bs_grus[(gid) % GRU_CHIPLETS_PER_BLADE]) :	\
	 NULL)

/* Scan all active GRUs in a GRU bitmap */
#define for_each_gru_in_bitmap(gid, map)				\
	for_each_set_bit((gid), (map), GRU_MAX_GRUS)

/* Scan all active GRUs on a specific blade */
#define for_each_gru_on_blade(gru, nid, i)				\
	for ((gru) = gru_base[nid]->bs_grus, (i) = 0;			\
			(i) < GRU_CHIPLETS_PER_BLADE;			\
			(i)++, (gru)++)

/* Scan all GRUs */
#define foreach_gid(gid)						\
	for ((gid) = 0; (gid) < gru_max_gids; (gid)++)

/* Scan all active GTSs on a gru. Note: must hold ss_lock to use this macro. */
#define for_each_gts_on_gru(gts, gru, ctxnum)				\
	for ((ctxnum) = 0; (ctxnum) < GRU_NUM_CCH; (ctxnum)++)		\
		if (((gts) = (gru)->gs_gts[ctxnum]))

/* Scan each CBR whose bit is set in a TFM (or copy of) */
#define for_each_cbr_in_tfm(i, map)					\
	for_each_set_bit((i), (map), GRU_NUM_CBE)

/* Scan each CBR in a CBR bitmap. Note: multiple CBRs in an allocation unit */
#define for_each_cbr_in_allocation_map(i, map, k)			\
	for_each_set_bit((k), (map), GRU_CBR_AU)			\
		for ((i) = (k)*GRU_CBR_AU_SIZE;				\
				(i) < ((k) + 1) * GRU_CBR_AU_SIZE; (i)++)

#define gseg_physical_address(gru, ctxnum)				\
		((gru)->gs_gru_base_paddr + ctxnum * GRU_GSEG_STRIDE)
#define gseg_virtual_address(gru, ctxnum)				\
		((gru)->gs_gru_base_vaddr + ctxnum * GRU_GSEG_STRIDE)

/*-----------------------------------------------------------------------------
 * Lock / Unlock GRU handles
 * 	Use the "delresp" bit in the handle as a "lock" bit.
 */

/* Lock hierarchy checking enabled only in emulator */

/* 0 = lock failed, 1 = locked */
static inline int __trylock_handle(void *h)
{
	return !test_and_set_bit(1, h);
}

static inline void __lock_handle(void *h)
{
	while (test_and_set_bit(1, h))
		cpu_relax();
}

static inline void __unlock_handle(void *h)
{
	clear_bit(1, h);
}

static inline int trylock_cch_handle(struct gru_context_configuration_handle *cch)
{
	return __trylock_handle(cch);
}

static inline void lock_cch_handle(struct gru_context_configuration_handle *cch)
{
	__lock_handle(cch);
}

static inline void unlock_cch_handle(struct gru_context_configuration_handle
				     *cch)
{
	__unlock_handle(cch);
}

static inline void lock_tgh_handle(struct gru_tlb_global_handle *tgh)
{
	__lock_handle(tgh);
}

static inline void unlock_tgh_handle(struct gru_tlb_global_handle *tgh)
{
	__unlock_handle(tgh);
}

static inline int is_kernel_context(struct gru_thread_state *gts)
{
	return !gts->ts_mm;
}

/*
 * The following are for Nehelem-EX. A more general scheme is needed for
 * future processors.
 */
#define UV_MAX_INT_CORES		8
#define uv_cpu_socket_number(p)		((cpu_physical_id(p) >> 5) & 1)
#define uv_cpu_ht_number(p)		(cpu_physical_id(p) & 1)
#define uv_cpu_core_number(p)		(((cpu_physical_id(p) >> 2) & 4) |	\
					((cpu_physical_id(p) >> 1) & 3))
/*-----------------------------------------------------------------------------
 * Function prototypes & externs
 */
struct gru_unload_context_req;

extern const struct vm_operations_struct gru_vm_ops;
extern struct device *grudev;

extern struct gru_vma_data *gru_alloc_vma_data(struct vm_area_struct *vma,
				int tsid);
extern struct gru_thread_state *gru_find_thread_state(struct vm_area_struct
				*vma, int tsid);
extern struct gru_thread_state *gru_alloc_thread_state(struct vm_area_struct
				*vma, int tsid);
extern struct gru_state *gru_assign_gru_context(struct gru_thread_state *gts);
extern void gru_load_context(struct gru_thread_state *gts);
extern void gru_steal_context(struct gru_thread_state *gts);
extern void gru_unload_context(struct gru_thread_state *gts, int savestate);
extern int gru_update_cch(struct gru_thread_state *gts);
extern void gts_drop(struct gru_thread_state *gts);
extern void gru_tgh_flush_init(struct gru_state *gru);
extern int gru_kservices_init(void);
extern void gru_kservices_exit(void);
extern irqreturn_t gru0_intr(int irq, void *dev_id);
extern irqreturn_t gru1_intr(int irq, void *dev_id);
extern irqreturn_t gru_intr_mblade(int irq, void *dev_id);
extern int gru_dump_chiplet_request(unsigned long arg);
extern long gru_get_gseg_statistics(unsigned long arg);
extern int gru_handle_user_call_os(unsigned long address);
extern int gru_user_flush_tlb(unsigned long arg);
extern int gru_user_unload_context(unsigned long arg);
extern int gru_get_exception_detail(unsigned long arg);
extern int gru_set_context_option(unsigned long address);
extern void gru_check_context_placement(struct gru_thread_state *gts);
extern int gru_cpu_fault_map_id(void);
extern struct vm_area_struct *gru_find_vma(unsigned long vaddr);
extern void gru_flush_all_tlb(struct gru_state *gru);
extern int gru_proc_init(void);
extern void gru_proc_exit(void);

extern struct gru_thread_state *gru_alloc_gts(struct vm_area_struct *vma,
		int cbr_au_count, int dsr_au_count,
		unsigned char tlb_preload_count, int options, int tsid);
extern unsigned long gru_reserve_cb_resources(struct gru_state *gru,
		int cbr_au_count, signed char *cbmap);
extern unsigned long gru_reserve_ds_resources(struct gru_state *gru,
		int dsr_au_count, signed char *dsmap);
extern vm_fault_t gru_fault(struct vm_fault *vmf);
extern struct gru_mm_struct *gru_register_mmu_notifier(void);
extern void gru_drop_mmu_notifier(struct gru_mm_struct *gms);

extern int gru_ktest(unsigned long arg);
extern void gru_flush_tlb_range(struct gru_mm_struct *gms, unsigned long start,
					unsigned long len);

extern unsigned long gru_options;

#endif /* __GRUTABLES_H__ */
