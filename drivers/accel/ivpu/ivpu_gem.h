/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020-2023 Intel Corporation
 */
#ifndef __IVPU_GEM_H__
#define __IVPU_GEM_H__

#include <drm/drm_gem.h>
#include <drm/drm_mm.h>

struct dma_buf;
struct ivpu_bo_ops;
struct ivpu_file_priv;

struct ivpu_bo {
	struct drm_gem_object base;
	const struct ivpu_bo_ops *ops;

	struct ivpu_mmu_context *ctx;
	struct list_head ctx_node;
	struct drm_mm_node mm_node;

	struct mutex lock; /* Protects: pages, sgt, mmu_mapped */
	struct sg_table *sgt;
	struct page **pages;
	bool mmu_mapped;

	void *kvaddr;
	u64 vpu_addr;
	u32 handle;
	u32 flags;
	uintptr_t user_ptr;
	u32 job_status;
};

enum ivpu_bo_type {
	IVPU_BO_TYPE_SHMEM = 1,
	IVPU_BO_TYPE_INTERNAL,
	IVPU_BO_TYPE_PRIME,
};

struct ivpu_bo_ops {
	enum ivpu_bo_type type;
	const char *name;
	int (*alloc_pages)(struct ivpu_bo *bo);
	void (*free_pages)(struct ivpu_bo *bo);
	int (*map_pages)(struct ivpu_bo *bo);
	void (*unmap_pages)(struct ivpu_bo *bo);
};

int ivpu_bo_pin(struct ivpu_bo *bo);
void ivpu_bo_remove_all_bos_from_context(struct ivpu_mmu_context *ctx);
void ivpu_bo_list(struct drm_device *dev, struct drm_printer *p);
void ivpu_bo_list_print(struct drm_device *dev);

struct ivpu_bo *
ivpu_bo_alloc_internal(struct ivpu_device *vdev, u64 vpu_addr, u64 size, u32 flags);
void ivpu_bo_free_internal(struct ivpu_bo *bo);
struct drm_gem_object *ivpu_gem_prime_import(struct drm_device *dev, struct dma_buf *dma_buf);
void ivpu_bo_unmap_sgt_and_remove_from_context(struct ivpu_bo *bo);

int ivpu_bo_create_ioctl(struct drm_device *dev, void *data, struct drm_file *file);
int ivpu_bo_info_ioctl(struct drm_device *dev, void *data, struct drm_file *file);
int ivpu_bo_wait_ioctl(struct drm_device *dev, void *data, struct drm_file *file);

static inline struct ivpu_bo *to_ivpu_bo(struct drm_gem_object *obj)
{
	return container_of(obj, struct ivpu_bo, base);
}

static inline struct page *ivpu_bo_get_page(struct ivpu_bo *bo, u64 offset)
{
	if (offset > bo->base.size || !bo->pages)
		return NULL;

	return bo->pages[offset / PAGE_SIZE];
}

static inline u32 ivpu_bo_cache_mode(struct ivpu_bo *bo)
{
	return bo->flags & DRM_IVPU_BO_CACHE_MASK;
}

static inline bool ivpu_bo_is_snooped(struct ivpu_bo *bo)
{
	return ivpu_bo_cache_mode(bo) == DRM_IVPU_BO_CACHED;
}

static inline pgprot_t ivpu_bo_pgprot(struct ivpu_bo *bo, pgprot_t prot)
{
	if (bo->flags & DRM_IVPU_BO_WC)
		return pgprot_writecombine(prot);

	if (bo->flags & DRM_IVPU_BO_UNCACHED)
		return pgprot_noncached(prot);

	return prot;
}

static inline struct ivpu_device *ivpu_bo_to_vdev(struct ivpu_bo *bo)
{
	return to_ivpu_device(bo->base.dev);
}

static inline void *ivpu_to_cpu_addr(struct ivpu_bo *bo, u32 vpu_addr)
{
	if (vpu_addr < bo->vpu_addr)
		return NULL;

	if (vpu_addr >= (bo->vpu_addr + bo->base.size))
		return NULL;

	return bo->kvaddr + (vpu_addr - bo->vpu_addr);
}

static inline u32 cpu_to_vpu_addr(struct ivpu_bo *bo, void *cpu_addr)
{
	if (cpu_addr < bo->kvaddr)
		return 0;

	if (cpu_addr >= (bo->kvaddr + bo->base.size))
		return 0;

	return bo->vpu_addr + (cpu_addr - bo->kvaddr);
}

#endif /* __IVPU_GEM_H__ */
