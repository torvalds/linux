/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_PT_TYPES_H_
#define _XE_PT_TYPES_H_

#include <linux/types.h>

#include "xe_page_reclaim.h"
#include "xe_pt_walk.h"

struct xe_bo;
struct xe_device;
struct xe_vma;

enum xe_cache_level {
	XE_CACHE_NONE,
	XE_CACHE_WT,
	XE_CACHE_WB,
	XE_CACHE_NONE_COMPRESSION, /*UC + COH_NONE + COMPRESSION */
	XE_CACHE_WB_COMPRESSION,
	__XE_CACHE_LEVEL_COUNT,
};

#define XE_VM_MAX_LEVEL 4

struct xe_pt {
	struct xe_ptw base;
	struct xe_bo *bo;
	unsigned int level;
	unsigned int num_live;
	bool rebind;
	bool is_compact;
#if IS_ENABLED(CONFIG_DRM_XE_DEBUG_VM)
	/** @addr: Virtual address start address of the PT. */
	u64 addr;
#endif
};

struct xe_pt_ops {
	u64 (*pte_encode_bo)(struct xe_bo *bo, u64 bo_offset,
			     u16 pat_index, u32 pt_level);
	u64 (*pte_encode_vma)(u64 pte, struct xe_vma *vma,
			      u16 pat_index, u32 pt_level);
	u64 (*pte_encode_addr)(struct xe_device *xe, u64 addr,
			       u16 pat_index,
			       u32 pt_level, bool devmem, u64 flags);
	u64 (*pde_encode_bo)(struct xe_bo *bo, u64 bo_offset);
};

struct xe_pt_entry {
	struct xe_pt *pt;
	u64 pte;
};

struct xe_vm_pgtable_update {
	/** @bo: page table bo to write to */
	struct xe_bo *pt_bo;

	/** @ofs: offset inside this PTE to begin writing to (in qwords) */
	u32 ofs;

	/** @qwords: number of PTE's to write */
	u32 qwords;

	/**
	 * @pt: opaque pointer useful for PT building in the bind IOCTL. Only
	 * safe to touch during the bind IOCTL (i.e., not in bind jobs).
	 */
	struct xe_pt *pt;

	/** @pt_entries: Newly added pagetable entries */
	struct xe_pt_entry *pt_entries;

	/** @level: level of update */
	unsigned int level;

	/** @flags: Target flags */
	u32 flags;
};

/** struct xe_vm_pgtable_update_op - Page table update operation */
struct xe_vm_pgtable_update_op {
	/** @entries: entries to update for this operation */
	struct xe_vm_pgtable_update entries[XE_VM_MAX_LEVEL * 2 + 1];
	/** @vma: VMA for operation, operation not valid if NULL */
	struct xe_vma *vma;
	/** @prl: Backing pointer to page reclaim list of pt_update_ops */
	struct xe_page_reclaim_list *prl;
	/** @num_entries: number of entries for this update operation */
	u32 num_entries;
	/** @bind: is a bind */
	bool bind;
	/** @rebind: is a rebind */
	bool rebind;
};

/**
 * struct xe_pt_job_ops - Page-table update operations (dynamically allocated)
 *
 * This is the portion of &struct xe_vma_ops and
 * &struct xe_vm_pgtable_update_ops that is dynamically allocated, as it
 * must remain valid until the associated bind job completes. A reference
 * count controls its lifetime.
 */
struct xe_pt_job_ops {
	/** @current_op: current page-table update operation */
	u32 current_op;
	/** @refcount: reference count */
	struct kref refcount;
	/** @deferred: list of deferred PT entries to destroy */
	struct llist_head deferred;
	/** @ops: page-table update operations */
	struct xe_vm_pgtable_update_op *ops;
};

/** struct xe_vm_pgtable_update_ops: page table update operations */
struct xe_vm_pgtable_update_ops {
	/** @pt_job_ops: PT update operations dynamic allocation*/
	struct xe_pt_job_ops *pt_job_ops;
	/** @prl: embedded page reclaim list */
	struct xe_page_reclaim_list prl;
	/** @start: start address of ops */
	u64 start;
	/** @last: last address of ops */
	u64 last;
	/** @num_ops: number of operations */
	u32 num_ops;
	/** @needs_svm_lock: Needs SVM lock */
	bool needs_svm_lock;
	/** @needs_invalidation: Needs invalidation */
	bool needs_invalidation;
};

#endif
