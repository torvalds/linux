// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author: Felix Zeng <felix.zeng@rock-chips.com>
 */

#include <drm/drm_device.h>
#include <drm/drm_vma_manager.h>
#include <drm/drm_prime.h>

#include <linux/shmem_fs.h>
#include <linux/dma-buf.h>
#include <linux/pfn_t.h>
#include <linux/version.h>

#if KERNEL_VERSION(5, 10, 0) <= LINUX_VERSION_CODE
#include <linux/dma-map-ops.h>
#endif

#include "rknpu_drv.h"
#include "rknpu_ioctl.h"
#include "rknpu_gem.h"

#define RKNPU_GEM_ALLOC_FROM_PAGES 0

#if RKNPU_GEM_ALLOC_FROM_PAGES
static int rknpu_gem_get_pages(struct rknpu_gem_object *rknpu_obj)
{
	struct drm_device *drm = rknpu_obj->base.dev;
	struct scatterlist *s = NULL;
	int ret = -EINVAL, i = 0;

	rknpu_obj->pages = drm_gem_get_pages(&rknpu_obj->base);
	if (IS_ERR(rknpu_obj->pages))
		return PTR_ERR(rknpu_obj->pages);

	rknpu_obj->num_pages = rknpu_obj->base.size >> PAGE_SHIFT;

#if KERNEL_VERSION(5, 10, 0) <= LINUX_VERSION_CODE
	rknpu_obj->sgt = drm_prime_pages_to_sg(drm, rknpu_obj->pages,
					       rknpu_obj->num_pages);
#else
	rknpu_obj->sgt =
		drm_prime_pages_to_sg(rknpu_obj->pages, rknpu_obj->num_pages);
#endif
	if (IS_ERR(rknpu_obj->sgt)) {
		ret = PTR_ERR(rknpu_obj->sgt);
		goto put_pages;
	}

	for_each_sg(rknpu_obj->sgt->sgl, s, rknpu_obj->sgt->nents, i) {
		sg_dma_address(s) = sg_phys(s);
		LOG_DEBUG(
			"gem pages alloc sgt[%d], phys_address: %#llx, length: %#x\n",
			i, (__u64)s->dma_address, s->length);
	}

	ret = dma_map_sg_attrs(drm->dev, rknpu_obj->sgt->sgl,
			       rknpu_obj->sgt->nents, DMA_BIDIRECTIONAL,
			       rknpu_obj->dma_attrs);
	if (ret == 0) {
		LOG_DEV_ERROR(drm->dev, "failed to map sg table.\n");
		ret = -EFAULT;
		goto free_sgt;
	}

	if (rknpu_obj->flags & RKNPU_MEM_KERNEL_MAPPING) {
		rknpu_obj->kv_addr =
			vmap(rknpu_obj->pages, rknpu_obj->num_pages, VM_MAP,
			     PAGE_KERNEL);
	}

	rknpu_obj->dma_addr = (__u64)sg_dma_address(rknpu_obj->sgt->sgl);

	return 0;

free_sgt:
	sg_free_table(rknpu_obj->sgt);
	kfree(rknpu_obj->sgt);
put_pages:
	drm_gem_put_pages(&rknpu_obj->base, rknpu_obj->pages, false, false);

	return ret;
}

static void rknpu_gem_put_pages(struct rknpu_gem_object *rknpu_obj)
{
	struct drm_device *drm = rknpu_obj->base.dev;

	if (rknpu_obj->flags & RKNPU_MEM_KERNEL_MAPPING)
		vunmap(rknpu_obj->kv_addr);

	dma_map_sg_attrs(drm->dev, rknpu_obj->sgt->sgl, rknpu_obj->sgt->nents,
			 DMA_BIDIRECTIONAL, rknpu_obj->dma_attrs);
	drm_gem_put_pages(&rknpu_obj->base, rknpu_obj->pages, true, true);
	sg_free_table(rknpu_obj->sgt);
	kfree(rknpu_obj->sgt);
}
#endif

static int rknpu_gem_alloc_buf(struct rknpu_gem_object *rknpu_obj)
{
	struct drm_device *drm = rknpu_obj->base.dev;
	struct rknpu_device *rknpu_dev = drm->dev_private;
	unsigned int nr_pages = 0;
	struct sg_table *sgt = NULL;
	struct scatterlist *s = NULL;
	gfp_t gfp_mask = GFP_KERNEL;
	int ret = -EINVAL, i = 0;

	if (rknpu_obj->dma_addr) {
		LOG_DEBUG("buffer already allocated.\n");
		return 0;
	}

	rknpu_obj->dma_attrs = 0;

	/*
	 * if RKNPU_MEM_CONTIGUOUS, fully physically contiguous memory
	 * region will be allocated else physically contiguous
	 * as possible.
	 */
	if (!(rknpu_obj->flags & RKNPU_MEM_NON_CONTIGUOUS))
		rknpu_obj->dma_attrs |= DMA_ATTR_FORCE_CONTIGUOUS;

	// cacheable mapping or writecombine mapping
	if (rknpu_obj->flags & RKNPU_MEM_CACHEABLE) {
#ifdef DMA_ATTR_NON_CONSISTENT
		rknpu_obj->dma_attrs |= DMA_ATTR_NON_CONSISTENT;
#endif
#ifdef DMA_ATTR_SYS_CACHE_ONLY
		rknpu_obj->dma_attrs |= DMA_ATTR_SYS_CACHE_ONLY;
#elif DMA_ATTR_FORCE_COHERENT
		// force coherent
		rknpu_obj->dma_attrs |= DMA_ATTR_FORCE_COHERENT;
#endif
	} else if (rknpu_obj->flags & RKNPU_MEM_WRITE_COMBINE) {
		rknpu_obj->dma_attrs |= DMA_ATTR_WRITE_COMBINE;
	}

	if (!(rknpu_obj->flags & RKNPU_MEM_KERNEL_MAPPING))
		rknpu_obj->dma_attrs |= DMA_ATTR_NO_KERNEL_MAPPING;

#ifdef DMA_ATTR_SKIP_ZEROING
	if (!(rknpu_obj->flags & RKNPU_MEM_ZEROING))
		rknpu_obj->dma_attrs |= DMA_ATTR_SKIP_ZEROING;
#endif

#if RKNPU_GEM_ALLOC_FROM_PAGES
	if ((rknpu_obj->flags & RKNPU_MEM_NON_CONTIGUOUS) &&
	    rknpu_dev->iommu_en) {
		return rknpu_gem_get_pages(rknpu_obj);
	}
#endif

	if (rknpu_obj->flags & RKNPU_MEM_ZEROING)
		gfp_mask |= __GFP_ZERO;

	if (!(rknpu_obj->flags & RKNPU_MEM_NON_DMA32)) {
		gfp_mask &= ~__GFP_HIGHMEM;
		gfp_mask |= __GFP_DMA32;
	}

	nr_pages = rknpu_obj->size >> PAGE_SHIFT;

	rknpu_obj->pages = rknpu_gem_alloc_page(nr_pages);
	if (!rknpu_obj->pages) {
		LOG_ERROR("failed to allocate pages.\n");
		return -ENOMEM;
	}

	rknpu_obj->cookie =
		dma_alloc_attrs(drm->dev, rknpu_obj->size, &rknpu_obj->dma_addr,
				gfp_mask, rknpu_obj->dma_attrs);
	if (!rknpu_obj->cookie) {
		/*
		 * when RKNPU_MEM_CONTIGUOUS and IOMMU is available
		 * try to fallback to allocate non-contiguous buffer
		 */
		if (!(rknpu_obj->flags & RKNPU_MEM_NON_CONTIGUOUS) &&
		    rknpu_dev->iommu_en) {
			LOG_DEV_WARN(
				drm->dev,
				"try to fallback to allocate non-contiguous %lu buffer.\n",
				rknpu_obj->size);
			rknpu_obj->dma_attrs &= ~DMA_ATTR_FORCE_CONTIGUOUS;
			rknpu_obj->flags |= RKNPU_MEM_NON_CONTIGUOUS;
			rknpu_obj->cookie =
				dma_alloc_attrs(drm->dev, rknpu_obj->size,
						&rknpu_obj->dma_addr, gfp_mask,
						rknpu_obj->dma_attrs);
			if (!rknpu_obj->cookie) {
				LOG_DEV_ERROR(
					drm->dev,
					"failed to allocate non-contiguous %lu buffer.\n",
					rknpu_obj->size);
				goto err_free;
			}
		} else {
			LOG_DEV_ERROR(drm->dev,
				      "failed to allocate %lu buffer.\n",
				      rknpu_obj->size);
			goto err_free;
		}
	}

	if (rknpu_obj->flags & RKNPU_MEM_KERNEL_MAPPING)
		rknpu_obj->kv_addr = rknpu_obj->cookie;

	sgt = kzalloc(sizeof(*sgt), GFP_KERNEL);
	if (!sgt) {
		ret = -ENOMEM;
		goto err_free_dma;
	}

	ret = dma_get_sgtable_attrs(drm->dev, sgt, rknpu_obj->cookie,
				    rknpu_obj->dma_addr, rknpu_obj->size,
				    rknpu_obj->dma_attrs);
	if (ret < 0) {
		LOG_DEV_ERROR(drm->dev, "failed to get sgtable.\n");
		goto err_free_sgt;
	}

	for_each_sg(sgt->sgl, s, sgt->nents, i) {
		sg_dma_address(s) = sg_phys(s);
		LOG_DEBUG(
			"dma alloc sgt[%d], phys_address: %#llx, length: %u\n",
			i, (__u64)s->dma_address, s->length);
	}

	if (drm_prime_sg_to_page_addr_arrays(sgt, rknpu_obj->pages, NULL,
					     nr_pages)) {
		LOG_DEV_ERROR(drm->dev, "invalid sgtable.\n");
		ret = -EINVAL;
		goto err_free_sg_table;
	}

	rknpu_obj->sgt = sgt;

	return ret;

err_free_sg_table:
	sg_free_table(sgt);
err_free_sgt:
	kfree(sgt);
err_free_dma:
	dma_free_attrs(drm->dev, rknpu_obj->size, rknpu_obj->cookie,
		       rknpu_obj->dma_addr, rknpu_obj->dma_attrs);
err_free:
	rknpu_gem_free_page(rknpu_obj->pages);

	return ret;
}

static void rknpu_gem_free_buf(struct rknpu_gem_object *rknpu_obj)
{
	struct drm_device *drm = rknpu_obj->base.dev;
#if RKNPU_GEM_ALLOC_FROM_PAGES
	struct rknpu_device *rknpu_dev = drm->dev_private;
#endif

	if (!rknpu_obj->dma_addr) {
		LOG_DEBUG("dma handle is invalid.\n");
		return;
	}

#if RKNPU_GEM_ALLOC_FROM_PAGES
	if ((rknpu_obj->flags & RKNPU_MEM_NON_CONTIGUOUS) &&
	    rknpu_dev->iommu_en) {
		rknpu_gem_put_pages(rknpu_obj);
		return;
	}
#endif

	sg_free_table(rknpu_obj->sgt);
	kfree(rknpu_obj->sgt);

	dma_free_attrs(drm->dev, rknpu_obj->size, rknpu_obj->cookie,
		       rknpu_obj->dma_addr, rknpu_obj->dma_attrs);

	rknpu_gem_free_page(rknpu_obj->pages);

	rknpu_obj->dma_addr = 0;
}

static int rknpu_gem_handle_create(struct drm_gem_object *obj,
				   struct drm_file *file_priv,
				   unsigned int *handle)
{
	int ret = -EINVAL;
	/*
	 * allocate a id of idr table where the obj is registered
	 * and handle has the id what user can see.
	 */
	ret = drm_gem_handle_create(file_priv, obj, handle);
	if (ret)
		return ret;

	LOG_DEBUG("gem handle = %#x\n", *handle);

	/* drop reference from allocate - handle holds it now. */
	rknpu_gem_object_put(obj);

	return 0;
}

static int rknpu_gem_handle_destroy(struct drm_file *file_priv,
				    unsigned int handle)
{
	return drm_gem_handle_delete(file_priv, handle);
}

static struct rknpu_gem_object *rknpu_gem_init(struct drm_device *drm,
					       unsigned long size)
{
	struct rknpu_gem_object *rknpu_obj = NULL;
	struct drm_gem_object *obj = NULL;
	gfp_t gfp_mask;
	int ret = -EINVAL;

	rknpu_obj = kzalloc(sizeof(*rknpu_obj), GFP_KERNEL);
	if (!rknpu_obj)
		return ERR_PTR(-ENOMEM);

	rknpu_obj->size = size;
	obj = &rknpu_obj->base;

	ret = drm_gem_object_init(drm, obj, size);
	if (ret < 0) {
		LOG_DEV_ERROR(drm->dev, "failed to initialize gem object\n");
		kfree(rknpu_obj);
		return ERR_PTR(ret);
	}

	gfp_mask = mapping_gfp_mask(obj->filp->f_mapping);

	if (rknpu_obj->flags & RKNPU_MEM_ZEROING)
		gfp_mask |= __GFP_ZERO;

	if (!(rknpu_obj->flags & RKNPU_MEM_NON_DMA32)) {
		gfp_mask &= ~__GFP_HIGHMEM;
		gfp_mask |= __GFP_DMA32;
	}

	mapping_set_gfp_mask(obj->filp->f_mapping, gfp_mask);

	ret = drm_gem_create_mmap_offset(obj);
	if (ret < 0) {
		drm_gem_object_release(obj);
		kfree(rknpu_obj);
		return ERR_PTR(ret);
	}

	return rknpu_obj;
}

struct rknpu_gem_object *rknpu_gem_object_create(struct drm_device *drm,
						 unsigned int flags,
						 unsigned long size)
{
	struct rknpu_device *rknpu_dev = drm->dev_private;
	struct rknpu_gem_object *rknpu_obj = NULL;
	int ret = -EINVAL;

	if (flags & ~(RKNPU_MEM_MASK)) {
		LOG_DEV_ERROR(drm->dev, "invalid buffer flags: %u\n", flags);
		return ERR_PTR(-EINVAL);
	}

	if (!size) {
		LOG_DEV_ERROR(drm->dev, "invalid buffer size: %lu\n", size);
		return ERR_PTR(-EINVAL);
	}

	size = roundup(size, PAGE_SIZE);

	rknpu_obj = rknpu_gem_init(drm, size);
	if (IS_ERR(rknpu_obj))
		return rknpu_obj;

	if (!rknpu_dev->iommu_en && (flags & RKNPU_MEM_NON_CONTIGUOUS)) {
		/*
		 * when no IOMMU is available, all allocated buffers are
		 * contiguous anyway, so drop RKNPU_MEM_NON_CONTIGUOUS flag
		 */
		flags &= ~RKNPU_MEM_NON_CONTIGUOUS;
		LOG_WARN(
			"non-contiguous allocation is not supported without IOMMU, falling back to contiguous buffer\n");
	}

	/* set memory type and cache attribute from user side. */
	rknpu_obj->flags = flags;

	ret = rknpu_gem_alloc_buf(rknpu_obj);
	if (ret < 0) {
		drm_gem_object_release(&rknpu_obj->base);
		kfree(rknpu_obj);
		return ERR_PTR(ret);
	}

	LOG_DEBUG(
		"create dma addr = %#llx, cookie = 0x%p, size = %lu, attrs = %#lx, flags = %#x\n",
		(__u64)rknpu_obj->dma_addr, rknpu_obj->cookie, rknpu_obj->size,
		rknpu_obj->dma_attrs, rknpu_obj->flags);

	return rknpu_obj;
}

void rknpu_gem_object_destroy(struct rknpu_gem_object *rknpu_obj)
{
	struct drm_gem_object *obj = &rknpu_obj->base;

	LOG_DEBUG(
		"destroy dma addr = %#llx, cookie = 0x%p, size = %lu, attrs = %#lx, flags = %#x, handle count = %d\n",
		(__u64)rknpu_obj->dma_addr, rknpu_obj->cookie, rknpu_obj->size,
		rknpu_obj->dma_attrs, rknpu_obj->flags, obj->handle_count);

	/*
	 * do not release memory region from exporter.
	 *
	 * the region will be released by exporter
	 * once dmabuf's refcount becomes 0.
	 */
	if (obj->import_attach) {
		drm_prime_gem_destroy(obj, rknpu_obj->sgt);
		rknpu_gem_free_page(rknpu_obj->pages);
	} else {
		rknpu_gem_free_buf(rknpu_obj);
	}

	/* release file pointer to gem object. */
	drm_gem_object_release(obj);

	kfree(rknpu_obj);
}

int rknpu_gem_create_ioctl(struct drm_device *dev, void *data,
			   struct drm_file *file_priv)
{
	struct rknpu_mem_create *args = data;
	struct rknpu_gem_object *rknpu_obj = NULL;
	int ret = -EINVAL;

	rknpu_obj = rknpu_gem_object_find(file_priv, args->handle);
	if (!rknpu_obj) {
		rknpu_obj =
			rknpu_gem_object_create(dev, args->flags, args->size);
		if (IS_ERR(rknpu_obj))
			return PTR_ERR(rknpu_obj);

		ret = rknpu_gem_handle_create(&rknpu_obj->base, file_priv,
					      &args->handle);
		if (ret) {
			rknpu_gem_object_destroy(rknpu_obj);
			return ret;
		}
	}

	// rknpu_gem_object_get(&rknpu_obj->base);

	args->size = rknpu_obj->size;
	args->obj_addr = (__u64)rknpu_obj;
	args->dma_addr = rknpu_obj->dma_addr;

	return 0;
}

int rknpu_gem_map_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv)
{
	struct rknpu_mem_map *args = data;

#if KERNEL_VERSION(4, 19, 0) > LINUX_VERSION_CODE
	return rknpu_gem_dumb_map_offset(file_priv, dev, args->handle,
					 &args->offset);
#else
	return drm_gem_dumb_map_offset(file_priv, dev, args->handle,
				       &args->offset);
#endif
}

int rknpu_gem_destroy_ioctl(struct drm_device *dev, void *data,
			    struct drm_file *file_priv)
{
	struct rknpu_gem_object *rknpu_obj = NULL;
	struct rknpu_mem_destroy *args = data;

	rknpu_obj = rknpu_gem_object_find(file_priv, args->handle);
	if (!rknpu_obj)
		return -EINVAL;

	// rknpu_gem_object_put(&rknpu_obj->base);

	return rknpu_gem_handle_destroy(file_priv, args->handle);
}

#if RKNPU_GEM_ALLOC_FROM_PAGES
/*
 * __vm_map_pages - maps range of kernel pages into user vma
 * @vma: user vma to map to
 * @pages: pointer to array of source kernel pages
 * @num: number of pages in page array
 * @offset: user's requested vm_pgoff
 *
 * This allows drivers to map range of kernel pages into a user vma.
 *
 * Return: 0 on success and error code otherwise.
 */
static int __vm_map_pages(struct vm_area_struct *vma, struct page **pages,
			  unsigned long num, unsigned long offset)
{
	unsigned long count = vma_pages(vma);
	unsigned long uaddr = vma->vm_start;
	int ret = -EINVAL, i = 0;

	/* Fail if the user requested offset is beyond the end of the object */
	if (offset >= num)
		return -ENXIO;

	/* Fail if the user requested size exceeds available object size */
	if (count > num - offset)
		return -ENXIO;

	for (i = 0; i < count; i++) {
		ret = vm_insert_page(vma, uaddr, pages[offset + i]);
		if (ret < 0)
			return ret;
		uaddr += PAGE_SIZE;
	}

	return 0;
}

static int rknpu_gem_mmap_pages(struct rknpu_gem_object *rknpu_obj,
				struct vm_area_struct *vma)
{
	struct drm_device *drm = rknpu_obj->base.dev;
	int ret = -EINVAL;

	vma->vm_flags |= VM_MIXEDMAP;

	ret = __vm_map_pages(vma, rknpu_obj->pages, rknpu_obj->num_pages,
			     vma->vm_pgoff);
	if (ret < 0)
		LOG_DEV_ERROR(drm->dev, "failed to map pages into vma: %d\n",
			      ret);

	return ret;
}
#endif

static int rknpu_gem_mmap_buffer(struct rknpu_gem_object *rknpu_obj,
				 struct vm_area_struct *vma)
{
	struct drm_device *drm = rknpu_obj->base.dev;
#if RKNPU_GEM_ALLOC_FROM_PAGES
	struct rknpu_device *rknpu_dev = drm->dev_private;
#endif
	unsigned long vm_size = 0;
	int ret = -EINVAL;

	/*
	 * clear the VM_PFNMAP flag that was set by drm_gem_mmap(), and set the
	 * vm_pgoff (used as a fake buffer offset by DRM) to 0 as we want to map
	 * the whole buffer.
	 */
	vma->vm_flags &= ~VM_PFNMAP;
	vma->vm_pgoff = 0;

	vm_size = vma->vm_end - vma->vm_start;

	/* check if user-requested size is valid. */
	if (vm_size > rknpu_obj->size)
		return -EINVAL;

#if RKNPU_GEM_ALLOC_FROM_PAGES
	if ((rknpu_obj->flags & RKNPU_MEM_NON_CONTIGUOUS) &&
	    rknpu_dev->iommu_en) {
		return rknpu_gem_mmap_pages(rknpu_obj, vma);
	}
#endif

	ret = dma_mmap_attrs(drm->dev, vma, rknpu_obj->cookie,
			     rknpu_obj->dma_addr, rknpu_obj->size,
			     rknpu_obj->dma_attrs);
	if (ret < 0) {
		LOG_DEV_ERROR(drm->dev, "failed to mmap, ret = %d\n", ret);
		return ret;
	}

	return 0;
}

void rknpu_gem_free_object(struct drm_gem_object *obj)
{
	rknpu_gem_object_destroy(to_rknpu_obj(obj));
}

int rknpu_gem_dumb_create(struct drm_file *file_priv, struct drm_device *drm,
			  struct drm_mode_create_dumb *args)
{
	struct rknpu_device *rknpu_dev = drm->dev_private;
	struct rknpu_gem_object *rknpu_obj = NULL;
	unsigned int flags = 0;
	int ret = -EINVAL;

	/*
	 * allocate memory to be used for framebuffer.
	 * - this callback would be called by user application
	 *	with DRM_IOCTL_MODE_CREATE_DUMB command.
	 */
	args->pitch = args->width * ((args->bpp + 7) / 8);
	args->size = args->pitch * args->height;

	if (rknpu_dev->iommu_en)
		flags = RKNPU_MEM_NON_CONTIGUOUS | RKNPU_MEM_WRITE_COMBINE;
	else
		flags = RKNPU_MEM_CONTIGUOUS | RKNPU_MEM_WRITE_COMBINE;

	rknpu_obj = rknpu_gem_object_create(drm, flags, args->size);
	if (IS_ERR(rknpu_obj)) {
		LOG_DEV_ERROR(drm->dev, "gem object allocate failed.\n");
		return PTR_ERR(rknpu_obj);
	}

	ret = rknpu_gem_handle_create(&rknpu_obj->base, file_priv,
				      &args->handle);
	if (ret) {
		rknpu_gem_object_destroy(rknpu_obj);
		return ret;
	}

	return 0;
}

#if KERNEL_VERSION(4, 19, 0) > LINUX_VERSION_CODE
int rknpu_gem_dumb_map_offset(struct drm_file *file_priv,
			      struct drm_device *drm, uint32_t handle,
			      uint64_t *offset)
{
	struct rknpu_gem_object *rknpu_obj = NULL;
	struct drm_gem_object *obj = NULL;
	int ret = -EINVAL;

	rknpu_obj = rknpu_gem_object_find(file_priv, handle);
	if (!rknpu_obj)
		return -EINVAL;

	/* Don't allow imported objects to be mapped */
	obj = &rknpu_obj->base;
	if (obj->import_attach)
		return -EINVAL;

	ret = drm_gem_create_mmap_offset(obj);
	if (ret)
		return ret;

	*offset = drm_vma_node_offset_addr(&obj->vma_node);

	return 0;
}
#endif

#if KERNEL_VERSION(4, 15, 0) <= LINUX_VERSION_CODE
vm_fault_t rknpu_gem_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct drm_gem_object *obj = vma->vm_private_data;
	struct rknpu_gem_object *rknpu_obj = to_rknpu_obj(obj);
	struct drm_device *drm = rknpu_obj->base.dev;
	unsigned long pfn = 0;
	pgoff_t page_offset = 0;

	page_offset = (vmf->address - vma->vm_start) >> PAGE_SHIFT;

	if (page_offset >= (rknpu_obj->size >> PAGE_SHIFT)) {
		LOG_DEV_ERROR(drm->dev, "invalid page offset\n");
		return VM_FAULT_SIGBUS;
	}

	pfn = page_to_pfn(rknpu_obj->pages[page_offset]);
	return vmf_insert_mixed(vma, vmf->address,
				__pfn_to_pfn_t(pfn, PFN_DEV));
}
#elif KERNEL_VERSION(4, 14, 0) <= LINUX_VERSION_CODE
int rknpu_gem_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct drm_gem_object *obj = vma->vm_private_data;
	struct rknpu_gem_object *rknpu_obj = to_rknpu_obj(obj);
	struct drm_device *drm = rknpu_obj->base.dev;
	unsigned long pfn = 0;
	pgoff_t page_offset = 0;
	int ret = -EINVAL;

	page_offset = (vmf->address - vma->vm_start) >> PAGE_SHIFT;

	if (page_offset >= (rknpu_obj->size >> PAGE_SHIFT)) {
		LOG_DEV_ERROR(drm->dev, "invalid page offset\n");
		ret = -EINVAL;
		goto out;
	}

	pfn = page_to_pfn(rknpu_obj->pages[page_offset]);
	ret = vm_insert_mixed(vma, vmf->address, __pfn_to_pfn_t(pfn, PFN_DEV));

out:
	switch (ret) {
	case 0:
	case -ERESTARTSYS:
	case -EINTR:
		return VM_FAULT_NOPAGE;
	case -ENOMEM:
		return VM_FAULT_OOM;
	default:
		return VM_FAULT_SIGBUS;
	}
}
#else
int rknpu_gem_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct drm_gem_object *obj = vma->vm_private_data;
	struct rknpu_gem_object *rknpu_obj = to_rknpu_obj(obj);
	struct drm_device *drm = rknpu_obj->base.dev;
	unsigned long pfn = 0;
	pgoff_t page_offset = 0;
	int ret = -EINVAL;

	page_offset = ((unsigned long)vmf->virtual_address - vma->vm_start) >>
		      PAGE_SHIFT;

	if (page_offset >= (rknpu_obj->size >> PAGE_SHIFT)) {
		LOG_DEV_ERROR(drm->dev, "invalid page offset\n");
		ret = -EINVAL;
		goto out;
	}

	pfn = page_to_pfn(rknpu_obj->pages[page_offset]);
	ret = vm_insert_mixed(vma, (unsigned long)vmf->virtual_address,
			      __pfn_to_pfn_t(pfn, PFN_DEV));

out:
	switch (ret) {
	case 0:
	case -ERESTARTSYS:
	case -EINTR:
		return VM_FAULT_NOPAGE;
	case -ENOMEM:
		return VM_FAULT_OOM;
	default:
		return VM_FAULT_SIGBUS;
	}
}
#endif

static int rknpu_gem_mmap_obj(struct drm_gem_object *obj,
			      struct vm_area_struct *vma)
{
	struct rknpu_gem_object *rknpu_obj = to_rknpu_obj(obj);
	int ret = -EINVAL;

	LOG_DEBUG("flags = %#x\n", rknpu_obj->flags);

	/* non-cacheable as default. */
	if (rknpu_obj->flags & RKNPU_MEM_CACHEABLE) {
		vma->vm_page_prot = vm_get_page_prot(vma->vm_flags);
	} else if (rknpu_obj->flags & RKNPU_MEM_WRITE_COMBINE) {
		vma->vm_page_prot =
			pgprot_writecombine(vm_get_page_prot(vma->vm_flags));
	} else {
		vma->vm_page_prot =
			pgprot_noncached(vm_get_page_prot(vma->vm_flags));
	}

	ret = rknpu_gem_mmap_buffer(rknpu_obj, vma);
	if (ret)
		goto err_close_vm;

	return 0;

err_close_vm:
	drm_gem_vm_close(vma);

	return ret;
}

int rknpu_gem_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct drm_gem_object *obj = NULL;
	int ret = -EINVAL;

	/* set vm_area_struct. */
	ret = drm_gem_mmap(filp, vma);
	if (ret < 0) {
		LOG_ERROR("failed to mmap, ret = %d\n", ret);
		return ret;
	}

	obj = vma->vm_private_data;

	if (obj->import_attach)
		return dma_buf_mmap(obj->dma_buf, vma, 0);

	return rknpu_gem_mmap_obj(obj, vma);
}

/* low-level interface prime helpers */
#if KERNEL_VERSION(4, 13, 0) <= LINUX_VERSION_CODE
struct drm_gem_object *rknpu_gem_prime_import(struct drm_device *dev,
					      struct dma_buf *dma_buf)
{
	return drm_gem_prime_import_dev(dev, dma_buf, dev->dev);
}
#endif

struct sg_table *rknpu_gem_prime_get_sg_table(struct drm_gem_object *obj)
{
	struct rknpu_gem_object *rknpu_obj = to_rknpu_obj(obj);
	int npages = 0;

	npages = rknpu_obj->size >> PAGE_SHIFT;

#if KERNEL_VERSION(5, 10, 0) <= LINUX_VERSION_CODE
	return drm_prime_pages_to_sg(obj->dev, rknpu_obj->pages, npages);
#else
	return drm_prime_pages_to_sg(rknpu_obj->pages, npages);
#endif
}

struct drm_gem_object *
rknpu_gem_prime_import_sg_table(struct drm_device *dev,
				struct dma_buf_attachment *attach,
				struct sg_table *sgt)
{
	struct rknpu_gem_object *rknpu_obj = NULL;
	int npages = 0;
	int ret = -EINVAL;

	rknpu_obj = rknpu_gem_init(dev, attach->dmabuf->size);
	if (IS_ERR(rknpu_obj)) {
		ret = PTR_ERR(rknpu_obj);
		return ERR_PTR(ret);
	}

	rknpu_obj->dma_addr = sg_dma_address(sgt->sgl);

	npages = rknpu_obj->size >> PAGE_SHIFT;
	rknpu_obj->pages = rknpu_gem_alloc_page(npages);
	if (!rknpu_obj->pages) {
		ret = -ENOMEM;
		goto err;
	}

	ret = drm_prime_sg_to_page_addr_arrays(sgt, rknpu_obj->pages, NULL,
					       npages);
	if (ret < 0)
		goto err_free_large;

	rknpu_obj->sgt = sgt;

	if (sgt->nents == 1) {
		/* always physically continuous memory if sgt->nents is 1. */
		rknpu_obj->flags |= RKNPU_MEM_CONTIGUOUS;
	} else {
		/*
		 * this case could be CONTIG or NONCONTIG type but for now
		 * sets NONCONTIG.
		 * TODO. we have to find a way that exporter can notify
		 * the type of its own buffer to importer.
		 */
		rknpu_obj->flags |= RKNPU_MEM_NON_CONTIGUOUS;
	}

	return &rknpu_obj->base;

err_free_large:
	rknpu_gem_free_page(rknpu_obj->pages);
err:
	drm_gem_object_release(&rknpu_obj->base);
	kfree(rknpu_obj);
	return ERR_PTR(ret);
}

void *rknpu_gem_prime_vmap(struct drm_gem_object *obj)
{
	struct rknpu_gem_object *rknpu_obj = to_rknpu_obj(obj);

	if (!rknpu_obj->pages)
		return NULL;

	return vmap(rknpu_obj->pages, rknpu_obj->num_pages, VM_MAP,
		    PAGE_KERNEL);
}

void rknpu_gem_prime_vunmap(struct drm_gem_object *obj, void *vaddr)
{
	vunmap(vaddr);
}

int rknpu_gem_prime_mmap(struct drm_gem_object *obj, struct vm_area_struct *vma)
{
	int ret = -EINVAL;

	ret = drm_gem_mmap_obj(obj, obj->size, vma);
	if (ret < 0)
		return ret;

	return rknpu_gem_mmap_obj(obj, vma);
}

int rknpu_gem_sync_ioctl(struct drm_device *dev, void *data,
			 struct drm_file *file_priv)
{
	struct rknpu_gem_object *rknpu_obj = NULL;
	struct rknpu_mem_sync *args = data;
	struct scatterlist *sg;
	dma_addr_t sg_dma_addr;
	unsigned long length, offset = 0;
	unsigned long sg_offset, sg_left, size = 0;
	unsigned long len = 0;
	int i;

	rknpu_obj = (struct rknpu_gem_object *)args->obj_addr;
	if (!rknpu_obj)
		return -EINVAL;

	if (!(rknpu_obj->flags & RKNPU_MEM_CACHEABLE))
		return -EINVAL;

	if (!(rknpu_obj->flags & RKNPU_MEM_NON_CONTIGUOUS)) {
		if (args->flags & RKNPU_MEM_SYNC_TO_DEVICE) {
			dma_sync_single_range_for_device(
				dev->dev, rknpu_obj->dma_addr, args->offset,
				args->size, DMA_TO_DEVICE);
		}
		if (args->flags & RKNPU_MEM_SYNC_FROM_DEVICE) {
			dma_sync_single_range_for_cpu(dev->dev,
						      rknpu_obj->dma_addr,
						      args->offset, args->size,
						      DMA_FROM_DEVICE);
		}
	} else {
		struct drm_device *drm = rknpu_obj->base.dev;
		struct rknpu_device *rknpu_dev = drm->dev_private;

		WARN_ON(!rknpu_dev->fake_dev);

		length = args->size;
		offset = args->offset;

		for_each_sg(rknpu_obj->sgt->sgl, sg, rknpu_obj->sgt->nents,
			     i) {
			len += sg->length;
			if (len <= offset)
				continue;

			sg_dma_addr = sg_dma_address(sg);
			sg_left = len - offset;
			sg_offset = sg->length - sg_left;
			size = (length < sg_left) ? length : sg_left;

			if (args->flags & RKNPU_MEM_SYNC_TO_DEVICE) {
				dma_sync_single_range_for_device(
					rknpu_dev->fake_dev, sg_dma_addr,
					sg_offset, size, DMA_TO_DEVICE);
			}

			if (args->flags & RKNPU_MEM_SYNC_FROM_DEVICE) {
				dma_sync_single_range_for_cpu(
					rknpu_dev->fake_dev, sg_dma_addr,
					sg_offset, size, DMA_FROM_DEVICE);
			}

			offset += size;
			length -= size;

			if (length == 0)
				break;
		}
	}

	return 0;
}
