/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 VeriSilicon Holdings Co., Ltd.
 */

#ifndef __VS_GEM_H__
#define __VS_GEM_H__

#include <linux/dma-buf.h>

#include <drm/drm_gem.h>

#include "vs_drv.h"

#if KERNEL_VERSION(5, 5, 0) <= LINUX_VERSION_CODE
#include <drm/drm_prime.h>
#endif

/*
 *
 * @base: drm gem object.
 * @size: size requested from user
 * @cookie: cookie returned by dma_alloc_attrs
 *	- not kernel virtual address with DMA_ATTR_NO_KERNEL_MAPPING
 * @dma_addr: bus address(accessed by dma) to allocated memory region.
 *	- this address could be physical address without IOMMU and
 *	device address with IOMMU.
 * @dma_attrs: attribute for DMA API
 * @get_pages: flag for manually applying for non-contiguous memory.
 * @pages: Array of backing pages.
 * @sgt: Imported sg_table.
 *
 */
struct vs_gem_object {
	struct drm_gem_object	base;
	size_t			size;
	void			*cookie;
	dma_addr_t		dma_addr;
	u32				iova;
	unsigned long	dma_attrs;
	bool			get_pages;
	struct page		**pages;
	struct sg_table *sgt;
};

static inline
struct vs_gem_object *to_vs_gem_object(struct drm_gem_object *obj)
{
	return container_of(obj, struct vs_gem_object, base);
}

struct vs_gem_object *vs_gem_create_object(struct drm_device *dev,
					   size_t size);

int vs_gem_prime_vmap(struct drm_gem_object *obj, struct dma_buf_map *map);
void vs_gem_prime_vunmap(struct drm_gem_object *obj, struct dma_buf_map *map);

int vs_gem_prime_mmap(struct drm_gem_object *obj,
			  struct vm_area_struct *vma);

int vs_gem_dumb_create(struct drm_file *file_priv,
			   struct drm_device *drm,
			   struct drm_mode_create_dumb *args);

int vs_gem_mmap(struct file *filp, struct vm_area_struct *vma);


struct sg_table *vs_gem_prime_get_sg_table(struct drm_gem_object *obj);

struct drm_gem_object *vs_gem_prime_import(struct drm_device *dev,
						struct dma_buf *dma_buf);
struct drm_gem_object *
vs_gem_prime_import_sg_table(struct drm_device *dev,
				 struct dma_buf_attachment *attach,
				 struct sg_table *sgt);

#endif /* __VS_GEM_H__ */
