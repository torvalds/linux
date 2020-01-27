/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2019 Linaro, Ltd, Rob Herring <robh@kernel.org> */

#ifndef __PANFROST_GEM_H__
#define __PANFROST_GEM_H__

#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_mm.h>

struct panfrost_mmu;

struct panfrost_gem_object {
	struct drm_gem_shmem_object base;
	struct sg_table *sgts;

	struct panfrost_mmu *mmu;
	struct drm_mm_node node;
	bool is_mapped		:1;
	bool noexec		:1;
	bool is_heap		:1;
};

static inline
struct  panfrost_gem_object *to_panfrost_bo(struct drm_gem_object *obj)
{
	return container_of(to_drm_gem_shmem_obj(obj), struct panfrost_gem_object, base);
}

static inline
struct  panfrost_gem_object *drm_mm_node_to_panfrost_bo(struct drm_mm_node *node)
{
	return container_of(node, struct panfrost_gem_object, node);
}

struct drm_gem_object *panfrost_gem_create_object(struct drm_device *dev, size_t size);

struct drm_gem_object *
panfrost_gem_prime_import_sg_table(struct drm_device *dev,
				   struct dma_buf_attachment *attach,
				   struct sg_table *sgt);

struct panfrost_gem_object *
panfrost_gem_create_with_handle(struct drm_file *file_priv,
				struct drm_device *dev, size_t size,
				u32 flags,
				uint32_t *handle);

int panfrost_gem_open(struct drm_gem_object *obj, struct drm_file *file_priv);
void panfrost_gem_close(struct drm_gem_object *obj,
			struct drm_file *file_priv);

void panfrost_gem_shrinker_init(struct drm_device *dev);
void panfrost_gem_shrinker_cleanup(struct drm_device *dev);

#endif /* __PANFROST_GEM_H__ */
