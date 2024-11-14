/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022-2023 Intel Corporation
 */

#ifndef _XE_GT_TYPES_H_
#define _XE_GT_TYPES_H_

#include "xe_device_types.h"
#include "xe_force_wake_types.h"
#include "xe_gt_idle_types.h"
#include "xe_gt_sriov_pf_types.h"
#include "xe_gt_sriov_vf_types.h"
#include "xe_gt_stats.h"
#include "xe_hw_engine_types.h"
#include "xe_hw_fence_types.h"
#include "xe_oa_types.h"
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

enum xe_gt_eu_type {
	XE_GT_EU_TYPE_SIMD8,
	XE_GT_EU_TYPE_SIMD16,
};

#define XE_MAX_DSS_FUSE_REGS		3
#define XE_MAX_DSS_FUSE_BITS		(32 * XE_MAX_DSS_FUSE_REGS)
#define XE_MAX_EU_FUSE_REGS		1
#define XE_MAX_EU_FUSE_BITS		(32 * XE_MAX_EU_FUSE_REGS)
#define XE_MAX_L3_BANK_MASK_BITS	64

typedef unsigned long xe_dss_mask_t[BITS_TO_LONGS(XE_MAX_DSS_FUSE_BITS)];
typedef unsigned long xe_eu_mask_t[BITS_TO_LONGS(XE_MAX_EU_FUSE_BITS)];
typedef unsigned long xe_l3_bank_mask_t[BITS_TO_LONGS(XE_MAX_L3_BANK_MASK_BITS)];

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
		/** @info.type: type of GT */
		enum xe_gt_type type;
		/** @info.reference_clock: clock frequency */
		u32 reference_clock;
		/**
		 * @info.engine_mask: mask of engines present on GT. Some of
		 * them may be reserved in runtime and not available for user.
		 * See @user_engines.mask
		 */
		u64 engine_mask;
		/** @info.gmdid: raw GMD_ID value from hardware */
		u32 gmdid;
		/** @info.id: Unique ID of this GT within the PCI Device */
		u8 id;
		/** @info.has_indirect_ring_state: GT has indirect ring state support */
		u8 has_indirect_ring_state:1;
	} info;

#if IS_ENABLED(CONFIG_DEBUG_FS)
	/** @stats: GT stats */
	struct {
		/** @stats.counters: counters for various GT stats */
		atomic_t counters[__XE_GT_STATS_NUM_IDS];
	} stats;
#endif

	/**
	 * @mmio: mmio info for GT.  All GTs within a tile share the same
	 * register space, but have their own copy of GSI registers at a
	 * specific offset.
	 */
	struct xe_mmio mmio;

	/**
	 * @pm: power management info for GT.  The driver uses the GT's
	 * "force wake" interface to wake up specific parts of the GT hardware
	 * from C6 sleep states and ensure the hardware remains awake while it
	 * is being actively used.
	 */
	struct {
		/** @pm.fw: force wake for GT */
		struct xe_force_wake fw;
	} pm;

	/** @sriov: virtualization data related to GT */
	union {
		/** @sriov.pf: PF data. Valid only if driver is running as PF */
		struct xe_gt_sriov_pf pf;
		/** @sriov.vf: VF data. Valid only if driver is running as VF */
		struct xe_gt_sriov_vf vf;
	} sriov;

	/**
	 * @reg_sr: table with registers to be restored on GT init/resume/reset
	 */
	struct xe_reg_sr reg_sr;

	/** @reset: state for GT resets */
	struct {
		/**
		 * @reset.worker: work so GT resets can done async allowing to reset
		 * code to safely flush all code paths
		 */
		struct work_struct worker;
	} reset;

	/** @tlb_invalidation: TLB invalidation state */
	struct {
		/** @tlb_invalidation.seqno: TLB invalidation seqno, protected by CT lock */
#define TLB_INVALIDATION_SEQNO_MAX	0x100000
		int seqno;
		/**
		 * @tlb_invalidation.seqno_recv: last received TLB invalidation seqno,
		 * protected by CT lock
		 */
		int seqno_recv;
		/**
		 * @tlb_invalidation.pending_fences: list of pending fences waiting TLB
		 * invaliations, protected by CT lock
		 */
		struct list_head pending_fences;
		/**
		 * @tlb_invalidation.pending_lock: protects @tlb_invalidation.pending_fences
		 * and updating @tlb_invalidation.seqno_recv.
		 */
		spinlock_t pending_lock;
		/**
		 * @tlb_invalidation.fence_tdr: schedules a delayed call to
		 * xe_gt_tlb_fence_timeout after the timeut interval is over.
		 */
		struct delayed_work fence_tdr;
		/** @tlb_invalidation.lock: protects TLB invalidation fences */
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
		 * @usm.bb_pool: Pool from which batchbuffers, for USM operations
		 * (e.g. migrations, fixing page tables), are allocated.
		 * Dedicated pool needed so USM operations to not get blocked
		 * behind any user operations which may have resulted in a
		 * fault.
		 */
		struct xe_sa_manager *bb_pool;
		/**
		 * @usm.reserved_bcs_instance: reserved BCS instance used for USM
		 * operations (e.g. mmigrations, fixing page tables)
		 */
		u16 reserved_bcs_instance;
		/** @usm.pf_wq: page fault work queue, unbound, high priority */
		struct workqueue_struct *pf_wq;
		/** @usm.acc_wq: access counter work queue, unbound, high priority */
		struct workqueue_struct *acc_wq;
		/**
		 * @usm.pf_queue: Page fault queue used to sync faults so faults can
		 * be processed not under the GuC CT lock. The queue is sized so
		 * it can sync all possible faults (1 per physical engine).
		 * Multiple queues exists for page faults from different VMs are
		 * be processed in parallel.
		 */
		struct pf_queue {
			/** @usm.pf_queue.gt: back pointer to GT */
			struct xe_gt *gt;
			/** @usm.pf_queue.data: data in the page fault queue */
			u32 *data;
			/**
			 * @usm.pf_queue.num_dw: number of DWORDS in the page
			 * fault queue. Dynamically calculated based on the number
			 * of compute resources available.
			 */
			u32 num_dw;
			/**
			 * @usm.pf_queue.tail: tail pointer in DWs for page fault queue,
			 * moved by worker which processes faults (consumer).
			 */
			u16 tail;
			/**
			 * @usm.pf_queue.head: head pointer in DWs for page fault queue,
			 * moved by G2H handler (producer).
			 */
			u16 head;
			/** @usm.pf_queue.lock: protects page fault queue */
			spinlock_t lock;
			/** @usm.pf_queue.worker: to process page faults */
			struct work_struct worker;
#define NUM_PF_QUEUE	4
		} pf_queue[NUM_PF_QUEUE];
		/**
		 * @usm.acc_queue: Same as page fault queue, cannot process access
		 * counters under CT lock.
		 */
		struct acc_queue {
			/** @usm.acc_queue.gt: back pointer to GT */
			struct xe_gt *gt;
#define ACC_QUEUE_NUM_DW	128
			/** @usm.acc_queue.data: data in the page fault queue */
			u32 data[ACC_QUEUE_NUM_DW];
			/**
			 * @usm.acc_queue.tail: tail pointer in DWs for access counter queue,
			 * moved by worker which processes counters
			 * (consumer).
			 */
			u16 tail;
			/**
			 * @usm.acc_queue.head: head pointer in DWs for access counter queue,
			 * moved by G2H handler (producer).
			 */
			u16 head;
			/** @usm.acc_queue.lock: protects page fault queue */
			spinlock_t lock;
			/** @usm.acc_queue.worker: to process access counters */
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

	/** @sysfs: sysfs' kobj used by xe_gt_sysfs */
	struct kobject *sysfs;

	/** @freq: Main GT freq sysfs control */
	struct kobject *freq;

	/** @mocs: info */
	struct {
		/** @mocs.uc_index: UC index */
		u8 uc_index;
		/** @mocs.wb_index: WB index, only used on L3_CCS platforms */
		u8 wb_index;
	} mocs;

	/** @fuse_topo: GT topology reported by fuse registers */
	struct {
		/** @fuse_topo.g_dss_mask: dual-subslices usable by geometry */
		xe_dss_mask_t g_dss_mask;

		/** @fuse_topo.c_dss_mask: dual-subslices usable by compute */
		xe_dss_mask_t c_dss_mask;

		/** @fuse_topo.eu_mask_per_dss: EU mask per DSS*/
		xe_eu_mask_t eu_mask_per_dss;

		/** @fuse_topo.l3_bank_mask: L3 bank mask */
		xe_l3_bank_mask_t l3_bank_mask;

		/**
		 * @fuse_topo.eu_type: type/width of EU stored in
		 * fuse_topo.eu_mask_per_dss
		 */
		enum xe_gt_eu_type eu_type;
	} fuse_topo;

	/** @steering: register steering for individual HW units */
	struct {
		/** @steering.ranges: register ranges used for this steering type */
		const struct xe_mmio_range *ranges;

		/** @steering.group_target: target to steer accesses to */
		u16 group_target;
		/** @steering.instance_target: instance to steer accesses to */
		u16 instance_target;
	} steering[NUM_STEERING_TYPES];

	/**
	 * @steering_dss_per_grp: number of DSS per steering group (gslice,
	 *    cslice, etc.).
	 */
	unsigned int steering_dss_per_grp;

	/**
	 * @mcr_lock: protects the MCR_SELECTOR register for the duration
	 *    of a steered operation
	 */
	spinlock_t mcr_lock;

	/**
	 * @global_invl_lock: protects the register for the duration
	 *    of a global invalidation of l2 cache
	 */
	spinlock_t global_invl_lock;

	/** @wa_active: keep track of active workarounds */
	struct {
		/** @wa_active.gt: bitmap with active GT workarounds */
		unsigned long *gt;
		/** @wa_active.engine: bitmap with active engine workarounds */
		unsigned long *engine;
		/** @wa_active.lrc: bitmap with active LRC workarounds */
		unsigned long *lrc;
		/** @wa_active.oob: bitmap with active OOB workarounds */
		unsigned long *oob;
		/**
		 * @wa_active.oob_initialized: mark oob as initialized to help
		 * detecting misuse of XE_WA() - it can only be called on
		 * initialization after OOB WAs have being processed
		 */
		bool oob_initialized;
	} wa_active;

	/** @user_engines: engines present in GT and available to userspace */
	struct {
		/**
		 * @user_engines.mask: like @info->engine_mask, but take in
		 * consideration only engines available to userspace
		 */
		u64 mask;

		/**
		 * @user_engines.instances_per_class: aggregate per class the
		 * number of engines available to userspace
		 */
		u8 instances_per_class[XE_ENGINE_CLASS_MAX];
	} user_engines;

	/** @oa: oa observation subsystem per gt info */
	struct xe_oa_gt oa;
};

#endif
