/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020 Intel Corporation
 */

#ifndef _XE_MIGRATE_
#define _XE_MIGRATE_

#include <linux/types.h>

struct dma_fence;
struct drm_pagemap_addr;
struct iosys_map;
struct ttm_resource;

struct xe_bo;
struct xe_gt;
struct xe_tlb_inval_job;
struct xe_exec_queue;
struct xe_migrate;
struct xe_migrate_pt_update;
struct xe_sync_entry;
struct xe_pt;
struct xe_tile;
struct xe_vm;
struct xe_vm_pgtable_update;
struct xe_vma;

enum xe_sriov_vf_ccs_rw_ctxs;

/**
 * struct xe_migrate_pt_update_ops - Callbacks for the
 * xe_migrate_update_pgtables() function.
 */
struct xe_migrate_pt_update_ops {
	/**
	 * @populate: Populate a command buffer or page-table with ptes.
	 * @pt_update: Embeddable callback argument.
	 * @tile: The tile for the current operation.
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
			 struct xe_tile *tile, struct iosys_map *map,
			 void *pos, u32 ofs, u32 num_qwords,
			 const struct xe_vm_pgtable_update *update);
	/**
	 * @clear: Clear a command buffer or page-table with ptes.
	 * @pt_update: Embeddable callback argument.
	 * @tile: The tile for the current operation.
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
	void (*clear)(struct xe_migrate_pt_update *pt_update,
		      struct xe_tile *tile, struct iosys_map *map,
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
	/** @vops: VMA operations */
	struct xe_vma_ops *vops;
	/** @job: The job if a GPU page-table update. NULL otherwise */
	struct xe_sched_job *job;
	/**
	 * @ijob: The TLB invalidation job for primary GT. NULL otherwise
	 */
	struct xe_tlb_inval_job *ijob;
	/**
	 * @mjob: The TLB invalidation job for media GT. NULL otherwise
	 */
	struct xe_tlb_inval_job *mjob;
	/** @tile_id: Tile ID of the update */
	u8 tile_id;
};

struct xe_migrate *xe_migrate_alloc(struct xe_tile *tile);
int xe_migrate_init(struct xe_migrate *m);

struct dma_fence *xe_migrate_to_vram(struct xe_migrate *m,
				     unsigned long npages,
				     struct drm_pagemap_addr *src_addr,
				     u64 dst_addr);

struct dma_fence *xe_migrate_from_vram(struct xe_migrate *m,
				       unsigned long npages,
				       u64 src_addr,
				       struct drm_pagemap_addr *dst_addr);

struct dma_fence *xe_migrate_copy(struct xe_migrate *m,
				  struct xe_bo *src_bo,
				  struct xe_bo *dst_bo,
				  struct ttm_resource *src,
				  struct ttm_resource *dst,
				  bool copy_only_ccs);

int xe_migrate_ccs_rw_copy(struct xe_tile *tile, struct xe_exec_queue *q,
			   struct xe_bo *src_bo,
			   enum xe_sriov_vf_ccs_rw_ctxs read_write);

struct xe_lrc *xe_migrate_lrc(struct xe_migrate *migrate);
struct xe_exec_queue *xe_migrate_exec_queue(struct xe_migrate *migrate);
int xe_migrate_access_memory(struct xe_migrate *m, struct xe_bo *bo,
			     unsigned long offset, void *buf, int len,
			     int write);

#define XE_MIGRATE_CLEAR_FLAG_BO_DATA		BIT(0)
#define XE_MIGRATE_CLEAR_FLAG_CCS_DATA		BIT(1)
#define XE_MIGRATE_CLEAR_FLAG_FULL	(XE_MIGRATE_CLEAR_FLAG_BO_DATA | \
					XE_MIGRATE_CLEAR_FLAG_CCS_DATA)
struct dma_fence *xe_migrate_clear(struct xe_migrate *m,
				   struct xe_bo *bo,
				   struct ttm_resource *dst,
				   u32 clear_flags);

struct xe_vm *xe_migrate_get_vm(struct xe_migrate *m);

struct dma_fence *
xe_migrate_update_pgtables(struct xe_migrate *m,
			   struct xe_migrate_pt_update *pt_update);

void xe_migrate_wait(struct xe_migrate *m);

void xe_migrate_job_lock(struct xe_migrate *m, struct xe_exec_queue *q);
void xe_migrate_job_unlock(struct xe_migrate *m, struct xe_exec_queue *q);

#endif
