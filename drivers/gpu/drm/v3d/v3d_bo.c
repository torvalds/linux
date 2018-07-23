// SPDX-License-Identifier: GPL-2.0+
/* Copyright (C) 2015-2018 Broadcom */

/**
 * DOC: V3D GEM BO management support
 *
 * Compared to VC4 (V3D 2.x), V3D 3.3 introduces an MMU between the
 * GPU and the bus, allowing us to use shmem objects for our storage
 * instead of CMA.
 *
 * Physically contiguous objects may still be imported to V3D, but the
 * driver doesn't allocate physically contiguous objects on its own.
 * Display engines requiring physically contiguous allocations should
 * look into Mesa's "renderonly" support (as used by the Mesa pl111
 * driver) for an example of how to integrate with V3D.
 *
 * Long term, we should support evicting pages from the MMU when under
 * memory pressure (thus the v3d_bo_get_pages() refcounting), but
 * that's not a high priority since our systems tend to not have swap.
 */

#include <linux/dma-buf.h>
#include <linux/pfn_t.h>

#include "v3d_drv.h"
#include "uapi/drm/v3d_drm.h"

/* Pins the shmem pages, fills in the .pages and .sgt fields of the BO, and maps
 * it for DMA.
 */
static int
v3d_bo_get_pages(struct v3d_bo *bo)
{
	struct drm_gem_object *obj = &bo->base;
	struct drm_device *dev = obj->dev;
	int npages = obj->size >> PAGE_SHIFT;
	int ret = 0;

	mutex_lock(&bo->lock);
	if (bo->pages_refcount++ != 0)
		goto unlock;

	if (!obj->import_attach) {
		bo->pages = drm_gem_get_pages(obj);
		if (IS_ERR(bo->pages)) {
			ret = PTR_ERR(bo->pages);
			goto unlock;
		}

		bo->sgt = drm_prime_pages_to_sg(bo->pages, npages);
		if (IS_ERR(bo->sgt)) {
			ret = PTR_ERR(bo->sgt);
			goto put_pages;
		}

		/* Map the pages for use by the GPU. */
		dma_map_sg(dev->dev, bo->sgt->sgl,
			   bo->sgt->nents, DMA_BIDIRECTIONAL);
	} else {
		bo->pages = kcalloc(npages, sizeof(*bo->pages), GFP_KERNEL);
		if (!bo->pages)
			goto put_pages;

		drm_prime_sg_to_page_addr_arrays(bo->sgt, bo->pages,
						 NULL, npages);

		/* Note that dma-bufs come in mapped. */
	}

	mutex_unlock(&bo->lock);

	return 0;

put_pages:
	drm_gem_put_pages(obj, bo->pages, true, true);
	bo->pages = NULL;
unlock:
	bo->pages_refcount--;
	mutex_unlock(&bo->lock);
	return ret;
}

static void
v3d_bo_put_pages(struct v3d_bo *bo)
{
	struct drm_gem_object *obj = &bo->base;

	mutex_lock(&bo->lock);
	if (--bo->pages_refcount == 0) {
		if (!obj->import_attach) {
			dma_unmap_sg(obj->dev->dev, bo->sgt->sgl,
				     bo->sgt->nents, DMA_BIDIRECTIONAL);
			sg_free_table(bo->sgt);
			kfree(bo->sgt);
			drm_gem_put_pages(obj, bo->pages, true, true);
		} else {
			kfree(bo->pages);
		}
	}
	mutex_unlock(&bo->lock);
}

static struct v3d_bo *v3d_bo_create_struct(struct drm_device *dev,
					   size_t unaligned_size)
{
	struct v3d_dev *v3d = to_v3d_dev(dev);
	struct drm_gem_object *obj;
	struct v3d_bo *bo;
	size_t size = roundup(unaligned_size, PAGE_SIZE);
	int ret;

	if (size == 0)
		return ERR_PTR(-EINVAL);

	bo = kzalloc(sizeof(*bo), GFP_KERNEL);
	if (!bo)
		return ERR_PTR(-ENOMEM);
	obj = &bo->base;

	INIT_LIST_HEAD(&bo->vmas);
	INIT_LIST_HEAD(&bo->unref_head);
	mutex_init(&bo->lock);

	ret = drm_gem_object_init(dev, obj, size);
	if (ret)
		goto free_bo;

	spin_lock(&v3d->mm_lock);
	ret = drm_mm_insert_node_generic(&v3d->mm, &bo->node,
					 obj->size >> PAGE_SHIFT,
					 GMP_GRANULARITY >> PAGE_SHIFT, 0, 0);
	spin_unlock(&v3d->mm_lock);
	if (ret)
		goto free_obj;

	return bo;

free_obj:
	drm_gem_object_release(obj);
free_bo:
	kfree(bo);
	return ERR_PTR(ret);
}

struct v3d_bo *v3d_bo_create(struct drm_device *dev, struct drm_file *file_priv,
			     size_t unaligned_size)
{
	struct v3d_dev *v3d = to_v3d_dev(dev);
	struct drm_gem_object *obj;
	struct v3d_bo *bo;
	int ret;

	bo = v3d_bo_create_struct(dev, unaligned_size);
	if (IS_ERR(bo))
		return bo;
	obj = &bo->base;

	bo->resv = &bo->_resv;
	reservation_object_init(bo->resv);

	ret = v3d_bo_get_pages(bo);
	if (ret)
		goto free_mm;

	v3d_mmu_insert_ptes(bo);

	mutex_lock(&v3d->bo_lock);
	v3d->bo_stats.num_allocated++;
	v3d->bo_stats.pages_allocated += obj->size >> PAGE_SHIFT;
	mutex_unlock(&v3d->bo_lock);

	return bo;

free_mm:
	spin_lock(&v3d->mm_lock);
	drm_mm_remove_node(&bo->node);
	spin_unlock(&v3d->mm_lock);

	drm_gem_object_release(obj);
	kfree(bo);
	return ERR_PTR(ret);
}

/* Called DRM core on the last userspace/kernel unreference of the
 * BO.
 */
void v3d_free_object(struct drm_gem_object *obj)
{
	struct v3d_dev *v3d = to_v3d_dev(obj->dev);
	struct v3d_bo *bo = to_v3d_bo(obj);

	mutex_lock(&v3d->bo_lock);
	v3d->bo_stats.num_allocated--;
	v3d->bo_stats.pages_allocated -= obj->size >> PAGE_SHIFT;
	mutex_unlock(&v3d->bo_lock);

	reservation_object_fini(&bo->_resv);

	v3d_bo_put_pages(bo);

	if (obj->import_attach)
		drm_prime_gem_destroy(obj, bo->sgt);

	v3d_mmu_remove_ptes(bo);
	spin_lock(&v3d->mm_lock);
	drm_mm_remove_node(&bo->node);
	spin_unlock(&v3d->mm_lock);

	mutex_destroy(&bo->lock);

	drm_gem_object_release(obj);
	kfree(bo);
}

struct reservation_object *v3d_prime_res_obj(struct drm_gem_object *obj)
{
	struct v3d_bo *bo = to_v3d_bo(obj);

	return bo->resv;
}

static void
v3d_set_mmap_vma_flags(struct vm_area_struct *vma)
{
	vma->vm_flags &= ~VM_PFNMAP;
	vma->vm_flags |= VM_MIXEDMAP;
	vma->vm_page_prot = pgprot_writecombine(vm_get_page_prot(vma->vm_flags));
}

vm_fault_t v3d_gem_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct drm_gem_object *obj = vma->vm_private_data;
	struct v3d_bo *bo = to_v3d_bo(obj);
	pfn_t pfn;
	pgoff_t pgoff;

	/* We don't use vmf->pgoff since that has the fake offset: */
	pgoff = (vmf->address - vma->vm_start) >> PAGE_SHIFT;
	pfn = __pfn_to_pfn_t(page_to_pfn(bo->pages[pgoff]), PFN_DEV);

	return vmf_insert_mixed(vma, vmf->address, pfn);
}

int v3d_mmap(struct file *filp, struct vm_area_struct *vma)
{
	int ret;

	ret = drm_gem_mmap(filp, vma);
	if (ret)
		return ret;

	v3d_set_mmap_vma_flags(vma);

	return ret;
}

int v3d_prime_mmap(struct drm_gem_object *obj, struct vm_area_struct *vma)
{
	int ret;

	ret = drm_gem_mmap_obj(obj, obj->size, vma);
	if (ret < 0)
		return ret;

	v3d_set_mmap_vma_flags(vma);

	return 0;
}

struct sg_table *
v3d_prime_get_sg_table(struct drm_gem_object *obj)
{
	struct v3d_bo *bo = to_v3d_bo(obj);
	int npages = obj->size >> PAGE_SHIFT;

	return drm_prime_pages_to_sg(bo->pages, npages);
}

struct drm_gem_object *
v3d_prime_import_sg_table(struct drm_device *dev,
			  struct dma_buf_attachment *attach,
			  struct sg_table *sgt)
{
	struct drm_gem_object *obj;
	struct v3d_bo *bo;

	bo = v3d_bo_create_struct(dev, attach->dmabuf->size);
	if (IS_ERR(bo))
		return ERR_CAST(bo);
	obj = &bo->base;

	bo->resv = attach->dmabuf->resv;

	bo->sgt = sgt;
	v3d_bo_get_pages(bo);

	v3d_mmu_insert_ptes(bo);

	return obj;
}

int v3d_create_bo_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv)
{
	struct drm_v3d_create_bo *args = data;
	struct v3d_bo *bo = NULL;
	int ret;

	if (args->flags != 0) {
		DRM_INFO("unknown create_bo flags: %d\n", args->flags);
		return -EINVAL;
	}

	bo = v3d_bo_create(dev, file_priv, PAGE_ALIGN(args->size));
	if (IS_ERR(bo))
		return PTR_ERR(bo);

	args->offset = bo->node.start << PAGE_SHIFT;

	ret = drm_gem_handle_create(file_priv, &bo->base, &args->handle);
	drm_gem_object_put_unlocked(&bo->base);

	return ret;
}

int v3d_mmap_bo_ioctl(struct drm_device *dev, void *data,
		      struct drm_file *file_priv)
{
	struct drm_v3d_mmap_bo *args = data;
	struct drm_gem_object *gem_obj;
	int ret;

	if (args->flags != 0) {
		DRM_INFO("unknown mmap_bo flags: %d\n", args->flags);
		return -EINVAL;
	}

	gem_obj = drm_gem_object_lookup(file_priv, args->handle);
	if (!gem_obj) {
		DRM_DEBUG("Failed to look up GEM BO %d\n", args->handle);
		return -ENOENT;
	}

	ret = drm_gem_create_mmap_offset(gem_obj);
	if (ret == 0)
		args->offset = drm_vma_node_offset_addr(&gem_obj->vma_node);
	drm_gem_object_put_unlocked(gem_obj);

	return ret;
}

int v3d_get_bo_offset_ioctl(struct drm_device *dev, void *data,
			    struct drm_file *file_priv)
{
	struct drm_v3d_get_bo_offset *args = data;
	struct drm_gem_object *gem_obj;
	struct v3d_bo *bo;

	gem_obj = drm_gem_object_lookup(file_priv, args->handle);
	if (!gem_obj) {
		DRM_DEBUG("Failed to look up GEM BO %d\n", args->handle);
		return -ENOENT;
	}
	bo = to_v3d_bo(gem_obj);

	args->offset = bo->node.start << PAGE_SHIFT;

	drm_gem_object_put_unlocked(gem_obj);
	return 0;
}
