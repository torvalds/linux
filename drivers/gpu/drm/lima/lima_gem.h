/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/* Copyright 2017-2019 Qiang Yu <yuq825@gmail.com> */

#ifndef __LIMA_GEM_H__
#define __LIMA_GEM_H__

#include <drm/drm_gem_shmem_helper.h>

struct lima_submit;
struct lima_vm;

struct lima_bo {
	struct drm_gem_shmem_object base;

	struct mutex lock;
	struct list_head va;

	size_t heap_size;
};

static inline struct lima_bo *
to_lima_bo(struct drm_gem_object *obj)
{
	return container_of(to_drm_gem_shmem_obj(obj), struct lima_bo, base);
}

static inline size_t lima_bo_size(struct lima_bo *bo)
{
	return bo->base.base.size;
}

static inline struct dma_resv *lima_bo_resv(struct lima_bo *bo)
{
	return bo->base.base.resv;
}

int lima_heap_alloc(struct lima_bo *bo, struct lima_vm *vm);
struct drm_gem_object *lima_gem_create_object(struct drm_device *dev, size_t size);
int lima_gem_create_handle(struct drm_device *dev, struct drm_file *file,
			   u32 size, u32 flags, u32 *handle);
int lima_gem_get_info(struct drm_file *file, u32 handle, u32 *va, u64 *offset);
int lima_gem_submit(struct drm_file *file, struct lima_submit *submit);
int lima_gem_wait(struct drm_file *file, u32 handle, u32 op, s64 timeout_ns);

void lima_set_vma_flags(struct vm_area_struct *vma);

#endif
