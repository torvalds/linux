/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_VM_TYPES_H_
#define _XE_VM_TYPES_H_

#include <drm/drm_gpusvm.h>
#include <drm/drm_gpuvm.h>

#include <linux/dma-resv.h>
#include <linux/kref.h>
#include <linux/mmu_notifier.h>
#include <linux/scatterlist.h>

#include "xe_device_types.h"
#include "xe_pt_types.h"
#include "xe_range_fence.h"
#include "xe_userptr.h"

struct xe_bo;
struct xe_svm_range;
struct xe_sync_entry;
struct xe_user_fence;
struct xe_vm;
struct xe_vm_pgtable_update_op;

#if IS_ENABLED(CONFIG_DRM_XE_DEBUG)
#define TEST_VM_OPS_ERROR
#define FORCE_OP_ERROR	BIT(31)

#define FORCE_OP_ERROR_LOCK	0
#define FORCE_OP_ERROR_PREPARE	1
#define FORCE_OP_ERROR_RUN	2
#define FORCE_OP_ERROR_COUNT	3
#endif

#define XE_VMA_READ_ONLY	DRM_GPUVA_USERBITS
#define XE_VMA_DESTROYED	(DRM_GPUVA_USERBITS << 1)
#define XE_VMA_ATOMIC_PTE_BIT	(DRM_GPUVA_USERBITS << 2)
#define XE_VMA_PTE_4K		(DRM_GPUVA_USERBITS << 3)
#define XE_VMA_PTE_2M		(DRM_GPUVA_USERBITS << 4)
#define XE_VMA_PTE_1G		(DRM_GPUVA_USERBITS << 5)
#define XE_VMA_PTE_64K		(DRM_GPUVA_USERBITS << 6)
#define XE_VMA_PTE_COMPACT	(DRM_GPUVA_USERBITS << 7)
#define XE_VMA_DUMPABLE		(DRM_GPUVA_USERBITS << 8)
#define XE_VMA_SYSTEM_ALLOCATOR	(DRM_GPUVA_USERBITS << 9)
#define XE_VMA_MADV_AUTORESET	(DRM_GPUVA_USERBITS << 10)

/**
 * struct xe_vma_mem_attr - memory attributes associated with vma
 */
struct xe_vma_mem_attr {
	/** @preferred_loc: perferred memory_location */
	struct {
		/** @preferred_loc.migration_policy: Pages migration policy */
		u32 migration_policy;

		/**
		 * @preferred_loc.devmem_fd: used for determining pagemap_fd
		 * requested by user DRM_XE_PREFERRED_LOC_DEFAULT_SYSTEM and
		 * DRM_XE_PREFERRED_LOC_DEFAULT_DEVICE mean system memory or
		 * closest device memory respectively.
		 */
		u32 devmem_fd;
	} preferred_loc;

	/**
	 * @atomic_access: The atomic access type for the vma
	 * See %DRM_XE_VMA_ATOMIC_UNDEFINED, %DRM_XE_VMA_ATOMIC_DEVICE,
	 * %DRM_XE_VMA_ATOMIC_GLOBAL, and %DRM_XE_VMA_ATOMIC_CPU for possible
	 * values. These are defined in uapi/drm/xe_drm.h.
	 */
	u32 atomic_access;

	/**
	 * @default_pat_index: The pat index for VMA set during first bind by user.
	 */
	u16 default_pat_index;

	/**
	 * @pat_index: The pat index to use when encoding the PTEs for this vma.
	 * same as default_pat_index unless overwritten by madvise.
	 */
	u16 pat_index;
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

	/**
	 * @tile_invalidated: Tile mask of binding are invalidated for this VMA.
	 * protected by BO's resv and for userptrs, vm->svm.gpusvm.notifier_lock in
	 * write mode for writing or vm->svm.gpusvm.notifier_lock in read mode and
	 * the vm->resv. For stable reading, BO's resv or userptr
	 * vm->svm.gpusvm.notifier_lock in read mode is required. Can be
	 * opportunistically read with READ_ONCE outside of locks.
	 */
	u8 tile_invalidated;

	/** @tile_mask: Tile mask of where to create binding for this VMA */
	u8 tile_mask;

	/**
	 * @tile_present: Tile mask of binding are present for this VMA.
	 * protected by vm->lock, vm->resv and for userptrs,
	 * vm->svm.gpusvm.notifier_lock for writing. Needs either for reading,
	 * but if reading is done under the vm->lock only, it needs to be held
	 * in write mode.
	 */
	u8 tile_present;

	/** @tile_staged: bind is staged for this VMA */
	u8 tile_staged;

	/**
	 * @skip_invalidation: Used in madvise to avoid invalidation
	 * if mem attributes doesn't change
	 */
	bool skip_invalidation;

	/**
	 * @ufence: The user fence that was provided with MAP.
	 * Needs to be signalled before UNMAP can be processed.
	 */
	struct xe_user_fence *ufence;

	/**
	 * @attr: The attributes of vma which determines the migration policy
	 * and encoding of the PTEs for this vma.
	 */
	struct xe_vma_mem_attr attr;
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

	/** @svm: Shared virtual memory state */
	struct {
		/** @svm.gpusvm: base GPUSVM used to track fault allocations */
		struct drm_gpusvm gpusvm;
		/**
		 * @svm.garbage_collector: Garbage collector which is used unmap
		 * SVM range's GPU bindings and destroy the ranges.
		 */
		struct {
			/** @svm.garbage_collector.lock: Protect's range list */
			spinlock_t lock;
			/**
			 * @svm.garbage_collector.range_list: List of SVM ranges
			 * in the garbage collector.
			 */
			struct list_head range_list;
			/**
			 * @svm.garbage_collector.work: Worker which the
			 * garbage collector runs on.
			 */
			struct work_struct work;
		} garbage_collector;
	} svm;

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
#define XE_VM_FLAG_GSC			BIT(8)
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
	struct xe_userptr_vm userptr;

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
		/**
		 * @preempt.pm_activate_link: Link to list of rebind workers to be
		 * kicked on resume.
		 */
		struct list_head pm_activate_link;
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
	 * @validation: Validation data only valid with the vm resv held.
	 * Note: This is really task state of the task holding the vm resv,
	 * and moving forward we should
	 * come up with a better way of passing this down the call-
	 * chain.
	 */
	struct {
		/**
		 * @validation.validating: The task that is currently making bos resident.
		 * for this vm.
		 * Protected by the VM's resv for writing. Opportunistic reading can be done
		 * using READ_ONCE. Note: This is a workaround for the
		 * TTM eviction_valuable() callback not being passed a struct
		 * ttm_operation_context(). Future work might want to address this.
		 */
		struct task_struct *validating;
		/**
		 *  @validation.exec The drm_exec context used when locking the vm resv.
		 *  Protected by the vm's resv.
		 */
		struct drm_exec *_exec;
	} validation;

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
	unsigned int vma_flags;
	/** @immediate: Immediate bind */
	bool immediate;
	/** @read_only: Read only */
	bool invalidate_on_bind;
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

/** struct xe_vma_op_map_range - VMA map range operation */
struct xe_vma_op_map_range {
	/** @vma: VMA to map (system allocator VMA) */
	struct xe_vma *vma;
	/** @range: SVM range to map */
	struct xe_svm_range *range;
};

/** struct xe_vma_op_unmap_range - VMA unmap range operation */
struct xe_vma_op_unmap_range {
	/** @range: SVM range to unmap */
	struct xe_svm_range *range;
};

/** struct xe_vma_op_prefetch_range - VMA prefetch range operation */
struct xe_vma_op_prefetch_range {
	/** @range: xarray for SVM ranges data */
	struct xarray range;
	/** @ranges_count: number of svm ranges to map */
	u32 ranges_count;
	/**
	 * @tile: Pointer to the tile structure containing memory to prefetch.
	 *        NULL if prefetch requested region is smem
	 */
	struct xe_tile *tile;
};

/** enum xe_vma_op_flags - flags for VMA operation */
enum xe_vma_op_flags {
	/** @XE_VMA_OP_COMMITTED: VMA operation committed */
	XE_VMA_OP_COMMITTED		= BIT(0),
	/** @XE_VMA_OP_PREV_COMMITTED: Previous VMA operation committed */
	XE_VMA_OP_PREV_COMMITTED	= BIT(1),
	/** @XE_VMA_OP_NEXT_COMMITTED: Next VMA operation committed */
	XE_VMA_OP_NEXT_COMMITTED	= BIT(2),
};

/** enum xe_vma_subop - VMA sub-operation */
enum xe_vma_subop {
	/** @XE_VMA_SUBOP_MAP_RANGE: Map range */
	XE_VMA_SUBOP_MAP_RANGE,
	/** @XE_VMA_SUBOP_UNMAP_RANGE: Unmap range */
	XE_VMA_SUBOP_UNMAP_RANGE,
};

/** struct xe_vma_op - VMA operation */
struct xe_vma_op {
	/** @base: GPUVA base operation */
	struct drm_gpuva_op base;
	/** @link: async operation link */
	struct list_head link;
	/** @flags: operation flags */
	enum xe_vma_op_flags flags;
	/** @subop: user defined sub-operation */
	enum xe_vma_subop subop;
	/** @tile_mask: Tile mask for operation */
	u8 tile_mask;

	union {
		/** @map: VMA map operation specific data */
		struct xe_vma_op_map map;
		/** @remap: VMA remap operation specific data */
		struct xe_vma_op_remap remap;
		/** @prefetch: VMA prefetch operation specific data */
		struct xe_vma_op_prefetch prefetch;
		/** @map_range: VMA map range operation specific data */
		struct xe_vma_op_map_range map_range;
		/** @unmap_range: VMA unmap range operation specific data */
		struct xe_vma_op_unmap_range unmap_range;
		/** @prefetch_range: VMA prefetch range operation specific data */
		struct xe_vma_op_prefetch_range prefetch_range;
	};
};

/** struct xe_vma_ops - VMA operations */
struct xe_vma_ops {
	/** @list: list of VMA operations */
	struct list_head list;
	/** @vm: VM */
	struct xe_vm *vm;
	/** @q: exec queue for VMA operations */
	struct xe_exec_queue *q;
	/** @syncs: syncs these operation */
	struct xe_sync_entry *syncs;
	/** @num_syncs: number of syncs */
	u32 num_syncs;
	/** @pt_update_ops: page table update operations */
	struct xe_vm_pgtable_update_ops pt_update_ops[XE_MAX_TILES_PER_DEVICE];
	/** @flag: signify the properties within xe_vma_ops*/
#define XE_VMA_OPS_FLAG_HAS_SVM_PREFETCH BIT(0)
#define XE_VMA_OPS_FLAG_MADVISE          BIT(1)
#define XE_VMA_OPS_ARRAY_OF_BINDS	 BIT(2)
	u32 flags;
#ifdef TEST_VM_OPS_ERROR
	/** @inject_error: inject error to test error handling */
	bool inject_error;
#endif
};

#endif
