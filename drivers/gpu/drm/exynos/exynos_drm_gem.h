/* SPDX-License-Identifier: GPL-2.0-or-later */
/* exynos_drm_gem.h
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Authoer: Inki Dae <inki.dae@samsung.com>
 */

#ifndef _EXYNOS_DRM_GEM_H_
#define _EXYNOS_DRM_GEM_H_

#include <drm/drm_gem.h>
#include <linux/mm_types.h>

#define to_exynos_gem(x)	container_of(x, struct exynos_drm_gem, base)

#define IS_NONCONTIG_BUFFER(f)		(f & EXYNOS_BO_NONCONTIG)

/*
 * exynos drm buffer structure.
 *
 * @base: a gem object.
 *	- a new handle to this gem object would be created
 *	by drm_gem_handle_create().
 * @flags: indicate memory type to allocated buffer and cache attruibute.
 * @size: size requested from user, in bytes and this size is aligned
 *	in page unit.
 * @cookie: cookie returned by dma_alloc_attrs
 * @kvaddr: kernel virtual address to allocated memory region (for fbdev)
 * @dma_addr: bus address(accessed by dma) to allocated memory region.
 *	- this address could be physical address without IOMMU and
 *	device address with IOMMU.
 * @dma_attrs: attrs passed dma mapping framework
 * @sgt: Imported sg_table.
 *
 * P.S. this object would be transferred to user as kms_bo.handle so
 *	user can access the buffer through kms_bo.handle.
 */
struct exynos_drm_gem {
	struct drm_gem_object	base;
	unsigned int		flags;
	unsigned long		size;
	void			*cookie;
	void __iomem		*kvaddr;
	dma_addr_t		dma_addr;
	unsigned long		dma_attrs;
	struct sg_table		*sgt;
};

/* destroy a buffer with gem object */
void exynos_drm_gem_destroy(struct exynos_drm_gem *exynos_gem);

/* create a new buffer with gem object */
struct exynos_drm_gem *exynos_drm_gem_create(struct drm_device *dev,
					     unsigned int flags,
					     unsigned long size,
					     bool kvmap);

/*
 * request gem object creation and buffer allocation as the size
 * that it is calculated with framebuffer information such as width,
 * height and bpp.
 */
int exynos_drm_gem_create_ioctl(struct drm_device *dev, void *data,
				struct drm_file *file_priv);

/* get fake-offset of gem object that can be used with mmap. */
int exynos_drm_gem_map_ioctl(struct drm_device *dev, void *data,
			     struct drm_file *file_priv);

/*
 * get exynos drm object from gem handle, this function could be used for
 * other drivers such as 2d/3d acceleration drivers.
 * with this function call, gem object reference count would be increased.
 */
struct exynos_drm_gem *exynos_drm_gem_get(struct drm_file *filp,
					  unsigned int gem_handle);

/*
 * put exynos drm object acquired from exynos_drm_gem_get(),
 * gem object reference count would be decreased.
 */
static inline void exynos_drm_gem_put(struct exynos_drm_gem *exynos_gem)
{
	drm_gem_object_put_unlocked(&exynos_gem->base);
}

/* get buffer information to memory region allocated by gem. */
int exynos_drm_gem_get_ioctl(struct drm_device *dev, void *data,
				      struct drm_file *file_priv);

/* free gem object. */
void exynos_drm_gem_free_object(struct drm_gem_object *obj);

/* create memory region for drm framebuffer. */
int exynos_drm_gem_dumb_create(struct drm_file *file_priv,
			       struct drm_device *dev,
			       struct drm_mode_create_dumb *args);

/* set vm_flags and we can change the vm attribute to other one at here. */
int exynos_drm_gem_mmap(struct file *filp, struct vm_area_struct *vma);

/* low-level interface prime helpers */
struct drm_gem_object *exynos_drm_gem_prime_import(struct drm_device *dev,
					    struct dma_buf *dma_buf);
struct sg_table *exynos_drm_gem_prime_get_sg_table(struct drm_gem_object *obj);
struct drm_gem_object *
exynos_drm_gem_prime_import_sg_table(struct drm_device *dev,
				     struct dma_buf_attachment *attach,
				     struct sg_table *sgt);
void *exynos_drm_gem_prime_vmap(struct drm_gem_object *obj);
void exynos_drm_gem_prime_vunmap(struct drm_gem_object *obj, void *vaddr);
int exynos_drm_gem_prime_mmap(struct drm_gem_object *obj,
			      struct vm_area_struct *vma);

#endif
