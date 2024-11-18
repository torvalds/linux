/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_PT_TYPES_H_
#define _XE_PT_TYPES_H_

#include <linux/types.h>

#include "xe_pt_walk.h"

struct xe_bo;
struct xe_device;
struct xe_vma;

enum xe_cache_level {
	XE_CACHE_NONE,
	XE_CACHE_WT,
	XE_CACHE_WB,
	XE_CACHE_NONE_COMPRESSION, /*UC + COH_NONE + COMPRESSION */
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
	/** addr: Virtual address start address of the PT. */
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
	u64 (*pde_encode_bo)(struct xe_bo *bo, u64 bo_offset,
			     u16 pat_index);
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

	/** @pt: opaque pointer useful for the caller of xe_migrate_update_pgtables */
	struct xe_pt *pt;

	/** @pt_entries: Newly added pagetable entries */
	struct xe_pt_entry *pt_entries;

	/** @flags: Target flags */
	u32 flags;
};

/** struct xe_vm_pgtable_update_op - Page table update operation */
struct xe_vm_pgtable_update_op {
	/** @entries: entries to update for this operation */
	struct xe_vm_pgtable_update entries[XE_VM_MAX_LEVEL * 2 + 1];
	/** @vma: VMA for operation, operation not valid if NULL */
	struct xe_vma *vma;
	/** @num_entries: number of entries for this update operation */
	u32 num_entries;
	/** @bind: is a bind */
	bool bind;
	/** @rebind: is a rebind */
	bool rebind;
};

/** struct xe_vm_pgtable_update_ops: page table update operations */
struct xe_vm_pgtable_update_ops {
	/** @ops: operations */
	struct xe_vm_pgtable_update_op *ops;
	/** @deferred: deferred list to destroy PT entries */
	struct llist_head deferred;
	/** @q: exec queue for PT operations */
	struct xe_exec_queue *q;
	/** @start: start address of ops */
	u64 start;
	/** @last: last address of ops */
	u64 last;
	/** @num_ops: number of operations */
	u32 num_ops;
	/** @current_op: current operations */
	u32 current_op;
	/** @needs_userptr_lock: Needs userptr lock */
	bool needs_userptr_lock;
	/** @needs_invalidation: Needs invalidation */
	bool needs_invalidation;
	/**
	 * @wait_vm_bookkeep: PT operations need to wait until VM is idle
	 * (bookkeep dma-resv slots are idle) and stage all future VM activity
	 * behind these operations (install PT operations into VM kernel
	 * dma-resv slot).
	 */
	bool wait_vm_bookkeep;
	/**
	 * @wait_vm_kernel: PT operations need to wait until VM kernel dma-resv
	 * slots are idle.
	 */
	bool wait_vm_kernel;
};

#endif
