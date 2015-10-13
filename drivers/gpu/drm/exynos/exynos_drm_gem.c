/* exynos_drm_gem.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Author: Inki Dae <inki.dae@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <drm/drmP.h>
#include <drm/drm_vma_manager.h>

#include <linux/shmem_fs.h>
#include <linux/dma-buf.h>
#include <drm/exynos_drm.h>

#include "exynos_drm_drv.h"
#include "exynos_drm_gem.h"
#include "exynos_drm_iommu.h"

static int exynos_drm_alloc_buf(struct exynos_drm_gem_obj *obj)
{
	struct drm_device *dev = obj->base.dev;
	enum dma_attr attr;
	unsigned int nr_pages;

	if (obj->dma_addr) {
		DRM_DEBUG_KMS("already allocated.\n");
		return 0;
	}

	init_dma_attrs(&obj->dma_attrs);

	/*
	 * if EXYNOS_BO_CONTIG, fully physically contiguous memory
	 * region will be allocated else physically contiguous
	 * as possible.
	 */
	if (!(obj->flags & EXYNOS_BO_NONCONTIG))
		dma_set_attr(DMA_ATTR_FORCE_CONTIGUOUS, &obj->dma_attrs);

	/*
	 * if EXYNOS_BO_WC or EXYNOS_BO_NONCACHABLE, writecombine mapping
	 * else cachable mapping.
	 */
	if (obj->flags & EXYNOS_BO_WC || !(obj->flags & EXYNOS_BO_CACHABLE))
		attr = DMA_ATTR_WRITE_COMBINE;
	else
		attr = DMA_ATTR_NON_CONSISTENT;

	dma_set_attr(attr, &obj->dma_attrs);
	dma_set_attr(DMA_ATTR_NO_KERNEL_MAPPING, &obj->dma_attrs);

	nr_pages = obj->size >> PAGE_SHIFT;

	if (!is_drm_iommu_supported(dev)) {
		obj->pages = drm_calloc_large(nr_pages, sizeof(struct page *));
		if (!obj->pages) {
			DRM_ERROR("failed to allocate pages.\n");
			return -ENOMEM;
		}
	}

	obj->cookie = dma_alloc_attrs(dev->dev, obj->size, &obj->dma_addr,
				      GFP_KERNEL, &obj->dma_attrs);
	if (!obj->cookie) {
		DRM_ERROR("failed to allocate buffer.\n");
		if (obj->pages)
			drm_free_large(obj->pages);
		return -ENOMEM;
	}

	if (obj->pages) {
		dma_addr_t start_addr;
		unsigned int i = 0;

		start_addr = obj->dma_addr;
		while (i < nr_pages) {
			obj->pages[i] = pfn_to_page(dma_to_pfn(dev->dev,
							       start_addr));
			start_addr += PAGE_SIZE;
			i++;
		}
	} else {
		obj->pages = obj->cookie;
	}

	DRM_DEBUG_KMS("dma_addr(0x%lx), size(0x%lx)\n",
			(unsigned long)obj->dma_addr,
			obj->size);

	return 0;
}

static void exynos_drm_free_buf(struct exynos_drm_gem_obj *obj)
{
	struct drm_device *dev = obj->base.dev;

	if (!obj->dma_addr) {
		DRM_DEBUG_KMS("dma_addr is invalid.\n");
		return;
	}

	DRM_DEBUG_KMS("dma_addr(0x%lx), size(0x%lx)\n",
			(unsigned long)obj->dma_addr, obj->size);

	dma_free_attrs(dev->dev, obj->size, obj->cookie,
			(dma_addr_t)obj->dma_addr, &obj->dma_attrs);

	if (!is_drm_iommu_supported(dev))
		drm_free_large(obj->pages);
}

static int exynos_drm_gem_handle_create(struct drm_gem_object *obj,
					struct drm_file *file_priv,
					unsigned int *handle)
{
	int ret;

	/*
	 * allocate a id of idr table where the obj is registered
	 * and handle has the id what user can see.
	 */
	ret = drm_gem_handle_create(file_priv, obj, handle);
	if (ret)
		return ret;

	DRM_DEBUG_KMS("gem handle = 0x%x\n", *handle);

	/* drop reference from allocate - handle holds it now. */
	drm_gem_object_unreference_unlocked(obj);

	return 0;
}

void exynos_drm_gem_destroy(struct exynos_drm_gem_obj *exynos_gem_obj)
{
	struct drm_gem_object *obj = &exynos_gem_obj->base;

	DRM_DEBUG_KMS("handle count = %d\n", obj->handle_count);

	/*
	 * do not release memory region from exporter.
	 *
	 * the region will be released by exporter
	 * once dmabuf's refcount becomes 0.
	 */
	if (obj->import_attach)
		drm_prime_gem_destroy(obj, exynos_gem_obj->sgt);
	else
		exynos_drm_free_buf(exynos_gem_obj);

	/* release file pointer to gem object. */
	drm_gem_object_release(obj);

	kfree(exynos_gem_obj);
}

unsigned long exynos_drm_gem_get_size(struct drm_device *dev,
						unsigned int gem_handle,
						struct drm_file *file_priv)
{
	struct exynos_drm_gem_obj *exynos_gem_obj;
	struct drm_gem_object *obj;

	obj = drm_gem_object_lookup(dev, file_priv, gem_handle);
	if (!obj) {
		DRM_ERROR("failed to lookup gem object.\n");
		return 0;
	}

	exynos_gem_obj = to_exynos_gem_obj(obj);

	drm_gem_object_unreference_unlocked(obj);

	return exynos_gem_obj->size;
}

static struct exynos_drm_gem_obj *exynos_drm_gem_init(struct drm_device *dev,
						      unsigned long size)
{
	struct exynos_drm_gem_obj *exynos_gem_obj;
	struct drm_gem_object *obj;
	int ret;

	exynos_gem_obj = kzalloc(sizeof(*exynos_gem_obj), GFP_KERNEL);
	if (!exynos_gem_obj)
		return ERR_PTR(-ENOMEM);

	exynos_gem_obj->size = size;
	obj = &exynos_gem_obj->base;

	ret = drm_gem_object_init(dev, obj, size);
	if (ret < 0) {
		DRM_ERROR("failed to initialize gem object\n");
		kfree(exynos_gem_obj);
		return ERR_PTR(ret);
	}

	ret = drm_gem_create_mmap_offset(obj);
	if (ret < 0) {
		drm_gem_object_release(obj);
		kfree(exynos_gem_obj);
		return ERR_PTR(ret);
	}

	DRM_DEBUG_KMS("created file object = 0x%x\n", (unsigned int)obj->filp);

	return exynos_gem_obj;
}

struct exynos_drm_gem_obj *exynos_drm_gem_create(struct drm_device *dev,
						unsigned int flags,
						unsigned long size)
{
	struct exynos_drm_gem_obj *exynos_gem_obj;
	int ret;

	if (flags & ~(EXYNOS_BO_MASK)) {
		DRM_ERROR("invalid flags.\n");
		return ERR_PTR(-EINVAL);
	}

	if (!size) {
		DRM_ERROR("invalid size.\n");
		return ERR_PTR(-EINVAL);
	}

	size = roundup(size, PAGE_SIZE);

	exynos_gem_obj = exynos_drm_gem_init(dev, size);
	if (IS_ERR(exynos_gem_obj))
		return exynos_gem_obj;

	/* set memory type and cache attribute from user side. */
	exynos_gem_obj->flags = flags;

	ret = exynos_drm_alloc_buf(exynos_gem_obj);
	if (ret < 0) {
		drm_gem_object_release(&exynos_gem_obj->base);
		kfree(exynos_gem_obj);
		return ERR_PTR(ret);
	}

	return exynos_gem_obj;
}

int exynos_drm_gem_create_ioctl(struct drm_device *dev, void *data,
				struct drm_file *file_priv)
{
	struct drm_exynos_gem_create *args = data;
	struct exynos_drm_gem_obj *exynos_gem_obj;
	int ret;

	exynos_gem_obj = exynos_drm_gem_create(dev, args->flags, args->size);
	if (IS_ERR(exynos_gem_obj))
		return PTR_ERR(exynos_gem_obj);

	ret = exynos_drm_gem_handle_create(&exynos_gem_obj->base, file_priv,
			&args->handle);
	if (ret) {
		exynos_drm_gem_destroy(exynos_gem_obj);
		return ret;
	}

	return 0;
}

dma_addr_t *exynos_drm_gem_get_dma_addr(struct drm_device *dev,
					unsigned int gem_handle,
					struct drm_file *filp)
{
	struct exynos_drm_gem_obj *exynos_gem_obj;
	struct drm_gem_object *obj;

	obj = drm_gem_object_lookup(dev, filp, gem_handle);
	if (!obj) {
		DRM_ERROR("failed to lookup gem object.\n");
		return ERR_PTR(-EINVAL);
	}

	exynos_gem_obj = to_exynos_gem_obj(obj);

	return &exynos_gem_obj->dma_addr;
}

void exynos_drm_gem_put_dma_addr(struct drm_device *dev,
					unsigned int gem_handle,
					struct drm_file *filp)
{
	struct drm_gem_object *obj;

	obj = drm_gem_object_lookup(dev, filp, gem_handle);
	if (!obj) {
		DRM_ERROR("failed to lookup gem object.\n");
		return;
	}

	drm_gem_object_unreference_unlocked(obj);

	/*
	 * decrease obj->refcount one more time because we has already
	 * increased it at exynos_drm_gem_get_dma_addr().
	 */
	drm_gem_object_unreference_unlocked(obj);
}

static int exynos_drm_gem_mmap_buffer(struct exynos_drm_gem_obj *exynos_gem_obj,
				      struct vm_area_struct *vma)
{
	struct drm_device *drm_dev = exynos_gem_obj->base.dev;
	unsigned long vm_size;
	int ret;

	vma->vm_flags &= ~VM_PFNMAP;
	vma->vm_pgoff = 0;

	vm_size = vma->vm_end - vma->vm_start;

	/* check if user-requested size is valid. */
	if (vm_size > exynos_gem_obj->size)
		return -EINVAL;

	ret = dma_mmap_attrs(drm_dev->dev, vma, exynos_gem_obj->pages,
				exynos_gem_obj->dma_addr, exynos_gem_obj->size,
				&exynos_gem_obj->dma_attrs);
	if (ret < 0) {
		DRM_ERROR("failed to mmap.\n");
		return ret;
	}

	return 0;
}

int exynos_drm_gem_get_ioctl(struct drm_device *dev, void *data,
				      struct drm_file *file_priv)
{
	struct exynos_drm_gem_obj *exynos_gem_obj;
	struct drm_exynos_gem_info *args = data;
	struct drm_gem_object *obj;

	mutex_lock(&dev->struct_mutex);

	obj = drm_gem_object_lookup(dev, file_priv, args->handle);
	if (!obj) {
		DRM_ERROR("failed to lookup gem object.\n");
		mutex_unlock(&dev->struct_mutex);
		return -EINVAL;
	}

	exynos_gem_obj = to_exynos_gem_obj(obj);

	args->flags = exynos_gem_obj->flags;
	args->size = exynos_gem_obj->size;

	drm_gem_object_unreference(obj);
	mutex_unlock(&dev->struct_mutex);

	return 0;
}

int exynos_gem_map_sgt_with_dma(struct drm_device *drm_dev,
				struct sg_table *sgt,
				enum dma_data_direction dir)
{
	int nents;

	mutex_lock(&drm_dev->struct_mutex);

	nents = dma_map_sg(drm_dev->dev, sgt->sgl, sgt->nents, dir);
	if (!nents) {
		DRM_ERROR("failed to map sgl with dma.\n");
		mutex_unlock(&drm_dev->struct_mutex);
		return nents;
	}

	mutex_unlock(&drm_dev->struct_mutex);
	return 0;
}

void exynos_gem_unmap_sgt_from_dma(struct drm_device *drm_dev,
				struct sg_table *sgt,
				enum dma_data_direction dir)
{
	dma_unmap_sg(drm_dev->dev, sgt->sgl, sgt->nents, dir);
}

void exynos_drm_gem_free_object(struct drm_gem_object *obj)
{
	exynos_drm_gem_destroy(to_exynos_gem_obj(obj));
}

int exynos_drm_gem_dumb_create(struct drm_file *file_priv,
			       struct drm_device *dev,
			       struct drm_mode_create_dumb *args)
{
	struct exynos_drm_gem_obj *exynos_gem_obj;
	unsigned int flags;
	int ret;

	/*
	 * allocate memory to be used for framebuffer.
	 * - this callback would be called by user application
	 *	with DRM_IOCTL_MODE_CREATE_DUMB command.
	 */

	args->pitch = args->width * ((args->bpp + 7) / 8);
	args->size = args->pitch * args->height;

	if (is_drm_iommu_supported(dev))
		flags = EXYNOS_BO_NONCONTIG | EXYNOS_BO_WC;
	else
		flags = EXYNOS_BO_CONTIG | EXYNOS_BO_WC;

	exynos_gem_obj = exynos_drm_gem_create(dev, flags, args->size);
	if (IS_ERR(exynos_gem_obj)) {
		dev_warn(dev->dev, "FB allocation failed.\n");
		return PTR_ERR(exynos_gem_obj);
	}

	ret = exynos_drm_gem_handle_create(&exynos_gem_obj->base, file_priv,
			&args->handle);
	if (ret) {
		exynos_drm_gem_destroy(exynos_gem_obj);
		return ret;
	}

	return 0;
}

int exynos_drm_gem_dumb_map_offset(struct drm_file *file_priv,
				   struct drm_device *dev, uint32_t handle,
				   uint64_t *offset)
{
	struct drm_gem_object *obj;
	int ret = 0;

	mutex_lock(&dev->struct_mutex);

	/*
	 * get offset of memory allocated for drm framebuffer.
	 * - this callback would be called by user application
	 *	with DRM_IOCTL_MODE_MAP_DUMB command.
	 */

	obj = drm_gem_object_lookup(dev, file_priv, handle);
	if (!obj) {
		DRM_ERROR("failed to lookup gem object.\n");
		ret = -EINVAL;
		goto unlock;
	}

	*offset = drm_vma_node_offset_addr(&obj->vma_node);
	DRM_DEBUG_KMS("offset = 0x%lx\n", (unsigned long)*offset);

	drm_gem_object_unreference(obj);
unlock:
	mutex_unlock(&dev->struct_mutex);
	return ret;
}

int exynos_drm_gem_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct drm_gem_object *obj = vma->vm_private_data;
	struct exynos_drm_gem_obj *exynos_gem_obj = to_exynos_gem_obj(obj);
	unsigned long pfn;
	pgoff_t page_offset;
	int ret;

	page_offset = ((unsigned long)vmf->virtual_address -
			vma->vm_start) >> PAGE_SHIFT;

	if (page_offset >= (exynos_gem_obj->size >> PAGE_SHIFT)) {
		DRM_ERROR("invalid page offset\n");
		ret = -EINVAL;
		goto out;
	}

	pfn = page_to_pfn(exynos_gem_obj->pages[page_offset]);
	ret = vm_insert_mixed(vma, (unsigned long)vmf->virtual_address, pfn);

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

int exynos_drm_gem_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct exynos_drm_gem_obj *exynos_gem_obj;
	struct drm_gem_object *obj;
	int ret;

	/* set vm_area_struct. */
	ret = drm_gem_mmap(filp, vma);
	if (ret < 0) {
		DRM_ERROR("failed to mmap.\n");
		return ret;
	}

	obj = vma->vm_private_data;
	exynos_gem_obj = to_exynos_gem_obj(obj);

	DRM_DEBUG_KMS("flags = 0x%x\n", exynos_gem_obj->flags);

	/* non-cachable as default. */
	if (exynos_gem_obj->flags & EXYNOS_BO_CACHABLE)
		vma->vm_page_prot = vm_get_page_prot(vma->vm_flags);
	else if (exynos_gem_obj->flags & EXYNOS_BO_WC)
		vma->vm_page_prot =
			pgprot_writecombine(vm_get_page_prot(vma->vm_flags));
	else
		vma->vm_page_prot =
			pgprot_noncached(vm_get_page_prot(vma->vm_flags));

	ret = exynos_drm_gem_mmap_buffer(exynos_gem_obj, vma);
	if (ret)
		goto err_close_vm;

	return ret;

err_close_vm:
	drm_gem_vm_close(vma);

	return ret;
}

/* low-level interface prime helpers */
struct sg_table *exynos_drm_gem_prime_get_sg_table(struct drm_gem_object *obj)
{
	struct exynos_drm_gem_obj *exynos_gem_obj = to_exynos_gem_obj(obj);
	int npages;

	npages = exynos_gem_obj->size >> PAGE_SHIFT;

	return drm_prime_pages_to_sg(exynos_gem_obj->pages, npages);
}

struct drm_gem_object *
exynos_drm_gem_prime_import_sg_table(struct drm_device *dev,
				     struct dma_buf_attachment *attach,
				     struct sg_table *sgt)
{
	struct exynos_drm_gem_obj *exynos_gem_obj;
	int npages;
	int ret;

	exynos_gem_obj = exynos_drm_gem_init(dev, attach->dmabuf->size);
	if (IS_ERR(exynos_gem_obj)) {
		ret = PTR_ERR(exynos_gem_obj);
		return ERR_PTR(ret);
	}

	exynos_gem_obj->dma_addr = sg_dma_address(sgt->sgl);

	npages = exynos_gem_obj->size >> PAGE_SHIFT;
	exynos_gem_obj->pages = drm_malloc_ab(npages, sizeof(struct page *));
	if (!exynos_gem_obj->pages) {
		ret = -ENOMEM;
		goto err;
	}

	ret = drm_prime_sg_to_page_addr_arrays(sgt, exynos_gem_obj->pages, NULL,
			npages);
	if (ret < 0)
		goto err_free_large;

	exynos_gem_obj->sgt = sgt;

	if (sgt->nents == 1) {
		/* always physically continuous memory if sgt->nents is 1. */
		exynos_gem_obj->flags |= EXYNOS_BO_CONTIG;
	} else {
		/*
		 * this case could be CONTIG or NONCONTIG type but for now
		 * sets NONCONTIG.
		 * TODO. we have to find a way that exporter can notify
		 * the type of its own buffer to importer.
		 */
		exynos_gem_obj->flags |= EXYNOS_BO_NONCONTIG;
	}

	return &exynos_gem_obj->base;

err_free_large:
	drm_free_large(exynos_gem_obj->pages);
err:
	drm_gem_object_release(&exynos_gem_obj->base);
	kfree(exynos_gem_obj);
	return ERR_PTR(ret);
}

void *exynos_drm_gem_prime_vmap(struct drm_gem_object *obj)
{
	return NULL;
}

void exynos_drm_gem_prime_vunmap(struct drm_gem_object *obj, void *vaddr)
{
	/* Nothing to do */
}
