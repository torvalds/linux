/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/* Copyright (c) 2025 Valve Corporation */

#ifndef _TTM_POOL_INTERNAL_H_
#define _TTM_POOL_INTERNAL_H_

#include <drm/ttm/ttm_pool.h>

static inline bool ttm_pool_uses_dma_alloc(struct ttm_pool *pool)
{
	return pool->use_dma_alloc;
}

static inline bool ttm_pool_uses_dma32(struct ttm_pool *pool)
{
	return pool->use_dma32;
}

#endif
