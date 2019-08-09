/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2019 Linaro, Ltd, Rob Herring <robh@kernel.org> */

#ifndef __PANFROST_GEM_H__
#define __PANFROST_GEM_H__

#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_mm.h>

struct panfrost_gem_object {
	struct drm_gem_shmem_object base;

	struct drm_mm_node node;
	bool is_mapped;
};

static inline
struct  panfrost_gem_object *to_panfrost_bo(struct drm_gem_object *obj)
{
	return container_of(to_drm_gem_shmem_obj(obj), struct panfrost_gem_object, base);
}

struct drm_gem_object *panfrost_gem_create_object(struct drm_device *dev, size_t size);

struct drm_gem_object *
panfrost_gem_prime_import_sg_table(struct drm_device *dev,
				   struct dma_buf_attachment *attach,
				   struct sg_table *sgt);

#endif /* __PANFROST_GEM_H__ */
