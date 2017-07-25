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

enum rockchip_gem_buf_type {
	ROCKCHIP_GEM_BUF_TYPE_CMA,
	ROCKCHIP_GEM_BUF_TYPE_SHMEM,
	ROCKCHIP_GEM_BUF_TYPE_SECURE,
};

struct rockchip_gem_object {
	struct drm_gem_object base;
	unsigned int flags;
	enum rockchip_gem_buf_type buf_type;

	void *kvaddr;
	void *cookie;
	dma_addr_t dma_addr;
	dma_addr_t dma_handle;

	/* Used when IOMMU is disabled */
	struct dma_attrs dma_attrs;

#ifdef CONFIG_DRM_DMA_SYNC
	struct fence *acquire_fence;
	atomic_t acquire_shared_count;
	bool acquire_exclusive;
#endif
	/* Used when IOMMU is enabled */
	struct drm_mm_node mm;
	unsigned long num_pages;
	struct page **pages;
	struct sg_table *sgt;
	size_t size;
};

/*
 * rockchip drm GEM object linked list structure.
 *
 * @list: list link.
 * @rockchip_gem_obj: struct rockchhip_gem_object that this entry points to.
 */
struct rockchip_gem_object_node {
	struct list_head		list;
	struct rockchip_gem_object	*rockchip_gem_obj;
};

struct sg_table *rockchip_gem_prime_get_sg_table(struct drm_gem_object *obj);
struct drm_gem_object *
rockchip_gem_prime_import_sg_table(struct drm_device *dev,
				   struct dma_buf_attachment *attach,
				   struct sg_table *sg);
void *rockchip_gem_prime_vmap(struct drm_gem_object *obj);
void rockchip_gem_prime_vunmap(struct drm_gem_object *obj, void *vaddr);

/* drm driver mmap file operations */
int rockchip_gem_mmap(struct file *filp, struct vm_area_struct *vma);

/* mmap a gem object to userspace. */
int rockchip_gem_mmap_buf(struct drm_gem_object *obj,
			  struct vm_area_struct *vma);

struct rockchip_gem_object *
rockchip_gem_create_object(struct drm_device *drm, unsigned int size,
			   bool alloc_kmap, unsigned int flags);

void rockchip_gem_free_object(struct drm_gem_object *obj);

int rockchip_gem_dumb_create(struct drm_file *file_priv,
			     struct drm_device *dev,
			     struct drm_mode_create_dumb *args);
int rockchip_gem_dumb_map_offset(struct drm_file *file_priv,
				 struct drm_device *dev, uint32_t handle,
				 uint64_t *offset);
/*
 * request gem object creation and buffer allocation as the size
 * that it is calculated with framebuffer information such as width,
 * height and bpp.
 */
int rockchip_gem_create_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *file_priv);

/* get buffer offset to map to user space. */
int rockchip_gem_map_offset_ioctl(struct drm_device *dev, void *data,
				  struct drm_file *file_priv);

int rockchip_gem_get_phys_ioctl(struct drm_device *dev, void *data,
				struct drm_file *file_priv);

/*
 * acquire gem object for CPU access.
 */
int rockchip_gem_cpu_acquire_ioctl(struct drm_device *dev, void* data,
				   struct drm_file *file_priv);
/*
 * release gem object after CPU access.
 */
int rockchip_gem_cpu_release_ioctl(struct drm_device *dev, void* data,
				   struct drm_file *file_priv);

int rockchip_gem_prime_begin_cpu_access(struct drm_gem_object *obj,
					size_t start, size_t len,
					enum dma_data_direction dir);

void rockchip_gem_prime_end_cpu_access(struct drm_gem_object *obj,
				       size_t start, size_t len,
				       enum dma_data_direction dir);

#endif /* _ROCKCHIP_DRM_GEM_H */
