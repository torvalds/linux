/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2026 Intel Corporation
 */

#ifndef _XE_MEM_POOL_TYPES_H_
#define _XE_MEM_POOL_TYPES_H_

#include <drm/drm_mm.h>

#define XE_MEM_POOL_BO_FLAG_INIT_SHADOW_COPY			BIT(0)

/**
 * struct xe_mem_pool_node - Sub-range allocations from mem pool.
 */
struct xe_mem_pool_node {
	/** @sa_node: drm_mm_node for this allocation. */
	struct drm_mm_node sa_node;
};

#endif
