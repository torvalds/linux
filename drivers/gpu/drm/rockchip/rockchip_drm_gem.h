/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author:Mark Yao <mark.yao@rock-chips.com>
 */

#ifndef _ROCKCHIP_DRM_GEM_H
#define _ROCKCHIP_DRM_GEM_H

#define to_rockchip_obj(x) container_of(x, struct rockchip_gem_object, base)

struct rockchip_gem_object {
	struct drm_gem_object base;
	unsigned int flags;

	void *kvaddr;
	dma_addr_t dma_addr;
	/* Used when IOMMU is disabled */
	unsigned long dma_attrs;

	/* Used when IOMMU is enabled */
	struct drm_mm_node mm;
	unsigned long num_pages;
	struct page **pages;
	struct sg_table *sgt;
	size_t size;
};

struct sg_table *rockchip_gem_prime_get_sg_table(struct drm_gem_object *obj);
struct drm_gem_object *
rockchip_gem_prime_import_sg_table(struct drm_device *dev,
				   struct dma_buf_attachment *attach,
				   struct sg_table *sg);
int rockchip_gem_prime_vmap(struct drm_gem_object *obj, struct iosys_map *map);
void rockchip_gem_prime_vunmap(struct drm_gem_object *obj,
			       struct iosys_map *map);

struct rockchip_gem_object *
	rockchip_gem_create_object(struct drm_device *drm, unsigned int size,
				   bool alloc_kmap);

void rockchip_gem_free_object(struct drm_gem_object *obj);

int rockchip_gem_dumb_create(struct drm_file *file_priv,
			     struct drm_device *dev,
			     struct drm_mode_create_dumb *args);
#endif /* _ROCKCHIP_DRM_GEM_H */
