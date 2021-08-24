/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author: Felix Zeng <felix.zeng@rock-chips.com>
 */

#ifndef __LINUX_RKNPU_GEM_H
#define __LINUX_RKNPU_GEM_H

#include <linux/mm_types.h>
#include <linux/version.h>

#include <drm/drm_device.h>
#include <drm/drm_vma_manager.h>
#include <drm/drm_gem.h>
#include <drm/drm_mode.h>

#if KERNEL_VERSION(4, 14, 0) > LINUX_VERSION_CODE
#include <drm/drm_mem_util.h>
#endif

#define to_rknpu_obj(x) container_of(x, struct rknpu_gem_object, base)

/*
 * rknpu drm buffer structure.
 *
 * @base: a gem object.
 *	- a new handle to this gem object would be created
 *	by drm_gem_handle_create().
 * @flags: indicate memory type to allocated buffer and cache attribute.
 * @size: size requested from user, in bytes and this size is aligned
 *	in page unit.
 * @cookie: cookie returned by dma_alloc_attrs
 * @kv_addr: kernel virtual address to allocated memory region.
 * @dma_addr: bus address(accessed by dma) to allocated memory region.
 *	- this address could be physical address without IOMMU and
 *	device address with IOMMU.
 * @pages: Array of backing pages.
 * @sgt: Imported sg_table.
 *
 * P.S. this object would be transferred to user as kms_bo.handle so
 *	user can access the buffer through kms_bo.handle.
 */
struct rknpu_gem_object {
	struct drm_gem_object base;
	unsigned int flags;
	unsigned long size;
	void *cookie;
	void __iomem *kv_addr;
	dma_addr_t dma_addr;
	unsigned long dma_attrs;
	unsigned long num_pages;
	struct page **pages;
	struct sg_table *sgt;
	struct drm_mm_node mm_node;
};

/* create a new buffer with gem object */
struct rknpu_gem_object *rknpu_gem_object_create(struct drm_device *dev,
						 unsigned int flags,
						 unsigned long size);

/* destroy a buffer with gem object */
void rknpu_gem_object_destroy(struct rknpu_gem_object *rknpu_obj);

/* request gem object creation and buffer allocation as the size */
int rknpu_gem_create_ioctl(struct drm_device *dev, void *data,
			   struct drm_file *file_priv);

/* get fake-offset of gem object that can be used with mmap. */
int rknpu_gem_map_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv);

int rknpu_gem_destroy_ioctl(struct drm_device *dev, void *data,
			    struct drm_file *file_priv);

/*
 * get rknpu drm object,
 * gem object reference count would be increased.
 */
static inline void rknpu_gem_object_get(struct drm_gem_object *obj)
{
#if KERNEL_VERSION(4, 13, 0) < LINUX_VERSION_CODE
	drm_gem_object_get(obj);
#else
	drm_gem_object_reference(obj);
#endif
}

/*
 * put rknpu drm object acquired from rknpu_gem_object_find() or rknpu_gem_object_get(),
 * gem object reference count would be decreased.
 */
static inline void rknpu_gem_object_put(struct drm_gem_object *obj)
{
#if KERNEL_VERSION(5, 9, 0) <= LINUX_VERSION_CODE
	drm_gem_object_put(obj);
#elif KERNEL_VERSION(4, 13, 0) < LINUX_VERSION_CODE
	drm_gem_object_put_unlocked(obj);
#else
	drm_gem_object_unreference_unlocked(obj);
#endif
}

/*
 * get rknpu drm object from gem handle, this function could be used for
 * other drivers such as 2d/3d acceleration drivers.
 * with this function call, gem object reference count would be increased.
 */
static inline struct rknpu_gem_object *
rknpu_gem_object_find(struct drm_file *filp, unsigned int handle)
{
	struct drm_gem_object *obj;

	obj = drm_gem_object_lookup(filp, handle);
	if (!obj) {
		// DRM_ERROR("failed to lookup gem object.\n");
		return NULL;
	}

	rknpu_gem_object_put(obj);

	return to_rknpu_obj(obj);
}

/* get buffer information to memory region allocated by gem. */
int rknpu_gem_get_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv);

/* free gem object. */
void rknpu_gem_free_object(struct drm_gem_object *obj);

/* create memory region for drm framebuffer. */
int rknpu_gem_dumb_create(struct drm_file *file_priv, struct drm_device *dev,
			  struct drm_mode_create_dumb *args);

#if KERNEL_VERSION(4, 19, 0) > LINUX_VERSION_CODE
/* map memory region for drm framebuffer to user space. */
int rknpu_gem_dumb_map_offset(struct drm_file *file_priv,
			      struct drm_device *dev, uint32_t handle,
			      uint64_t *offset);
#endif

/* page fault handler and mmap fault address(virtual) to physical memory. */
#if KERNEL_VERSION(4, 15, 0) <= LINUX_VERSION_CODE
vm_fault_t rknpu_gem_fault(struct vm_fault *vmf);
#elif KERNEL_VERSION(4, 14, 0) <= LINUX_VERSION_CODE
int rknpu_gem_fault(struct vm_fault *vmf);
#else
int rknpu_gem_fault(struct vm_area_struct *vma, struct vm_fault *vmf);
#endif

/* set vm_flags and we can change the vm attribute to other one at here. */
int rknpu_gem_mmap(struct file *filp, struct vm_area_struct *vma);

/* low-level interface prime helpers */
#if KERNEL_VERSION(4, 13, 0) <= LINUX_VERSION_CODE
struct drm_gem_object *rknpu_gem_prime_import(struct drm_device *dev,
					      struct dma_buf *dma_buf);
#endif
struct sg_table *rknpu_gem_prime_get_sg_table(struct drm_gem_object *obj);
struct drm_gem_object *
rknpu_gem_prime_import_sg_table(struct drm_device *dev,
				struct dma_buf_attachment *attach,
				struct sg_table *sgt);
void *rknpu_gem_prime_vmap(struct drm_gem_object *obj);
void rknpu_gem_prime_vunmap(struct drm_gem_object *obj, void *vaddr);
int rknpu_gem_prime_mmap(struct drm_gem_object *obj,
			 struct vm_area_struct *vma);

int rknpu_gem_sync_ioctl(struct drm_device *dev, void *data,
			 struct drm_file *file_priv);

static inline void *rknpu_gem_alloc_page(size_t nr_pages)
{
#if KERNEL_VERSION(4, 13, 0) <= LINUX_VERSION_CODE
	return kvmalloc_array(nr_pages, sizeof(struct page *),
			      GFP_KERNEL | __GFP_ZERO);
#else
	return drm_calloc_large(nr_pages, sizeof(struct page *));
#endif
}

static inline void rknpu_gem_free_page(void *pages)
{
#if KERNEL_VERSION(4, 13, 0) <= LINUX_VERSION_CODE
	kvfree(pages);
#else
	drm_free_large(pages);
#endif
}

#endif
