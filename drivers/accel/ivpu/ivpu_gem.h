/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020-2023 Intel Corporation
 */
#ifndef __IVPU_GEM_H__
#define __IVPU_GEM_H__

#include <drm/drm_gem.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_mm.h>

struct ivpu_file_priv;

struct ivpu_bo {
	struct drm_gem_shmem_object base;
	struct ivpu_mmu_context *ctx;
	struct list_head bo_list_node;
	struct drm_mm_node mm_node;

	struct mutex lock; /* Protects: ctx, mmu_mapped, vpu_addr */
	u64 vpu_addr;
	u32 flags;
	u32 job_status; /* Valid only for command buffer */
	bool mmu_mapped;
};

int ivpu_bo_pin(struct ivpu_bo *bo);
void ivpu_bo_unbind_all_bos_from_context(struct ivpu_device *vdev, struct ivpu_mmu_context *ctx);

struct drm_gem_object *ivpu_gem_create_object(struct drm_device *dev, size_t size);
struct ivpu_bo *ivpu_bo_create(struct ivpu_device *vdev, struct ivpu_mmu_context *ctx,
			       struct ivpu_addr_range *range, u64 size, u32 flags);
struct ivpu_bo *ivpu_bo_create_global(struct ivpu_device *vdev, u64 size, u32 flags);
void ivpu_bo_free(struct ivpu_bo *bo);

int ivpu_bo_create_ioctl(struct drm_device *dev, void *data, struct drm_file *file);
int ivpu_bo_info_ioctl(struct drm_device *dev, void *data, struct drm_file *file);
int ivpu_bo_wait_ioctl(struct drm_device *dev, void *data, struct drm_file *file);

void ivpu_bo_list(struct drm_device *dev, struct drm_printer *p);
void ivpu_bo_list_print(struct drm_device *dev);

static inline struct ivpu_bo *to_ivpu_bo(struct drm_gem_object *obj)
{
	return container_of(obj, struct ivpu_bo, base.base);
}

static inline void *ivpu_bo_vaddr(struct ivpu_bo *bo)
{
	return bo->base.vaddr;
}

static inline size_t ivpu_bo_size(struct ivpu_bo *bo)
{
	return bo->base.base.size;
}

static inline u32 ivpu_bo_cache_mode(struct ivpu_bo *bo)
{
	return bo->flags & DRM_IVPU_BO_CACHE_MASK;
}

static inline bool ivpu_bo_is_snooped(struct ivpu_bo *bo)
{
	return ivpu_bo_cache_mode(bo) == DRM_IVPU_BO_CACHED;
}

static inline struct ivpu_device *ivpu_bo_to_vdev(struct ivpu_bo *bo)
{
	return to_ivpu_device(bo->base.base.dev);
}

static inline void *ivpu_to_cpu_addr(struct ivpu_bo *bo, u32 vpu_addr)
{
	if (vpu_addr < bo->vpu_addr)
		return NULL;

	if (vpu_addr >= (bo->vpu_addr + ivpu_bo_size(bo)))
		return NULL;

	return ivpu_bo_vaddr(bo) + (vpu_addr - bo->vpu_addr);
}

static inline u32 cpu_to_vpu_addr(struct ivpu_bo *bo, void *cpu_addr)
{
	if (cpu_addr < ivpu_bo_vaddr(bo))
		return 0;

	if (cpu_addr >= (ivpu_bo_vaddr(bo) + ivpu_bo_size(bo)))
		return 0;

	return bo->vpu_addr + (cpu_addr - ivpu_bo_vaddr(bo));
}

#endif /* __IVPU_GEM_H__ */
