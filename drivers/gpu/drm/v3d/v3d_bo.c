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
#include <linux/vmalloc.h>

#include "v3d_drv.h"
#include "uapi/drm/v3d_drm.h"

/* Called DRM core on the last userspace/kernel unreference of the
 * BO.
 */
void v3d_free_object(struct drm_gem_object *obj)
{
	struct v3d_dev *v3d = to_v3d_dev(obj->dev);
	struct v3d_bo *bo = to_v3d_bo(obj);

	if (bo->vaddr)
		v3d_put_bo_vaddr(bo);

	v3d_mmu_remove_ptes(bo);

	mutex_lock(&v3d->bo_lock);
	v3d->bo_stats.num_allocated--;
	v3d->bo_stats.pages_allocated -= obj->size >> V3D_MMU_PAGE_SHIFT;
	mutex_unlock(&v3d->bo_lock);

	spin_lock(&v3d->mm_lock);
	drm_mm_remove_node(&bo->node);
	spin_unlock(&v3d->mm_lock);

	/* GPU execution may have dirtied any pages in the BO. */
	bo->base.pages_mark_dirty_on_put = true;

	drm_gem_shmem_free(&bo->base);
}

static const struct drm_gem_object_funcs v3d_gem_funcs = {
	.free = v3d_free_object,
	.print_info = drm_gem_shmem_object_print_info,
	.pin = drm_gem_shmem_object_pin,
	.unpin = drm_gem_shmem_object_unpin,
	.get_sg_table = drm_gem_shmem_object_get_sg_table,
	.vmap = drm_gem_shmem_object_vmap,
	.vunmap = drm_gem_shmem_object_vunmap,
	.mmap = drm_gem_shmem_object_mmap,
	.vm_ops = &drm_gem_shmem_vm_ops,
};

/* gem_create_object function for allocating a BO struct and doing
 * early setup.
 */
struct drm_gem_object *v3d_create_object(struct drm_device *dev, size_t size)
{
	struct v3d_bo *bo;
	struct drm_gem_object *obj;

	if (size == 0)
		return ERR_PTR(-EINVAL);

	bo = kzalloc(sizeof(*bo), GFP_KERNEL);
	if (!bo)
		return ERR_PTR(-ENOMEM);
	obj = &bo->base.base;

	obj->funcs = &v3d_gem_funcs;
	bo->base.map_wc = true;
	INIT_LIST_HEAD(&bo->unref_head);

	return &bo->base.base;
}

static int
v3d_bo_create_finish(struct drm_gem_object *obj)
{
	struct v3d_dev *v3d = to_v3d_dev(obj->dev);
	struct v3d_bo *bo = to_v3d_bo(obj);
	struct sg_table *sgt;
	int ret;

	/* So far we pin the BO in the MMU for its lifetime, so use
	 * shmem's helper for getting a lifetime sgt.
	 */
	sgt = drm_gem_shmem_get_pages_sgt(&bo->base);
	if (IS_ERR(sgt))
		return PTR_ERR(sgt);

	spin_lock(&v3d->mm_lock);
	/* Allocate the object's space in the GPU's page tables.
	 * Inserting PTEs will happen later, but the offset is for the
	 * lifetime of the BO.
	 */
	ret = drm_mm_insert_node_generic(&v3d->mm, &bo->node,
					 obj->size >> V3D_MMU_PAGE_SHIFT,
					 GMP_GRANULARITY >> V3D_MMU_PAGE_SHIFT, 0, 0);
	spin_unlock(&v3d->mm_lock);
	if (ret)
		return ret;

	/* Track stats for /debug/dri/n/bo_stats. */
	mutex_lock(&v3d->bo_lock);
	v3d->bo_stats.num_allocated++;
	v3d->bo_stats.pages_allocated += obj->size >> V3D_MMU_PAGE_SHIFT;
	mutex_unlock(&v3d->bo_lock);

	v3d_mmu_insert_ptes(bo);

	return 0;
}

struct v3d_bo *v3d_bo_create(struct drm_device *dev, struct drm_file *file_priv,
			     size_t unaligned_size)
{
	struct drm_gem_shmem_object *shmem_obj;
	struct v3d_bo *bo;
	int ret;

	shmem_obj = drm_gem_shmem_create(dev, unaligned_size);
	if (IS_ERR(shmem_obj))
		return ERR_CAST(shmem_obj);
	bo = to_v3d_bo(&shmem_obj->base);
	bo->vaddr = NULL;

	ret = v3d_bo_create_finish(&shmem_obj->base);
	if (ret)
		goto free_obj;

	return bo;

free_obj:
	drm_gem_shmem_free(shmem_obj);
	return ERR_PTR(ret);
}

struct drm_gem_object *
v3d_prime_import_sg_table(struct drm_device *dev,
			  struct dma_buf_attachment *attach,
			  struct sg_table *sgt)
{
	struct drm_gem_object *obj;
	int ret;

	obj = drm_gem_shmem_prime_import_sg_table(dev, attach, sgt);
	if (IS_ERR(obj))
		return obj;

	ret = v3d_bo_create_finish(obj);
	if (ret) {
		drm_gem_shmem_free(&to_v3d_bo(obj)->base);
		return ERR_PTR(ret);
	}

	return obj;
}

void v3d_get_bo_vaddr(struct v3d_bo *bo)
{
	struct drm_gem_shmem_object *obj = &bo->base;

	bo->vaddr = vmap(obj->pages, obj->base.size >> PAGE_SHIFT, VM_MAP,
			 pgprot_writecombine(PAGE_KERNEL));
}

void v3d_put_bo_vaddr(struct v3d_bo *bo)
{
	vunmap(bo->vaddr);
	bo->vaddr = NULL;
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

	args->offset = bo->node.start << V3D_MMU_PAGE_SHIFT;

	ret = drm_gem_handle_create(file_priv, &bo->base.base, &args->handle);
	drm_gem_object_put(&bo->base.base);

	return ret;
}

int v3d_mmap_bo_ioctl(struct drm_device *dev, void *data,
		      struct drm_file *file_priv)
{
	struct drm_v3d_mmap_bo *args = data;
	struct drm_gem_object *gem_obj;

	if (args->flags != 0) {
		DRM_INFO("unknown mmap_bo flags: %d\n", args->flags);
		return -EINVAL;
	}

	gem_obj = drm_gem_object_lookup(file_priv, args->handle);
	if (!gem_obj) {
		DRM_DEBUG("Failed to look up GEM BO %d\n", args->handle);
		return -ENOENT;
	}

	args->offset = drm_vma_node_offset_addr(&gem_obj->vma_node);
	drm_gem_object_put(gem_obj);

	return 0;
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

	args->offset = bo->node.start << V3D_MMU_PAGE_SHIFT;

	drm_gem_object_put(gem_obj);
	return 0;
}

int
v3d_wait_bo_ioctl(struct drm_device *dev, void *data,
		  struct drm_file *file_priv)
{
	int ret;
	struct drm_v3d_wait_bo *args = data;
	ktime_t start = ktime_get();
	u64 delta_ns;
	unsigned long timeout_jiffies =
		nsecs_to_jiffies_timeout(args->timeout_ns);

	if (args->pad != 0)
		return -EINVAL;

	ret = drm_gem_dma_resv_wait(file_priv, args->handle,
				    true, timeout_jiffies);

	/* Decrement the user's timeout, in case we got interrupted
	 * such that the ioctl will be restarted.
	 */
	delta_ns = ktime_to_ns(ktime_sub(ktime_get(), start));
	if (delta_ns < args->timeout_ns)
		args->timeout_ns -= delta_ns;
	else
		args->timeout_ns = 0;

	/* Asked to wait beyond the jiffie/scheduler precision? */
	if (ret == -ETIME && args->timeout_ns)
		ret = -EAGAIN;

	return ret;
}
