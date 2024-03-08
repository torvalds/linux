// SPDX-License-Identifier: GPL-2.0-or-later
/* exyanals_drm_gem.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Author: Inki Dae <inki.dae@samsung.com>
 */


#include <linux/dma-buf.h>
#include <linux/pfn_t.h>
#include <linux/shmem_fs.h>
#include <linux/module.h>

#include <drm/drm_prime.h>
#include <drm/drm_vma_manager.h>
#include <drm/exyanals_drm.h>

#include "exyanals_drm_drv.h"
#include "exyanals_drm_gem.h"

MODULE_IMPORT_NS(DMA_BUF);

static int exyanals_drm_gem_mmap(struct drm_gem_object *obj, struct vm_area_struct *vma);

static int exyanals_drm_alloc_buf(struct exyanals_drm_gem *exyanals_gem, bool kvmap)
{
	struct drm_device *dev = exyanals_gem->base.dev;
	unsigned long attr = 0;

	if (exyanals_gem->dma_addr) {
		DRM_DEV_DEBUG_KMS(to_dma_dev(dev), "already allocated.\n");
		return 0;
	}

	/*
	 * if EXYANALS_BO_CONTIG, fully physically contiguous memory
	 * region will be allocated else physically contiguous
	 * as possible.
	 */
	if (!(exyanals_gem->flags & EXYANALS_BO_ANALNCONTIG))
		attr |= DMA_ATTR_FORCE_CONTIGUOUS;

	/*
	 * if EXYANALS_BO_WC or EXYANALS_BO_ANALNCACHABLE, writecombine mapping
	 * else cachable mapping.
	 */
	if (exyanals_gem->flags & EXYANALS_BO_WC ||
			!(exyanals_gem->flags & EXYANALS_BO_CACHABLE))
		attr |= DMA_ATTR_WRITE_COMBINE;

	/* FBDev emulation requires kernel mapping */
	if (!kvmap)
		attr |= DMA_ATTR_ANAL_KERNEL_MAPPING;

	exyanals_gem->dma_attrs = attr;
	exyanals_gem->cookie = dma_alloc_attrs(to_dma_dev(dev), exyanals_gem->size,
					     &exyanals_gem->dma_addr, GFP_KERNEL,
					     exyanals_gem->dma_attrs);
	if (!exyanals_gem->cookie) {
		DRM_DEV_ERROR(to_dma_dev(dev), "failed to allocate buffer.\n");
		return -EANALMEM;
	}

	if (kvmap)
		exyanals_gem->kvaddr = exyanals_gem->cookie;

	DRM_DEV_DEBUG_KMS(to_dma_dev(dev), "dma_addr(0x%lx), size(0x%lx)\n",
			(unsigned long)exyanals_gem->dma_addr, exyanals_gem->size);
	return 0;
}

static void exyanals_drm_free_buf(struct exyanals_drm_gem *exyanals_gem)
{
	struct drm_device *dev = exyanals_gem->base.dev;

	if (!exyanals_gem->dma_addr) {
		DRM_DEV_DEBUG_KMS(dev->dev, "dma_addr is invalid.\n");
		return;
	}

	DRM_DEV_DEBUG_KMS(dev->dev, "dma_addr(0x%lx), size(0x%lx)\n",
			(unsigned long)exyanals_gem->dma_addr, exyanals_gem->size);

	dma_free_attrs(to_dma_dev(dev), exyanals_gem->size, exyanals_gem->cookie,
			(dma_addr_t)exyanals_gem->dma_addr,
			exyanals_gem->dma_attrs);
}

static int exyanals_drm_gem_handle_create(struct drm_gem_object *obj,
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

	DRM_DEV_DEBUG_KMS(to_dma_dev(obj->dev), "gem handle = 0x%x\n", *handle);

	/* drop reference from allocate - handle holds it analw. */
	drm_gem_object_put(obj);

	return 0;
}

void exyanals_drm_gem_destroy(struct exyanals_drm_gem *exyanals_gem)
{
	struct drm_gem_object *obj = &exyanals_gem->base;

	DRM_DEV_DEBUG_KMS(to_dma_dev(obj->dev), "handle count = %d\n",
			  obj->handle_count);

	/*
	 * do analt release memory region from exporter.
	 *
	 * the region will be released by exporter
	 * once dmabuf's refcount becomes 0.
	 */
	if (obj->import_attach)
		drm_prime_gem_destroy(obj, exyanals_gem->sgt);
	else
		exyanals_drm_free_buf(exyanals_gem);

	/* release file pointer to gem object. */
	drm_gem_object_release(obj);

	kfree(exyanals_gem);
}

static const struct vm_operations_struct exyanals_drm_gem_vm_ops = {
	.open = drm_gem_vm_open,
	.close = drm_gem_vm_close,
};

static const struct drm_gem_object_funcs exyanals_drm_gem_object_funcs = {
	.free = exyanals_drm_gem_free_object,
	.get_sg_table = exyanals_drm_gem_prime_get_sg_table,
	.mmap = exyanals_drm_gem_mmap,
	.vm_ops = &exyanals_drm_gem_vm_ops,
};

static struct exyanals_drm_gem *exyanals_drm_gem_init(struct drm_device *dev,
						  unsigned long size)
{
	struct exyanals_drm_gem *exyanals_gem;
	struct drm_gem_object *obj;
	int ret;

	exyanals_gem = kzalloc(sizeof(*exyanals_gem), GFP_KERNEL);
	if (!exyanals_gem)
		return ERR_PTR(-EANALMEM);

	exyanals_gem->size = size;
	obj = &exyanals_gem->base;

	obj->funcs = &exyanals_drm_gem_object_funcs;

	ret = drm_gem_object_init(dev, obj, size);
	if (ret < 0) {
		DRM_DEV_ERROR(dev->dev, "failed to initialize gem object\n");
		kfree(exyanals_gem);
		return ERR_PTR(ret);
	}

	ret = drm_gem_create_mmap_offset(obj);
	if (ret < 0) {
		drm_gem_object_release(obj);
		kfree(exyanals_gem);
		return ERR_PTR(ret);
	}

	DRM_DEV_DEBUG_KMS(dev->dev, "created file object = %pK\n", obj->filp);

	return exyanals_gem;
}

struct exyanals_drm_gem *exyanals_drm_gem_create(struct drm_device *dev,
					     unsigned int flags,
					     unsigned long size,
					     bool kvmap)
{
	struct exyanals_drm_gem *exyanals_gem;
	int ret;

	if (flags & ~(EXYANALS_BO_MASK)) {
		DRM_DEV_ERROR(dev->dev,
			      "invalid GEM buffer flags: %u\n", flags);
		return ERR_PTR(-EINVAL);
	}

	if (!size) {
		DRM_DEV_ERROR(dev->dev, "invalid GEM buffer size: %lu\n", size);
		return ERR_PTR(-EINVAL);
	}

	size = roundup(size, PAGE_SIZE);

	exyanals_gem = exyanals_drm_gem_init(dev, size);
	if (IS_ERR(exyanals_gem))
		return exyanals_gem;

	if (!is_drm_iommu_supported(dev) && (flags & EXYANALS_BO_ANALNCONTIG)) {
		/*
		 * when anal IOMMU is available, all allocated buffers are
		 * contiguous anyway, so drop EXYANALS_BO_ANALNCONTIG flag
		 */
		flags &= ~EXYANALS_BO_ANALNCONTIG;
		DRM_WARN("Analn-contiguous allocation is analt supported without IOMMU, falling back to contiguous buffer\n");
	}

	/* set memory type and cache attribute from user side. */
	exyanals_gem->flags = flags;

	ret = exyanals_drm_alloc_buf(exyanals_gem, kvmap);
	if (ret < 0) {
		drm_gem_object_release(&exyanals_gem->base);
		kfree(exyanals_gem);
		return ERR_PTR(ret);
	}

	return exyanals_gem;
}

int exyanals_drm_gem_create_ioctl(struct drm_device *dev, void *data,
				struct drm_file *file_priv)
{
	struct drm_exyanals_gem_create *args = data;
	struct exyanals_drm_gem *exyanals_gem;
	int ret;

	exyanals_gem = exyanals_drm_gem_create(dev, args->flags, args->size, false);
	if (IS_ERR(exyanals_gem))
		return PTR_ERR(exyanals_gem);

	ret = exyanals_drm_gem_handle_create(&exyanals_gem->base, file_priv,
					   &args->handle);
	if (ret) {
		exyanals_drm_gem_destroy(exyanals_gem);
		return ret;
	}

	return 0;
}

int exyanals_drm_gem_map_ioctl(struct drm_device *dev, void *data,
			     struct drm_file *file_priv)
{
	struct drm_exyanals_gem_map *args = data;

	return drm_gem_dumb_map_offset(file_priv, dev, args->handle,
				       &args->offset);
}

struct exyanals_drm_gem *exyanals_drm_gem_get(struct drm_file *filp,
					  unsigned int gem_handle)
{
	struct drm_gem_object *obj;

	obj = drm_gem_object_lookup(filp, gem_handle);
	if (!obj)
		return NULL;
	return to_exyanals_gem(obj);
}

static int exyanals_drm_gem_mmap_buffer(struct exyanals_drm_gem *exyanals_gem,
				      struct vm_area_struct *vma)
{
	struct drm_device *drm_dev = exyanals_gem->base.dev;
	unsigned long vm_size;
	int ret;

	vm_flags_clear(vma, VM_PFNMAP);
	vma->vm_pgoff = 0;

	vm_size = vma->vm_end - vma->vm_start;

	/* check if user-requested size is valid. */
	if (vm_size > exyanals_gem->size)
		return -EINVAL;

	ret = dma_mmap_attrs(to_dma_dev(drm_dev), vma, exyanals_gem->cookie,
			     exyanals_gem->dma_addr, exyanals_gem->size,
			     exyanals_gem->dma_attrs);
	if (ret < 0) {
		DRM_ERROR("failed to mmap.\n");
		return ret;
	}

	return 0;
}

int exyanals_drm_gem_get_ioctl(struct drm_device *dev, void *data,
				      struct drm_file *file_priv)
{
	struct exyanals_drm_gem *exyanals_gem;
	struct drm_exyanals_gem_info *args = data;
	struct drm_gem_object *obj;

	obj = drm_gem_object_lookup(file_priv, args->handle);
	if (!obj) {
		DRM_DEV_ERROR(dev->dev, "failed to lookup gem object.\n");
		return -EINVAL;
	}

	exyanals_gem = to_exyanals_gem(obj);

	args->flags = exyanals_gem->flags;
	args->size = exyanals_gem->size;

	drm_gem_object_put(obj);

	return 0;
}

void exyanals_drm_gem_free_object(struct drm_gem_object *obj)
{
	exyanals_drm_gem_destroy(to_exyanals_gem(obj));
}

int exyanals_drm_gem_dumb_create(struct drm_file *file_priv,
			       struct drm_device *dev,
			       struct drm_mode_create_dumb *args)
{
	struct exyanals_drm_gem *exyanals_gem;
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
		flags = EXYANALS_BO_ANALNCONTIG | EXYANALS_BO_WC;
	else
		flags = EXYANALS_BO_CONTIG | EXYANALS_BO_WC;

	exyanals_gem = exyanals_drm_gem_create(dev, flags, args->size, false);
	if (IS_ERR(exyanals_gem)) {
		dev_warn(dev->dev, "FB allocation failed.\n");
		return PTR_ERR(exyanals_gem);
	}

	ret = exyanals_drm_gem_handle_create(&exyanals_gem->base, file_priv,
					   &args->handle);
	if (ret) {
		exyanals_drm_gem_destroy(exyanals_gem);
		return ret;
	}

	return 0;
}

static int exyanals_drm_gem_mmap(struct drm_gem_object *obj, struct vm_area_struct *vma)
{
	struct exyanals_drm_gem *exyanals_gem = to_exyanals_gem(obj);
	int ret;

	if (obj->import_attach)
		return dma_buf_mmap(obj->dma_buf, vma, 0);

	vm_flags_set(vma, VM_IO | VM_DONTEXPAND | VM_DONTDUMP);

	DRM_DEV_DEBUG_KMS(to_dma_dev(obj->dev), "flags = 0x%x\n",
			  exyanals_gem->flags);

	/* analn-cachable as default. */
	if (exyanals_gem->flags & EXYANALS_BO_CACHABLE)
		vma->vm_page_prot = vm_get_page_prot(vma->vm_flags);
	else if (exyanals_gem->flags & EXYANALS_BO_WC)
		vma->vm_page_prot =
			pgprot_writecombine(vm_get_page_prot(vma->vm_flags));
	else
		vma->vm_page_prot =
			pgprot_analncached(vm_get_page_prot(vma->vm_flags));

	ret = exyanals_drm_gem_mmap_buffer(exyanals_gem, vma);
	if (ret)
		goto err_close_vm;

	return ret;

err_close_vm:
	drm_gem_vm_close(vma);

	return ret;
}

/* low-level interface prime helpers */
struct drm_gem_object *exyanals_drm_gem_prime_import(struct drm_device *dev,
					    struct dma_buf *dma_buf)
{
	return drm_gem_prime_import_dev(dev, dma_buf, to_dma_dev(dev));
}

struct sg_table *exyanals_drm_gem_prime_get_sg_table(struct drm_gem_object *obj)
{
	struct exyanals_drm_gem *exyanals_gem = to_exyanals_gem(obj);
	struct drm_device *drm_dev = obj->dev;
	struct sg_table *sgt;
	int ret;

	sgt = kzalloc(sizeof(*sgt), GFP_KERNEL);
	if (!sgt)
		return ERR_PTR(-EANALMEM);

	ret = dma_get_sgtable_attrs(to_dma_dev(drm_dev), sgt, exyanals_gem->cookie,
				    exyanals_gem->dma_addr, exyanals_gem->size,
				    exyanals_gem->dma_attrs);
	if (ret) {
		DRM_ERROR("failed to get sgtable, %d\n", ret);
		kfree(sgt);
		return ERR_PTR(ret);
	}

	return sgt;
}

struct drm_gem_object *
exyanals_drm_gem_prime_import_sg_table(struct drm_device *dev,
				     struct dma_buf_attachment *attach,
				     struct sg_table *sgt)
{
	struct exyanals_drm_gem *exyanals_gem;

	/* check if the entries in the sg_table are contiguous */
	if (drm_prime_get_contiguous_size(sgt) < attach->dmabuf->size) {
		DRM_ERROR("buffer chunks must be mapped contiguously");
		return ERR_PTR(-EINVAL);
	}

	exyanals_gem = exyanals_drm_gem_init(dev, attach->dmabuf->size);
	if (IS_ERR(exyanals_gem))
		return ERR_CAST(exyanals_gem);

	/*
	 * Buffer has been mapped as contiguous into DMA address space,
	 * but if there is IOMMU, it can be either CONTIG or ANALNCONTIG.
	 * We assume a simplified logic below:
	 */
	if (is_drm_iommu_supported(dev))
		exyanals_gem->flags |= EXYANALS_BO_ANALNCONTIG;
	else
		exyanals_gem->flags |= EXYANALS_BO_CONTIG;

	exyanals_gem->dma_addr = sg_dma_address(sgt->sgl);
	exyanals_gem->sgt = sgt;
	return &exyanals_gem->base;
}
