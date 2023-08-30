// SPDX-License-Identifier: GPL-2.0 OR MIT
/**************************************************************************
 *
 * Copyright © 2011-2023 VMware, Inc., Palo Alto, CA., USA
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

#include "vmwgfx_bo.h"
#include "vmwgfx_drv.h"


#include <drm/ttm/ttm_placement.h>

static void vmw_bo_release(struct vmw_bo *vbo)
{
	vmw_bo_unmap(vbo);
	drm_gem_object_release(&vbo->tbo.base);
}

/**
 * vmw_bo_free - vmw_bo destructor
 *
 * @bo: Pointer to the embedded struct ttm_buffer_object
 */
static void vmw_bo_free(struct ttm_buffer_object *bo)
{
	struct vmw_bo *vbo = to_vmw_bo(&bo->base);

	WARN_ON(vbo->dirty);
	WARN_ON(!RB_EMPTY_ROOT(&vbo->res_tree));
	vmw_bo_release(vbo);
	kfree(vbo);
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
static int vmw_bo_pin_in_placement(struct vmw_private *dev_priv,
				   struct vmw_bo *buf,
				   struct ttm_placement *placement,
				   bool interruptible)
{
	struct ttm_operation_ctx ctx = {interruptible, false };
	struct ttm_buffer_object *bo = &buf->tbo;
	int ret;

	vmw_execbuf_release_pinned_bo(dev_priv);

	ret = ttm_bo_reserve(bo, interruptible, false, NULL);
	if (unlikely(ret != 0))
		goto err;

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
			      struct vmw_bo *buf,
			      bool interruptible)
{
	struct ttm_operation_ctx ctx = {interruptible, false };
	struct ttm_buffer_object *bo = &buf->tbo;
	int ret;

	vmw_execbuf_release_pinned_bo(dev_priv);

	ret = ttm_bo_reserve(bo, interruptible, false, NULL);
	if (unlikely(ret != 0))
		goto err;

	vmw_bo_placement_set(buf,
			     VMW_BO_DOMAIN_GMR | VMW_BO_DOMAIN_VRAM,
			     VMW_BO_DOMAIN_GMR);
	ret = ttm_bo_validate(bo, &buf->placement, &ctx);
	if (likely(ret == 0) || ret == -ERESTARTSYS)
		goto out_unreserve;

	vmw_bo_placement_set(buf,
			     VMW_BO_DOMAIN_VRAM,
			     VMW_BO_DOMAIN_VRAM);
	ret = ttm_bo_validate(bo, &buf->placement, &ctx);

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
		       struct vmw_bo *buf,
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
				struct vmw_bo *buf,
				bool interruptible)
{
	struct ttm_operation_ctx ctx = {interruptible, false };
	struct ttm_buffer_object *bo = &buf->tbo;
	int ret = 0;

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
	    bo->resource->start < PFN_UP(bo->resource->size) &&
	    bo->resource->start > 0 &&
	    buf->tbo.pin_count == 0) {
		ctx.interruptible = false;
		vmw_bo_placement_set(buf,
				     VMW_BO_DOMAIN_SYS,
				     VMW_BO_DOMAIN_SYS);
		(void)ttm_bo_validate(bo, &buf->placement, &ctx);
	}

	vmw_bo_placement_set(buf,
			     VMW_BO_DOMAIN_VRAM,
			     VMW_BO_DOMAIN_VRAM);
	buf->places[0].lpfn = PFN_UP(bo->resource->size);
	ret = ttm_bo_validate(bo, &buf->placement, &ctx);

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
		 struct vmw_bo *buf,
		 bool interruptible)
{
	struct ttm_buffer_object *bo = &buf->tbo;
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
void vmw_bo_pin_reserved(struct vmw_bo *vbo, bool pin)
{
	struct ttm_operation_ctx ctx = { false, true };
	struct ttm_place pl;
	struct ttm_placement placement;
	struct ttm_buffer_object *bo = &vbo->tbo;
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
void *vmw_bo_map_and_cache(struct vmw_bo *vbo)
{
	struct ttm_buffer_object *bo = &vbo->tbo;
	bool not_used;
	void *virtual;
	int ret;

	virtual = ttm_kmap_obj_virtual(&vbo->map, &not_used);
	if (virtual)
		return virtual;

	ret = ttm_bo_kmap(bo, 0, PFN_UP(bo->base.size), &vbo->map);
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
 * vmw_bo_map_and_cache().
 */
void vmw_bo_unmap(struct vmw_bo *vbo)
{
	if (vbo->map.bo == NULL)
		return;

	ttm_bo_kunmap(&vbo->map);
	vbo->map.bo = NULL;
}


/**
 * vmw_bo_init - Initialize a vmw buffer object
 *
 * @dev_priv: Pointer to the device private struct
 * @vmw_bo: Buffer object to initialize
 * @params: Parameters used to initialize the buffer object
 * @destroy: The function used to delete the buffer object
 * Returns: Zero on success, negative error code on error.
 *
 */
static int vmw_bo_init(struct vmw_private *dev_priv,
		       struct vmw_bo *vmw_bo,
		       struct vmw_bo_params *params,
		       void (*destroy)(struct ttm_buffer_object *))
{
	struct ttm_operation_ctx ctx = {
		.interruptible = params->bo_type != ttm_bo_type_kernel,
		.no_wait_gpu = false
	};
	struct ttm_device *bdev = &dev_priv->bdev;
	struct drm_device *vdev = &dev_priv->drm;
	int ret;

	memset(vmw_bo, 0, sizeof(*vmw_bo));

	BUILD_BUG_ON(TTM_MAX_BO_PRIORITY <= 3);
	vmw_bo->tbo.priority = 3;
	vmw_bo->res_tree = RB_ROOT;

	params->size = ALIGN(params->size, PAGE_SIZE);
	drm_gem_private_object_init(vdev, &vmw_bo->tbo.base, params->size);

	vmw_bo_placement_set(vmw_bo, params->domain, params->busy_domain);
	ret = ttm_bo_init_reserved(bdev, &vmw_bo->tbo, params->bo_type,
				   &vmw_bo->placement, 0, &ctx, NULL,
				   NULL, destroy);
	if (unlikely(ret))
		return ret;

	if (params->pin)
		ttm_bo_pin(&vmw_bo->tbo);
	ttm_bo_unreserve(&vmw_bo->tbo);

	return 0;
}

int vmw_bo_create(struct vmw_private *vmw,
		  struct vmw_bo_params *params,
		  struct vmw_bo **p_bo)
{
	int ret;

	*p_bo = kmalloc(sizeof(**p_bo), GFP_KERNEL);
	if (unlikely(!*p_bo)) {
		DRM_ERROR("Failed to allocate a buffer.\n");
		return -ENOMEM;
	}

	/*
	 * vmw_bo_init will delete the *p_bo object if it fails
	 */
	ret = vmw_bo_init(vmw, *p_bo, params, vmw_bo_free);
	if (unlikely(ret != 0))
		goto out_error;

	return ret;
out_error:
	*p_bo = NULL;
	return ret;
}

/**
 * vmw_user_bo_synccpu_grab - Grab a struct vmw_bo for cpu
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
static int vmw_user_bo_synccpu_grab(struct vmw_bo *vmw_bo,
				    uint32_t flags)
{
	bool nonblock = !!(flags & drm_vmw_synccpu_dontblock);
	struct ttm_buffer_object *bo = &vmw_bo->tbo;
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
	struct vmw_bo *vmw_bo;
	int ret = vmw_user_bo_lookup(filp, handle, &vmw_bo);

	if (!ret) {
		if (!(flags & drm_vmw_synccpu_allow_cs)) {
			atomic_dec(&vmw_bo->cpu_writers);
		}
		ttm_bo_put(&vmw_bo->tbo);
	}

	drm_gem_object_put(&vmw_bo->tbo.base);
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
	struct vmw_bo *vbo;
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
		drm_gem_object_put(&vbo->tbo.base);
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

	return drm_gem_handle_delete(file_priv, arg->handle);
}


/**
 * vmw_user_bo_lookup - Look up a vmw user buffer object from a handle.
 *
 * @filp: The file the handle is registered with.
 * @handle: The user buffer object handle
 * @out: Pointer to a where a pointer to the embedded
 * struct vmw_bo should be placed.
 * Return: Zero on success, Negative error code on error.
 *
 * The vmw buffer object pointer will be refcounted (both ttm and gem)
 */
int vmw_user_bo_lookup(struct drm_file *filp,
		       u32 handle,
		       struct vmw_bo **out)
{
	struct drm_gem_object *gobj;

	gobj = drm_gem_object_lookup(filp, handle);
	if (!gobj) {
		DRM_ERROR("Invalid buffer object handle 0x%08lx.\n",
			  (unsigned long)handle);
		return -ESRCH;
	}

	*out = to_vmw_bo(gobj);
	ttm_bo_get(&(*out)->tbo);

	return 0;
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
	struct vmw_private *dev_priv = vmw_priv_from_ttm(bdev);
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
	struct vmw_bo *vbo;
	int cpp = DIV_ROUND_UP(args->bpp, 8);
	int ret;

	switch (cpp) {
	case 1: /* DRM_FORMAT_C8 */
	case 2: /* DRM_FORMAT_RGB565 */
	case 4: /* DRM_FORMAT_XRGB8888 */
		break;
	default:
		/*
		 * Dumb buffers don't allow anything else.
		 * This is tested via IGT's dumb_buffers
		 */
		return -EINVAL;
	}

	args->pitch = args->width * cpp;
	args->size = ALIGN(args->pitch * args->height, PAGE_SIZE);

	ret = vmw_gem_object_create_with_handle(dev_priv, file_priv,
						args->size, &args->handle,
						&vbo);
	/* drop reference from allocate - handle holds it now */
	drm_gem_object_put(&vbo->tbo.base);
	return ret;
}

/**
 * vmw_bo_swap_notify - swapout notify callback.
 *
 * @bo: The buffer object to be swapped out.
 */
void vmw_bo_swap_notify(struct ttm_buffer_object *bo)
{
	/* Kill any cached kernel maps before swapout */
	vmw_bo_unmap(to_vmw_bo(&bo->base));
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
	struct vmw_bo *vbo = to_vmw_bo(&bo->base);

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

static u32
set_placement_list(struct ttm_place *pl, u32 domain)
{
	u32 n = 0;

	/*
	 * The placements are ordered according to our preferences
	 */
	if (domain & VMW_BO_DOMAIN_MOB) {
		pl[n].mem_type = VMW_PL_MOB;
		pl[n].flags = 0;
		pl[n].fpfn = 0;
		pl[n].lpfn = 0;
		n++;
	}
	if (domain & VMW_BO_DOMAIN_GMR) {
		pl[n].mem_type = VMW_PL_GMR;
		pl[n].flags = 0;
		pl[n].fpfn = 0;
		pl[n].lpfn = 0;
		n++;
	}
	if (domain & VMW_BO_DOMAIN_VRAM) {
		pl[n].mem_type = TTM_PL_VRAM;
		pl[n].flags = 0;
		pl[n].fpfn = 0;
		pl[n].lpfn = 0;
		n++;
	}
	if (domain & VMW_BO_DOMAIN_WAITABLE_SYS) {
		pl[n].mem_type = VMW_PL_SYSTEM;
		pl[n].flags = 0;
		pl[n].fpfn = 0;
		pl[n].lpfn = 0;
		n++;
	}
	if (domain & VMW_BO_DOMAIN_SYS) {
		pl[n].mem_type = TTM_PL_SYSTEM;
		pl[n].flags = 0;
		pl[n].fpfn = 0;
		pl[n].lpfn = 0;
		n++;
	}

	WARN_ON(!n);
	if (!n) {
		pl[n].mem_type = TTM_PL_SYSTEM;
		pl[n].flags = 0;
		pl[n].fpfn = 0;
		pl[n].lpfn = 0;
		n++;
	}
	return n;
}

void vmw_bo_placement_set(struct vmw_bo *bo, u32 domain, u32 busy_domain)
{
	struct ttm_device *bdev = bo->tbo.bdev;
	struct vmw_private *vmw = vmw_priv_from_ttm(bdev);
	struct ttm_placement *pl = &bo->placement;
	bool mem_compatible = false;
	u32 i;

	pl->placement = bo->places;
	pl->num_placement = set_placement_list(bo->places, domain);

	if (drm_debug_enabled(DRM_UT_DRIVER) && bo->tbo.resource) {
		for (i = 0; i < pl->num_placement; ++i) {
			if (bo->tbo.resource->mem_type == TTM_PL_SYSTEM ||
			    bo->tbo.resource->mem_type == pl->placement[i].mem_type)
				mem_compatible = true;
		}
		if (!mem_compatible)
			drm_warn(&vmw->drm,
				 "%s: Incompatible transition from "
				 "bo->base.resource->mem_type = %u to domain = %u\n",
				 __func__, bo->tbo.resource->mem_type, domain);
	}

	pl->busy_placement = bo->busy_places;
	pl->num_busy_placement = set_placement_list(bo->busy_places, busy_domain);
}

void vmw_bo_placement_set_default_accelerated(struct vmw_bo *bo)
{
	struct ttm_device *bdev = bo->tbo.bdev;
	struct vmw_private *vmw = vmw_priv_from_ttm(bdev);
	u32 domain = VMW_BO_DOMAIN_GMR | VMW_BO_DOMAIN_VRAM;

	if (vmw->has_mob)
		domain = VMW_BO_DOMAIN_MOB;

	vmw_bo_placement_set(bo, domain, domain);
}
