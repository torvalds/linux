/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/* Copyright 2018-2019 Qiang Yu <yuq825@gmail.com> */

#ifndef __LIMA_OBJECT_H__
#define __LIMA_OBJECT_H__

#include <drm/drm_gem.h>

#include "lima_device.h"

struct lima_bo {
	struct drm_gem_object gem;

	struct page **pages;
	dma_addr_t *pages_dma_addr;
	struct sg_table *sgt;
	void *vaddr;

	struct mutex lock;
	struct list_head va;
};

static inline struct lima_bo *
to_lima_bo(struct drm_gem_object *obj)
{
	return container_of(obj, struct lima_bo, gem);
}

struct lima_bo *lima_bo_create(struct lima_device *dev, u32 size,
			       u32 flags, struct sg_table *sgt);
void lima_bo_destroy(struct lima_bo *bo);
void *lima_bo_vmap(struct lima_bo *bo);
void lima_bo_vunmap(struct lima_bo *bo);

#endif
