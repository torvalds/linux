/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_GT_TYPES_H_
#define _XE_GT_TYPES_H_

#include "xe_force_wake_types.h"
#include "xe_hw_engine_types.h"
#include "xe_hw_fence_types.h"
#include "xe_reg_sr_types.h"
#include "xe_sa_types.h"
#include "xe_uc_types.h"

struct xe_engine_ops;
struct xe_ggtt;
struct xe_migrate;
struct xe_ring_ops;
struct xe_ttm_gtt_mgr;
struct xe_ttm_vram_mgr;

enum xe_gt_type {
	XE_GT_TYPE_UNINITIALIZED,
	XE_GT_TYPE_MAIN,
	XE_GT_TYPE_REMOTE,
	XE_GT_TYPE_MEDIA,
};

#define XE_MAX_DSS_FUSE_REGS	2
#define XE_MAX_EU_FUSE_REGS	1

typedef unsigned long xe_dss_mask_t[BITS_TO_LONGS(32 * XE_MAX_DSS_FUSE_REGS)];
typedef unsigned long xe_eu_mask_t[BITS_TO_LONGS(32 * XE_MAX_DSS_FUSE_REGS)];

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

	/*
	 * On some platforms there are multiple types of MCR registers that
	 * will always return a non-terminated value at instance (0, 0).  We'll
	 * lump those all into a single category to keep things simple.
	 */
	INSTANCE0,

	NUM_STEERING_TYPES
};

/**
 * struct xe_gt - Top level struct of a graphics tile
 *
 * A graphics tile may be a physical split (duplicate pieces of silicon,
 * different GGTT + VRAM) or a virtual split (shared GGTT + VRAM). Either way
 * this structure encapsulates of everything a GT is (MMIO, VRAM, memory
 * management, microcontrols, and a hardware set of engines).
 */
struct xe_gt {
	/** @xe: backpointer to XE device */
	struct xe_device *xe;

	/** @info: GT info */
	struct {
		/** @type: type of GT */
		enum xe_gt_type type;
		/** @id: id of GT */
		u8 id;
		/** @vram: id of the VRAM for this GT */
		u8 vram_id;
		/** @clock_freq: clock frequency */
		u32 clock_freq;
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
	 * @mmio: mmio info for GT, can be subset of the global device mmio
	 * space
	 */
	struct {
		/** @size: size of MMIO space on GT */
		size_t size;
		/** @regs: pointer to MMIO space on GT */
		void *regs;
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

	/**
	 * @mem: memory management info for GT, multiple GTs can point to same
	 * objects (virtual split)
	 */
	struct {
		/**
		 * @vram: VRAM info for GT, multiple GTs can point to same info
		 * (virtual split), can be subset of global device VRAM
		 */
		struct {
			/** @io_start: start address of VRAM */
			resource_size_t io_start;
			/** @size: size of VRAM */
			resource_size_t size;
			/** @mapping: pointer to VRAM mappable space */
			void *__iomem mapping;
		} vram;
		/** @vram_mgr: VRAM TTM manager */
		struct xe_ttm_vram_mgr *vram_mgr;
		/** @gtt_mr: GTT TTM manager */
		struct xe_ttm_gtt_mgr *gtt_mgr;
		/** @ggtt: Global graphics translation table */
		struct xe_ggtt *ggtt;
	} mem;

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
	} tlb_invalidation;

	/** @usm: unified shared memory state */
	struct {
		/**
		 * @bb_pool: Pool from which batchbuffers, for USM operations
		 * (e.g. migrations, fixing page tables), are allocated.
		 * Dedicated pool needed so USM operations to not get blocked
		 * behind any user operations which may have resulted in a
		 * fault.
		 */
		struct xe_sa_manager bb_pool;
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

	/** @engine_ops: submission backend engine operations */
	const struct xe_engine_ops *engine_ops;

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

	/** @kernel_bb_pool: Pool from which batchbuffers are allocated */
	struct xe_sa_manager kernel_bb_pool;

	/** @migrate: Migration helper for vram blits and clearing */
	struct xe_migrate *migrate;

	/** @pcode: GT's PCODE */
	struct {
		/** @lock: protecting GT's PCODE mailbox data */
		struct mutex lock;
	} pcode;

	/** @sysfs: sysfs' kobj used by xe_gt_sysfs */
	struct kobject *sysfs;

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
};

#endif
