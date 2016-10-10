/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author:Mark Yao <mark.yao@rock-chips.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _ROCKCHIP_DRM_GEM_H
#define _ROCKCHIP_DRM_GEM_H

#define to_rockchip_obj(x) container_of(x, struct rockchip_gem_object, base)

struct rockchip_gem_object {
	struct drm_gem_object base;
	unsigned int flags;

	void *kvaddr;
	dma_addr_t dma_addr;
	unsigned long dma_attrs;
};

struct sg_table *rockchip_gem_prime_get_sg_table(struct drm_gem_object *obj);
struct drm_gem_object *
rockchip_gem_prime_import_sg_table(struct drm_device *dev, size_t size,
				   struct sg_table *sgt);
void *rockchip_gem_prime_vmap(struct drm_gem_object *obj);
void rockchip_gem_prime_vunmap(struct drm_gem_object *obj, void *vaddr);

/* drm driver mmap file operations */
int rockchip_gem_mmap(struct file *filp, struct vm_area_struct *vma);

/* mmap a gem object to userspace. */
int rockchip_gem_mmap_buf(struct drm_gem_object *obj,
			  struct vm_area_struct *vma);

struct rockchip_gem_object *
	rockchip_gem_create_object(struct drm_device *drm, unsigned int size,
				   bool alloc_kmap);

void rockchip_gem_free_object(struct drm_gem_object *obj);

int rockchip_gem_dumb_create(struct drm_file *file_priv,
			     struct drm_device *dev,
			     struct drm_mode_create_dumb *args);
int rockchip_gem_dumb_map_offset(struct drm_file *file_priv,
				 struct drm_device *dev, uint32_t handle,
				 uint64_t *offset);
#endif /* _ROCKCHIP_DRM_GEM_H */
