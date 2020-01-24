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

	/*
	 * Use a list for now. If searching a mapping ever becomes the
	 * bottleneck, we should consider using an RB-tree, or even better,
	 * let the core store drm_gem_object_mapping entries (where we
	 * could place driver specific data) instead of drm_gem_object ones
	 * in its drm_file->object_idr table.
	 *
	 * struct drm_gem_object_mapping {
	 *	struct drm_gem_object *obj;
	 *	void *driver_priv;
	 * };
	 */
	struct {
		struct list_head list;
		struct mutex lock;
	} mappings;

	bool noexec		:1;
	bool is_heap		:1;
};

struct panfrost_gem_mapping {
	struct list_head node;
	struct kref refcount;
	struct panfrost_gem_object *obj;
	struct drm_mm_node mmnode;
	struct panfrost_mmu *mmu;
	bool active		:1;
};

static inline
struct  panfrost_gem_object *to_panfrost_bo(struct drm_gem_object *obj)
{
	return container_of(to_drm_gem_shmem_obj(obj), struct panfrost_gem_object, base);
}

static inline struct panfrost_gem_mapping *
drm_mm_node_to_panfrost_mapping(struct drm_mm_node *node)
{
	return container_of(node, struct panfrost_gem_mapping, mmnode);
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

struct panfrost_gem_mapping *
panfrost_gem_mapping_get(struct panfrost_gem_object *bo,
			 struct panfrost_file_priv *priv);
void panfrost_gem_mapping_put(struct panfrost_gem_mapping *mapping);
void panfrost_gem_teardown_mappings(struct panfrost_gem_object *bo);

void panfrost_gem_shrinker_init(struct drm_device *dev);
void panfrost_gem_shrinker_cleanup(struct drm_device *dev);

#endif /* __PANFROST_GEM_H__ */
