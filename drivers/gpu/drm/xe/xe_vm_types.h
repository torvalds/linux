/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_VM_TYPES_H_
#define _XE_VM_TYPES_H_

#include <drm/drm_gpuvm.h>

#include <linux/dma-resv.h>
#include <linux/kref.h>
#include <linux/mmu_notifier.h>
#include <linux/scatterlist.h>

#include "xe_device_types.h"
#include "xe_pt_types.h"
#include "xe_range_fence.h"

struct xe_bo;
struct xe_sync_entry;
struct xe_user_fence;
struct xe_vm;

#define XE_VMA_READ_ONLY	DRM_GPUVA_USERBITS
#define XE_VMA_DESTROYED	(DRM_GPUVA_USERBITS << 1)
#define XE_VMA_ATOMIC_PTE_BIT	(DRM_GPUVA_USERBITS << 2)
#define XE_VMA_FIRST_REBIND	(DRM_GPUVA_USERBITS << 3)
#define XE_VMA_LAST_REBIND	(DRM_GPUVA_USERBITS << 4)
#define XE_VMA_PTE_4K		(DRM_GPUVA_USERBITS << 5)
#define XE_VMA_PTE_2M		(DRM_GPUVA_USERBITS << 6)
#define XE_VMA_PTE_1G		(DRM_GPUVA_USERBITS << 7)
#define XE_VMA_PTE_64K		(DRM_GPUVA_USERBITS << 8)
#define XE_VMA_PTE_COMPACT	(DRM_GPUVA_USERBITS << 9)
#define XE_VMA_DUMPABLE		(DRM_GPUVA_USERBITS << 10)

/** struct xe_userptr - User pointer */
struct xe_userptr {
	/** @invalidate_link: Link for the vm::userptr.invalidated list */
	struct list_head invalidate_link;
	/** @userptr: link into VM repin list if userptr. */
	struct list_head repin_link;
	/**
	 * @notifier: MMU notifier for user pointer (invalidation call back)
	 */
	struct mmu_interval_notifier notifier;
	/** @sgt: storage for a scatter gather table */
	struct sg_table sgt;
	/** @sg: allocated scatter gather table */
	struct sg_table *sg;
	/** @notifier_seq: notifier sequence number */
	unsigned long notifier_seq;
	/**
	 * @initial_bind: user pointer has been bound at least once.
	 * write: vm->userptr.notifier_lock in read mode and vm->resv held.
	 * read: vm->userptr.notifier_lock in write mode or vm->resv held.
	 */
	bool initial_bind;
#if IS_ENABLED(CONFIG_DRM_XE_USERPTR_INVAL_INJECT)
	u32 divisor;
#endif
};

struct xe_vma {
	/** @gpuva: Base GPUVA object */
	struct drm_gpuva gpuva;

	/**
	 * @combined_links: links into lists which are mutually exclusive.
	 * Locking: vm lock in write mode OR vm lock in read mode and the vm's
	 * resv.
	 */
	union {
		/** @rebind: link into VM if this VMA needs rebinding. */
		struct list_head rebind;
		/** @destroy: link to contested list when VM is being closed. */
		struct list_head destroy;
	} combined_links;

	union {
		/** @destroy_cb: callback to destroy VMA when unbind job is done */
		struct dma_fence_cb destroy_cb;
		/** @destroy_work: worker to destroy this BO */
		struct work_struct destroy_work;
	};

	/** @tile_invalidated: VMA has been invalidated */
	u8 tile_invalidated;

	/** @tile_mask: Tile mask of where to create binding for this VMA */
	u8 tile_mask;

	/**
	 * @tile_present: GT mask of binding are present for this VMA.
	 * protected by vm->lock, vm->resv and for userptrs,
	 * vm->userptr.notifier_lock for writing. Needs either for reading,
	 * but if reading is done under the vm->lock only, it needs to be held
	 * in write mode.
	 */
	u8 tile_present;

	/**
	 * @pat_index: The pat index to use when encoding the PTEs for this vma.
	 */
	u16 pat_index;

	/**
	 * @ufence: The user fence that was provided with MAP.
	 * Needs to be signalled before UNMAP can be processed.
	 */
	struct xe_user_fence *ufence;
};

/**
 * struct xe_userptr_vma - A userptr vma subclass
 * @vma: The vma.
 * @userptr: Additional userptr information.
 */
struct xe_userptr_vma {
	struct xe_vma vma;
	struct xe_userptr userptr;
};

struct xe_device;

struct xe_vm {
	/** @gpuvm: base GPUVM used to track VMAs */
	struct drm_gpuvm gpuvm;

	struct xe_device *xe;

	/* exec queue used for (un)binding vma's */
	struct xe_exec_queue *q[XE_MAX_TILES_PER_DEVICE];

	/** @lru_bulk_move: Bulk LRU move list for this VM's BOs */
	struct ttm_lru_bulk_move lru_bulk_move;

	u64 size;

	struct xe_pt *pt_root[XE_MAX_TILES_PER_DEVICE];
	struct xe_pt *scratch_pt[XE_MAX_TILES_PER_DEVICE][XE_VM_MAX_LEVEL];

	/**
	 * @flags: flags for this VM, statically setup a creation time aside
	 * from XE_VM_FLAG_BANNED which requires vm->lock to set / read safely
	 */
#define XE_VM_FLAG_64K			BIT(0)
#define XE_VM_FLAG_LR_MODE		BIT(1)
#define XE_VM_FLAG_MIGRATION		BIT(2)
#define XE_VM_FLAG_SCRATCH_PAGE		BIT(3)
#define XE_VM_FLAG_FAULT_MODE		BIT(4)
#define XE_VM_FLAG_BANNED		BIT(5)
#define XE_VM_FLAG_TILE_ID(flags)	FIELD_GET(GENMASK(7, 6), flags)
#define XE_VM_FLAG_SET_TILE_ID(tile)	FIELD_PREP(GENMASK(7, 6), (tile)->id)
	unsigned long flags;

	/** @composite_fence_ctx: context composite fence */
	u64 composite_fence_ctx;
	/** @composite_fence_seqno: seqno for composite fence */
	u32 composite_fence_seqno;

	/**
	 * @lock: outer most lock, protects objects of anything attached to this
	 * VM
	 */
	struct rw_semaphore lock;
	/**
	 * @snap_mutex: Mutex used to guard insertions and removals from gpuva,
	 * so we can take a snapshot safely from devcoredump.
	 */
	struct mutex snap_mutex;

	/**
	 * @rebind_list: list of VMAs that need rebinding. Protected by the
	 * vm->lock in write mode, OR (the vm->lock in read mode and the
	 * vm resv).
	 */
	struct list_head rebind_list;

	/**
	 * @destroy_work: worker to destroy VM, needed as a dma_fence signaling
	 * from an irq context can be last put and the destroy needs to be able
	 * to sleep.
	 */
	struct work_struct destroy_work;

	/**
	 * @rftree: range fence tree to track updates to page table structure.
	 * Used to implement conflict tracking between independent bind engines.
	 */
	struct xe_range_fence_tree rftree[XE_MAX_TILES_PER_DEVICE];

	const struct xe_pt_ops *pt_ops;

	/** @userptr: user pointer state */
	struct {
		/**
		 * @userptr.repin_list: list of VMAs which are user pointers,
		 * and needs repinning. Protected by @lock.
		 */
		struct list_head repin_list;
		/**
		 * @notifier_lock: protects notifier in write mode and
		 * submission in read mode.
		 */
		struct rw_semaphore notifier_lock;
		/**
		 * @userptr.invalidated_lock: Protects the
		 * @userptr.invalidated list.
		 */
		spinlock_t invalidated_lock;
		/**
		 * @userptr.invalidated: List of invalidated userptrs, not yet
		 * picked
		 * up for revalidation. Protected from access with the
		 * @invalidated_lock. Removing items from the list
		 * additionally requires @lock in write mode, and adding
		 * items to the list requires the @userptr.notifer_lock in
		 * write mode.
		 */
		struct list_head invalidated;
	} userptr;

	/** @preempt: preempt state */
	struct {
		/**
		 * @min_run_period_ms: The minimum run period before preempting
		 * an engine again
		 */
		s64 min_run_period_ms;
		/** @exec_queues: list of exec queues attached to this VM */
		struct list_head exec_queues;
		/** @num_exec_queues: number exec queues attached to this VM */
		int num_exec_queues;
		/**
		 * @rebind_deactivated: Whether rebind has been temporarily deactivated
		 * due to no work available. Protected by the vm resv.
		 */
		bool rebind_deactivated;
		/**
		 * @rebind_work: worker to rebind invalidated userptrs / evicted
		 * BOs
		 */
		struct work_struct rebind_work;
	} preempt;

	/** @um: unified memory state */
	struct {
		/** @asid: address space ID, unique to each VM */
		u32 asid;
		/**
		 * @last_fault_vma: Last fault VMA, used for fast lookup when we
		 * get a flood of faults to the same VMA
		 */
		struct xe_vma *last_fault_vma;
	} usm;

	/** @error_capture: allow to track errors */
	struct {
		/** @capture_once: capture only one error per VM */
		bool capture_once;
	} error_capture;

	/**
	 * @tlb_flush_seqno: Required TLB flush seqno for the next exec.
	 * protected by the vm resv.
	 */
	u64 tlb_flush_seqno;
	/** @batch_invalidate_tlb: Always invalidate TLB before batch start */
	bool batch_invalidate_tlb;
	/** @xef: XE file handle for tracking this VM's drm client */
	struct xe_file *xef;
};

/** struct xe_vma_op_map - VMA map operation */
struct xe_vma_op_map {
	/** @vma: VMA to map */
	struct xe_vma *vma;
	/** @is_null: is NULL binding */
	bool is_null;
	/** @dumpable: whether BO is dumped on GPU hang */
	bool dumpable;
	/** @pat_index: The pat index to use for this operation. */
	u16 pat_index;
};

/** struct xe_vma_op_remap - VMA remap operation */
struct xe_vma_op_remap {
	/** @prev: VMA preceding part of a split mapping */
	struct xe_vma *prev;
	/** @next: VMA subsequent part of a split mapping */
	struct xe_vma *next;
	/** @start: start of the VMA unmap */
	u64 start;
	/** @range: range of the VMA unmap */
	u64 range;
	/** @skip_prev: skip prev rebind */
	bool skip_prev;
	/** @skip_next: skip next rebind */
	bool skip_next;
	/** @unmap_done: unmap operation in done */
	bool unmap_done;
};

/** struct xe_vma_op_prefetch - VMA prefetch operation */
struct xe_vma_op_prefetch {
	/** @region: memory region to prefetch to */
	u32 region;
};

/** enum xe_vma_op_flags - flags for VMA operation */
enum xe_vma_op_flags {
	/** @XE_VMA_OP_FIRST: first VMA operation for a set of syncs */
	XE_VMA_OP_FIRST			= BIT(0),
	/** @XE_VMA_OP_LAST: last VMA operation for a set of syncs */
	XE_VMA_OP_LAST			= BIT(1),
	/** @XE_VMA_OP_COMMITTED: VMA operation committed */
	XE_VMA_OP_COMMITTED		= BIT(2),
	/** @XE_VMA_OP_PREV_COMMITTED: Previous VMA operation committed */
	XE_VMA_OP_PREV_COMMITTED	= BIT(3),
	/** @XE_VMA_OP_NEXT_COMMITTED: Next VMA operation committed */
	XE_VMA_OP_NEXT_COMMITTED	= BIT(4),
};

/** struct xe_vma_op - VMA operation */
struct xe_vma_op {
	/** @base: GPUVA base operation */
	struct drm_gpuva_op base;
	/**
	 * @ops: GPUVA ops, when set call drm_gpuva_ops_free after this
	 * operations is processed
	 */
	struct drm_gpuva_ops *ops;
	/** @q: exec queue for this operation */
	struct xe_exec_queue *q;
	/**
	 * @syncs: syncs for this operation, only used on first and last
	 * operation
	 */
	struct xe_sync_entry *syncs;
	/** @num_syncs: number of syncs */
	u32 num_syncs;
	/** @link: async operation link */
	struct list_head link;
	/** @flags: operation flags */
	enum xe_vma_op_flags flags;

	union {
		/** @map: VMA map operation specific data */
		struct xe_vma_op_map map;
		/** @remap: VMA remap operation specific data */
		struct xe_vma_op_remap remap;
		/** @prefetch: VMA prefetch operation specific data */
		struct xe_vma_op_prefetch prefetch;
	};
};
#endif
