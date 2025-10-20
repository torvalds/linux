/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/* Copyright (c) 2025 Valve Corporation */

#ifndef _TTM_POOL_INTERNAL_H_
#define _TTM_POOL_INTERNAL_H_

#include <drm/ttm/ttm_allocation.h>
#include <drm/ttm/ttm_pool.h>

static inline bool ttm_pool_uses_dma_alloc(struct ttm_pool *pool)
{
	return pool->alloc_flags & TTM_ALLOCATION_POOL_USE_DMA_ALLOC;
}

static inline bool ttm_pool_uses_dma32(struct ttm_pool *pool)
{
	return pool->alloc_flags & TTM_ALLOCATION_POOL_USE_DMA32;
}

static inline bool ttm_pool_beneficial_order(struct ttm_pool *pool)
{
	return pool->alloc_flags & 0xff;
}

#endif
