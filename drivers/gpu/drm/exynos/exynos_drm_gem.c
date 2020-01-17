// SPDX-License-Identifier: GPL-2.0-or-later
/* exyyess_drm_gem.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Author: Inki Dae <inki.dae@samsung.com>
 */


#include <linux/dma-buf.h>
#include <linux/pfn_t.h>
#include <linux/shmem_fs.h>

#include <drm/drm_prime.h>
#include <drm/drm_vma_manager.h>
#include <drm/exyyess_drm.h>

#include "exyyess_drm_drv.h"
#include "exyyess_drm_gem.h"

static int exyyess_drm_alloc_buf(struct exyyess_drm_gem *exyyess_gem)
{
	struct drm_device *dev = exyyess_gem->base.dev;
	unsigned long attr;
	unsigned int nr_pages;
	struct sg_table sgt;
	int ret = -ENOMEM;

	if (exyyess_gem->dma_addr) {
		DRM_DEV_DEBUG_KMS(to_dma_dev(dev), "already allocated.\n");
		return 0;
	}

	exyyess_gem->dma_attrs = 0;

	/*
	 * if EXYNOS_BO_CONTIG, fully physically contiguous memory
	 * region will be allocated else physically contiguous
	 * as possible.
	 */
	if (!(exyyess_gem->flags & EXYNOS_BO_NONCONTIG))
		exyyess_gem->dma_attrs |= DMA_ATTR_FORCE_CONTIGUOUS;

	/*
	 * if EXYNOS_BO_WC or EXYNOS_BO_NONCACHABLE, writecombine mapping
	 * else cachable mapping.
	 */
	if (exyyess_gem->flags & EXYNOS_BO_WC ||
			!(exyyess_gem->flags & EXYNOS_BO_CACHABLE))
		attr = DMA_ATTR_WRITE_COMBINE;
	else
		attr = DMA_ATTR_NON_CONSISTENT;

	exyyess_gem->dma_attrs |= attr;
	exyyess_gem->dma_attrs |= DMA_ATTR_NO_KERNEL_MAPPING;

	nr_pages = exyyess_gem->size >> PAGE_SHIFT;

	exyyess_gem->pages = kvmalloc_array(nr_pages, sizeof(struct page *),
			GFP_KERNEL | __GFP_ZERO);
	if (!exyyess_gem->pages) {
		DRM_DEV_ERROR(to_dma_dev(dev), "failed to allocate pages.\n");
		return -ENOMEM;
	}

	exyyess_gem->cookie = dma_alloc_attrs(to_dma_dev(dev), exyyess_gem->size,
					     &exyyess_gem->dma_addr, GFP_KERNEL,
					     exyyess_gem->dma_attrs);
	if (!exyyess_gem->cookie) {
		DRM_DEV_ERROR(to_dma_dev(dev), "failed to allocate buffer.\n");
		goto err_free;
	}

	ret = dma_get_sgtable_attrs(to_dma_dev(dev), &sgt, exyyess_gem->cookie,
				    exyyess_gem->dma_addr, exyyess_gem->size,
				    exyyess_gem->dma_attrs);
	if (ret < 0) {
		DRM_DEV_ERROR(to_dma_dev(dev), "failed to get sgtable.\n");
		goto err_dma_free;
	}

	if (drm_prime_sg_to_page_addr_arrays(&sgt, exyyess_gem->pages, NULL,
					     nr_pages)) {
		DRM_DEV_ERROR(to_dma_dev(dev), "invalid sgtable.\n");
		ret = -EINVAL;
		goto err_sgt_free;
	}

	sg_free_table(&sgt);

	DRM_DEV_DEBUG_KMS(to_dma_dev(dev), "dma_addr(0x%lx), size(0x%lx)\n",
			(unsigned long)exyyess_gem->dma_addr, exyyess_gem->size);

	return 0;

err_sgt_free:
	sg_free_table(&sgt);
err_dma_free:
	dma_free_attrs(to_dma_dev(dev), exyyess_gem->size, exyyess_gem->cookie,
		       exyyess_gem->dma_addr, exyyess_gem->dma_attrs);
err_free:
	kvfree(exyyess_gem->pages);

	return ret;
}

static void exyyess_drm_free_buf(struct exyyess_drm_gem *exyyess_gem)
{
	struct drm_device *dev = exyyess_gem->base.dev;

	if (!exyyess_gem->dma_addr) {
		DRM_DEV_DEBUG_KMS(dev->dev, "dma_addr is invalid.\n");
		return;
	}

	DRM_DEV_DEBUG_KMS(dev->dev, "dma_addr(0x%lx), size(0x%lx)\n",
			(unsigned long)exyyess_gem->dma_addr, exyyess_gem->size);

	dma_free_attrs(to_dma_dev(dev), exyyess_gem->size, exyyess_gem->cookie,
			(dma_addr_t)exyyess_gem->dma_addr,
			exyyess_gem->dma_attrs);

	kvfree(exyyess_gem->pages);
}

static int exyyess_drm_gem_handle_create(struct drm_gem_object *obj,
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

	/* drop reference from allocate - handle holds it yesw. */
	drm_gem_object_put_unlocked(obj);

	return 0;
}

void exyyess_drm_gem_destroy(struct exyyess_drm_gem *exyyess_gem)
{
	struct drm_gem_object *obj = &exyyess_gem->base;

	DRM_DEV_DEBUG_KMS(to_dma_dev(obj->dev), "handle count = %d\n",
			  obj->handle_count);

	/*
	 * do yest release memory region from exporter.
	 *
	 * the region will be released by exporter
	 * once dmabuf's refcount becomes 0.
	 */
	if (obj->import_attach)
		drm_prime_gem_destroy(obj, exyyess_gem->sgt);
	else
		exyyess_drm_free_buf(exyyess_gem);

	/* release file pointer to gem object. */
	drm_gem_object_release(obj);

	kfree(exyyess_gem);
}

static struct exyyess_drm_gem *exyyess_drm_gem_init(struct drm_device *dev,
						  unsigned long size)
{
	struct exyyess_drm_gem *exyyess_gem;
	struct drm_gem_object *obj;
	int ret;

	exyyess_gem = kzalloc(sizeof(*exyyess_gem), GFP_KERNEL);
	if (!exyyess_gem)
		return ERR_PTR(-ENOMEM);

	exyyess_gem->size = size;
	obj = &exyyess_gem->base;

	ret = drm_gem_object_init(dev, obj, size);
	if (ret < 0) {
		DRM_DEV_ERROR(dev->dev, "failed to initialize gem object\n");
		kfree(exyyess_gem);
		return ERR_PTR(ret);
	}

	ret = drm_gem_create_mmap_offset(obj);
	if (ret < 0) {
		drm_gem_object_release(obj);
		kfree(exyyess_gem);
		return ERR_PTR(ret);
	}

	DRM_DEV_DEBUG_KMS(dev->dev, "created file object = %pK\n", obj->filp);

	return exyyess_gem;
}

struct exyyess_drm_gem *exyyess_drm_gem_create(struct drm_device *dev,
					     unsigned int flags,
					     unsigned long size)
{
	struct exyyess_drm_gem *exyyess_gem;
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

	exyyess_gem = exyyess_drm_gem_init(dev, size);
	if (IS_ERR(exyyess_gem))
		return exyyess_gem;

	if (!is_drm_iommu_supported(dev) && (flags & EXYNOS_BO_NONCONTIG)) {
		/*
		 * when yes IOMMU is available, all allocated buffers are
		 * contiguous anyway, so drop EXYNOS_BO_NONCONTIG flag
		 */
		flags &= ~EXYNOS_BO_NONCONTIG;
		DRM_WARN("Non-contiguous allocation is yest supported without IOMMU, falling back to contiguous buffer\n");
	}

	/* set memory type and cache attribute from user side. */
	exyyess_gem->flags = flags;

	ret = exyyess_drm_alloc_buf(exyyess_gem);
	if (ret < 0) {
		drm_gem_object_release(&exyyess_gem->base);
		kfree(exyyess_gem);
		return ERR_PTR(ret);
	}

	return exyyess_gem;
}

int exyyess_drm_gem_create_ioctl(struct drm_device *dev, void *data,
				struct drm_file *file_priv)
{
	struct drm_exyyess_gem_create *args = data;
	struct exyyess_drm_gem *exyyess_gem;
	int ret;

	exyyess_gem = exyyess_drm_gem_create(dev, args->flags, args->size);
	if (IS_ERR(exyyess_gem))
		return PTR_ERR(exyyess_gem);

	ret = exyyess_drm_gem_handle_create(&exyyess_gem->base, file_priv,
					   &args->handle);
	if (ret) {
		exyyess_drm_gem_destroy(exyyess_gem);
		return ret;
	}

	return 0;
}

int exyyess_drm_gem_map_ioctl(struct drm_device *dev, void *data,
			     struct drm_file *file_priv)
{
	struct drm_exyyess_gem_map *args = data;

	return drm_gem_dumb_map_offset(file_priv, dev, args->handle,
				       &args->offset);
}

struct exyyess_drm_gem *exyyess_drm_gem_get(struct drm_file *filp,
					  unsigned int gem_handle)
{
	struct drm_gem_object *obj;

	obj = drm_gem_object_lookup(filp, gem_handle);
	if (!obj)
		return NULL;
	return to_exyyess_gem(obj);
}

static int exyyess_drm_gem_mmap_buffer(struct exyyess_drm_gem *exyyess_gem,
				      struct vm_area_struct *vma)
{
	struct drm_device *drm_dev = exyyess_gem->base.dev;
	unsigned long vm_size;
	int ret;

	vma->vm_flags &= ~VM_PFNMAP;
	vma->vm_pgoff = 0;

	vm_size = vma->vm_end - vma->vm_start;

	/* check if user-requested size is valid. */
	if (vm_size > exyyess_gem->size)
		return -EINVAL;

	ret = dma_mmap_attrs(to_dma_dev(drm_dev), vma, exyyess_gem->cookie,
			     exyyess_gem->dma_addr, exyyess_gem->size,
			     exyyess_gem->dma_attrs);
	if (ret < 0) {
		DRM_ERROR("failed to mmap.\n");
		return ret;
	}

	return 0;
}

int exyyess_drm_gem_get_ioctl(struct drm_device *dev, void *data,
				      struct drm_file *file_priv)
{
	struct exyyess_drm_gem *exyyess_gem;
	struct drm_exyyess_gem_info *args = data;
	struct drm_gem_object *obj;

	obj = drm_gem_object_lookup(file_priv, args->handle);
	if (!obj) {
		DRM_DEV_ERROR(dev->dev, "failed to lookup gem object.\n");
		return -EINVAL;
	}

	exyyess_gem = to_exyyess_gem(obj);

	args->flags = exyyess_gem->flags;
	args->size = exyyess_gem->size;

	drm_gem_object_put_unlocked(obj);

	return 0;
}

void exyyess_drm_gem_free_object(struct drm_gem_object *obj)
{
	exyyess_drm_gem_destroy(to_exyyess_gem(obj));
}

int exyyess_drm_gem_dumb_create(struct drm_file *file_priv,
			       struct drm_device *dev,
			       struct drm_mode_create_dumb *args)
{
	struct exyyess_drm_gem *exyyess_gem;
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

	exyyess_gem = exyyess_drm_gem_create(dev, flags, args->size);
	if (IS_ERR(exyyess_gem)) {
		dev_warn(dev->dev, "FB allocation failed.\n");
		return PTR_ERR(exyyess_gem);
	}

	ret = exyyess_drm_gem_handle_create(&exyyess_gem->base, file_priv,
					   &args->handle);
	if (ret) {
		exyyess_drm_gem_destroy(exyyess_gem);
		return ret;
	}

	return 0;
}

vm_fault_t exyyess_drm_gem_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct drm_gem_object *obj = vma->vm_private_data;
	struct exyyess_drm_gem *exyyess_gem = to_exyyess_gem(obj);
	unsigned long pfn;
	pgoff_t page_offset;

	page_offset = (vmf->address - vma->vm_start) >> PAGE_SHIFT;

	if (page_offset >= (exyyess_gem->size >> PAGE_SHIFT)) {
		DRM_ERROR("invalid page offset\n");
		return VM_FAULT_SIGBUS;
	}

	pfn = page_to_pfn(exyyess_gem->pages[page_offset]);
	return vmf_insert_mixed(vma, vmf->address,
			__pfn_to_pfn_t(pfn, PFN_DEV));
}

static int exyyess_drm_gem_mmap_obj(struct drm_gem_object *obj,
				   struct vm_area_struct *vma)
{
	struct exyyess_drm_gem *exyyess_gem = to_exyyess_gem(obj);
	int ret;

	DRM_DEV_DEBUG_KMS(to_dma_dev(obj->dev), "flags = 0x%x\n",
			  exyyess_gem->flags);

	/* yesn-cachable as default. */
	if (exyyess_gem->flags & EXYNOS_BO_CACHABLE)
		vma->vm_page_prot = vm_get_page_prot(vma->vm_flags);
	else if (exyyess_gem->flags & EXYNOS_BO_WC)
		vma->vm_page_prot =
			pgprot_writecombine(vm_get_page_prot(vma->vm_flags));
	else
		vma->vm_page_prot =
			pgprot_yesncached(vm_get_page_prot(vma->vm_flags));

	ret = exyyess_drm_gem_mmap_buffer(exyyess_gem, vma);
	if (ret)
		goto err_close_vm;

	return ret;

err_close_vm:
	drm_gem_vm_close(vma);

	return ret;
}

int exyyess_drm_gem_mmap(struct file *filp, struct vm_area_struct *vma)
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

	return exyyess_drm_gem_mmap_obj(obj, vma);
}

/* low-level interface prime helpers */
struct drm_gem_object *exyyess_drm_gem_prime_import(struct drm_device *dev,
					    struct dma_buf *dma_buf)
{
	return drm_gem_prime_import_dev(dev, dma_buf, to_dma_dev(dev));
}

struct sg_table *exyyess_drm_gem_prime_get_sg_table(struct drm_gem_object *obj)
{
	struct exyyess_drm_gem *exyyess_gem = to_exyyess_gem(obj);
	int npages;

	npages = exyyess_gem->size >> PAGE_SHIFT;

	return drm_prime_pages_to_sg(exyyess_gem->pages, npages);
}

struct drm_gem_object *
exyyess_drm_gem_prime_import_sg_table(struct drm_device *dev,
				     struct dma_buf_attachment *attach,
				     struct sg_table *sgt)
{
	struct exyyess_drm_gem *exyyess_gem;
	int npages;
	int ret;

	exyyess_gem = exyyess_drm_gem_init(dev, attach->dmabuf->size);
	if (IS_ERR(exyyess_gem)) {
		ret = PTR_ERR(exyyess_gem);
		return ERR_PTR(ret);
	}

	exyyess_gem->dma_addr = sg_dma_address(sgt->sgl);

	npages = exyyess_gem->size >> PAGE_SHIFT;
	exyyess_gem->pages = kvmalloc_array(npages, sizeof(struct page *), GFP_KERNEL);
	if (!exyyess_gem->pages) {
		ret = -ENOMEM;
		goto err;
	}

	ret = drm_prime_sg_to_page_addr_arrays(sgt, exyyess_gem->pages, NULL,
					       npages);
	if (ret < 0)
		goto err_free_large;

	exyyess_gem->sgt = sgt;

	if (sgt->nents == 1) {
		/* always physically continuous memory if sgt->nents is 1. */
		exyyess_gem->flags |= EXYNOS_BO_CONTIG;
	} else {
		/*
		 * this case could be CONTIG or NONCONTIG type but for yesw
		 * sets NONCONTIG.
		 * TODO. we have to find a way that exporter can yestify
		 * the type of its own buffer to importer.
		 */
		exyyess_gem->flags |= EXYNOS_BO_NONCONTIG;
	}

	return &exyyess_gem->base;

err_free_large:
	kvfree(exyyess_gem->pages);
err:
	drm_gem_object_release(&exyyess_gem->base);
	kfree(exyyess_gem);
	return ERR_PTR(ret);
}

void *exyyess_drm_gem_prime_vmap(struct drm_gem_object *obj)
{
	return NULL;
}

void exyyess_drm_gem_prime_vunmap(struct drm_gem_object *obj, void *vaddr)
{
	/* Nothing to do */
}

int exyyess_drm_gem_prime_mmap(struct drm_gem_object *obj,
			      struct vm_area_struct *vma)
{
	int ret;

	ret = drm_gem_mmap_obj(obj, obj->size, vma);
	if (ret < 0)
		return ret;

	return exyyess_drm_gem_mmap_obj(obj, vma);
}
