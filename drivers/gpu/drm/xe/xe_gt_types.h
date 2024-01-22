/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022-2023 Intel Corporation
 */

#ifndef _XE_GT_TYPES_H_
#define _XE_GT_TYPES_H_

#include "xe_force_wake_types.h"
#include "xe_gt_idle_types.h"
#include "xe_hw_engine_types.h"
#include "xe_hw_fence_types.h"
#include "xe_reg_sr_types.h"
#include "xe_sa_types.h"
#include "xe_uc_types.h"

struct xe_exec_queue_ops;
struct xe_migrate;
struct xe_ring_ops;

enum xe_gt_type {
	XE_GT_TYPE_UNINITIALIZED,
	XE_GT_TYPE_MAIN,
	XE_GT_TYPE_MEDIA,
};

#define XE_MAX_DSS_FUSE_REGS	3
#define XE_MAX_EU_FUSE_REGS	1

typedef unsigned long xe_dss_mask_t[BITS_TO_LONGS(32 * XE_MAX_DSS_FUSE_REGS)];
typedef unsigned long xe_eu_mask_t[BITS_TO_LONGS(32 * XE_MAX_EU_FUSE_REGS)];

struct xe_mmio_range {
	u32 start;
	u32 end;
};

/*
 * The hardware has multiple kinds of multicast register ranges that need
 * special register steering (and future platforms are expected to add
 * additional types).
 *
 * During driver startup, we initialize the steering control register to
 * direct reads to a slice/subslice that are valid for the 'subslice' class
 * of multicast registers.  If another type of steering does not have any
 * overlap in valid steering targets with 'subslice' style registers, we will
 * need to explicitly re-steer reads of registers of the other type.
 *
 * Only the replication types that may need additional non-default steering
 * are listed here.
 */
enum xe_steering_type {
	L3BANK,
	MSLICE,
	LNCF,
	DSS,
	OADDRM,
	SQIDI_PSMI,

	/*
	 * On some platforms there are multiple types of MCR registers that
	 * will always return a non-terminated value at instance (0, 0).  We'll
	 * lump those all into a single category to keep things simple.
	 */
	INSTANCE0,

	/*
	 * Register ranges that don't need special steering for each register:
	 * it's sufficient to keep the HW-default for the selector, or only
	 * change it once, on GT initialization. This needs to be the last
	 * steering type.
	 */
	IMPLICIT_STEERING,
	NUM_STEERING_TYPES
};

#define gt_to_tile(gt__)							\
	_Generic(gt__,								\
		 const struct xe_gt * : (const struct xe_tile *)((gt__)->tile),	\
		 struct xe_gt * : (gt__)->tile)

#define gt_to_xe(gt__)										\
	_Generic(gt__,										\
		 const struct xe_gt * : (const struct xe_device *)(gt_to_tile(gt__)->xe),	\
		 struct xe_gt * : gt_to_tile(gt__)->xe)

/**
 * struct xe_gt - A "Graphics Technology" unit of the GPU
 *
 * A GT ("Graphics Technology") is the subset of a GPU primarily responsible
 * for implementing the graphics, compute, and/or media IP.  It encapsulates
 * the hardware engines, programmable execution units, and GuC.   Each GT has
 * its own handling of power management (RC6+forcewake) and multicast register
 * steering.
 *
 * A GPU/tile may have a single GT that supplies all graphics, compute, and
 * media functionality, or the graphics/compute and media may be split into
 * separate GTs within a tile.
 */
struct xe_gt {
	/** @tile: Backpointer to GT's tile */
	struct xe_tile *tile;

	/** @info: GT info */
	struct {
		/** @type: type of GT */
		enum xe_gt_type type;
		/** @id: Unique ID of this GT within the PCI Device */
		u8 id;
		/** @reference_clock: clock frequency */
		u32 reference_clock;
		/** @engine_mask: mask of engines present on GT */
		u64 engine_mask;
		/**
		 * @__engine_mask: mask of engines present on GT read from
		 * xe_pci.c, used to fake reading the engine_mask from the
		 * hwconfig blob.
		 */
		u64 __engine_mask;
	} info;

	/**
	 * @mmio: mmio info for GT.  All GTs within a tile share the same
	 * register space, but have their own copy of GSI registers at a
	 * specific offset, as well as their own forcewake handling.
	 */
	struct {
		/** @fw: force wake for GT */
		struct xe_force_wake fw;
		/**
		 * @adj_limit: adjust MMIO address if address is below this
		 * value
		 */
		u32 adj_limit;
		/** @adj_offset: offect to add to MMIO address when adjusting */
		u32 adj_offset;
	} mmio;

	/**
	 * @reg_sr: table with registers to be restored on GT init/resume/reset
	 */
	struct xe_reg_sr reg_sr;

	/** @reset: state for GT resets */
	struct {
		/**
		 * @worker: work so GT resets can done async allowing to reset
		 * code to safely flush all code paths
		 */
		struct work_struct worker;
	} reset;

	/** @tlb_invalidation: TLB invalidation state */
	struct {
		/** @seqno: TLB invalidation seqno, protected by CT lock */
#define TLB_INVALIDATION_SEQNO_MAX	0x100000
		int seqno;
		/**
		 * @seqno_recv: last received TLB invalidation seqno, protected by CT lock
		 */
		int seqno_recv;
		/**
		 * @pending_fences: list of pending fences waiting TLB
		 * invaliations, protected by CT lock
		 */
		struct list_head pending_fences;
		/**
		 * @pending_lock: protects @pending_fences and updating
		 * @seqno_recv.
		 */
		spinlock_t pending_lock;
		/**
		 * @fence_tdr: schedules a delayed call to
		 * xe_gt_tlb_fence_timeout after the timeut interval is over.
		 */
		struct delayed_work fence_tdr;
		/** @fence_context: context for TLB invalidation fences */
		u64 fence_context;
		/**
		 * @fence_seqno: seqno to TLB invalidation fences, protected by
		 * tlb_invalidation.lock
		 */
		u32 fence_seqno;
		/** @lock: protects TLB invalidation fences */
		spinlock_t lock;
	} tlb_invalidation;

	/**
	 * @ccs_mode: Number of compute engines enabled.
	 * Allows fixed mapping of available compute slices to compute engines.
	 * By default only the first available compute engine is enabled and all
	 * available compute slices are allocated to it.
	 */
	u32 ccs_mode;

	/** @usm: unified shared memory state */
	struct {
		/**
		 * @bb_pool: Pool from which batchbuffers, for USM operations
		 * (e.g. migrations, fixing page tables), are allocated.
		 * Dedicated pool needed so USM operations to not get blocked
		 * behind any user operations which may have resulted in a
		 * fault.
		 */
		struct xe_sa_manager *bb_pool;
		/**
		 * @reserved_bcs_instance: reserved BCS instance used for USM
		 * operations (e.g. mmigrations, fixing page tables)
		 */
		u16 reserved_bcs_instance;
		/** @pf_wq: page fault work queue, unbound, high priority */
		struct workqueue_struct *pf_wq;
		/** @acc_wq: access counter work queue, unbound, high priority */
		struct workqueue_struct *acc_wq;
		/**
		 * @pf_queue: Page fault queue used to sync faults so faults can
		 * be processed not under the GuC CT lock. The queue is sized so
		 * it can sync all possible faults (1 per physical engine).
		 * Multiple queues exists for page faults from different VMs are
		 * be processed in parallel.
		 */
		struct pf_queue {
			/** @gt: back pointer to GT */
			struct xe_gt *gt;
#define PF_QUEUE_NUM_DW	128
			/** @data: data in the page fault queue */
			u32 data[PF_QUEUE_NUM_DW];
			/**
			 * @head: head pointer in DWs for page fault queue,
			 * moved by worker which processes faults.
			 */
			u16 head;
			/**
			 * @tail: tail pointer in DWs for page fault queue,
			 * moved by G2H handler.
			 */
			u16 tail;
			/** @lock: protects page fault queue */
			spinlock_t lock;
			/** @worker: to process page faults */
			struct work_struct worker;
#define NUM_PF_QUEUE	4
		} pf_queue[NUM_PF_QUEUE];
		/**
		 * @acc_queue: Same as page fault queue, cannot process access
		 * counters under CT lock.
		 */
		struct acc_queue {
			/** @gt: back pointer to GT */
			struct xe_gt *gt;
#define ACC_QUEUE_NUM_DW	128
			/** @data: data in the page fault queue */
			u32 data[ACC_QUEUE_NUM_DW];
			/**
			 * @head: head pointer in DWs for page fault queue,
			 * moved by worker which processes faults.
			 */
			u16 head;
			/**
			 * @tail: tail pointer in DWs for page fault queue,
			 * moved by G2H handler.
			 */
			u16 tail;
			/** @lock: protects page fault queue */
			spinlock_t lock;
			/** @worker: to process access counters */
			struct work_struct worker;
#define NUM_ACC_QUEUE	4
		} acc_queue[NUM_ACC_QUEUE];
	} usm;

	/** @ordered_wq: used to serialize GT resets and TDRs */
	struct workqueue_struct *ordered_wq;

	/** @uc: micro controllers on the GT */
	struct xe_uc uc;

	/** @gtidle: idle properties of GT */
	struct xe_gt_idle gtidle;

	/** @exec_queue_ops: submission backend exec queue operations */
	const struct xe_exec_queue_ops *exec_queue_ops;

	/**
	 * @ring_ops: ring operations for this hw engine (1 per engine class)
	 */
	const struct xe_ring_ops *ring_ops[XE_ENGINE_CLASS_MAX];

	/** @fence_irq: fence IRQs (1 per engine class) */
	struct xe_hw_fence_irq fence_irq[XE_ENGINE_CLASS_MAX];

	/** @default_lrc: default LRC state */
	void *default_lrc[XE_ENGINE_CLASS_MAX];

	/** @hw_engines: hardware engines on the GT */
	struct xe_hw_engine hw_engines[XE_NUM_HW_ENGINES];

	/** @eclass: per hardware engine class interface on the GT */
	struct xe_hw_engine_class_intf  eclass[XE_ENGINE_CLASS_MAX];

	/** @pcode: GT's PCODE */
	struct {
		/** @lock: protecting GT's PCODE mailbox data */
		struct mutex lock;
	} pcode;

	/** @sysfs: sysfs' kobj used by xe_gt_sysfs */
	struct kobject *sysfs;

	/** @freq: Main GT freq sysfs control */
	struct kobject *freq;

	/** @mocs: info */
	struct {
		/** @uc_index: UC index */
		u8 uc_index;
		/** @wb_index: WB index, only used on L3_CCS platforms */
		u8 wb_index;
	} mocs;

	/** @fuse_topo: GT topology reported by fuse registers */
	struct {
		/** @g_dss_mask: dual-subslices usable by geometry */
		xe_dss_mask_t g_dss_mask;

		/** @c_dss_mask: dual-subslices usable by compute */
		xe_dss_mask_t c_dss_mask;

		/** @eu_mask_per_dss: EU mask per DSS*/
		xe_eu_mask_t eu_mask_per_dss;
	} fuse_topo;

	/** @steering: register steering for individual HW units */
	struct {
		/* @ranges: register ranges used for this steering type */
		const struct xe_mmio_range *ranges;

		/** @group_target: target to steer accesses to */
		u16 group_target;
		/** @instance_target: instance to steer accesses to */
		u16 instance_target;
	} steering[NUM_STEERING_TYPES];

	/**
	 * @mcr_lock: protects the MCR_SELECTOR register for the duration
	 *    of a steered operation
	 */
	spinlock_t mcr_lock;

	/** @wa_active: keep track of active workarounds */
	struct {
		/** @gt: bitmap with active GT workarounds */
		unsigned long *gt;
		/** @engine: bitmap with active engine workarounds */
		unsigned long *engine;
		/** @lrc: bitmap with active LRC workarounds */
		unsigned long *lrc;
		/** @oob: bitmap with active OOB workaroudns */
		unsigned long *oob;
	} wa_active;
};

#endif
