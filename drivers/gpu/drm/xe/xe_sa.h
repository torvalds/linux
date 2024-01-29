/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */
#ifndef _XE_SA_H_
#define _XE_SA_H_

#include "xe_sa_types.h"

struct dma_fence;
struct xe_bo;
struct xe_tile;

struct xe_sa_manager *xe_sa_bo_manager_init(struct xe_tile *tile, u32 size, u32 align);

struct drm_suballoc *xe_sa_bo_new(struct xe_sa_manager *sa_manager,
				  u32 size);
void xe_sa_bo_flush_write(struct drm_suballoc *sa_bo);
void xe_sa_bo_free(struct drm_suballoc *sa_bo,
		   struct dma_fence *fence);

static inline struct xe_sa_manager *
to_xe_sa_manager(struct drm_suballoc_manager *mng)
{
	return container_of(mng, struct xe_sa_manager, base);
}

static inline u64 xe_sa_bo_gpu_addr(struct drm_suballoc *sa)
{
	return to_xe_sa_manager(sa->manager)->gpu_addr +
		drm_suballoc_soffset(sa);
}

static inline void *xe_sa_bo_cpu_addr(struct drm_suballoc *sa)
{
	return to_xe_sa_manager(sa->manager)->cpu_ptr +
		drm_suballoc_soffset(sa);
}

#endif
