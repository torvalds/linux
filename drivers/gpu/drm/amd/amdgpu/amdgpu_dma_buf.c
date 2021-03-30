/*
 * Copyright 2019 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * based on nouveau_prime.c
 *
 * Authors: Alex Deucher
 */

/**
 * DOC: PRIME Buffer Sharing
 *
 * The following callback implementations are used for :ref:`sharing GEM buffer
 * objects between different devices via PRIME <prime_buffer_sharing>`.
 */

#include "amdgpu.h"
#include "amdgpu_display.h"
#include "amdgpu_gem.h"
#include "amdgpu_dma_buf.h"
#include "amdgpu_xgmi.h"
#include <drm/amdgpu_drm.h>
#include <linux/dma-buf.h>
#include <linux/dma-fence-array.h>
#include <linux/pci-p2pdma.h>
#include <linux/pm_runtime.h>

/**
 * amdgpu_gem_prime_mmap - &drm_driver.gem_prime_mmap implementation
 * @obj: GEM BO
 * @vma: Virtual memory area
 *
 * Sets up a userspace mapping of the BO's memory in the given
 * virtual memory area.
 *
 * Returns:
 * 0 on success or a negative error code on failure.
 */
int amdgpu_gem_prime_mmap(struct drm_gem_object *obj,
			  struct vm_area_struct *vma)
{
	struct amdgpu_bo *bo = gem_to_amdgpu_bo(obj);
	struct amdgpu_device *adev = amdgpu_ttm_adev(bo->tbo.bdev);
	unsigned asize = amdgpu_bo_size(bo);
	int ret;

	if (!vma->vm_file)
		return -ENODEV;

	if (adev == NULL)
		return -ENODEV;

	/* Check for valid size. */
	if (asize < vma->vm_end - vma->vm_start)
		return -EINVAL;

	if (amdgpu_ttm_tt_get_usermm(bo->tbo.ttm) ||
	    (bo->flags & AMDGPU_GEM_CREATE_NO_CPU_ACCESS)) {
		return -EPERM;
	}
	vma->vm_pgoff += amdgpu_bo_mmap_offset(bo) >> PAGE_SHIFT;

	/* prime mmap does not need to check access, so allow here */
	ret = drm_vma_node_allow(&obj->vma_node, vma->vm_file->private_data);
	if (ret)
		return ret;

	ret = ttm_bo_mmap(vma->vm_file, vma, &adev->mman.bdev);
	drm_vma_node_revoke(&obj->vma_node, vma->vm_file->private_data);

	return ret;
}

static int
__dma_resv_make_exclusive(struct dma_resv *obj)
{
	struct dma_fence **fences;
	unsigned int count;
	int r;

	if (!dma_resv_get_list(obj)) /* no shared fences to convert */
		return 0;

	r = dma_resv_get_fences_rcu(obj, NULL, &count, &fences);
	if (r)
		return r;

	if (count == 0) {
		/* Now that was unexpected. */
	} else if (count == 1) {
		dma_resv_add_excl_fence(obj, fences[0]);
		dma_fence_put(fences[0]);
		kfree(fences);
	} else {
		struct dma_fence_array *array;

		array = dma_fence_array_create(count, fences,
					       dma_fence_context_alloc(1), 0,
					       false);
		if (!array)
			goto err_fences_put;

		dma_resv_add_excl_fence(obj, &array->base);
		dma_fence_put(&array->base);
	}

	return 0;

err_fences_put:
	while (count--)
		dma_fence_put(fences[count]);
	kfree(fences);
	return -ENOMEM;
}

/**
 * amdgpu_dma_buf_attach - &dma_buf_ops.attach implementation
 *
 * @dmabuf: DMA-buf where we attach to
 * @attach: attachment to add
 *
 * Add the attachment as user to the exported DMA-buf.
 */
static int amdgpu_dma_buf_attach(struct dma_buf *dmabuf,
				 struct dma_buf_attachment *attach)
{
	struct drm_gem_object *obj = dmabuf->priv;
	struct amdgpu_bo *bo = gem_to_amdgpu_bo(obj);
	struct amdgpu_device *adev = amdgpu_ttm_adev(bo->tbo.bdev);
	int r;

	if (pci_p2pdma_distance_many(adev->pdev, &attach->dev, 1, true) < 0)
		attach->peer2peer = false;

	if (attach->dev->driver == adev->dev->driver)
		return 0;

	r = pm_runtime_get_sync(adev_to_drm(adev)->dev);
	if (r < 0)
		goto out;

	r = amdgpu_bo_reserve(bo, false);
	if (unlikely(r != 0))
		goto out;

	/*
	 * We only create shared fences for internal use, but importers
	 * of the dmabuf rely on exclusive fences for implicitly
	 * tracking write hazards. As any of the current fences may
	 * correspond to a write, we need to convert all existing
	 * fences on the reservation object into a single exclusive
	 * fence.
	 */
	r = __dma_resv_make_exclusive(bo->tbo.base.resv);
	if (r)
		goto out;

	bo->prime_shared_count++;
	amdgpu_bo_unreserve(bo);
	return 0;

out:
	pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
	return r;
}

/**
 * amdgpu_dma_buf_detach - &dma_buf_ops.detach implementation
 *
 * @dmabuf: DMA-buf where we remove the attachment from
 * @attach: the attachment to remove
 *
 * Called when an attachment is removed from the DMA-buf.
 */
static void amdgpu_dma_buf_detach(struct dma_buf *dmabuf,
				  struct dma_buf_attachment *attach)
{
	struct drm_gem_object *obj = dmabuf->priv;
	struct amdgpu_bo *bo = gem_to_amdgpu_bo(obj);
	struct amdgpu_device *adev = amdgpu_ttm_adev(bo->tbo.bdev);

	if (attach->dev->driver != adev->dev->driver && bo->prime_shared_count)
		bo->prime_shared_count--;

	pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
	pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
}

/**
 * amdgpu_dma_buf_pin - &dma_buf_ops.pin implementation
 *
 * @attach: attachment to pin down
 *
 * Pin the BO which is backing the DMA-buf so that it can't move any more.
 */
static int amdgpu_dma_buf_pin(struct dma_buf_attachment *attach)
{
	struct drm_gem_object *obj = attach->dmabuf->priv;
	struct amdgpu_bo *bo = gem_to_amdgpu_bo(obj);

	/* pin buffer into GTT */
	return amdgpu_bo_pin(bo, AMDGPU_GEM_DOMAIN_GTT);
}

/**
 * amdgpu_dma_buf_unpin - &dma_buf_ops.unpin implementation
 *
 * @attach: attachment to unpin
 *
 * Unpin a previously pinned BO to make it movable again.
 */
static void amdgpu_dma_buf_unpin(struct dma_buf_attachment *attach)
{
	struct drm_gem_object *obj = attach->dmabuf->priv;
	struct amdgpu_bo *bo = gem_to_amdgpu_bo(obj);

	amdgpu_bo_unpin(bo);
}

/**
 * amdgpu_dma_buf_map - &dma_buf_ops.map_dma_buf implementation
 * @attach: DMA-buf attachment
 * @dir: DMA direction
 *
 * Makes sure that the shared DMA buffer can be accessed by the target device.
 * For now, simply pins it to the GTT domain, where it should be accessible by
 * all DMA devices.
 *
 * Returns:
 * sg_table filled with the DMA addresses to use or ERR_PRT with negative error
 * code.
 */
static struct sg_table *amdgpu_dma_buf_map(struct dma_buf_attachment *attach,
					   enum dma_data_direction dir)
{
	struct dma_buf *dma_buf = attach->dmabuf;
	struct drm_gem_object *obj = dma_buf->priv;
	struct amdgpu_bo *bo = gem_to_amdgpu_bo(obj);
	struct amdgpu_device *adev = amdgpu_ttm_adev(bo->tbo.bdev);
	struct sg_table *sgt;
	long r;

	if (!bo->tbo.pin_count) {
		/* move buffer into GTT or VRAM */
		struct ttm_operation_ctx ctx = { false, false };
		unsigned domains = AMDGPU_GEM_DOMAIN_GTT;

		if (bo->preferred_domains & AMDGPU_GEM_DOMAIN_VRAM &&
		    attach->peer2peer) {
			bo->flags |= AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED;
			domains |= AMDGPU_GEM_DOMAIN_VRAM;
		}
		amdgpu_bo_placement_from_domain(bo, domains);
		r = ttm_bo_validate(&bo->tbo, &bo->placement, &ctx);
		if (r)
			return ERR_PTR(r);

	} else if (!(amdgpu_mem_type_to_domain(bo->tbo.mem.mem_type) &
		     AMDGPU_GEM_DOMAIN_GTT)) {
		return ERR_PTR(-EBUSY);
	}

	switch (bo->tbo.mem.mem_type) {
	case TTM_PL_TT:
		sgt = drm_prime_pages_to_sg(obj->dev,
					    bo->tbo.ttm->pages,
					    bo->tbo.ttm->num_pages);
		if (IS_ERR(sgt))
			return sgt;

		if (dma_map_sgtable(attach->dev, sgt, dir,
				    DMA_ATTR_SKIP_CPU_SYNC))
			goto error_free;
		break;

	case TTM_PL_VRAM:
		r = amdgpu_vram_mgr_alloc_sgt(adev, &bo->tbo.mem, attach->dev,
					      dir, &sgt);
		if (r)
			return ERR_PTR(r);
		break;
	default:
		return ERR_PTR(-EINVAL);
	}

	return sgt;

error_free:
	sg_free_table(sgt);
	kfree(sgt);
	return ERR_PTR(-EBUSY);
}

/**
 * amdgpu_dma_buf_unmap - &dma_buf_ops.unmap_dma_buf implementation
 * @attach: DMA-buf attachment
 * @sgt: sg_table to unmap
 * @dir: DMA direction
 *
 * This is called when a shared DMA buffer no longer needs to be accessible by
 * another device. For now, simply unpins the buffer from GTT.
 */
static void amdgpu_dma_buf_unmap(struct dma_buf_attachment *attach,
				 struct sg_table *sgt,
				 enum dma_data_direction dir)
{
	struct dma_buf *dma_buf = attach->dmabuf;
	struct drm_gem_object *obj = dma_buf->priv;
	struct amdgpu_bo *bo = gem_to_amdgpu_bo(obj);
	struct amdgpu_device *adev = amdgpu_ttm_adev(bo->tbo.bdev);

	if (sgt->sgl->page_link) {
		dma_unmap_sgtable(attach->dev, sgt, dir, 0);
		sg_free_table(sgt);
		kfree(sgt);
	} else {
		amdgpu_vram_mgr_free_sgt(adev, attach->dev, dir, sgt);
	}
}

/**
 * amdgpu_dma_buf_begin_cpu_access - &dma_buf_ops.begin_cpu_access implementation
 * @dma_buf: Shared DMA buffer
 * @direction: Direction of DMA transfer
 *
 * This is called before CPU access to the shared DMA buffer's memory. If it's
 * a read access, the buffer is moved to the GTT domain if possible, for optimal
 * CPU read performance.
 *
 * Returns:
 * 0 on success or a negative error code on failure.
 */
static int amdgpu_dma_buf_begin_cpu_access(struct dma_buf *dma_buf,
					   enum dma_data_direction direction)
{
	struct amdgpu_bo *bo = gem_to_amdgpu_bo(dma_buf->priv);
	struct amdgpu_device *adev = amdgpu_ttm_adev(bo->tbo.bdev);
	struct ttm_operation_ctx ctx = { true, false };
	u32 domain = amdgpu_display_supported_domains(adev, bo->flags);
	int ret;
	bool reads = (direction == DMA_BIDIRECTIONAL ||
		      direction == DMA_FROM_DEVICE);

	if (!reads || !(domain & AMDGPU_GEM_DOMAIN_GTT))
		return 0;

	/* move to gtt */
	ret = amdgpu_bo_reserve(bo, false);
	if (unlikely(ret != 0))
		return ret;

	if (!bo->tbo.pin_count &&
	    (bo->allowed_domains & AMDGPU_GEM_DOMAIN_GTT)) {
		amdgpu_bo_placement_from_domain(bo, AMDGPU_GEM_DOMAIN_GTT);
		ret = ttm_bo_validate(&bo->tbo, &bo->placement, &ctx);
	}

	amdgpu_bo_unreserve(bo);
	return ret;
}

const struct dma_buf_ops amdgpu_dmabuf_ops = {
	.attach = amdgpu_dma_buf_attach,
	.detach = amdgpu_dma_buf_detach,
	.pin = amdgpu_dma_buf_pin,
	.unpin = amdgpu_dma_buf_unpin,
	.map_dma_buf = amdgpu_dma_buf_map,
	.unmap_dma_buf = amdgpu_dma_buf_unmap,
	.release = drm_gem_dmabuf_release,
	.begin_cpu_access = amdgpu_dma_buf_begin_cpu_access,
	.mmap = drm_gem_dmabuf_mmap,
	.vmap = drm_gem_dmabuf_vmap,
	.vunmap = drm_gem_dmabuf_vunmap,
};

/**
 * amdgpu_gem_prime_export - &drm_driver.gem_prime_export implementation
 * @gobj: GEM BO
 * @flags: Flags such as DRM_CLOEXEC and DRM_RDWR.
 *
 * The main work is done by the &drm_gem_prime_export helper.
 *
 * Returns:
 * Shared DMA buffer representing the GEM BO from the given device.
 */
struct dma_buf *amdgpu_gem_prime_export(struct drm_gem_object *gobj,
					int flags)
{
	struct amdgpu_bo *bo = gem_to_amdgpu_bo(gobj);
	struct dma_buf *buf;

	if (amdgpu_ttm_tt_get_usermm(bo->tbo.ttm) ||
	    bo->flags & AMDGPU_GEM_CREATE_VM_ALWAYS_VALID)
		return ERR_PTR(-EPERM);

	buf = drm_gem_prime_export(gobj, flags);
	if (!IS_ERR(buf))
		buf->ops = &amdgpu_dmabuf_ops;

	return buf;
}

/**
 * amdgpu_dma_buf_create_obj - create BO for DMA-buf import
 *
 * @dev: DRM device
 * @dma_buf: DMA-buf
 *
 * Creates an empty SG BO for DMA-buf import.
 *
 * Returns:
 * A new GEM BO of the given DRM device, representing the memory
 * described by the given DMA-buf attachment and scatter/gather table.
 */
static struct drm_gem_object *
amdgpu_dma_buf_create_obj(struct drm_device *dev, struct dma_buf *dma_buf)
{
	struct dma_resv *resv = dma_buf->resv;
	struct amdgpu_device *adev = drm_to_adev(dev);
	struct amdgpu_bo *bo;
	struct amdgpu_bo_param bp;
	struct drm_gem_object *gobj;
	int ret;

	memset(&bp, 0, sizeof(bp));
	bp.size = dma_buf->size;
	bp.byte_align = PAGE_SIZE;
	bp.domain = AMDGPU_GEM_DOMAIN_CPU;
	bp.flags = 0;
	bp.type = ttm_bo_type_sg;
	bp.resv = resv;
	dma_resv_lock(resv, NULL);
	ret = amdgpu_gem_object_create(adev, dma_buf->size, PAGE_SIZE,
			AMDGPU_GEM_DOMAIN_CPU,
			0, ttm_bo_type_sg, resv, &gobj);
	if (ret)
		goto error;

	bo = gem_to_amdgpu_bo(gobj);
	bo->allowed_domains = AMDGPU_GEM_DOMAIN_GTT;
	bo->preferred_domains = AMDGPU_GEM_DOMAIN_GTT;
	if (dma_buf->ops != &amdgpu_dmabuf_ops)
		bo->prime_shared_count = 1;

	dma_resv_unlock(resv);
	return gobj;

error:
	dma_resv_unlock(resv);
	return ERR_PTR(ret);
}

/**
 * amdgpu_dma_buf_move_notify - &attach.move_notify implementation
 *
 * @attach: the DMA-buf attachment
 *
 * Invalidate the DMA-buf attachment, making sure that the we re-create the
 * mapping before the next use.
 */
static void
amdgpu_dma_buf_move_notify(struct dma_buf_attachment *attach)
{
	struct drm_gem_object *obj = attach->importer_priv;
	struct ww_acquire_ctx *ticket = dma_resv_locking_ctx(obj->resv);
	struct amdgpu_bo *bo = gem_to_amdgpu_bo(obj);
	struct amdgpu_device *adev = amdgpu_ttm_adev(bo->tbo.bdev);
	struct ttm_operation_ctx ctx = { false, false };
	struct ttm_placement placement = {};
	struct amdgpu_vm_bo_base *bo_base;
	int r;

	if (bo->tbo.mem.mem_type == TTM_PL_SYSTEM)
		return;

	r = ttm_bo_validate(&bo->tbo, &placement, &ctx);
	if (r) {
		DRM_ERROR("Failed to invalidate DMA-buf import (%d))\n", r);
		return;
	}

	for (bo_base = bo->vm_bo; bo_base; bo_base = bo_base->next) {
		struct amdgpu_vm *vm = bo_base->vm;
		struct dma_resv *resv = vm->root.base.bo->tbo.base.resv;

		if (ticket) {
			/* When we get an error here it means that somebody
			 * else is holding the VM lock and updating page tables
			 * So we can just continue here.
			 */
			r = dma_resv_lock(resv, ticket);
			if (r)
				continue;

		} else {
			/* TODO: This is more problematic and we actually need
			 * to allow page tables updates without holding the
			 * lock.
			 */
			if (!dma_resv_trylock(resv))
				continue;
		}

		r = amdgpu_vm_clear_freed(adev, vm, NULL);
		if (!r)
			r = amdgpu_vm_handle_moved(adev, vm);

		if (r && r != -EBUSY)
			DRM_ERROR("Failed to invalidate VM page tables (%d))\n",
				  r);

		dma_resv_unlock(resv);
	}
}

static const struct dma_buf_attach_ops amdgpu_dma_buf_attach_ops = {
	.allow_peer2peer = true,
	.move_notify = amdgpu_dma_buf_move_notify
};

/**
 * amdgpu_gem_prime_import - &drm_driver.gem_prime_import implementation
 * @dev: DRM device
 * @dma_buf: Shared DMA buffer
 *
 * Import a dma_buf into a the driver and potentially create a new GEM object.
 *
 * Returns:
 * GEM BO representing the shared DMA buffer for the given device.
 */
struct drm_gem_object *amdgpu_gem_prime_import(struct drm_device *dev,
					       struct dma_buf *dma_buf)
{
	struct dma_buf_attachment *attach;
	struct drm_gem_object *obj;

	if (dma_buf->ops == &amdgpu_dmabuf_ops) {
		obj = dma_buf->priv;
		if (obj->dev == dev) {
			/*
			 * Importing dmabuf exported from out own gem increases
			 * refcount on gem itself instead of f_count of dmabuf.
			 */
			drm_gem_object_get(obj);
			return obj;
		}
	}

	obj = amdgpu_dma_buf_create_obj(dev, dma_buf);
	if (IS_ERR(obj))
		return obj;

	attach = dma_buf_dynamic_attach(dma_buf, dev->dev,
					&amdgpu_dma_buf_attach_ops, obj);
	if (IS_ERR(attach)) {
		drm_gem_object_put(obj);
		return ERR_CAST(attach);
	}

	get_dma_buf(dma_buf);
	obj->import_attach = attach;
	return obj;
}

/**
 * amdgpu_dmabuf_is_xgmi_accessible - Check if xgmi available for P2P transfer
 *
 * @adev: amdgpu_device pointer of the importer
 * @bo: amdgpu buffer object
 *
 * Returns:
 * True if dmabuf accessible over xgmi, false otherwise.
 */
bool amdgpu_dmabuf_is_xgmi_accessible(struct amdgpu_device *adev,
				      struct amdgpu_bo *bo)
{
	struct drm_gem_object *obj = &bo->tbo.base;
	struct drm_gem_object *gobj;

	if (obj->import_attach) {
		struct dma_buf *dma_buf = obj->import_attach->dmabuf;

		if (dma_buf->ops != &amdgpu_dmabuf_ops)
			/* No XGMI with non AMD GPUs */
			return false;

		gobj = dma_buf->priv;
		bo = gem_to_amdgpu_bo(gobj);
	}

	if (amdgpu_xgmi_same_hive(adev, amdgpu_ttm_adev(bo->tbo.bdev)) &&
			(bo->preferred_domains & AMDGPU_GEM_DOMAIN_VRAM))
		return true;

	return false;
}
