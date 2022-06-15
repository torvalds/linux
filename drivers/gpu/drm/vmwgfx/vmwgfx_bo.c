// SPDX-License-Identifier: GPL-2.0 OR MIT
/**************************************************************************
 *
 * Copyright © 2011-2018 VMware, Inc., Palo Alto, CA., USA
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#include <drm/ttm/ttm_placement.h>

#include "vmwgfx_drv.h"
#include "ttm_object.h"


/**
 * vmw_buffer_object - Convert a struct ttm_buffer_object to a struct
 * vmw_buffer_object.
 *
 * @bo: Pointer to the TTM buffer object.
 * Return: Pointer to the struct vmw_buffer_object embedding the
 * TTM buffer object.
 */
static struct vmw_buffer_object *
vmw_buffer_object(struct ttm_buffer_object *bo)
{
	return container_of(bo, struct vmw_buffer_object, base);
}

/**
 * bo_is_vmw - check if the buffer object is a &vmw_buffer_object
 * @bo: ttm buffer object to be checked
 *
 * Uses destroy function associated with the object to determine if this is
 * a &vmw_buffer_object.
 *
 * Returns:
 * true if the object is of &vmw_buffer_object type, false if not.
 */
static bool bo_is_vmw(struct ttm_buffer_object *bo)
{
	return bo->destroy == &vmw_bo_bo_free ||
	       bo->destroy == &vmw_gem_destroy;
}

/**
 * vmw_bo_pin_in_placement - Validate a buffer to placement.
 *
 * @dev_priv:  Driver private.
 * @buf:  DMA buffer to move.
 * @placement:  The placement to pin it.
 * @interruptible:  Use interruptible wait.
 * Return: Zero on success, Negative error code on failure. In particular
 * -ERESTARTSYS if interrupted by a signal
 */
int vmw_bo_pin_in_placement(struct vmw_private *dev_priv,
			    struct vmw_buffer_object *buf,
			    struct ttm_placement *placement,
			    bool interruptible)
{
	struct ttm_operation_ctx ctx = {interruptible, false };
	struct ttm_buffer_object *bo = &buf->base;
	int ret;

	vmw_execbuf_release_pinned_bo(dev_priv);

	ret = ttm_bo_reserve(bo, interruptible, false, NULL);
	if (unlikely(ret != 0))
		goto err;

	if (buf->base.pin_count > 0)
		ret = ttm_resource_compat(bo->resource, placement)
			? 0 : -EINVAL;
	else
		ret = ttm_bo_validate(bo, placement, &ctx);

	if (!ret)
		vmw_bo_pin_reserved(buf, true);

	ttm_bo_unreserve(bo);
err:
	return ret;
}


/**
 * vmw_bo_pin_in_vram_or_gmr - Move a buffer to vram or gmr.
 *
 * This function takes the reservation_sem in write mode.
 * Flushes and unpins the query bo to avoid failures.
 *
 * @dev_priv:  Driver private.
 * @buf:  DMA buffer to move.
 * @interruptible:  Use interruptible wait.
 * Return: Zero on success, Negative error code on failure. In particular
 * -ERESTARTSYS if interrupted by a signal
 */
int vmw_bo_pin_in_vram_or_gmr(struct vmw_private *dev_priv,
			      struct vmw_buffer_object *buf,
			      bool interruptible)
{
	struct ttm_operation_ctx ctx = {interruptible, false };
	struct ttm_buffer_object *bo = &buf->base;
	int ret;

	vmw_execbuf_release_pinned_bo(dev_priv);

	ret = ttm_bo_reserve(bo, interruptible, false, NULL);
	if (unlikely(ret != 0))
		goto err;

	if (buf->base.pin_count > 0) {
		ret = ttm_resource_compat(bo->resource, &vmw_vram_gmr_placement)
			? 0 : -EINVAL;
		goto out_unreserve;
	}

	ret = ttm_bo_validate(bo, &vmw_vram_gmr_placement, &ctx);
	if (likely(ret == 0) || ret == -ERESTARTSYS)
		goto out_unreserve;

	ret = ttm_bo_validate(bo, &vmw_vram_placement, &ctx);

out_unreserve:
	if (!ret)
		vmw_bo_pin_reserved(buf, true);

	ttm_bo_unreserve(bo);
err:
	return ret;
}


/**
 * vmw_bo_pin_in_vram - Move a buffer to vram.
 *
 * This function takes the reservation_sem in write mode.
 * Flushes and unpins the query bo to avoid failures.
 *
 * @dev_priv:  Driver private.
 * @buf:  DMA buffer to move.
 * @interruptible:  Use interruptible wait.
 * Return: Zero on success, Negative error code on failure. In particular
 * -ERESTARTSYS if interrupted by a signal
 */
int vmw_bo_pin_in_vram(struct vmw_private *dev_priv,
		       struct vmw_buffer_object *buf,
		       bool interruptible)
{
	return vmw_bo_pin_in_placement(dev_priv, buf, &vmw_vram_placement,
				       interruptible);
}


/**
 * vmw_bo_pin_in_start_of_vram - Move a buffer to start of vram.
 *
 * This function takes the reservation_sem in write mode.
 * Flushes and unpins the query bo to avoid failures.
 *
 * @dev_priv:  Driver private.
 * @buf:  DMA buffer to pin.
 * @interruptible:  Use interruptible wait.
 * Return: Zero on success, Negative error code on failure. In particular
 * -ERESTARTSYS if interrupted by a signal
 */
int vmw_bo_pin_in_start_of_vram(struct vmw_private *dev_priv,
				struct vmw_buffer_object *buf,
				bool interruptible)
{
	struct ttm_operation_ctx ctx = {interruptible, false };
	struct ttm_buffer_object *bo = &buf->base;
	struct ttm_placement placement;
	struct ttm_place place;
	int ret = 0;

	place = vmw_vram_placement.placement[0];
	place.lpfn = bo->resource->num_pages;
	placement.num_placement = 1;
	placement.placement = &place;
	placement.num_busy_placement = 1;
	placement.busy_placement = &place;

	vmw_execbuf_release_pinned_bo(dev_priv);
	ret = ttm_bo_reserve(bo, interruptible, false, NULL);
	if (unlikely(ret != 0))
		goto err_unlock;

	/*
	 * Is this buffer already in vram but not at the start of it?
	 * In that case, evict it first because TTM isn't good at handling
	 * that situation.
	 */
	if (bo->resource->mem_type == TTM_PL_VRAM &&
	    bo->resource->start < bo->resource->num_pages &&
	    bo->resource->start > 0 &&
	    buf->base.pin_count == 0) {
		ctx.interruptible = false;
		(void) ttm_bo_validate(bo, &vmw_sys_placement, &ctx);
	}

	if (buf->base.pin_count > 0)
		ret = ttm_resource_compat(bo->resource, &placement)
			? 0 : -EINVAL;
	else
		ret = ttm_bo_validate(bo, &placement, &ctx);

	/* For some reason we didn't end up at the start of vram */
	WARN_ON(ret == 0 && bo->resource->start != 0);
	if (!ret)
		vmw_bo_pin_reserved(buf, true);

	ttm_bo_unreserve(bo);
err_unlock:

	return ret;
}


/**
 * vmw_bo_unpin - Unpin the buffer given buffer, does not move the buffer.
 *
 * This function takes the reservation_sem in write mode.
 *
 * @dev_priv:  Driver private.
 * @buf:  DMA buffer to unpin.
 * @interruptible:  Use interruptible wait.
 * Return: Zero on success, Negative error code on failure. In particular
 * -ERESTARTSYS if interrupted by a signal
 */
int vmw_bo_unpin(struct vmw_private *dev_priv,
		 struct vmw_buffer_object *buf,
		 bool interruptible)
{
	struct ttm_buffer_object *bo = &buf->base;
	int ret;

	ret = ttm_bo_reserve(bo, interruptible, false, NULL);
	if (unlikely(ret != 0))
		goto err;

	vmw_bo_pin_reserved(buf, false);

	ttm_bo_unreserve(bo);

err:
	return ret;
}

/**
 * vmw_bo_get_guest_ptr - Get the guest ptr representing the current placement
 * of a buffer.
 *
 * @bo: Pointer to a struct ttm_buffer_object. Must be pinned or reserved.
 * @ptr: SVGAGuestPtr returning the result.
 */
void vmw_bo_get_guest_ptr(const struct ttm_buffer_object *bo,
			  SVGAGuestPtr *ptr)
{
	if (bo->resource->mem_type == TTM_PL_VRAM) {
		ptr->gmrId = SVGA_GMR_FRAMEBUFFER;
		ptr->offset = bo->resource->start << PAGE_SHIFT;
	} else {
		ptr->gmrId = bo->resource->start;
		ptr->offset = 0;
	}
}


/**
 * vmw_bo_pin_reserved - Pin or unpin a buffer object without moving it.
 *
 * @vbo: The buffer object. Must be reserved.
 * @pin: Whether to pin or unpin.
 *
 */
void vmw_bo_pin_reserved(struct vmw_buffer_object *vbo, bool pin)
{
	struct ttm_operation_ctx ctx = { false, true };
	struct ttm_place pl;
	struct ttm_placement placement;
	struct ttm_buffer_object *bo = &vbo->base;
	uint32_t old_mem_type = bo->resource->mem_type;
	int ret;

	dma_resv_assert_held(bo->base.resv);

	if (pin == !!bo->pin_count)
		return;

	pl.fpfn = 0;
	pl.lpfn = 0;
	pl.mem_type = bo->resource->mem_type;
	pl.flags = bo->resource->placement;

	memset(&placement, 0, sizeof(placement));
	placement.num_placement = 1;
	placement.placement = &pl;

	ret = ttm_bo_validate(bo, &placement, &ctx);

	BUG_ON(ret != 0 || bo->resource->mem_type != old_mem_type);

	if (pin)
		ttm_bo_pin(bo);
	else
		ttm_bo_unpin(bo);
}

/**
 * vmw_bo_map_and_cache - Map a buffer object and cache the map
 *
 * @vbo: The buffer object to map
 * Return: A kernel virtual address or NULL if mapping failed.
 *
 * This function maps a buffer object into the kernel address space, or
 * returns the virtual kernel address of an already existing map. The virtual
 * address remains valid as long as the buffer object is pinned or reserved.
 * The cached map is torn down on either
 * 1) Buffer object move
 * 2) Buffer object swapout
 * 3) Buffer object destruction
 *
 */
void *vmw_bo_map_and_cache(struct vmw_buffer_object *vbo)
{
	struct ttm_buffer_object *bo = &vbo->base;
	bool not_used;
	void *virtual;
	int ret;

	virtual = ttm_kmap_obj_virtual(&vbo->map, &not_used);
	if (virtual)
		return virtual;

	ret = ttm_bo_kmap(bo, 0, bo->resource->num_pages, &vbo->map);
	if (ret)
		DRM_ERROR("Buffer object map failed: %d.\n", ret);

	return ttm_kmap_obj_virtual(&vbo->map, &not_used);
}


/**
 * vmw_bo_unmap - Tear down a cached buffer object map.
 *
 * @vbo: The buffer object whose map we are tearing down.
 *
 * This function tears down a cached map set up using
 * vmw_buffer_object_map_and_cache().
 */
void vmw_bo_unmap(struct vmw_buffer_object *vbo)
{
	if (vbo->map.bo == NULL)
		return;

	ttm_bo_kunmap(&vbo->map);
}


/**
 * vmw_bo_bo_free - vmw buffer object destructor
 *
 * @bo: Pointer to the embedded struct ttm_buffer_object
 */
void vmw_bo_bo_free(struct ttm_buffer_object *bo)
{
	struct vmw_buffer_object *vmw_bo = vmw_buffer_object(bo);

	WARN_ON(vmw_bo->dirty);
	WARN_ON(!RB_EMPTY_ROOT(&vmw_bo->res_tree));
	vmw_bo_unmap(vmw_bo);
	drm_gem_object_release(&bo->base);
	kfree(vmw_bo);
}

/* default destructor */
static void vmw_bo_default_destroy(struct ttm_buffer_object *bo)
{
	kfree(bo);
}

/**
 * vmw_bo_create_kernel - Create a pinned BO for internal kernel use.
 *
 * @dev_priv: Pointer to the device private struct
 * @size: size of the BO we need
 * @placement: where to put it
 * @p_bo: resulting BO
 *
 * Creates and pin a simple BO for in kernel use.
 */
int vmw_bo_create_kernel(struct vmw_private *dev_priv, unsigned long size,
			 struct ttm_placement *placement,
			 struct ttm_buffer_object **p_bo)
{
	struct ttm_operation_ctx ctx = {
		.interruptible = false,
		.no_wait_gpu = false
	};
	struct ttm_buffer_object *bo;
	struct drm_device *vdev = &dev_priv->drm;
	int ret;

	bo = kzalloc(sizeof(*bo), GFP_KERNEL);
	if (unlikely(!bo))
		return -ENOMEM;

	size = ALIGN(size, PAGE_SIZE);

	drm_gem_private_object_init(vdev, &bo->base, size);

	ret = ttm_bo_init_reserved(&dev_priv->bdev, bo, size,
				   ttm_bo_type_kernel, placement, 0,
				   &ctx, NULL, NULL, vmw_bo_default_destroy);
	if (unlikely(ret))
		goto error_free;

	ttm_bo_pin(bo);
	ttm_bo_unreserve(bo);
	*p_bo = bo;

	return 0;

error_free:
	kfree(bo);
	return ret;
}

int vmw_bo_create(struct vmw_private *vmw,
		  size_t size, struct ttm_placement *placement,
		  bool interruptible, bool pin,
		  void (*bo_free)(struct ttm_buffer_object *bo),
		  struct vmw_buffer_object **p_bo)
{
	int ret;

	BUG_ON(!bo_free);

	*p_bo = kmalloc(sizeof(**p_bo), GFP_KERNEL);
	if (unlikely(!*p_bo)) {
		DRM_ERROR("Failed to allocate a buffer.\n");
		return -ENOMEM;
	}

	ret = vmw_bo_init(vmw, *p_bo, size,
			  placement, interruptible, pin,
			  bo_free);
	if (unlikely(ret != 0))
		goto out_error;

	return ret;
out_error:
	kfree(*p_bo);
	*p_bo = NULL;
	return ret;
}

/**
 * vmw_bo_init - Initialize a vmw buffer object
 *
 * @dev_priv: Pointer to the device private struct
 * @vmw_bo: Pointer to the struct vmw_buffer_object to initialize.
 * @size: Buffer object size in bytes.
 * @placement: Initial placement.
 * @interruptible: Whether waits should be performed interruptible.
 * @pin: If the BO should be created pinned at a fixed location.
 * @bo_free: The buffer object destructor.
 * Returns: Zero on success, negative error code on error.
 *
 * Note that on error, the code will free the buffer object.
 */
int vmw_bo_init(struct vmw_private *dev_priv,
		struct vmw_buffer_object *vmw_bo,
		size_t size, struct ttm_placement *placement,
		bool interruptible, bool pin,
		void (*bo_free)(struct ttm_buffer_object *bo))
{
	struct ttm_operation_ctx ctx = {
		.interruptible = interruptible,
		.no_wait_gpu = false
	};
	struct ttm_device *bdev = &dev_priv->bdev;
	struct drm_device *vdev = &dev_priv->drm;
	int ret;

	WARN_ON_ONCE(!bo_free);
	memset(vmw_bo, 0, sizeof(*vmw_bo));
	BUILD_BUG_ON(TTM_MAX_BO_PRIORITY <= 3);
	vmw_bo->base.priority = 3;
	vmw_bo->res_tree = RB_ROOT;

	size = ALIGN(size, PAGE_SIZE);
	drm_gem_private_object_init(vdev, &vmw_bo->base.base, size);

	ret = ttm_bo_init_reserved(bdev, &vmw_bo->base, size,
				   ttm_bo_type_device,
				   placement,
				   0, &ctx, NULL, NULL, bo_free);
	if (unlikely(ret)) {
		return ret;
	}

	if (pin)
		ttm_bo_pin(&vmw_bo->base);
	ttm_bo_unreserve(&vmw_bo->base);

	return 0;
}

/**
 * vmw_user_bo_synccpu_grab - Grab a struct vmw_buffer_object for cpu
 * access, idling previous GPU operations on the buffer and optionally
 * blocking it for further command submissions.
 *
 * @vmw_bo: Pointer to the buffer object being grabbed for CPU access
 * @flags: Flags indicating how the grab should be performed.
 * Return: Zero on success, Negative error code on error. In particular,
 * -EBUSY will be returned if a dontblock operation is requested and the
 * buffer object is busy, and -ERESTARTSYS will be returned if a wait is
 * interrupted by a signal.
 *
 * A blocking grab will be automatically released when @tfile is closed.
 */
static int vmw_user_bo_synccpu_grab(struct vmw_buffer_object *vmw_bo,
				    uint32_t flags)
{
	bool nonblock = !!(flags & drm_vmw_synccpu_dontblock);
	struct ttm_buffer_object *bo = &vmw_bo->base;
	int ret;

	if (flags & drm_vmw_synccpu_allow_cs) {
		long lret;

		lret = dma_resv_wait_timeout(bo->base.resv, DMA_RESV_USAGE_READ,
					     true, nonblock ? 0 :
					     MAX_SCHEDULE_TIMEOUT);
		if (!lret)
			return -EBUSY;
		else if (lret < 0)
			return lret;
		return 0;
	}

	ret = ttm_bo_reserve(bo, true, nonblock, NULL);
	if (unlikely(ret != 0))
		return ret;

	ret = ttm_bo_wait(bo, true, nonblock);
	if (likely(ret == 0))
		atomic_inc(&vmw_bo->cpu_writers);

	ttm_bo_unreserve(bo);
	if (unlikely(ret != 0))
		return ret;

	return ret;
}

/**
 * vmw_user_bo_synccpu_release - Release a previous grab for CPU access,
 * and unblock command submission on the buffer if blocked.
 *
 * @filp: Identifying the caller.
 * @handle: Handle identifying the buffer object.
 * @flags: Flags indicating the type of release.
 */
static int vmw_user_bo_synccpu_release(struct drm_file *filp,
				       uint32_t handle,
				       uint32_t flags)
{
	struct vmw_buffer_object *vmw_bo;
	int ret = vmw_user_bo_lookup(filp, handle, &vmw_bo);

	if (!ret) {
		if (!(flags & drm_vmw_synccpu_allow_cs)) {
			atomic_dec(&vmw_bo->cpu_writers);
		}
		ttm_bo_put(&vmw_bo->base);
	}

	return ret;
}


/**
 * vmw_user_bo_synccpu_ioctl - ioctl function implementing the synccpu
 * functionality.
 *
 * @dev: Identifies the drm device.
 * @data: Pointer to the ioctl argument.
 * @file_priv: Identifies the caller.
 * Return: Zero on success, negative error code on error.
 *
 * This function checks the ioctl arguments for validity and calls the
 * relevant synccpu functions.
 */
int vmw_user_bo_synccpu_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *file_priv)
{
	struct drm_vmw_synccpu_arg *arg =
		(struct drm_vmw_synccpu_arg *) data;
	struct vmw_buffer_object *vbo;
	int ret;

	if ((arg->flags & (drm_vmw_synccpu_read | drm_vmw_synccpu_write)) == 0
	    || (arg->flags & ~(drm_vmw_synccpu_read | drm_vmw_synccpu_write |
			       drm_vmw_synccpu_dontblock |
			       drm_vmw_synccpu_allow_cs)) != 0) {
		DRM_ERROR("Illegal synccpu flags.\n");
		return -EINVAL;
	}

	switch (arg->op) {
	case drm_vmw_synccpu_grab:
		ret = vmw_user_bo_lookup(file_priv, arg->handle, &vbo);
		if (unlikely(ret != 0))
			return ret;

		ret = vmw_user_bo_synccpu_grab(vbo, arg->flags);
		vmw_bo_unreference(&vbo);
		if (unlikely(ret != 0)) {
			if (ret == -ERESTARTSYS || ret == -EBUSY)
				return -EBUSY;
			DRM_ERROR("Failed synccpu grab on handle 0x%08x.\n",
				  (unsigned int) arg->handle);
			return ret;
		}
		break;
	case drm_vmw_synccpu_release:
		ret = vmw_user_bo_synccpu_release(file_priv,
						  arg->handle,
						  arg->flags);
		if (unlikely(ret != 0)) {
			DRM_ERROR("Failed synccpu release on handle 0x%08x.\n",
				  (unsigned int) arg->handle);
			return ret;
		}
		break;
	default:
		DRM_ERROR("Invalid synccpu operation.\n");
		return -EINVAL;
	}

	return 0;
}

/**
 * vmw_bo_unref_ioctl - Generic handle close ioctl.
 *
 * @dev: Identifies the drm device.
 * @data: Pointer to the ioctl argument.
 * @file_priv: Identifies the caller.
 * Return: Zero on success, negative error code on error.
 *
 * This function checks the ioctl arguments for validity and closes a
 * handle to a TTM base object, optionally freeing the object.
 */
int vmw_bo_unref_ioctl(struct drm_device *dev, void *data,
		       struct drm_file *file_priv)
{
	struct drm_vmw_unref_dmabuf_arg *arg =
	    (struct drm_vmw_unref_dmabuf_arg *)data;

	drm_gem_handle_delete(file_priv, arg->handle);
	return 0;
}


/**
 * vmw_user_bo_lookup - Look up a vmw user buffer object from a handle.
 *
 * @filp: The file the handle is registered with.
 * @handle: The user buffer object handle
 * @out: Pointer to a where a pointer to the embedded
 * struct vmw_buffer_object should be placed.
 * Return: Zero on success, Negative error code on error.
 *
 * The vmw buffer object pointer will be refcounted.
 */
int vmw_user_bo_lookup(struct drm_file *filp,
		       uint32_t handle,
		       struct vmw_buffer_object **out)
{
	struct drm_gem_object *gobj;

	gobj = drm_gem_object_lookup(filp, handle);
	if (!gobj) {
		DRM_ERROR("Invalid buffer object handle 0x%08lx.\n",
			  (unsigned long)handle);
		return -ESRCH;
	}

	*out = gem_to_vmw_bo(gobj);
	ttm_bo_get(&(*out)->base);
	drm_gem_object_put(gobj);

	return 0;
}

/**
 * vmw_user_bo_noref_lookup - Look up a vmw user buffer object without reference
 * @filp: The TTM object file the handle is registered with.
 * @handle: The user buffer object handle.
 *
 * This function looks up a struct vmw_bo and returns a pointer to the
 * struct vmw_buffer_object it derives from without refcounting the pointer.
 * The returned pointer is only valid until vmw_user_bo_noref_release() is
 * called, and the object pointed to by the returned pointer may be doomed.
 * Any persistent usage of the object requires a refcount to be taken using
 * ttm_bo_reference_unless_doomed(). Iff this function returns successfully it
 * needs to be paired with vmw_user_bo_noref_release() and no sleeping-
 * or scheduling functions may be called inbetween these function calls.
 *
 * Return: A struct vmw_buffer_object pointer if successful or negative
 * error pointer on failure.
 */
struct vmw_buffer_object *
vmw_user_bo_noref_lookup(struct drm_file *filp, u32 handle)
{
	struct vmw_buffer_object *vmw_bo;
	struct ttm_buffer_object *bo;
	struct drm_gem_object *gobj = drm_gem_object_lookup(filp, handle);

	if (!gobj) {
		DRM_ERROR("Invalid buffer object handle 0x%08lx.\n",
			  (unsigned long)handle);
		return ERR_PTR(-ESRCH);
	}
	vmw_bo = gem_to_vmw_bo(gobj);
	bo = ttm_bo_get_unless_zero(&vmw_bo->base);
	vmw_bo = vmw_buffer_object(bo);
	drm_gem_object_put(gobj);

	return vmw_bo;
}


/**
 * vmw_bo_fence_single - Utility function to fence a single TTM buffer
 *                       object without unreserving it.
 *
 * @bo:             Pointer to the struct ttm_buffer_object to fence.
 * @fence:          Pointer to the fence. If NULL, this function will
 *                  insert a fence into the command stream..
 *
 * Contrary to the ttm_eu version of this function, it takes only
 * a single buffer object instead of a list, and it also doesn't
 * unreserve the buffer object, which needs to be done separately.
 */
void vmw_bo_fence_single(struct ttm_buffer_object *bo,
			 struct vmw_fence_obj *fence)
{
	struct ttm_device *bdev = bo->bdev;
	struct vmw_private *dev_priv =
		container_of(bdev, struct vmw_private, bdev);
	int ret;

	if (fence == NULL)
		vmw_execbuf_fence_commands(NULL, dev_priv, &fence, NULL);
	else
		dma_fence_get(&fence->base);

	ret = dma_resv_reserve_fences(bo->base.resv, 1);
	if (!ret)
		dma_resv_add_fence(bo->base.resv, &fence->base,
				   DMA_RESV_USAGE_KERNEL);
	else
		/* Last resort fallback when we are OOM */
		dma_fence_wait(&fence->base, false);
	dma_fence_put(&fence->base);
}


/**
 * vmw_dumb_create - Create a dumb kms buffer
 *
 * @file_priv: Pointer to a struct drm_file identifying the caller.
 * @dev: Pointer to the drm device.
 * @args: Pointer to a struct drm_mode_create_dumb structure
 * Return: Zero on success, negative error code on failure.
 *
 * This is a driver callback for the core drm create_dumb functionality.
 * Note that this is very similar to the vmw_bo_alloc ioctl, except
 * that the arguments have a different format.
 */
int vmw_dumb_create(struct drm_file *file_priv,
		    struct drm_device *dev,
		    struct drm_mode_create_dumb *args)
{
	struct vmw_private *dev_priv = vmw_priv(dev);
	struct vmw_buffer_object *vbo;
	int ret;

	args->pitch = args->width * ((args->bpp + 7) / 8);
	args->size = ALIGN(args->pitch * args->height, PAGE_SIZE);

	ret = vmw_gem_object_create_with_handle(dev_priv, file_priv,
						args->size, &args->handle,
						&vbo);

	return ret;
}

/**
 * vmw_bo_swap_notify - swapout notify callback.
 *
 * @bo: The buffer object to be swapped out.
 */
void vmw_bo_swap_notify(struct ttm_buffer_object *bo)
{
	/* Is @bo embedded in a struct vmw_buffer_object? */
	if (!bo_is_vmw(bo))
		return;

	/* Kill any cached kernel maps before swapout */
	vmw_bo_unmap(vmw_buffer_object(bo));
}


/**
 * vmw_bo_move_notify - TTM move_notify_callback
 *
 * @bo: The TTM buffer object about to move.
 * @mem: The struct ttm_resource indicating to what memory
 *       region the move is taking place.
 *
 * Detaches cached maps and device bindings that require that the
 * buffer doesn't move.
 */
void vmw_bo_move_notify(struct ttm_buffer_object *bo,
			struct ttm_resource *mem)
{
	struct vmw_buffer_object *vbo;

	/* Make sure @bo is embedded in a struct vmw_buffer_object? */
	if (!bo_is_vmw(bo))
		return;

	vbo = container_of(bo, struct vmw_buffer_object, base);

	/*
	 * Kill any cached kernel maps before move to or from VRAM.
	 * With other types of moves, the underlying pages stay the same,
	 * and the map can be kept.
	 */
	if (mem->mem_type == TTM_PL_VRAM || bo->resource->mem_type == TTM_PL_VRAM)
		vmw_bo_unmap(vbo);

	/*
	 * If we're moving a backup MOB out of MOB placement, then make sure we
	 * read back all resource content first, and unbind the MOB from
	 * the resource.
	 */
	if (mem->mem_type != VMW_PL_MOB && bo->resource->mem_type == VMW_PL_MOB)
		vmw_resource_unbind_list(vbo);
}
