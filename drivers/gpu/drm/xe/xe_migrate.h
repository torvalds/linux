/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020 Intel Corporation
 */

#ifndef __XE_MIGRATE__
#define __XE_MIGRATE__

#include <drm/drm_mm.h>

struct dma_fence;
struct iosys_map;
struct ttm_resource;

struct xe_bo;
struct xe_gt;
struct xe_engine;
struct xe_migrate;
struct xe_migrate_pt_update;
struct xe_sync_entry;
struct xe_pt;
struct xe_vm;
struct xe_vm_pgtable_update;
struct xe_vma;

/**
 * struct xe_migrate_pt_update_ops - Callbacks for the
 * xe_migrate_update_pgtables() function.
 */
struct xe_migrate_pt_update_ops {
	/**
	 * @populate: Populate a command buffer or page-table with ptes.
	 * @pt_update: Embeddable callback argument.
	 * @gt: The gt for the current operation.
	 * @map: struct iosys_map into the memory to be populated.
	 * @pos: If @map is NULL, map into the memory to be populated.
	 * @ofs: qword offset into @map, unused if @map is NULL.
	 * @num_qwords: Number of qwords to write.
	 * @update: Information about the PTEs to be inserted.
	 *
	 * This interface is intended to be used as a callback into the
	 * page-table system to populate command buffers or shared
	 * page-tables with PTEs.
	 */
	void (*populate)(struct xe_migrate_pt_update *pt_update,
			 struct xe_gt *gt, struct iosys_map *map,
			 void *pos, u32 ofs, u32 num_qwords,
			 const struct xe_vm_pgtable_update *update);

	/**
	 * @pre_commit: Callback to be called just before arming the
	 * sched_job.
	 * @pt_update: Pointer to embeddable callback argument.
	 *
	 * Return: 0 on success, negative error code on error.
	 */
	int (*pre_commit)(struct xe_migrate_pt_update *pt_update);
};

/**
 * struct xe_migrate_pt_update - Argument to the
 * struct xe_migrate_pt_update_ops callbacks.
 *
 * Intended to be subclassed to support additional arguments if necessary.
 */
struct xe_migrate_pt_update {
	/** @ops: Pointer to the struct xe_migrate_pt_update_ops callbacks */
	const struct xe_migrate_pt_update_ops *ops;
	/** @vma: The vma we're updating the pagetable for. */
	struct xe_vma *vma;
};

struct xe_migrate *xe_migrate_init(struct xe_gt *gt);

struct dma_fence *xe_migrate_copy(struct xe_migrate *m,
				  struct xe_bo *bo,
				  struct ttm_resource *src,
				  struct ttm_resource *dst);

struct dma_fence *xe_migrate_clear(struct xe_migrate *m,
				   struct xe_bo *bo,
				   struct ttm_resource *dst,
				   u32 value);

struct xe_vm *xe_migrate_get_vm(struct xe_migrate *m);

struct dma_fence *
xe_migrate_update_pgtables(struct xe_migrate *m,
			   struct xe_vm *vm,
			   struct xe_bo *bo,
			   struct xe_engine *eng,
			   const struct xe_vm_pgtable_update *updates,
			   u32 num_updates,
			   struct xe_sync_entry *syncs, u32 num_syncs,
			   struct xe_migrate_pt_update *pt_update);

void xe_migrate_wait(struct xe_migrate *m);

struct xe_engine *xe_gt_migrate_engine(struct xe_gt *gt);
#endif
