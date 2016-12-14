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
#include <linux/pfn_t.h>
#include <drm/exynos_drm.h>

#include "exynos_drm_drv.h"
#include "exynos_drm_gem.h"
#include "exynos_drm_iommu.h"

static int exynos_drm_alloc_buf(struct exynos_drm_gem *exynos_gem)
{
	struct drm_device *dev = exynos_gem->base.dev;
	unsigned long attr;
	unsigned int nr_pages;
	struct sg_table sgt;
	int ret = -ENOMEM;

	if (exynos_gem->dma_addr) {
		DRM_DEBUG_KMS("already allocated.\n");
		return 0;
	}

	exynos_gem->dma_attrs = 0;

	/*
	 * if EXYNOS_BO_CONTIG, fully physically contiguous memory
	 * region will be allocated else physically contiguous
	 * as possible.
	 */
	if (!(exynos_gem->flags & EXYNOS_BO_NONCONTIG))
		exynos_gem->dma_attrs |= DMA_ATTR_FORCE_CONTIGUOUS;

	/*
	 * if EXYNOS_BO_WC or EXYNOS_BO_NONCACHABLE, writecombine mapping
	 * else cachable mapping.
	 */
	if (exynos_gem->flags & EXYNOS_BO_WC ||
			!(exynos_gem->flags & EXYNOS_BO_CACHABLE))
		attr = DMA_ATTR_WRITE_COMBINE;
	else
		attr = DMA_ATTR_NON_CONSISTENT;

	exynos_gem->dma_attrs |= attr;
	exynos_gem->dma_attrs |= DMA_ATTR_NO_KERNEL_MAPPING;

	nr_pages = exynos_gem->size >> PAGE_SHIFT;

	exynos_gem->pages = drm_calloc_large(nr_pages, sizeof(struct page *));
	if (!exynos_gem->pages) {
		DRM_ERROR("failed to allocate pages.\n");
		return -ENOMEM;
	}

	exynos_gem->cookie = dma_alloc_attrs(to_dma_dev(dev), exynos_gem->size,
					     &exynos_gem->dma_addr, GFP_KERNEL,
					     exynos_gem->dma_attrs);
	if (!exynos_gem->cookie) {
		DRM_ERROR("failed to allocate buffer.\n");
		goto err_free;
	}

	ret = dma_get_sgtable_attrs(to_dma_dev(dev), &sgt, exynos_gem->cookie,
				    exynos_gem->dma_addr, exynos_gem->size,
				    exynos_gem->dma_attrs);
	if (ret < 0) {
		DRM_ERROR("failed to get sgtable.\n");
		goto err_dma_free;
	}

	if (drm_prime_sg_to_page_addr_arrays(&sgt, exynos_gem->pages, NULL,
					     nr_pages)) {
		DRM_ERROR("invalid sgtable.\n");
		ret = -EINVAL;
		goto err_sgt_free;
	}

	sg_free_table(&sgt);

	DRM_DEBUG_KMS("dma_addr(0x%lx), size(0x%lx)\n",
			(unsigned long)exynos_gem->dma_addr, exynos_gem->size);

	return 0;

err_sgt_free:
	sg_free_table(&sgt);
err_dma_free:
	dma_free_attrs(to_dma_dev(dev), exynos_gem->size, exynos_gem->cookie,
		       exynos_gem->dma_addr, exynos_gem->dma_attrs);
err_free:
	drm_free_large(exynos_gem->pages);

	return ret;
}

static void exynos_drm_free_buf(struct exynos_drm_gem *exynos_gem)
{
	struct drm_device *dev = exynos_gem->base.dev;

	if (!exynos_gem->dma_addr) {
		DRM_DEBUG_KMS("dma_addr is invalid.\n");
		return;
	}

	DRM_DEBUG_KMS("dma_addr(0x%lx), size(0x%lx)\n",
			(unsigned long)exynos_gem->dma_addr, exynos_gem->size);

	dma_free_attrs(to_dma_dev(dev), exynos_gem->size, exynos_gem->cookie,
			(dma_addr_t)exynos_gem->dma_addr,
			exynos_gem->dma_attrs);

	drm_free_large(exynos_gem->pages);
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

void exynos_drm_gem_destroy(struct exynos_drm_gem *exynos_gem)
{
	struct drm_gem_object *obj = &exynos_gem->base;

	DRM_DEBUG_KMS("handle count = %d\n", obj->handle_count);

	/*
	 * do not release memory region from exporter.
	 *
	 * the region will be released by exporter
	 * once dmabuf's refcount becomes 0.
	 */
	if (obj->import_attach)
		drm_prime_gem_destroy(obj, exynos_gem->sgt);
	else
		exynos_drm_free_buf(exynos_gem);

	/* release file pointer to gem object. */
	drm_gem_object_release(obj);

	kfree(exynos_gem);
}

unsigned long exynos_drm_gem_get_size(struct drm_device *dev,
						unsigned int gem_handle,
						struct drm_file *file_priv)
{
	struct exynos_drm_gem *exynos_gem;
	struct drm_gem_object *obj;

	obj = drm_gem_object_lookup(file_priv, gem_handle);
	if (!obj) {
		DRM_ERROR("failed to lookup gem object.\n");
		return 0;
	}

	exynos_gem = to_exynos_gem(obj);

	drm_gem_object_unreference_unlocked(obj);

	return exynos_gem->size;
}

static struct exynos_drm_gem *exynos_drm_gem_init(struct drm_device *dev,
						  unsigned long size)
{
	struct exynos_drm_gem *exynos_gem;
	struct drm_gem_object *obj;
	int ret;

	exynos_gem = kzalloc(sizeof(*exynos_gem), GFP_KERNEL);
	if (!exynos_gem)
		return ERR_PTR(-ENOMEM);

	exynos_gem->size = size;
	obj = &exynos_gem->base;

	ret = drm_gem_object_init(dev, obj, size);
	if (ret < 0) {
		DRM_ERROR("failed to initialize gem object\n");
		kfree(exynos_gem);
		return ERR_PTR(ret);
	}

	ret = drm_gem_create_mmap_offset(obj);
	if (ret < 0) {
		drm_gem_object_release(obj);
		kfree(exynos_gem);
		return ERR_PTR(ret);
	}

	DRM_DEBUG_KMS("created file object = %p\n", obj->filp);

	return exynos_gem;
}

struct exynos_drm_gem *exynos_drm_gem_create(struct drm_device *dev,
					     unsigned int flags,
					     unsigned long size)
{
	struct exynos_drm_gem *exynos_gem;
	int ret;

	if (flags & ~(EXYNOS_BO_MASK)) {
		DRM_ERROR("invalid GEM buffer flags: %u\n", flags);
		return ERR_PTR(-EINVAL);
	}

	if (!size) {
		DRM_ERROR("invalid GEM buffer size: %lu\n", size);
		return ERR_PTR(-EINVAL);
	}

	size = roundup(size, PAGE_SIZE);

	exynos_gem = exynos_drm_gem_init(dev, size);
	if (IS_ERR(exynos_gem))
		return exynos_gem;

	/* set memory type and cache attribute from user side. */
	exynos_gem->flags = flags;

	ret = exynos_drm_alloc_buf(exynos_gem);
	if (ret < 0) {
		drm_gem_object_release(&exynos_gem->base);
		kfree(exynos_gem);
		return ERR_PTR(ret);
	}

	return exynos_gem;
}

int exynos_drm_gem_create_ioctl(struct drm_device *dev, void *data,
				struct drm_file *file_priv)
{
	struct drm_exynos_gem_create *args = data;
	struct exynos_drm_gem *exynos_gem;
	int ret;

	exynos_gem = exynos_drm_gem_create(dev, args->flags, args->size);
	if (IS_ERR(exynos_gem))
		return PTR_ERR(exynos_gem);

	ret = exynos_drm_gem_handle_create(&exynos_gem->base, file_priv,
					   &args->handle);
	if (ret) {
		exynos_drm_gem_destroy(exynos_gem);
		return ret;
	}

	return 0;
}

int exynos_drm_gem_map_ioctl(struct drm_device *dev, void *data,
			     struct drm_file *file_priv)
{
	struct drm_exynos_gem_map *args = data;

	return exynos_drm_gem_dumb_map_offset(file_priv, dev, args->handle,
					      &args->offset);
}

dma_addr_t *exynos_drm_gem_get_dma_addr(struct drm_device *dev,
					unsigned int gem_handle,
					struct drm_file *filp)
{
	struct exynos_drm_gem *exynos_gem;
	struct drm_gem_object *obj;

	obj = drm_gem_object_lookup(filp, gem_handle);
	if (!obj) {
		DRM_ERROR("failed to lookup gem object.\n");
		return ERR_PTR(-EINVAL);
	}

	exynos_gem = to_exynos_gem(obj);

	return &exynos_gem->dma_addr;
}

void exynos_drm_gem_put_dma_addr(struct drm_device *dev,
					unsigned int gem_handle,
					struct drm_file *filp)
{
	struct drm_gem_object *obj;

	obj = drm_gem_object_lookup(filp, gem_handle);
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

static int exynos_drm_gem_mmap_buffer(struct exynos_drm_gem *exynos_gem,
				      struct vm_area_struct *vma)
{
	struct drm_device *drm_dev = exynos_gem->base.dev;
	unsigned long vm_size;
	int ret;

	vma->vm_flags &= ~VM_PFNMAP;
	vma->vm_pgoff = 0;

	vm_size = vma->vm_end - vma->vm_start;

	/* check if user-requested size is valid. */
	if (vm_size > exynos_gem->size)
		return -EINVAL;

	ret = dma_mmap_attrs(to_dma_dev(drm_dev), vma, exynos_gem->cookie,
			     exynos_gem->dma_addr, exynos_gem->size,
			     exynos_gem->dma_attrs);
	if (ret < 0) {
		DRM_ERROR("failed to mmap.\n");
		return ret;
	}

	return 0;
}

int exynos_drm_gem_get_ioctl(struct drm_device *dev, void *data,
				      struct drm_file *file_priv)
{
	struct exynos_drm_gem *exynos_gem;
	struct drm_exynos_gem_info *args = data;
	struct drm_gem_object *obj;

	obj = drm_gem_object_lookup(file_priv, args->handle);
	if (!obj) {
		DRM_ERROR("failed to lookup gem object.\n");
		return -EINVAL;
	}

	exynos_gem = to_exynos_gem(obj);

	args->flags = exynos_gem->flags;
	args->size = exynos_gem->size;

	drm_gem_object_unreference_unlocked(obj);

	return 0;
}

void exynos_drm_gem_free_object(struct drm_gem_object *obj)
{
	exynos_drm_gem_destroy(to_exynos_gem(obj));
}

int exynos_drm_gem_dumb_create(struct drm_file *file_priv,
			       struct drm_device *dev,
			       struct drm_mode_create_dumb *args)
{
	struct exynos_drm_gem *exynos_gem;
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

	exynos_gem = exynos_drm_gem_create(dev, flags, args->size);
	if (IS_ERR(exynos_gem)) {
		dev_warn(dev->dev, "FB allocation failed.\n");
		return PTR_ERR(exynos_gem);
	}

	ret = exynos_drm_gem_handle_create(&exynos_gem->base, file_priv,
					   &args->handle);
	if (ret) {
		exynos_drm_gem_destroy(exynos_gem);
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

	/*
	 * get offset of memory allocated for drm framebuffer.
	 * - this callback would be called by user application
	 *	with DRM_IOCTL_MODE_MAP_DUMB command.
	 */

	obj = drm_gem_object_lookup(file_priv, handle);
	if (!obj) {
		DRM_ERROR("failed to lookup gem object.\n");
		return -EINVAL;
	}

	*offset = drm_vma_node_offset_addr(&obj->vma_node);
	DRM_DEBUG_KMS("offset = 0x%lx\n", (unsigned long)*offset);

	drm_gem_object_unreference_unlocked(obj);
	return ret;
}

int exynos_drm_gem_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct drm_gem_object *obj = vma->vm_private_data;
	struct exynos_drm_gem *exynos_gem = to_exynos_gem(obj);
	unsigned long pfn;
	pgoff_t page_offset;
	int ret;

	page_offset = ((unsigned long)vmf->virtual_address -
			vma->vm_start) >> PAGE_SHIFT;

	if (page_offset >= (exynos_gem->size >> PAGE_SHIFT)) {
		DRM_ERROR("invalid page offset\n");
		ret = -EINVAL;
		goto out;
	}

	pfn = page_to_pfn(exynos_gem->pages[page_offset]);
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

static int exynos_drm_gem_mmap_obj(struct drm_gem_object *obj,
				   struct vm_area_struct *vma)
{
	struct exynos_drm_gem *exynos_gem = to_exynos_gem(obj);
	int ret;

	DRM_DEBUG_KMS("flags = 0x%x\n", exynos_gem->flags);

	/* non-cachable as default. */
	if (exynos_gem->flags & EXYNOS_BO_CACHABLE)
		vma->vm_page_prot = vm_get_page_prot(vma->vm_flags);
	else if (exynos_gem->flags & EXYNOS_BO_WC)
		vma->vm_page_prot =
			pgprot_writecombine(vm_get_page_prot(vma->vm_flags));
	else
		vma->vm_page_prot =
			pgprot_noncached(vm_get_page_prot(vma->vm_flags));

	ret = exynos_drm_gem_mmap_buffer(exynos_gem, vma);
	if (ret)
		goto err_close_vm;

	return ret;

err_close_vm:
	drm_gem_vm_close(vma);

	return ret;
}

int exynos_drm_gem_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct drm_gem_object *obj;
	int ret;

	/* set vm_area_struct. */
	ret = drm_gem_mmap(filp, vma);
	if (ret < 0) {
		DRM_ERROR("failed to mmap.\n");
		return ret;
	}

	obj = vma->vm_private_data;

	if (obj->import_attach)
		return dma_buf_mmap(obj->dma_buf, vma, 0);

	return exynos_drm_gem_mmap_obj(obj, vma);
}

/* low-level interface prime helpers */
struct sg_table *exynos_drm_gem_prime_get_sg_table(struct drm_gem_object *obj)
{
	struct exynos_drm_gem *exynos_gem = to_exynos_gem(obj);
	int npages;

	npages = exynos_gem->size >> PAGE_SHIFT;

	return drm_prime_pages_to_sg(exynos_gem->pages, npages);
}

struct drm_gem_object *
exynos_drm_gem_prime_import_sg_table(struct drm_device *dev,
				     struct dma_buf_attachment *attach,
				     struct sg_table *sgt)
{
	struct exynos_drm_gem *exynos_gem;
	int npages;
	int ret;

	exynos_gem = exynos_drm_gem_init(dev, attach->dmabuf->size);
	if (IS_ERR(exynos_gem)) {
		ret = PTR_ERR(exynos_gem);
		return ERR_PTR(ret);
	}

	exynos_gem->dma_addr = sg_dma_address(sgt->sgl);

	npages = exynos_gem->size >> PAGE_SHIFT;
	exynos_gem->pages = drm_malloc_ab(npages, sizeof(struct page *));
	if (!exynos_gem->pages) {
		ret = -ENOMEM;
		goto err;
	}

	ret = drm_prime_sg_to_page_addr_arrays(sgt, exynos_gem->pages, NULL,
					       npages);
	if (ret < 0)
		goto err_free_large;

	exynos_gem->sgt = sgt;

	if (sgt->nents == 1) {
		/* always physically continuous memory if sgt->nents is 1. */
		exynos_gem->flags |= EXYNOS_BO_CONTIG;
	} else {
		/*
		 * this case could be CONTIG or NONCONTIG type but for now
		 * sets NONCONTIG.
		 * TODO. we have to find a way that exporter can notify
		 * the type of its own buffer to importer.
		 */
		exynos_gem->flags |= EXYNOS_BO_NONCONTIG;
	}

	return &exynos_gem->base;

err_free_large:
	drm_free_large(exynos_gem->pages);
err:
	drm_gem_object_release(&exynos_gem->base);
	kfree(exynos_gem);
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

int exynos_drm_gem_prime_mmap(struct drm_gem_object *obj,
			      struct vm_area_struct *vma)
{
	int ret;

	ret = drm_gem_mmap_obj(obj, obj->size, vma);
	if (ret < 0)
		return ret;

	return exynos_drm_gem_mmap_obj(obj, vma);
}
