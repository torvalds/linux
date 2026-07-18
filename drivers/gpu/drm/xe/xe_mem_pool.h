/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2026 Intel Corporation
 */
#ifndef _XE_MEM_POOL_H_
#define _XE_MEM_POOL_H_

#include <linux/sizes.h>
#include <linux/types.h>

#include <drm/drm_mm.h>
#include "xe_mem_pool_types.h"

struct drm_printer;
struct xe_mem_pool;
struct xe_tile;

struct xe_mem_pool *xe_mem_pool_init(struct xe_tile *tile, u32 size,
				     u32 guard, int flags);
void xe_mem_pool_sync(struct xe_mem_pool *pool);
void xe_mem_pool_swap_shadow_locked(struct xe_mem_pool *pool);
void xe_mem_pool_sync_shadow_locked(struct xe_mem_pool_node *node);
u64 xe_mem_pool_gpu_addr(struct xe_mem_pool *pool);
void *xe_mem_pool_cpu_addr(struct xe_mem_pool *pool);
struct mutex *xe_mem_pool_bo_swap_guard(struct xe_mem_pool *pool);
void xe_mem_pool_bo_flush_write(struct xe_mem_pool_node *node);
void xe_mem_pool_bo_sync_read(struct xe_mem_pool_node *node);
struct xe_mem_pool_node *xe_mem_pool_alloc_node(void);
int xe_mem_pool_insert_node(struct xe_mem_pool *pool,
			    struct xe_mem_pool_node *node, u32 size);
void xe_mem_pool_free_node(struct xe_mem_pool_node *node);
void *xe_mem_pool_node_cpu_addr(struct xe_mem_pool_node *node);
void xe_mem_pool_dump(struct xe_mem_pool *pool, struct drm_printer *p);

#endif
