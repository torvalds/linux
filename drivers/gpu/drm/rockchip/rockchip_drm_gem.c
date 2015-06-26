/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author:Mark Yao <mark.yao@rock-chips.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <drm/drm.h>
#include <drm/drmP.h>
#include <drm/drm_gem.h>
#include <drm/drm_sync_helper.h>
#include <drm/drm_vma_manager.h>
#include <drm/rockchip_drm.h>

#include <linux/completion.h>
#include <linux/dma-attrs.h>
#include <linux/dma-buf.h>
#include <linux/reservation.h>

#include "rockchip_drm_drv.h"
#include "rockchip_drm_gem.h"

static int rockchip_gem_alloc_buf(struct rockchip_gem_object *rk_obj,
				  bool alloc_kmap)
{
	struct drm_gem_object *obj = &rk_obj->base;
	struct drm_device *drm = obj->dev;

	init_dma_attrs(&rk_obj->dma_attrs);
	dma_set_attr(DMA_ATTR_WRITE_COMBINE, &rk_obj->dma_attrs);

	if (!alloc_kmap)
		dma_set_attr(DMA_ATTR_NO_KERNEL_MAPPING, &rk_obj->dma_attrs);

	rk_obj->kvaddr = dma_alloc_attrs(drm->dev, obj->size,
					 &rk_obj->dma_addr, GFP_KERNEL,
					 &rk_obj->dma_attrs);
	if (!rk_obj->kvaddr) {
		DRM_ERROR("failed to allocate %zu byte dma buffer", obj->size);
		return -ENOMEM;
	}

	return 0;
}

static void rockchip_gem_free_buf(struct rockchip_gem_object *rk_obj)
{
	struct drm_gem_object *obj = &rk_obj->base;
	struct drm_device *drm = obj->dev;

	dma_free_attrs(drm->dev, obj->size, rk_obj->kvaddr, rk_obj->dma_addr,
		       &rk_obj->dma_attrs);
}

static int rockchip_drm_gem_object_mmap(struct drm_gem_object *obj,
					struct vm_area_struct *vma)

{
	int ret;
	struct rockchip_gem_object *rk_obj = to_rockchip_obj(obj);
	struct drm_device *drm = obj->dev;

	/*
	 * dma_alloc_attrs() allocated a struct page table for rk_obj, so clear
	 * VM_PFNMAP flag that was set by drm_gem_mmap_obj()/drm_gem_mmap().
	 */
	vma->vm_flags &= ~VM_PFNMAP;
	vma->vm_pgoff = 0;

	ret = dma_mmap_attrs(drm->dev, vma, rk_obj->kvaddr, rk_obj->dma_addr,
			     obj->size, &rk_obj->dma_attrs);
	if (ret)
		drm_gem_vm_close(vma);

	return ret;
}

int rockchip_gem_mmap_buf(struct drm_gem_object *obj,
			  struct vm_area_struct *vma)
{
	int ret;

	ret = drm_gem_mmap_obj(obj, obj->size, vma);
	if (ret)
		return ret;

	return rockchip_drm_gem_object_mmap(obj, vma);
}

/* drm driver mmap file operations */
int rockchip_gem_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct drm_gem_object *obj;
	int ret;

	ret = drm_gem_mmap(filp, vma);
	if (ret)
		return ret;

	obj = vma->vm_private_data;

	return rockchip_drm_gem_object_mmap(obj, vma);
}

struct drm_gem_object *
rockchip_gem_prime_import_sg_table(struct drm_device *drm,
				   struct dma_buf_attachment *attach,
				   struct sg_table *sgt)
{
	struct rockchip_gem_object *rk_obj;
	struct drm_gem_object *obj;

	rk_obj = kzalloc(sizeof(*rk_obj), GFP_KERNEL);
	if (!rk_obj)
		return ERR_PTR(-ENOMEM);

	obj = &rk_obj->base;

	drm_gem_private_object_init(drm, obj, attach->dmabuf->size);

	rk_obj->dma_addr = sg_dma_address(sgt->sgl);
	rk_obj->sgt = sgt;
	sg_dma_len(sgt->sgl) = obj->size;

	return obj;
}

struct rockchip_gem_object *
	rockchip_gem_create_object(struct drm_device *drm, unsigned int size,
				   bool alloc_kmap)
{
	struct rockchip_gem_object *rk_obj;
	struct drm_gem_object *obj;
	int ret;

	size = round_up(size, PAGE_SIZE);

	rk_obj = kzalloc(sizeof(*rk_obj), GFP_KERNEL);
	if (!rk_obj)
		return ERR_PTR(-ENOMEM);

	obj = &rk_obj->base;

	drm_gem_private_object_init(drm, obj, size);

	ret = rockchip_gem_alloc_buf(rk_obj, alloc_kmap);
	if (ret)
		goto err_free_rk_obj;

	return rk_obj;

err_free_rk_obj:
	kfree(rk_obj);
	return ERR_PTR(ret);
}

/*
 * rockchip_gem_free_object - (struct drm_driver)->gem_free_object callback
 * function
 */
void rockchip_gem_free_object(struct drm_gem_object *obj)
{
	struct rockchip_gem_object *rk_obj;

	rk_obj = to_rockchip_obj(obj);

	if (obj->import_attach) {
		drm_prime_gem_destroy(obj, rk_obj->sgt);
	} else {
		drm_gem_free_mmap_offset(obj);
		rockchip_gem_free_buf(rk_obj);
	}

#ifdef CONFIG_DRM_DMA_SYNC
	drm_fence_signal_and_put(&rk_obj->acquire_fence);
#endif

	kfree(rk_obj);
}

/*
 * rockchip_gem_create_with_handle - allocate an object with the given
 * size and create a gem handle on it
 *
 * returns a struct rockchip_gem_object* on success or ERR_PTR values
 * on failure.
 */
static struct rockchip_gem_object *
rockchip_gem_create_with_handle(struct drm_file *file_priv,
				struct drm_device *drm, unsigned int size,
				unsigned int *handle)
{
	struct rockchip_gem_object *rk_obj;
	struct drm_gem_object *obj;
	int ret;

	rk_obj = rockchip_gem_create_object(drm, size, false);
	if (IS_ERR(rk_obj))
		return ERR_CAST(rk_obj);

	obj = &rk_obj->base;

	/*
	 * allocate a id of idr table where the obj is registered
	 * and handle has the id what user can see.
	 */
	ret = drm_gem_handle_create(file_priv, obj, handle);
	if (ret)
		goto err_handle_create;

	/* drop reference from allocate - handle holds it now. */
	drm_gem_object_unreference_unlocked(obj);

	return rk_obj;

err_handle_create:
	rockchip_gem_free_object(obj);

	return ERR_PTR(ret);
}

int rockchip_gem_dumb_map_offset(struct drm_file *file_priv,
				 struct drm_device *dev, uint32_t handle,
				 uint64_t *offset)
{
	struct drm_gem_object *obj;
	int ret;

	obj = drm_gem_object_lookup(dev, file_priv, handle);
	if (!obj) {
		DRM_ERROR("failed to lookup gem object.\n");
		return -EINVAL;
	}

	ret = drm_gem_create_mmap_offset(obj);
	if (ret)
		goto out;

	*offset = drm_vma_node_offset_addr(&obj->vma_node);
	DRM_DEBUG_KMS("offset = 0x%llx\n", *offset);

out:
	drm_gem_object_unreference_unlocked(obj);

	return 0;
}

/*
 * rockchip_gem_dumb_create - (struct drm_driver)->dumb_create callback
 * function
 *
 * This aligns the pitch and size arguments to the minimum required. wrap
 * this into your own function if you need bigger alignment.
 */
int rockchip_gem_dumb_create(struct drm_file *file_priv,
			     struct drm_device *dev,
			     struct drm_mode_create_dumb *args)
{
	struct rockchip_gem_object *rk_obj;
	int min_pitch = DIV_ROUND_UP(args->width * args->bpp, 8);

	/*
	 * align to 64 bytes since Mali requires it.
	 */
	args->pitch = ALIGN(min_pitch, 64);
	args->size = args->pitch * args->height;

	rk_obj = rockchip_gem_create_with_handle(file_priv, dev, args->size,
						 &args->handle);

	return PTR_ERR_OR_ZERO(rk_obj);
}

int rockchip_gem_map_offset_ioctl(struct drm_device *drm, void *data,
				  struct drm_file *file_priv)
{
	struct drm_rockchip_gem_map_off *args = data;

	return rockchip_gem_dumb_map_offset(file_priv, drm, args->handle,
					    &args->offset);
}

int rockchip_gem_create_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *file_priv)
{
	struct drm_rockchip_gem_create *args = data;
	struct rockchip_gem_object *rk_obj;

	rk_obj = rockchip_gem_create_with_handle(file_priv, dev, args->size,
						 &args->handle);
	return PTR_ERR_OR_ZERO(rk_obj);
}

static struct reservation_object *drm_gem_get_resv(struct drm_gem_object *gem)
{
	struct dma_buf *dma_buf = gem->dma_buf;
	return dma_buf ? dma_buf->resv : NULL;
}

#ifdef CONFIG_DRM_DMA_SYNC
static void rockchip_gem_acquire_complete(struct drm_reservation_cb *rcb,
					void *context)
{
	struct completion *compl = context;
	complete(compl);
}

static int rockchip_gem_acquire(struct drm_device *dev,
				struct rockchip_gem_object *rockchip_gem_obj,
				bool exclusive)
{
	struct fence *fence;
	struct rockchip_drm_private *dev_priv = dev->dev_private;
	struct reservation_object *resv =
		drm_gem_get_resv(&rockchip_gem_obj->base);
	int ret = 0;
	struct drm_reservation_cb rcb;
	DECLARE_COMPLETION_ONSTACK(compl);

	if (!resv)
		return ret;

	if (!exclusive &&
	    !rockchip_gem_obj->acquire_exclusive &&
	    rockchip_gem_obj->acquire_fence) {
		atomic_inc(&rockchip_gem_obj->acquire_shared_count);
		return ret;
	}

	fence = drm_sw_fence_new(dev_priv->cpu_fence_context,
			atomic_add_return(1, &dev_priv->cpu_fence_seqno));
	if (IS_ERR(fence)) {
		ret = PTR_ERR(fence);
		DRM_ERROR("Failed to create acquire fence %d.\n", ret);
		return ret;
	}
	ww_mutex_lock(&resv->lock, NULL);
	if (!exclusive) {
		ret = reservation_object_reserve_shared(resv);
		if (ret < 0) {
			DRM_ERROR("Failed to reserve space for shared fence %d.\n",
				  ret);
			goto resv_unlock;
		}
	}
	drm_reservation_cb_init(&rcb, rockchip_gem_acquire_complete, &compl);
	ret = drm_reservation_cb_add(&rcb, resv, exclusive);
	if (ret < 0) {
		DRM_ERROR("Failed to add reservation to callback %d.\n", ret);
		goto resv_unlock;
	}
	drm_reservation_cb_done(&rcb);
	if (exclusive)
		reservation_object_add_excl_fence(resv, fence);
	else
		reservation_object_add_shared_fence(resv, fence);

	ww_mutex_unlock(&resv->lock);
	mutex_unlock(&dev->struct_mutex);
	ret = wait_for_completion_interruptible(&compl);
	mutex_lock(&dev->struct_mutex);
	if (ret < 0) {
		DRM_ERROR("Failed wait for reservation callback %d.\n", ret);
		drm_reservation_cb_fini(&rcb);
		/* somebody else may be already waiting on it */
		drm_fence_signal_and_put(&fence);
		return ret;
	}
	rockchip_gem_obj->acquire_fence = fence;
	rockchip_gem_obj->acquire_exclusive = exclusive;
	atomic_set(&rockchip_gem_obj->acquire_shared_count, 1);
	return ret;

resv_unlock:
	ww_mutex_unlock(&resv->lock);
	fence_put(fence);
	return ret;
}

static void rockchip_gem_release(struct rockchip_gem_object *rockchip_gem_obj)
{
	BUG_ON(!rockchip_gem_obj->acquire_fence);
	if (atomic_sub_and_test(1,
			&rockchip_gem_obj->acquire_shared_count))
		drm_fence_signal_and_put(&rockchip_gem_obj->acquire_fence);
}
#endif

int rockchip_gem_cpu_acquire_ioctl(struct drm_device *dev, void *data,
				   struct drm_file *file)
{
	struct drm_rockchip_gem_cpu_acquire *args = data;
	struct rockchip_drm_file_private *file_priv = file->driver_priv;
	struct drm_gem_object *obj;
	struct rockchip_gem_object *rockchip_gem_obj;
	struct rockchip_gem_object_node *gem_node;
	int ret = 0;

	DRM_DEBUG_KMS("[BO:%u] flags: 0x%x\n", args->handle, args->flags);

	mutex_lock(&dev->struct_mutex);

	obj = drm_gem_object_lookup(dev, file, args->handle);
	if (!obj) {
		DRM_ERROR("failed to lookup gem object.\n");
		ret = -EINVAL;
		goto unlock;
	}

	rockchip_gem_obj = to_rockchip_obj(obj);

	if (!drm_gem_get_resv(&rockchip_gem_obj->base)) {
		/* If there is no reservation object present, there is no
		 * cross-process/cross-device sharing and sync is unnecessary.
		 */
		ret = 0;
		goto unref_obj;
	}

#ifdef CONFIG_DRM_DMA_SYNC
	ret = rockchip_gem_acquire(dev, rockchip_gem_obj,
			args->flags & DRM_ROCKCHIP_GEM_CPU_ACQUIRE_EXCLUSIVE);
	if (ret < 0)
		goto unref_obj;
#endif

	gem_node = kzalloc(sizeof(*gem_node), GFP_KERNEL);
	if (!gem_node) {
		DRM_ERROR("Failed to allocate rockchip_drm_gem_obj_node.\n");
		ret = -ENOMEM;
		goto release_sync;
	}

	gem_node->rockchip_gem_obj = rockchip_gem_obj;
	list_add(&gem_node->list, &file_priv->gem_cpu_acquire_list);
	mutex_unlock(&dev->struct_mutex);
	return 0;

release_sync:
#ifdef CONFIG_DRM_DMA_SYNC
	rockchip_gem_release(rockchip_gem_obj);
#endif
unref_obj:
	drm_gem_object_unreference(obj);

unlock:
	mutex_unlock(&dev->struct_mutex);
	return ret;
}

int rockchip_gem_cpu_release_ioctl(struct drm_device *dev, void *data,
				   struct drm_file *file)
{
	struct drm_rockchip_gem_cpu_release *args = data;
	struct rockchip_drm_file_private *file_priv = file->driver_priv;
	struct drm_gem_object *obj;
	struct rockchip_gem_object *rockchip_gem_obj;
	struct list_head *cur;
	int ret = 0;

	DRM_DEBUG_KMS("[BO:%u]\n", args->handle);

	mutex_lock(&dev->struct_mutex);

	obj = drm_gem_object_lookup(dev, file, args->handle);
	if (!obj) {
		DRM_ERROR("failed to lookup gem object.\n");
		ret = -EINVAL;
		goto unlock;
	}

	rockchip_gem_obj = to_rockchip_obj(obj);

	if (!drm_gem_get_resv(&rockchip_gem_obj->base)) {
		/* If there is no reservation object present, there is no
		 * cross-process/cross-device sharing and sync is unnecessary.
		 */
		ret = 0;
		goto unref_obj;
	}

	list_for_each(cur, &file_priv->gem_cpu_acquire_list) {
		struct rockchip_gem_object_node *node = list_entry(
				cur, struct rockchip_gem_object_node, list);
		if (node->rockchip_gem_obj == rockchip_gem_obj)
			break;
	}
	if (cur == &file_priv->gem_cpu_acquire_list) {
		DRM_ERROR("gem object not acquired for current process.\n");
		ret = -EINVAL;
		goto unref_obj;
	}

#ifdef CONFIG_DRM_DMA_SYNC
	rockchip_gem_release(rockchip_gem_obj);
#endif

	list_del(cur);
	kfree(list_entry(cur, struct rockchip_gem_object_node, list));
	/* unreference for the reference held since cpu_acquire_ioctl */
	drm_gem_object_unreference(obj);
	ret = 0;

unref_obj:
	/* unreference for the reference from drm_gem_object_lookup() */
	drm_gem_object_unreference(obj);

unlock:
	mutex_unlock(&dev->struct_mutex);
	return ret;
}

/*
 * Allocate a sg_table for this GEM object.
 * Note: Both the table's contents, and the sg_table itself must be freed by
 *       the caller.
 * Returns a pointer to the newly allocated sg_table, or an ERR_PTR() error.
 */
struct sg_table *rockchip_gem_prime_get_sg_table(struct drm_gem_object *obj)
{
	struct rockchip_gem_object *rk_obj = to_rockchip_obj(obj);
	struct drm_device *drm = obj->dev;
	struct sg_table *sgt;
	int ret;

	sgt = kzalloc(sizeof(*sgt), GFP_KERNEL);
	if (!sgt)
		return ERR_PTR(-ENOMEM);

	ret = dma_get_sgtable_attrs(drm->dev, sgt, rk_obj->kvaddr,
				    rk_obj->dma_addr, obj->size,
				    &rk_obj->dma_attrs);
	if (ret) {
		DRM_ERROR("failed to allocate sgt, %d\n", ret);
		kfree(sgt);
		return ERR_PTR(ret);
	}

	return sgt;
}

void *rockchip_gem_prime_vmap(struct drm_gem_object *obj)
{
	struct rockchip_gem_object *rk_obj = to_rockchip_obj(obj);

	if (dma_get_attr(DMA_ATTR_NO_KERNEL_MAPPING, &rk_obj->dma_attrs))
		return NULL;

	return rk_obj->kvaddr;
}

void rockchip_gem_prime_vunmap(struct drm_gem_object *obj, void *vaddr)
{
	/* Nothing to do */
}
