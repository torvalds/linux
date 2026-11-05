/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020 Intel Corporation
 */

#ifndef _XE_MIGRATE_H_
#define _XE_MIGRATE_H_

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
struct xe_vm_pgtable_update_op;
struct xe_vma;

enum xe_sriov_vf_ccs_rw_ctxs;

enum xe_migrate_copy_dir {
	XE_MIGRATE_COPY_TO_VRAM,
	XE_MIGRATE_COPY_TO_SRAM,
};

struct xe_migrate *xe_migrate_alloc(struct xe_tile *tile);
int xe_migrate_init(struct xe_migrate *m);

struct dma_fence *xe_migrate_to_vram(struct xe_migrate *m,
				     unsigned long npages,
				     struct drm_pagemap_addr *src_addr,
				     u64 dst_addr,
				     struct dma_fence *deps);

struct dma_fence *xe_migrate_from_vram(struct xe_migrate *m,
				       unsigned long npages,
				       u64 src_addr,
				       struct drm_pagemap_addr *dst_addr,
				       struct dma_fence *deps);

struct dma_fence *xe_migrate_copy(struct xe_migrate *m,
				  struct xe_bo *src_bo,
				  struct xe_bo *dst_bo,
				  struct ttm_resource *src,
				  struct ttm_resource *dst,
				  bool copy_only_ccs);

struct dma_fence *xe_migrate_resolve(struct xe_migrate *m,
				     struct xe_bo *bo,
				     struct ttm_resource *res);

int xe_migrate_ccs_rw_copy(struct xe_tile *tile, struct xe_exec_queue *q,
			   struct xe_bo *src_bo,
			   struct ttm_resource *new_mem,
			   enum xe_sriov_vf_ccs_rw_ctxs read_write);

void xe_migrate_ccs_rw_copy_clear(struct xe_bo *src_bo,
				  enum xe_sriov_vf_ccs_rw_ctxs read_write,
				  bool bound);

struct xe_lrc *xe_migrate_lrc(struct xe_migrate *migrate);
struct xe_exec_queue *xe_migrate_exec_queue(struct xe_migrate *migrate);
struct dma_fence *xe_migrate_vram_copy_chunk(struct xe_bo *vram_bo, u64 vram_offset,
					     struct xe_bo *sysmem_bo, u64 sysmem_offset,
					     u64 size, enum xe_migrate_copy_dir dir);
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

void xe_migrate_wait(struct xe_migrate *m);

#if IS_ENABLED(CONFIG_DRM_XE_DEBUG_MEM)
int xe_migrate_debug_ccs_overlap(struct xe_migrate *m,
				 struct xe_bo *scratch_bo,
				 bool write_to_ccs);
#endif

void xe_migrate_ulls_enter(struct xe_migrate *m);

#endif
