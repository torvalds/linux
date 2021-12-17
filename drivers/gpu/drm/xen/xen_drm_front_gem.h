/* SPDX-License-Identifier: GPL-2.0 OR MIT */

/*
 *  Xen para-virtual DRM device
 *
 * Copyright (C) 2016-2018 EPAM Systems Inc.
 *
 * Author: Oleksandr Andrushchenko <oleksandr_andrushchenko@epam.com>
 */

#ifndef __XEN_DRM_FRONT_GEM_H
#define __XEN_DRM_FRONT_GEM_H

struct dma_buf_attachment;
struct dma_buf_map;
struct drm_device;
struct drm_gem_object;
struct sg_table;

struct drm_gem_object *xen_drm_front_gem_create(struct drm_device *dev,
						size_t size);

struct drm_gem_object *
xen_drm_front_gem_import_sg_table(struct drm_device *dev,
				  struct dma_buf_attachment *attach,
				  struct sg_table *sgt);

struct sg_table *xen_drm_front_gem_get_sg_table(struct drm_gem_object *gem_obj);

struct page **xen_drm_front_gem_get_pages(struct drm_gem_object *obj);

void xen_drm_front_gem_free_object_unlocked(struct drm_gem_object *gem_obj);

int xen_drm_front_gem_prime_vmap(struct drm_gem_object *gem_obj,
				 struct dma_buf_map *map);

void xen_drm_front_gem_prime_vunmap(struct drm_gem_object *gem_obj,
				    struct dma_buf_map *map);

#endif /* __XEN_DRM_FRONT_GEM_H */
