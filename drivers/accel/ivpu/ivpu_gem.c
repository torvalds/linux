// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2023 Intel Corporation
 */

#include <linux/dma-buf.h>
#include <linux/highmem.h>
#include <linux/module.h>
#include <linux/set_memory.h>
#include <linux/xarray.h>

#include <drm/drm_cache.h>
#include <drm/drm_debugfs.h>
#include <drm/drm_file.h>
#include <drm/drm_utils.h>

#include "ivpu_drv.h"
#include "ivpu_gem.h"
#include "ivpu_hw.h"
#include "ivpu_mmu.h"
#include "ivpu_mmu_context.h"

MODULE_IMPORT_NS(DMA_BUF);

static const struct drm_gem_object_funcs ivpu_gem_funcs;

static struct lock_class_key prime_bo_lock_class_key;

static int __must_check prime_alloc_pages_locked(struct ivpu_bo *bo)
{
	/* Pages are managed by the underlying dma-buf */
	return 0;
}

static void prime_free_pages_locked(struct ivpu_bo *bo)
{
	/* Pages are managed by the underlying dma-buf */
}

static int prime_map_pages_locked(struct ivpu_bo *bo)
{
	struct ivpu_device *vdev = ivpu_bo_to_vdev(bo);
	struct sg_table *sgt;

	sgt = dma_buf_map_attachment_unlocked(bo->base.import_attach, DMA_BIDIRECTIONAL);
	if (IS_ERR(sgt)) {
		ivpu_err(vdev, "Failed to map attachment: %ld\n", PTR_ERR(sgt));
		return PTR_ERR(sgt);
	}

	bo->sgt = sgt;
	return 0;
}

static void prime_unmap_pages_locked(struct ivpu_bo *bo)
{
	dma_buf_unmap_attachment_unlocked(bo->base.import_attach, bo->sgt, DMA_BIDIRECTIONAL);
	bo->sgt = NULL;
}

static const struct ivpu_bo_ops prime_ops = {
	.type = IVPU_BO_TYPE_PRIME,
	.name = "prime",
	.alloc_pages = prime_alloc_pages_locked,
	.free_pages = prime_free_pages_locked,
	.map_pages = prime_map_pages_locked,
	.unmap_pages = prime_unmap_pages_locked,
};

static int __must_check shmem_alloc_pages_locked(struct ivpu_bo *bo)
{
	int npages = bo->base.size >> PAGE_SHIFT;
	struct page **pages;

	pages = drm_gem_get_pages(&bo->base);
	if (IS_ERR(pages))
		return PTR_ERR(pages);

	if (bo->flags & DRM_IVPU_BO_WC)
		set_pages_array_wc(pages, npages);
	else if (bo->flags & DRM_IVPU_BO_UNCACHED)
		set_pages_array_uc(pages, npages);

	bo->pages = pages;
	return 0;
}

static void shmem_free_pages_locked(struct ivpu_bo *bo)
{
	if (ivpu_bo_cache_mode(bo) != DRM_IVPU_BO_CACHED)
		set_pages_array_wb(bo->pages, bo->base.size >> PAGE_SHIFT);

	drm_gem_put_pages(&bo->base, bo->pages, true, false);
	bo->pages = NULL;
}

static int ivpu_bo_map_pages_locked(struct ivpu_bo *bo)
{
	int npages = bo->base.size >> PAGE_SHIFT;
	struct ivpu_device *vdev = ivpu_bo_to_vdev(bo);
	struct sg_table *sgt;
	int ret;

	sgt = drm_prime_pages_to_sg(&vdev->drm, bo->pages, npages);
	if (IS_ERR(sgt)) {
		ivpu_err(vdev, "Failed to allocate sgtable\n");
		return PTR_ERR(sgt);
	}

	ret = dma_map_sgtable(vdev->drm.dev, sgt, DMA_BIDIRECTIONAL, 0);
	if (ret) {
		ivpu_err(vdev, "Failed to map BO in IOMMU: %d\n", ret);
		goto err_free_sgt;
	}

	bo->sgt = sgt;
	return 0;

err_free_sgt:
	kfree(sgt);
	return ret;
}

static void ivpu_bo_unmap_pages_locked(struct ivpu_bo *bo)
{
	struct ivpu_device *vdev = ivpu_bo_to_vdev(bo);

	dma_unmap_sgtable(vdev->drm.dev, bo->sgt, DMA_BIDIRECTIONAL, 0);
	sg_free_table(bo->sgt);
	kfree(bo->sgt);
	bo->sgt = NULL;
}

static const struct ivpu_bo_ops shmem_ops = {
	.type = IVPU_BO_TYPE_SHMEM,
	.name = "shmem",
	.alloc_pages = shmem_alloc_pages_locked,
	.free_pages = shmem_free_pages_locked,
	.map_pages = ivpu_bo_map_pages_locked,
	.unmap_pages = ivpu_bo_unmap_pages_locked,
};

static int __must_check internal_alloc_pages_locked(struct ivpu_bo *bo)
{
	unsigned int i, npages = bo->base.size >> PAGE_SHIFT;
	struct page **pages;
	int ret;

	pages = kvmalloc_array(npages, sizeof(*bo->pages), GFP_KERNEL);
	if (!pages)
		return -ENOMEM;

	for (i = 0; i < npages; i++) {
		pages[i] = alloc_page(GFP_KERNEL | __GFP_HIGHMEM | __GFP_ZERO);
		if (!pages[i]) {
			ret = -ENOMEM;
			goto err_free_pages;
		}
		cond_resched();
	}

	bo->pages = pages;
	return 0;

err_free_pages:
	while (i--)
		put_page(pages[i]);
	kvfree(pages);
	return ret;
}

static void internal_free_pages_locked(struct ivpu_bo *bo)
{
	unsigned int i, npages = bo->base.size >> PAGE_SHIFT;

	if (ivpu_bo_cache_mode(bo) != DRM_IVPU_BO_CACHED)
		set_pages_array_wb(bo->pages, bo->base.size >> PAGE_SHIFT);

	for (i = 0; i < npages; i++)
		put_page(bo->pages[i]);

	kvfree(bo->pages);
	bo->pages = NULL;
}

static const struct ivpu_bo_ops internal_ops = {
	.type = IVPU_BO_TYPE_INTERNAL,
	.name = "internal",
	.alloc_pages = internal_alloc_pages_locked,
	.free_pages = internal_free_pages_locked,
	.map_pages = ivpu_bo_map_pages_locked,
	.unmap_pages = ivpu_bo_unmap_pages_locked,
};

static int __must_check ivpu_bo_alloc_and_map_pages_locked(struct ivpu_bo *bo)
{
	struct ivpu_device *vdev = ivpu_bo_to_vdev(bo);
	int ret;

	lockdep_assert_held(&bo->lock);
	drm_WARN_ON(&vdev->drm, bo->sgt);

	ret = bo->ops->alloc_pages(bo);
	if (ret) {
		ivpu_err(vdev, "Failed to allocate pages for BO: %d", ret);
		return ret;
	}

	ret = bo->ops->map_pages(bo);
	if (ret) {
		ivpu_err(vdev, "Failed to map pages for BO: %d", ret);
		goto err_free_pages;
	}
	return ret;

err_free_pages:
	bo->ops->free_pages(bo);
	return ret;
}

static void ivpu_bo_unmap_and_free_pages(struct ivpu_bo *bo)
{
	mutex_lock(&bo->lock);

	WARN_ON(!bo->sgt);
	bo->ops->unmap_pages(bo);
	WARN_ON(bo->sgt);
	bo->ops->free_pages(bo);
	WARN_ON(bo->pages);

	mutex_unlock(&bo->lock);
}

/*
 * ivpu_bo_pin() - pin the backing physical pages and map them to VPU.
 *
 * This function pins physical memory pages, then maps the physical pages
 * to IOMMU address space and finally updates the VPU MMU page tables
 * to allow the VPU to translate VPU address to IOMMU address.
 */
int __must_check ivpu_bo_pin(struct ivpu_bo *bo)
{
	struct ivpu_device *vdev = ivpu_bo_to_vdev(bo);
	int ret = 0;

	mutex_lock(&bo->lock);

	if (!bo->vpu_addr) {
		ivpu_err(vdev, "vpu_addr not set for BO ctx_id: %d handle: %d\n",
			 bo->ctx->id, bo->handle);
		ret = -EINVAL;
		goto unlock;
	}

	if (!bo->sgt) {
		ret = ivpu_bo_alloc_and_map_pages_locked(bo);
		if (ret)
			goto unlock;
	}

	if (!bo->mmu_mapped) {
		ret = ivpu_mmu_context_map_sgt(vdev, bo->ctx, bo->vpu_addr, bo->sgt,
					       ivpu_bo_is_snooped(bo));
		if (ret) {
			ivpu_err(vdev, "Failed to map BO in MMU: %d\n", ret);
			goto unlock;
		}
		bo->mmu_mapped = true;
	}

unlock:
	mutex_unlock(&bo->lock);

	return ret;
}

static int
ivpu_bo_alloc_vpu_addr(struct ivpu_bo *bo, struct ivpu_mmu_context *ctx,
		       const struct ivpu_addr_range *range)
{
	struct ivpu_device *vdev = ivpu_bo_to_vdev(bo);
	int ret;

	if (!range) {
		if (bo->flags & DRM_IVPU_BO_HIGH_MEM)
			range = &vdev->hw->ranges.user_high;
		else
			range = &vdev->hw->ranges.user_low;
	}

	mutex_lock(&ctx->lock);
	ret = ivpu_mmu_context_insert_node_locked(ctx, range, bo->base.size, &bo->mm_node);
	if (!ret) {
		bo->ctx = ctx;
		bo->vpu_addr = bo->mm_node.start;
		list_add_tail(&bo->ctx_node, &ctx->bo_list);
	}
	mutex_unlock(&ctx->lock);

	return ret;
}

static void ivpu_bo_free_vpu_addr(struct ivpu_bo *bo)
{
	struct ivpu_device *vdev = ivpu_bo_to_vdev(bo);
	struct ivpu_mmu_context *ctx = bo->ctx;

	ivpu_dbg(vdev, BO, "remove from ctx: ctx %d vpu_addr 0x%llx allocated %d mmu_mapped %d\n",
		 ctx->id, bo->vpu_addr, (bool)bo->sgt, bo->mmu_mapped);

	mutex_lock(&bo->lock);

	if (bo->mmu_mapped) {
		drm_WARN_ON(&vdev->drm, !bo->sgt);
		ivpu_mmu_context_unmap_sgt(vdev, ctx, bo->vpu_addr, bo->sgt);
		bo->mmu_mapped = false;
	}

	mutex_lock(&ctx->lock);
	list_del(&bo->ctx_node);
	bo->vpu_addr = 0;
	bo->ctx = NULL;
	ivpu_mmu_context_remove_node_locked(ctx, &bo->mm_node);
	mutex_unlock(&ctx->lock);

	mutex_unlock(&bo->lock);
}

void ivpu_bo_remove_all_bos_from_context(struct ivpu_mmu_context *ctx)
{
	struct ivpu_bo *bo, *tmp;

	list_for_each_entry_safe(bo, tmp, &ctx->bo_list, ctx_node)
		ivpu_bo_free_vpu_addr(bo);
}

static struct ivpu_bo *
ivpu_bo_alloc(struct ivpu_device *vdev, struct ivpu_mmu_context *mmu_context,
	      u64 size, u32 flags, const struct ivpu_bo_ops *ops,
	      const struct ivpu_addr_range *range, u64 user_ptr)
{
	struct ivpu_bo *bo;
	int ret = 0;

	if (drm_WARN_ON(&vdev->drm, size == 0 || !PAGE_ALIGNED(size)))
		return ERR_PTR(-EINVAL);

	switch (flags & DRM_IVPU_BO_CACHE_MASK) {
	case DRM_IVPU_BO_CACHED:
	case DRM_IVPU_BO_UNCACHED:
	case DRM_IVPU_BO_WC:
		break;
	default:
		return ERR_PTR(-EINVAL);
	}

	bo = kzalloc(sizeof(*bo), GFP_KERNEL);
	if (!bo)
		return ERR_PTR(-ENOMEM);

	mutex_init(&bo->lock);
	bo->base.funcs = &ivpu_gem_funcs;
	bo->flags = flags;
	bo->ops = ops;
	bo->user_ptr = user_ptr;

	if (ops->type == IVPU_BO_TYPE_SHMEM)
		ret = drm_gem_object_init(&vdev->drm, &bo->base, size);
	else
		drm_gem_private_object_init(&vdev->drm, &bo->base, size);

	if (ret) {
		ivpu_err(vdev, "Failed to initialize drm object\n");
		goto err_free;
	}

	if (flags & DRM_IVPU_BO_MAPPABLE) {
		ret = drm_gem_create_mmap_offset(&bo->base);
		if (ret) {
			ivpu_err(vdev, "Failed to allocate mmap offset\n");
			goto err_release;
		}
	}

	if (mmu_context) {
		ret = ivpu_bo_alloc_vpu_addr(bo, mmu_context, range);
		if (ret) {
			ivpu_err(vdev, "Failed to add BO to context: %d\n", ret);
			goto err_release;
		}
	}

	return bo;

err_release:
	drm_gem_object_release(&bo->base);
err_free:
	kfree(bo);
	return ERR_PTR(ret);
}

static void ivpu_bo_free(struct drm_gem_object *obj)
{
	struct ivpu_bo *bo = to_ivpu_bo(obj);
	struct ivpu_device *vdev = ivpu_bo_to_vdev(bo);

	if (bo->ctx)
		ivpu_dbg(vdev, BO, "free: ctx %d vpu_addr 0x%llx allocated %d mmu_mapped %d\n",
			 bo->ctx->id, bo->vpu_addr, (bool)bo->sgt, bo->mmu_mapped);
	else
		ivpu_dbg(vdev, BO, "free: ctx (released) allocated %d mmu_mapped %d\n",
			 (bool)bo->sgt, bo->mmu_mapped);

	drm_WARN_ON(&vdev->drm, !dma_resv_test_signaled(obj->resv, DMA_RESV_USAGE_READ));

	vunmap(bo->kvaddr);

	if (bo->ctx)
		ivpu_bo_free_vpu_addr(bo);

	if (bo->sgt)
		ivpu_bo_unmap_and_free_pages(bo);

	if (bo->base.import_attach)
		drm_prime_gem_destroy(&bo->base, bo->sgt);

	drm_gem_object_release(&bo->base);

	mutex_destroy(&bo->lock);
	kfree(bo);
}

static int ivpu_bo_mmap(struct drm_gem_object *obj, struct vm_area_struct *vma)
{
	struct ivpu_bo *bo = to_ivpu_bo(obj);
	struct ivpu_device *vdev = ivpu_bo_to_vdev(bo);

	ivpu_dbg(vdev, BO, "mmap: ctx %u handle %u vpu_addr 0x%llx size %zu type %s",
		 bo->ctx->id, bo->handle, bo->vpu_addr, bo->base.size, bo->ops->name);

	if (obj->import_attach) {
		/* Drop the reference drm_gem_mmap_obj() acquired.*/
		drm_gem_object_put(obj);
		vma->vm_private_data = NULL;
		return dma_buf_mmap(obj->dma_buf, vma, 0);
	}

	vm_flags_set(vma, VM_PFNMAP | VM_DONTEXPAND);
	vma->vm_page_prot = ivpu_bo_pgprot(bo, vm_get_page_prot(vma->vm_flags));

	return 0;
}

static struct sg_table *ivpu_bo_get_sg_table(struct drm_gem_object *obj)
{
	struct ivpu_bo *bo = to_ivpu_bo(obj);
	loff_t npages = obj->size >> PAGE_SHIFT;
	int ret = 0;

	mutex_lock(&bo->lock);

	if (!bo->sgt)
		ret = ivpu_bo_alloc_and_map_pages_locked(bo);

	mutex_unlock(&bo->lock);

	if (ret)
		return ERR_PTR(ret);

	return drm_prime_pages_to_sg(obj->dev, bo->pages, npages);
}

static vm_fault_t ivpu_vm_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct drm_gem_object *obj = vma->vm_private_data;
	struct ivpu_bo *bo = to_ivpu_bo(obj);
	loff_t npages = obj->size >> PAGE_SHIFT;
	pgoff_t page_offset;
	struct page *page;
	vm_fault_t ret;
	int err;

	mutex_lock(&bo->lock);

	if (!bo->sgt) {
		err = ivpu_bo_alloc_and_map_pages_locked(bo);
		if (err) {
			ret = vmf_error(err);
			goto unlock;
		}
	}

	/* We don't use vmf->pgoff since that has the fake offset */
	page_offset = (vmf->address - vma->vm_start) >> PAGE_SHIFT;
	if (page_offset >= npages) {
		ret = VM_FAULT_SIGBUS;
	} else {
		page = bo->pages[page_offset];
		ret = vmf_insert_pfn(vma, vmf->address, page_to_pfn(page));
	}

unlock:
	mutex_unlock(&bo->lock);

	return ret;
}

static const struct vm_operations_struct ivpu_vm_ops = {
	.fault = ivpu_vm_fault,
	.open = drm_gem_vm_open,
	.close = drm_gem_vm_close,
};

static const struct drm_gem_object_funcs ivpu_gem_funcs = {
	.free = ivpu_bo_free,
	.mmap = ivpu_bo_mmap,
	.vm_ops = &ivpu_vm_ops,
	.get_sg_table = ivpu_bo_get_sg_table,
};

int
ivpu_bo_create_ioctl(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct ivpu_file_priv *file_priv = file->driver_priv;
	struct ivpu_device *vdev = file_priv->vdev;
	struct drm_ivpu_bo_create *args = data;
	u64 size = PAGE_ALIGN(args->size);
	struct ivpu_bo *bo;
	int ret;

	if (args->flags & ~DRM_IVPU_BO_FLAGS)
		return -EINVAL;

	if (size == 0)
		return -EINVAL;

	bo = ivpu_bo_alloc(vdev, &file_priv->ctx, size, args->flags, &shmem_ops, NULL, 0);
	if (IS_ERR(bo)) {
		ivpu_err(vdev, "Failed to create BO: %pe (ctx %u size %llu flags 0x%x)",
			 bo, file_priv->ctx.id, args->size, args->flags);
		return PTR_ERR(bo);
	}

	ret = drm_gem_handle_create(file, &bo->base, &bo->handle);
	if (!ret) {
		args->vpu_addr = bo->vpu_addr;
		args->handle = bo->handle;
	}

	drm_gem_object_put(&bo->base);

	ivpu_dbg(vdev, BO, "alloc shmem: ctx %u vpu_addr 0x%llx size %zu flags 0x%x\n",
		 file_priv->ctx.id, bo->vpu_addr, bo->base.size, bo->flags);

	return ret;
}

struct ivpu_bo *
ivpu_bo_alloc_internal(struct ivpu_device *vdev, u64 vpu_addr, u64 size, u32 flags)
{
	const struct ivpu_addr_range *range;
	struct ivpu_addr_range fixed_range;
	struct ivpu_bo *bo;
	pgprot_t prot;
	int ret;

	drm_WARN_ON(&vdev->drm, !PAGE_ALIGNED(vpu_addr));
	drm_WARN_ON(&vdev->drm, !PAGE_ALIGNED(size));

	if (vpu_addr) {
		fixed_range.start = vpu_addr;
		fixed_range.end = vpu_addr + size;
		range = &fixed_range;
	} else {
		range = &vdev->hw->ranges.global_low;
	}

	bo = ivpu_bo_alloc(vdev, &vdev->gctx, size, flags, &internal_ops, range, 0);
	if (IS_ERR(bo)) {
		ivpu_err(vdev, "Failed to create BO: %pe (vpu_addr 0x%llx size %llu flags 0x%x)",
			 bo, vpu_addr, size, flags);
		return NULL;
	}

	ret = ivpu_bo_pin(bo);
	if (ret)
		goto err_put;

	if (ivpu_bo_cache_mode(bo) != DRM_IVPU_BO_CACHED)
		drm_clflush_pages(bo->pages, bo->base.size >> PAGE_SHIFT);

	if (bo->flags & DRM_IVPU_BO_WC)
		set_pages_array_wc(bo->pages, bo->base.size >> PAGE_SHIFT);
	else if (bo->flags & DRM_IVPU_BO_UNCACHED)
		set_pages_array_uc(bo->pages, bo->base.size >> PAGE_SHIFT);

	prot = ivpu_bo_pgprot(bo, PAGE_KERNEL);
	bo->kvaddr = vmap(bo->pages, bo->base.size >> PAGE_SHIFT, VM_MAP, prot);
	if (!bo->kvaddr) {
		ivpu_err(vdev, "Failed to map BO into kernel virtual memory\n");
		goto err_put;
	}

	ivpu_dbg(vdev, BO, "alloc internal: ctx 0 vpu_addr 0x%llx size %zu flags 0x%x\n",
		 bo->vpu_addr, bo->base.size, flags);

	return bo;

err_put:
	drm_gem_object_put(&bo->base);
	return NULL;
}

void ivpu_bo_free_internal(struct ivpu_bo *bo)
{
	drm_gem_object_put(&bo->base);
}

struct drm_gem_object *ivpu_gem_prime_import(struct drm_device *dev, struct dma_buf *buf)
{
	struct ivpu_device *vdev = to_ivpu_device(dev);
	struct dma_buf_attachment *attach;
	struct ivpu_bo *bo;

	attach = dma_buf_attach(buf, dev->dev);
	if (IS_ERR(attach))
		return ERR_CAST(attach);

	get_dma_buf(buf);

	bo = ivpu_bo_alloc(vdev, NULL, buf->size, DRM_IVPU_BO_MAPPABLE, &prime_ops, NULL, 0);
	if (IS_ERR(bo)) {
		ivpu_err(vdev, "Failed to import BO: %pe (size %lu)", bo, buf->size);
		goto err_detach;
	}

	lockdep_set_class(&bo->lock, &prime_bo_lock_class_key);

	bo->base.import_attach = attach;

	return &bo->base;

err_detach:
	dma_buf_detach(buf, attach);
	dma_buf_put(buf);
	return ERR_CAST(bo);
}

int ivpu_bo_info_ioctl(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct ivpu_file_priv *file_priv = file->driver_priv;
	struct ivpu_device *vdev = to_ivpu_device(dev);
	struct drm_ivpu_bo_info *args = data;
	struct drm_gem_object *obj;
	struct ivpu_bo *bo;
	int ret = 0;

	obj = drm_gem_object_lookup(file, args->handle);
	if (!obj)
		return -ENOENT;

	bo = to_ivpu_bo(obj);

	mutex_lock(&bo->lock);

	if (!bo->ctx) {
		ret = ivpu_bo_alloc_vpu_addr(bo, &file_priv->ctx, NULL);
		if (ret) {
			ivpu_err(vdev, "Failed to allocate vpu_addr: %d\n", ret);
			goto unlock;
		}
	}

	args->flags = bo->flags;
	args->mmap_offset = drm_vma_node_offset_addr(&obj->vma_node);
	args->vpu_addr = bo->vpu_addr;
	args->size = obj->size;
unlock:
	mutex_unlock(&bo->lock);
	drm_gem_object_put(obj);
	return ret;
}

int ivpu_bo_wait_ioctl(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_ivpu_bo_wait *args = data;
	struct drm_gem_object *obj;
	unsigned long timeout;
	long ret;

	timeout = drm_timeout_abs_to_jiffies(args->timeout_ns);

	obj = drm_gem_object_lookup(file, args->handle);
	if (!obj)
		return -EINVAL;

	ret = dma_resv_wait_timeout(obj->resv, DMA_RESV_USAGE_READ, true, timeout);
	if (ret == 0) {
		ret = -ETIMEDOUT;
	} else if (ret > 0) {
		ret = 0;
		args->job_status = to_ivpu_bo(obj)->job_status;
	}

	drm_gem_object_put(obj);

	return ret;
}

static void ivpu_bo_print_info(struct ivpu_bo *bo, struct drm_printer *p)
{
	unsigned long dma_refcount = 0;

	if (bo->base.dma_buf && bo->base.dma_buf->file)
		dma_refcount = atomic_long_read(&bo->base.dma_buf->file->f_count);

	drm_printf(p, "%5u %6d %16llx %10lu %10u %12lu %14s\n",
		   bo->ctx->id, bo->handle, bo->vpu_addr, bo->base.size,
		   kref_read(&bo->base.refcount), dma_refcount, bo->ops->name);
}

void ivpu_bo_list(struct drm_device *dev, struct drm_printer *p)
{
	struct ivpu_device *vdev = to_ivpu_device(dev);
	struct ivpu_file_priv *file_priv;
	unsigned long ctx_id;
	struct ivpu_bo *bo;

	drm_printf(p, "%5s %6s %16s %10s %10s %12s %14s\n",
		   "ctx", "handle", "vpu_addr", "size", "refcount", "dma_refcount", "type");

	mutex_lock(&vdev->gctx.lock);
	list_for_each_entry(bo, &vdev->gctx.bo_list, ctx_node)
		ivpu_bo_print_info(bo, p);
	mutex_unlock(&vdev->gctx.lock);

	xa_for_each(&vdev->context_xa, ctx_id, file_priv) {
		file_priv = ivpu_file_priv_get_by_ctx_id(vdev, ctx_id);
		if (!file_priv)
			continue;

		mutex_lock(&file_priv->ctx.lock);
		list_for_each_entry(bo, &file_priv->ctx.bo_list, ctx_node)
			ivpu_bo_print_info(bo, p);
		mutex_unlock(&file_priv->ctx.lock);

		ivpu_file_priv_put(&file_priv);
	}
}

void ivpu_bo_list_print(struct drm_device *dev)
{
	struct drm_printer p = drm_info_printer(dev->dev);

	ivpu_bo_list(dev, &p);
}
