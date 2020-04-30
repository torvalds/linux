// SPDX-License-Identifier: GPL-2.0-or-later
/* exynos_drm_gem.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Author: Inki Dae <inki.dae@samsung.com>
 */


#include <linux/dma-buf.h>
#include <linux/pfn_t.h>
#include <linux/shmem_fs.h>

#include <drm/drm_prime.h>
#include <drm/drm_vma_manager.h>
#include <drm/exynos_drm.h>

#include "exynos_drm_drv.h"
#include "exynos_drm_gem.h"

static int exynos_drm_alloc_buf(struct exynos_drm_gem *exynos_gem, bool kvmap)
{
	struct drm_device *dev = exynos_gem->base.dev;
	unsigned long attr = 0;

	if (exynos_gem->dma_addr) {
		DRM_DEV_DEBUG_KMS(to_dma_dev(dev), "already allocated.\n");
		return 0;
	}

	/*
	 * if EXYNOS_BO_CONTIG, fully physically contiguous memory
	 * region will be allocated else physically contiguous
	 * as possible.
	 */
	if (!(exynos_gem->flags & EXYNOS_BO_NONCONTIG))
		attr |= DMA_ATTR_FORCE_CONTIGUOUS;

	/*
	 * if EXYNOS_BO_WC or EXYNOS_BO_NONCACHABLE, writecombine mapping
	 * else cachable mapping.
	 */
	if (exynos_gem->flags & EXYNOS_BO_WC ||
			!(exynos_gem->flags & EXYNOS_BO_CACHABLE))
		attr |= DMA_ATTR_WRITE_COMBINE;
	else
		attr |= DMA_ATTR_NON_CONSISTENT;

	/* FBDev emulation requires kernel mapping */
	if (!kvmap)
		attr |= DMA_ATTR_NO_KERNEL_MAPPING;

	exynos_gem->dma_attrs = attr;
	exynos_gem->cookie = dma_alloc_attrs(to_dma_dev(dev), exynos_gem->size,
					     &exynos_gem->dma_addr, GFP_KERNEL,
					     exynos_gem->dma_attrs);
	if (!exynos_gem->cookie) {
		DRM_DEV_ERROR(to_dma_dev(dev), "failed to allocate buffer.\n");
		return -ENOMEM;
	}

	if (kvmap)
		exynos_gem->kvaddr = exynos_gem->cookie;

	DRM_DEV_DEBUG_KMS(to_dma_dev(dev), "dma_addr(0x%lx), size(0x%lx)\n",
			(unsigned long)exynos_gem->dma_addr, exynos_gem->size);
	return 0;
}

static void exynos_drm_free_buf(struct exynos_drm_gem *exynos_gem)
{
	struct drm_device *dev = exynos_gem->base.dev;

	if (!exynos_gem->dma_addr) {
		DRM_DEV_DEBUG_KMS(dev->dev, "dma_addr is invalid.\n");
		return;
	}

	DRM_DEV_DEBUG_KMS(dev->dev, "dma_addr(0x%lx), size(0x%lx)\n",
			(unsigned long)exynos_gem->dma_addr, exynos_gem->size);

	dma_free_attrs(to_dma_dev(dev), exynos_gem->size, exynos_gem->cookie,
			(dma_addr_t)exynos_gem->dma_addr,
			exynos_gem->dma_attrs);
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

	DRM_DEV_DEBUG_KMS(to_dma_dev(obj->dev), "gem handle = 0x%x\n", *handle);

	/* drop reference from allocate - handle holds it now. */
	drm_gem_object_put(obj);

	return 0;
}

void exynos_drm_gem_destroy(struct exynos_drm_gem *exynos_gem)
{
	struct drm_gem_object *obj = &exynos_gem->base;

	DRM_DEV_DEBUG_KMS(to_dma_dev(obj->dev), "handle count = %d\n",
			  obj->handle_count);

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
		DRM_DEV_ERROR(dev->dev, "failed to initialize gem object\n");
		kfree(exynos_gem);
		return ERR_PTR(ret);
	}

	ret = drm_gem_create_mmap_offset(obj);
	if (ret < 0) {
		drm_gem_object_release(obj);
		kfree(exynos_gem);
		return ERR_PTR(ret);
	}

	DRM_DEV_DEBUG_KMS(dev->dev, "created file object = %pK\n", obj->filp);

	return exynos_gem;
}

struct exynos_drm_gem *exynos_drm_gem_create(struct drm_device *dev,
					     unsigned int flags,
					     unsigned long size,
					     bool kvmap)
{
	struct exynos_drm_gem *exynos_gem;
	int ret;

	if (flags & ~(EXYNOS_BO_MASK)) {
		DRM_DEV_ERROR(dev->dev,
			      "invalid GEM buffer flags: %u\n", flags);
		return ERR_PTR(-EINVAL);
	}

	if (!size) {
		DRM_DEV_ERROR(dev->dev, "invalid GEM buffer size: %lu\n", size);
		return ERR_PTR(-EINVAL);
	}

	size = roundup(size, PAGE_SIZE);

	exynos_gem = exynos_drm_gem_init(dev, size);
	if (IS_ERR(exynos_gem))
		return exynos_gem;

	if (!is_drm_iommu_supported(dev) && (flags & EXYNOS_BO_NONCONTIG)) {
		/*
		 * when no IOMMU is available, all allocated buffers are
		 * contiguous anyway, so drop EXYNOS_BO_NONCONTIG flag
		 */
		flags &= ~EXYNOS_BO_NONCONTIG;
		DRM_WARN("Non-contiguous allocation is not supported without IOMMU, falling back to contiguous buffer\n");
	}

	/* set memory type and cache attribute from user side. */
	exynos_gem->flags = flags;

	ret = exynos_drm_alloc_buf(exynos_gem, kvmap);
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

	exynos_gem = exynos_drm_gem_create(dev, args->flags, args->size, false);
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

	return drm_gem_dumb_map_offset(file_priv, dev, args->handle,
				       &args->offset);
}

struct exynos_drm_gem *exynos_drm_gem_get(struct drm_file *filp,
					  unsigned int gem_handle)
{
	struct drm_gem_object *obj;

	obj = drm_gem_object_lookup(filp, gem_handle);
	if (!obj)
		return NULL;
	return to_exynos_gem(obj);
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
		DRM_DEV_ERROR(dev->dev, "failed to lookup gem object.\n");
		return -EINVAL;
	}

	exynos_gem = to_exynos_gem(obj);

	args->flags = exynos_gem->flags;
	args->size = exynos_gem->size;

	drm_gem_object_put(obj);

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

	exynos_gem = exynos_drm_gem_create(dev, flags, args->size, false);
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

static int exynos_drm_gem_mmap_obj(struct drm_gem_object *obj,
				   struct vm_area_struct *vma)
{
	struct exynos_drm_gem *exynos_gem = to_exynos_gem(obj);
	int ret;

	DRM_DEV_DEBUG_KMS(to_dma_dev(obj->dev), "flags = 0x%x\n",
			  exynos_gem->flags);

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
struct drm_gem_object *exynos_drm_gem_prime_import(struct drm_device *dev,
					    struct dma_buf *dma_buf)
{
	return drm_gem_prime_import_dev(dev, dma_buf, to_dma_dev(dev));
}

struct sg_table *exynos_drm_gem_prime_get_sg_table(struct drm_gem_object *obj)
{
	struct exynos_drm_gem *exynos_gem = to_exynos_gem(obj);
	struct drm_device *drm_dev = obj->dev;
	struct sg_table *sgt;
	int ret;

	sgt = kzalloc(sizeof(*sgt), GFP_KERNEL);
	if (!sgt)
		return ERR_PTR(-ENOMEM);

	ret = dma_get_sgtable_attrs(to_dma_dev(drm_dev), sgt, exynos_gem->cookie,
				    exynos_gem->dma_addr, exynos_gem->size,
				    exynos_gem->dma_attrs);
	if (ret) {
		DRM_ERROR("failed to get sgtable, %d\n", ret);
		kfree(sgt);
		return ERR_PTR(ret);
	}

	return sgt;
}

struct drm_gem_object *
exynos_drm_gem_prime_import_sg_table(struct drm_device *dev,
				     struct dma_buf_attachment *attach,
				     struct sg_table *sgt)
{
	struct exynos_drm_gem *exynos_gem;

	/* check if the entries in the sg_table are contiguous */
	if (drm_prime_get_contiguous_size(sgt) < attach->dmabuf->size) {
		DRM_ERROR("buffer chunks must be mapped contiguously");
		return ERR_PTR(-EINVAL);
	}

	exynos_gem = exynos_drm_gem_init(dev, attach->dmabuf->size);
	if (IS_ERR(exynos_gem))
		return ERR_CAST(exynos_gem);

	/*
	 * Buffer has been mapped as contiguous into DMA address space,
	 * but if there is IOMMU, it can be either CONTIG or NONCONTIG.
	 * We assume a simplified logic below:
	 */
	if (is_drm_iommu_supported(dev))
		exynos_gem->flags |= EXYNOS_BO_NONCONTIG;
	else
		exynos_gem->flags |= EXYNOS_BO_CONTIG;

	exynos_gem->dma_addr = sg_dma_address(sgt->sgl);
	exynos_gem->sgt = sgt;
	return &exynos_gem->base;
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
