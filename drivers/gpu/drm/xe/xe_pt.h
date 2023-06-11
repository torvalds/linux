/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */
#ifndef _XE_PT_H_
#define _XE_PT_H_

#include <linux/types.h>

#include "xe_pt_types.h"

struct dma_fence;
struct xe_bo;
struct xe_device;
struct xe_engine;
struct xe_sync_entry;
struct xe_tile;
struct xe_vm;
struct xe_vma;

#define xe_pt_write(xe, map, idx, data) \
	xe_map_wr(xe, map, (idx) * sizeof(u64), u64, data)

unsigned int xe_pt_shift(unsigned int level);

struct xe_pt *xe_pt_create(struct xe_vm *vm, struct xe_tile *tile,
			   unsigned int level);

int xe_pt_create_scratch(struct xe_device *xe, struct xe_tile *tile,
			 struct xe_vm *vm);

void xe_pt_populate_empty(struct xe_tile *tile, struct xe_vm *vm,
			  struct xe_pt *pt);

void xe_pt_destroy(struct xe_pt *pt, u32 flags, struct llist_head *deferred);

struct dma_fence *
__xe_pt_bind_vma(struct xe_tile *tile, struct xe_vma *vma, struct xe_engine *e,
		 struct xe_sync_entry *syncs, u32 num_syncs,
		 bool rebind);

struct dma_fence *
__xe_pt_unbind_vma(struct xe_tile *tile, struct xe_vma *vma, struct xe_engine *e,
		   struct xe_sync_entry *syncs, u32 num_syncs);

bool xe_pt_zap_ptes(struct xe_tile *tile, struct xe_vma *vma);

u64 xe_pde_encode(struct xe_bo *bo, u64 bo_offset,
		  const enum xe_cache_level level);

u64 xe_pte_encode(struct xe_vma *vma, struct xe_bo *bo,
		  u64 offset, enum xe_cache_level cache,
		  u32 flags, u32 pt_level);
#endif
