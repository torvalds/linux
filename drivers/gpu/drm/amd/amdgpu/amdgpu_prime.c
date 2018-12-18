/*
 * Copyright 2012 Advanced Micro Devices, Inc.
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

#include <drm/drmP.h>

#include "amdgpu.h"
#include "amdgpu_display.h"
#include <drm/amdgpu_drm.h>
#include <linux/dma-buf.h>

static const struct dma_buf_ops amdgpu_dmabuf_ops;

/**
 * amdgpu_gem_prime_get_sg_table - &drm_driver.gem_prime_get_sg_table
 * implementation
 * @obj: GEM buffer object
 *
 * Returns:
 * A scatter/gather table for the pinned pages of the buffer object's memory.
 */
struct sg_table *amdgpu_gem_prime_get_sg_table(struct drm_gem_object *obj)
{
	struct amdgpu_bo *bo = gem_to_amdgpu_bo(obj);
	int npages = bo->tbo.num_pages;

	return drm_prime_pages_to_sg(bo->tbo.ttm->pages, npages);
}

/**
 * amdgpu_gem_prime_vmap - &dma_buf_ops.vmap implementation
 * @obj: GEM buffer object
 *
 * Sets up an in-kernel virtual mapping of the buffer object's memory.
 *
 * Returns:
 * The virtual address of the mapping or an error pointer.
 */
void *amdgpu_gem_prime_vmap(struct drm_gem_object *obj)
{
	struct amdgpu_bo *bo = gem_to_amdgpu_bo(obj);
	int ret;

	ret = ttm_bo_kmap(&bo->tbo, 0, bo->tbo.num_pages,
			  &bo->dma_buf_vmap);
	if (ret)
		return ERR_PTR(ret);

	return bo->dma_buf_vmap.virtual;
}

/**
 * amdgpu_gem_prime_vunmap - &dma_buf_ops.vunmap implementation
 * @obj: GEM buffer object
 * @vaddr: virtual address (unused)
 *
 * Tears down the in-kernel virtual mapping of the buffer object's memory.
 */
void amdgpu_gem_prime_vunmap(struct drm_gem_object *obj, void *vaddr)
{
	struct amdgpu_bo *bo = gem_to_amdgpu_bo(obj);

	ttm_bo_kunmap(&bo->dma_buf_vmap);
}

/**
 * amdgpu_gem_prime_mmap - &drm_driver.gem_prime_mmap implementation
 * @obj: GEM buffer object
 * @vma: virtual memory area
 *
 * Sets up a userspace mapping of the buffer object's memory in the given
 * virtual memory area.
 *
 * Returns:
 * 0 on success or negative error code.
 */
int amdgpu_gem_prime_mmap(struct drm_gem_object *obj, struct vm_area_struct *vma)
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

/**
 * amdgpu_gem_prime_import_sg_table - &drm_driver.gem_prime_import_sg_table
 * implementation
 * @dev: DRM device
 * @attach: DMA-buf attachment
 * @sg: Scatter/gather table
 *
 * Import shared DMA buffer memory exported by another device.
 *
 * Returns:
 * A new GEM buffer object of the given DRM device, representing the memory
 * described by the given DMA-buf attachment and scatter/gather table.
 */
struct drm_gem_object *
amdgpu_gem_prime_import_sg_table(struct drm_device *dev,
				 struct dma_buf_attachment *attach,
				 struct sg_table *sg)
{
	struct reservation_object *resv = attach->dmabuf->resv;
	struct amdgpu_device *adev = dev->dev_private;
	struct amdgpu_bo *bo;
	struct amdgpu_bo_param bp;
	int ret;

	memset(&bp, 0, sizeof(bp));
	bp.size = attach->dmabuf->size;
	bp.byte_align = PAGE_SIZE;
	bp.domain = AMDGPU_GEM_DOMAIN_CPU;
	bp.flags = 0;
	bp.type = ttm_bo_type_sg;
	bp.resv = resv;
	ww_mutex_lock(&resv->lock, NULL);
	ret = amdgpu_bo_create(adev, &bp, &bo);
	if (ret)
		goto error;

	bo->tbo.sg = sg;
	bo->tbo.ttm->sg = sg;
	bo->allowed_domains = AMDGPU_GEM_DOMAIN_GTT;
	bo->preferred_domains = AMDGPU_GEM_DOMAIN_GTT;
	if (attach->dmabuf->ops != &amdgpu_dmabuf_ops)
		bo->prime_shared_count = 1;

	ww_mutex_unlock(&resv->lock);
	return &bo->gem_base;

error:
	ww_mutex_unlock(&resv->lock);
	return ERR_PTR(ret);
}

/**
 * amdgpu_gem_map_attach - &dma_buf_ops.attach implementation
 * @dma_buf: shared DMA buffer
 * @attach: DMA-buf attachment
 *
 * Makes sure that the shared DMA buffer can be accessed by the target device.
 * For now, simply pins it to the GTT domain, where it should be accessible by
 * all DMA devices.
 *
 * Returns:
 * 0 on success or negative error code.
 */
static int amdgpu_gem_map_attach(struct dma_buf *dma_buf,
				 struct dma_buf_attachment *attach)
{
	struct drm_gem_object *obj = dma_buf->priv;
	struct amdgpu_bo *bo = gem_to_amdgpu_bo(obj);
	struct amdgpu_device *adev = amdgpu_ttm_adev(bo->tbo.bdev);
	long r;

	r = drm_gem_map_attach(dma_buf, attach);
	if (r)
		return r;

	r = amdgpu_bo_reserve(bo, false);
	if (unlikely(r != 0))
		goto error_detach;


	if (attach->dev->driver != adev->dev->driver) {
		/*
		 * Wait for all shared fences to complete before we switch to future
		 * use of exclusive fence on this prime shared bo.
		 */
		r = reservation_object_wait_timeout_rcu(bo->tbo.resv,
							true, false,
							MAX_SCHEDULE_TIMEOUT);
		if (unlikely(r < 0)) {
			DRM_DEBUG_PRIME("Fence wait failed: %li\n", r);
			goto error_unreserve;
		}
	}

	/* pin buffer into GTT */
	r = amdgpu_bo_pin(bo, AMDGPU_GEM_DOMAIN_GTT);
	if (r)
		goto error_unreserve;

	if (attach->dev->driver != adev->dev->driver)
		bo->prime_shared_count++;

error_unreserve:
	amdgpu_bo_unreserve(bo);

error_detach:
	if (r)
		drm_gem_map_detach(dma_buf, attach);
	return r;
}

/**
 * amdgpu_gem_map_detach - &dma_buf_ops.detach implementation
 * @dma_buf: shared DMA buffer
 * @attach: DMA-buf attachment
 *
 * This is called when a shared DMA buffer no longer needs to be accessible by
 * the other device. For now, simply unpins the buffer from GTT.
 */
static void amdgpu_gem_map_detach(struct dma_buf *dma_buf,
				  struct dma_buf_attachment *attach)
{
	struct drm_gem_object *obj = dma_buf->priv;
	struct amdgpu_bo *bo = gem_to_amdgpu_bo(obj);
	struct amdgpu_device *adev = amdgpu_ttm_adev(bo->tbo.bdev);
	int ret = 0;

	ret = amdgpu_bo_reserve(bo, true);
	if (unlikely(ret != 0))
		goto error;

	amdgpu_bo_unpin(bo);
	if (attach->dev->driver != adev->dev->driver && bo->prime_shared_count)
		bo->prime_shared_count--;
	amdgpu_bo_unreserve(bo);

error:
	drm_gem_map_detach(dma_buf, attach);
}

/**
 * amdgpu_gem_prime_res_obj - &drm_driver.gem_prime_res_obj implementation
 * @obj: GEM buffer object
 *
 * Returns:
 * The buffer object's reservation object.
 */
struct reservation_object *amdgpu_gem_prime_res_obj(struct drm_gem_object *obj)
{
	struct amdgpu_bo *bo = gem_to_amdgpu_bo(obj);

	return bo->tbo.resv;
}

/**
 * amdgpu_gem_begin_cpu_access - &dma_buf_ops.begin_cpu_access implementation
 * @dma_buf: shared DMA buffer
 * @direction: direction of DMA transfer
 *
 * This is called before CPU access to the shared DMA buffer's memory. If it's
 * a read access, the buffer is moved to the GTT domain if possible, for optimal
 * CPU read performance.
 *
 * Returns:
 * 0 on success or negative error code.
 */
static int amdgpu_gem_begin_cpu_access(struct dma_buf *dma_buf,
				       enum dma_data_direction direction)
{
	struct amdgpu_bo *bo = gem_to_amdgpu_bo(dma_buf->priv);
	struct amdgpu_device *adev = amdgpu_ttm_adev(bo->tbo.bdev);
	struct ttm_operation_ctx ctx = { true, false };
	u32 domain = amdgpu_display_supported_domains(adev);
	int ret;
	bool reads = (direction == DMA_BIDIRECTIONAL ||
		      direction == DMA_FROM_DEVICE);

	if (!reads || !(domain & AMDGPU_GEM_DOMAIN_GTT))
		return 0;

	/* move to gtt */
	ret = amdgpu_bo_reserve(bo, false);
	if (unlikely(ret != 0))
		return ret;

	if (!bo->pin_count && (bo->allowed_domains & AMDGPU_GEM_DOMAIN_GTT)) {
		amdgpu_bo_placement_from_domain(bo, AMDGPU_GEM_DOMAIN_GTT);
		ret = ttm_bo_validate(&bo->tbo, &bo->placement, &ctx);
	}

	amdgpu_bo_unreserve(bo);
	return ret;
}

static const struct dma_buf_ops amdgpu_dmabuf_ops = {
	.attach = amdgpu_gem_map_attach,
	.detach = amdgpu_gem_map_detach,
	.map_dma_buf = drm_gem_map_dma_buf,
	.unmap_dma_buf = drm_gem_unmap_dma_buf,
	.release = drm_gem_dmabuf_release,
	.begin_cpu_access = amdgpu_gem_begin_cpu_access,
	.map = drm_gem_dmabuf_kmap,
	.unmap = drm_gem_dmabuf_kunmap,
	.mmap = drm_gem_dmabuf_mmap,
	.vmap = drm_gem_dmabuf_vmap,
	.vunmap = drm_gem_dmabuf_vunmap,
};

/**
 * amdgpu_gem_prime_export - &drm_driver.gem_prime_export implementation
 * @dev: DRM device
 * @gobj: GEM buffer object
 * @flags: flags like DRM_CLOEXEC and DRM_RDWR
 *
 * The main work is done by the &drm_gem_prime_export helper, which in turn
 * uses &amdgpu_gem_prime_res_obj.
 *
 * Returns:
 * Shared DMA buffer representing the GEM buffer object from the given device.
 */
struct dma_buf *amdgpu_gem_prime_export(struct drm_device *dev,
					struct drm_gem_object *gobj,
					int flags)
{
	struct amdgpu_bo *bo = gem_to_amdgpu_bo(gobj);
	struct dma_buf *buf;

	if (amdgpu_ttm_tt_get_usermm(bo->tbo.ttm) ||
	    bo->flags & AMDGPU_GEM_CREATE_VM_ALWAYS_VALID)
		return ERR_PTR(-EPERM);

	buf = drm_gem_prime_export(dev, gobj, flags);
	if (!IS_ERR(buf)) {
		buf->file->f_mapping = dev->anon_inode->i_mapping;
		buf->ops = &amdgpu_dmabuf_ops;
	}

	return buf;
}

/**
 * amdgpu_gem_prime_import - &drm_driver.gem_prime_import implementation
 * @dev: DRM device
 * @dma_buf: Shared DMA buffer
 *
 * The main work is done by the &drm_gem_prime_import helper, which in turn
 * uses &amdgpu_gem_prime_import_sg_table.
 *
 * Returns:
 * GEM buffer object representing the shared DMA buffer for the given device.
 */
struct drm_gem_object *amdgpu_gem_prime_import(struct drm_device *dev,
					    struct dma_buf *dma_buf)
{
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

	return drm_gem_prime_import(dev, dma_buf);
}
