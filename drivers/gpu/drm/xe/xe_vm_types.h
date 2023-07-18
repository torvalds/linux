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

struct async_op_fence;
struct xe_bo;
struct xe_sync_entry;
struct xe_vm;

#define TEST_VM_ASYNC_OPS_ERROR
#define FORCE_ASYNC_OP_ERROR	BIT(31)

#define XE_VMA_READ_ONLY	DRM_GPUVA_USERBITS
#define XE_VMA_DESTROYED	(DRM_GPUVA_USERBITS << 1)
#define XE_VMA_ATOMIC_PTE_BIT	(DRM_GPUVA_USERBITS << 2)
#define XE_VMA_FIRST_REBIND	(DRM_GPUVA_USERBITS << 3)
#define XE_VMA_LAST_REBIND	(DRM_GPUVA_USERBITS << 4)

struct xe_vma {
	/** @gpuva: Base GPUVA object */
	struct drm_gpuva gpuva;

	/** @tile_mask: Tile mask of where to create binding for this VMA */
	u64 tile_mask;

	/**
	 * @tile_present: GT mask of binding are present for this VMA.
	 * protected by vm->lock, vm->resv and for userptrs,
	 * vm->userptr.notifier_lock for writing. Needs either for reading,
	 * but if reading is done under the vm->lock only, it needs to be held
	 * in write mode.
	 */
	u64 tile_present;

	/** @userptr_link: link into VM repin list if userptr */
	struct list_head userptr_link;

	/**
	 * @rebind_link: link into VM if this VMA needs rebinding, and
	 * if it's a bo (not userptr) needs validation after a possible
	 * eviction. Protected by the vm's resv lock.
	 */
	struct list_head rebind_link;

	/**
	 * @unbind_link: link or list head if an unbind of multiple VMAs, in
	 * single unbind op, is being done.
	 */
	struct list_head unbind_link;

	/** @destroy_cb: callback to destroy VMA when unbind job is done */
	struct dma_fence_cb destroy_cb;

	/** @destroy_work: worker to destroy this BO */
	struct work_struct destroy_work;

	/** @userptr: user pointer state */
	struct {
		/** @invalidate_link: Link for the vm::userptr.invalidated list */
		struct list_head invalidate_link;
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
	} userptr;

	/** @usm: unified shared memory state */
	struct {
		/** @tile_invalidated: VMA has been invalidated */
		u64 tile_invalidated;
	} usm;

	struct {
		struct list_head rebind_link;
	} notifier;

	struct {
		/**
		 * @extobj.link: Link into vm's external object list.
		 * protected by the vm lock.
		 */
		struct list_head link;
	} extobj;
};

struct xe_device;

struct xe_vm {
	/** @gpuvm: base GPUVM used to track VMAs */
	struct drm_gpuvm gpuvm;

	struct xe_device *xe;

	/* engine used for (un)binding vma's */
	struct xe_engine *eng[XE_MAX_TILES_PER_DEVICE];

	/** @lru_bulk_move: Bulk LRU move list for this VM's BOs */
	struct ttm_lru_bulk_move lru_bulk_move;

	u64 size;

	struct xe_pt *pt_root[XE_MAX_TILES_PER_DEVICE];
	struct xe_bo *scratch_bo[XE_MAX_TILES_PER_DEVICE];
	struct xe_pt *scratch_pt[XE_MAX_TILES_PER_DEVICE][XE_VM_MAX_LEVEL];

	/**
	 * @flags: flags for this VM, statically setup a creation time aside
	 * from XE_VM_FLAG_BANNED which requires vm->lock to set / read safely
	 */
#define XE_VM_FLAG_64K			BIT(0)
#define XE_VM_FLAG_COMPUTE_MODE		BIT(1)
#define XE_VM_FLAG_ASYNC_BIND_OPS	BIT(2)
#define XE_VM_FLAG_MIGRATION		BIT(3)
#define XE_VM_FLAG_SCRATCH_PAGE		BIT(4)
#define XE_VM_FLAG_FAULT_MODE		BIT(5)
#define XE_VM_FLAG_BANNED		BIT(6)
#define XE_VM_FLAG_TILE_ID(flags)	(((flags) >> 7) & 0x3)
#define XE_VM_FLAG_SET_TILE_ID(tile)	((tile)->id << 7)
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
	 * @rebind_list: list of VMAs that need rebinding, and if they are
	 * bos (not userptr), need validation after a possible eviction. The
	 * list is protected by @resv.
	 */
	struct list_head rebind_list;

	/** @rebind_fence: rebind fence from execbuf */
	struct dma_fence *rebind_fence;

	/**
	 * @destroy_work: worker to destroy VM, needed as a dma_fence signaling
	 * from an irq context can be last put and the destroy needs to be able
	 * to sleep.
	 */
	struct work_struct destroy_work;

	/** @extobj: bookkeeping for external objects. Protected by the vm lock */
	struct {
		/** @enties: number of external BOs attached this VM */
		u32 entries;
		/** @list: list of vmas with external bos attached */
		struct list_head list;
	} extobj;

	/** @async_ops: async VM operations (bind / unbinds) */
	struct {
		/** @list: list of pending async VM ops */
		struct list_head pending;
		/** @work: worker to execute async VM ops */
		struct work_struct work;
		/** @lock: protects list of pending async VM ops and fences */
		spinlock_t lock;
		/** @error_capture: error capture state */
		struct {
			/** @mm: user MM */
			struct mm_struct *mm;
			/**
			 * @addr: user pointer to copy error capture state too
			 */
			u64 addr;
			/** @wq: user fence wait queue for VM errors */
			wait_queue_head_t wq;
		} error_capture;
		/** @fence: fence state */
		struct {
			/** @context: context of async fence */
			u64 context;
			/** @seqno: seqno of async fence */
			u32 seqno;
		} fence;
		/** @error: error state for async VM ops */
		int error;
		/**
		 * @munmap_rebind_inflight: an munmap style VM bind is in the
		 * middle of a set of ops which requires a rebind at the end.
		 */
		bool munmap_rebind_inflight;
	} async_ops;

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
		/** @engines: list of engines attached to this VM */
		struct list_head engines;
		/** @num_engines: number user engines attached to this VM */
		int num_engines;
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

	/**
	 * @notifier: Lists and locks for temporary usage within notifiers where
	 * we either can't grab the vm lock or the vm resv.
	 */
	struct {
		/** @notifier.list_lock: lock protecting @rebind_list */
		spinlock_t list_lock;
		/**
		 * @notifier.rebind_list: list of vmas that we want to put on the
		 * main @rebind_list. This list is protected for writing by both
		 * notifier.list_lock, and the resv of the bo the vma points to,
		 * and for reading by the notifier.list_lock only.
		 */
		struct list_head rebind_list;
	} notifier;

	/** @error_capture: allow to track errors */
	struct {
		/** @capture_once: capture only one error per VM */
		bool capture_once;
	} error_capture;

	/** @batch_invalidate_tlb: Always invalidate TLB before batch start */
	bool batch_invalidate_tlb;
};

/** struct xe_vma_op_map - VMA map operation */
struct xe_vma_op_map {
	/** @vma: VMA to map */
	struct xe_vma *vma;
	/** @immediate: Immediate bind */
	bool immediate;
	/** @read_only: Read only */
	bool read_only;
	/** @is_null: is NULL binding */
	bool is_null;
};

/** struct xe_vma_op_unmap - VMA unmap operation */
struct xe_vma_op_unmap {
	/** @start: start of the VMA unmap */
	u64 start;
	/** @range: range of the VMA unmap */
	u64 range;
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
	XE_VMA_OP_FIRST		= (0x1 << 0),
	/** @XE_VMA_OP_LAST: last VMA operation for a set of syncs */
	XE_VMA_OP_LAST		= (0x1 << 1),
	/** @XE_VMA_OP_COMMITTED: VMA operation committed */
	XE_VMA_OP_COMMITTED	= (0x1 << 2),
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
	/** @engine: engine for this operation */
	struct xe_engine *engine;
	/**
	 * @syncs: syncs for this operation, only used on first and last
	 * operation
	 */
	struct xe_sync_entry *syncs;
	/** @num_syncs: number of syncs */
	u32 num_syncs;
	/** @link: async operation link */
	struct list_head link;
	/**
	 * @fence: async operation fence, signaled on last operation complete
	 */
	struct async_op_fence *fence;
	/** @tile_mask: gt mask for this operation */
	u64 tile_mask;
	/** @flags: operation flags */
	enum xe_vma_op_flags flags;

#ifdef TEST_VM_ASYNC_OPS_ERROR
	/** @inject_error: inject error to test async op error handling */
	bool inject_error;
#endif

	union {
		/** @map: VMA map operation specific data */
		struct xe_vma_op_map map;
		/** @unmap: VMA unmap operation specific data */
		struct xe_vma_op_unmap unmap;
		/** @remap: VMA remap operation specific data */
		struct xe_vma_op_remap remap;
		/** @prefetch: VMA prefetch operation specific data */
		struct xe_vma_op_prefetch prefetch;
	};
};
#endif
