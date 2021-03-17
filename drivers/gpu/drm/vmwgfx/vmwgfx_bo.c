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
 * struct vmw_user_buffer_object - User-space-visible buffer object
 *
 * @prime: The prime object providing user visibility.
 * @vbo: The struct vmw_buffer_object
 */
struct vmw_user_buffer_object {
	struct ttm_prime_object prime;
	struct vmw_buffer_object vbo;
};


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
 * vmw_user_buffer_object - Convert a struct ttm_buffer_object to a struct
 * vmw_user_buffer_object.
 *
 * @bo: Pointer to the TTM buffer object.
 * Return: Pointer to the struct vmw_buffer_object embedding the TTM buffer
 * object.
 */
static struct vmw_user_buffer_object *
vmw_user_buffer_object(struct ttm_buffer_object *bo)
{
	struct vmw_buffer_object *vmw_bo = vmw_buffer_object(bo);

	return container_of(vmw_bo, struct vmw_user_buffer_object, vbo);
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
	uint32_t new_flags;

	ret = ttm_write_lock(&dev_priv->reservation_sem, interruptible);
	if (unlikely(ret != 0))
		return ret;

	vmw_execbuf_release_pinned_bo(dev_priv);

	ret = ttm_bo_reserve(bo, interruptible, false, NULL);
	if (unlikely(ret != 0))
		goto err;

	if (buf->pin_count > 0)
		ret = ttm_bo_mem_compat(placement, &bo->mem,
					&new_flags) == true ? 0 : -EINVAL;
	else
		ret = ttm_bo_validate(bo, placement, &ctx);

	if (!ret)
		vmw_bo_pin_reserved(buf, true);

	ttm_bo_unreserve(bo);

err:
	ttm_write_unlock(&dev_priv->reservation_sem);
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
 * @pin:  Pin buffer if true.
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
	uint32_t new_flags;

	ret = ttm_write_lock(&dev_priv->reservation_sem, interruptible);
	if (unlikely(ret != 0))
		return ret;

	vmw_execbuf_release_pinned_bo(dev_priv);

	ret = ttm_bo_reserve(bo, interruptible, false, NULL);
	if (unlikely(ret != 0))
		goto err;

	if (buf->pin_count > 0) {
		ret = ttm_bo_mem_compat(&vmw_vram_gmr_placement, &bo->mem,
					&new_flags) == true ? 0 : -EINVAL;
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
	ttm_write_unlock(&dev_priv->reservation_sem);
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
	uint32_t new_flags;

	place = vmw_vram_placement.placement[0];
	place.lpfn = bo->num_pages;
	placement.num_placement = 1;
	placement.placement = &place;
	placement.num_busy_placement = 1;
	placement.busy_placement = &place;

	ret = ttm_write_lock(&dev_priv->reservation_sem, interruptible);
	if (unlikely(ret != 0))
		return ret;

	vmw_execbuf_release_pinned_bo(dev_priv);
	ret = ttm_bo_reserve(bo, interruptible, false, NULL);
	if (unlikely(ret != 0))
		goto err_unlock;

	/*
	 * Is this buffer already in vram but not at the start of it?
	 * In that case, evict it first because TTM isn't good at handling
	 * that situation.
	 */
	if (bo->mem.mem_type == TTM_PL_VRAM &&
	    bo->mem.start < bo->num_pages &&
	    bo->mem.start > 0 &&
	    buf->pin_count == 0) {
		ctx.interruptible = false;
		(void) ttm_bo_validate(bo, &vmw_sys_placement, &ctx);
	}

	if (buf->pin_count > 0)
		ret = ttm_bo_mem_compat(&placement, &bo->mem,
					&new_flags) == true ? 0 : -EINVAL;
	else
		ret = ttm_bo_validate(bo, &placement, &ctx);

	/* For some reason we didn't end up at the start of vram */
	WARN_ON(ret == 0 && bo->mem.start != 0);
	if (!ret)
		vmw_bo_pin_reserved(buf, true);

	ttm_bo_unreserve(bo);
err_unlock:
	ttm_write_unlock(&dev_priv->reservation_sem);

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

	ret = ttm_read_lock(&dev_priv->reservation_sem, interruptible);
	if (unlikely(ret != 0))
		return ret;

	ret = ttm_bo_reserve(bo, interruptible, false, NULL);
	if (unlikely(ret != 0))
		goto err;

	vmw_bo_pin_reserved(buf, false);

	ttm_bo_unreserve(bo);

err:
	ttm_read_unlock(&dev_priv->reservation_sem);
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
	if (bo->mem.mem_type == TTM_PL_VRAM) {
		ptr->gmrId = SVGA_GMR_FRAMEBUFFER;
		ptr->offset = bo->mem.start << PAGE_SHIFT;
	} else {
		ptr->gmrId = bo->mem.start;
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
	uint32_t old_mem_type = bo->mem.mem_type;
	int ret;

	dma_resv_assert_held(bo->base.resv);

	if (pin) {
		if (vbo->pin_count++ > 0)
			return;
	} else {
		WARN_ON(vbo->pin_count <= 0);
		if (--vbo->pin_count > 0)
			return;
	}

	pl.fpfn = 0;
	pl.lpfn = 0;
	pl.mem_type = bo->mem.mem_type;
	pl.flags = bo->mem.placement;
	if (pin)
		pl.flags |= TTM_PL_FLAG_NO_EVICT;
	else
		pl.flags &= ~TTM_PL_FLAG_NO_EVICT;

	memset(&placement, 0, sizeof(placement));
	placement.num_placement = 1;
	placement.placement = &pl;

	ret = ttm_bo_validate(bo, &placement, &ctx);

	BUG_ON(ret != 0 || bo->mem.mem_type != old_mem_type);
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

	ret = ttm_bo_kmap(bo, 0, bo->num_pages, &vbo->map);
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
 * vmw_bo_acc_size - Calculate the pinned memory usage of buffers
 *
 * @dev_priv: Pointer to a struct vmw_private identifying the device.
 * @size: The requested buffer size.
 * @user: Whether this is an ordinary dma buffer or a user dma buffer.
 */
static size_t vmw_bo_acc_size(struct vmw_private *dev_priv, size_t size,
			      bool user)
{
	static size_t struct_size, user_struct_size;
	size_t num_pages = PAGE_ALIGN(size) >> PAGE_SHIFT;
	size_t page_array_size = ttm_round_pot(num_pages * sizeof(void *));

	if (unlikely(struct_size == 0)) {
		size_t backend_size = ttm_round_pot(vmw_tt_size);

		struct_size = backend_size +
			ttm_round_pot(sizeof(struct vmw_buffer_object));
		user_struct_size = backend_size +
		  ttm_round_pot(sizeof(struct vmw_user_buffer_object)) +
				      TTM_OBJ_EXTRA_SIZE;
	}

	if (dev_priv->map_mode == vmw_dma_alloc_coherent)
		page_array_size +=
			ttm_round_pot(num_pages * sizeof(dma_addr_t));

	return ((user) ? user_struct_size : struct_size) +
		page_array_size;
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
	kfree(vmw_bo);
}


/**
 * vmw_user_bo_destroy - vmw buffer object destructor
 *
 * @bo: Pointer to the embedded struct ttm_buffer_object
 */
static void vmw_user_bo_destroy(struct ttm_buffer_object *bo)
{
	struct vmw_user_buffer_object *vmw_user_bo = vmw_user_buffer_object(bo);
	struct vmw_buffer_object *vbo = &vmw_user_bo->vbo;

	WARN_ON(vbo->dirty);
	WARN_ON(!RB_EMPTY_ROOT(&vbo->res_tree));
	vmw_bo_unmap(vbo);
	ttm_prime_object_kfree(vmw_user_bo, prime);
}


/**
 * vmw_bo_init - Initialize a vmw buffer object
 *
 * @dev_priv: Pointer to the device private struct
 * @vmw_bo: Pointer to the struct vmw_buffer_object to initialize.
 * @size: Buffer object size in bytes.
 * @placement: Initial placement.
 * @interruptible: Whether waits should be performed interruptible.
 * @bo_free: The buffer object destructor.
 * Returns: Zero on success, negative error code on error.
 *
 * Note that on error, the code will free the buffer object.
 */
int vmw_bo_init(struct vmw_private *dev_priv,
		struct vmw_buffer_object *vmw_bo,
		size_t size, struct ttm_placement *placement,
		bool interruptible,
		void (*bo_free)(struct ttm_buffer_object *bo))
{
	struct ttm_bo_device *bdev = &dev_priv->bdev;
	size_t acc_size;
	int ret;
	bool user = (bo_free == &vmw_user_bo_destroy);

	WARN_ON_ONCE(!bo_free && (!user && (bo_free != vmw_bo_bo_free)));

	acc_size = vmw_bo_acc_size(dev_priv, size, user);
	memset(vmw_bo, 0, sizeof(*vmw_bo));
	BUILD_BUG_ON(TTM_MAX_BO_PRIORITY <= 3);
	vmw_bo->base.priority = 3;
	vmw_bo->res_tree = RB_ROOT;

	ret = ttm_bo_init(bdev, &vmw_bo->base, size,
			  ttm_bo_type_device, placement,
			  0, interruptible, acc_size,
			  NULL, NULL, bo_free);
	return ret;
}


/**
 * vmw_user_bo_release - TTM reference base object release callback for
 * vmw user buffer objects
 *
 * @p_base: The TTM base object pointer about to be unreferenced.
 *
 * Clears the TTM base object pointer and drops the reference the
 * base object has on the underlying struct vmw_buffer_object.
 */
static void vmw_user_bo_release(struct ttm_base_object **p_base)
{
	struct vmw_user_buffer_object *vmw_user_bo;
	struct ttm_base_object *base = *p_base;

	*p_base = NULL;

	if (unlikely(base == NULL))
		return;

	vmw_user_bo = container_of(base, struct vmw_user_buffer_object,
				   prime.base);
	ttm_bo_put(&vmw_user_bo->vbo.base);
}


/**
 * vmw_user_bo_ref_obj-release - TTM synccpu reference object release callback
 * for vmw user buffer objects
 *
 * @base: Pointer to the TTM base object
 * @ref_type: Reference type of the reference reaching zero.
 *
 * Called when user-space drops its last synccpu reference on the buffer
 * object, Either explicitly or as part of a cleanup file close.
 */
static void vmw_user_bo_ref_obj_release(struct ttm_base_object *base,
					enum ttm_ref_type ref_type)
{
	struct vmw_user_buffer_object *user_bo;

	user_bo = container_of(base, struct vmw_user_buffer_object, prime.base);

	switch (ref_type) {
	case TTM_REF_SYNCCPU_WRITE:
		atomic_dec(&user_bo->vbo.cpu_writers);
		break;
	default:
		WARN_ONCE(true, "Undefined buffer object reference release.\n");
	}
}


/**
 * vmw_user_bo_alloc - Allocate a user buffer object
 *
 * @dev_priv: Pointer to a struct device private.
 * @tfile: Pointer to a struct ttm_object_file on which to register the user
 * object.
 * @size: Size of the buffer object.
 * @shareable: Boolean whether the buffer is shareable with other open files.
 * @handle: Pointer to where the handle value should be assigned.
 * @p_vbo: Pointer to where the refcounted struct vmw_buffer_object pointer
 * should be assigned.
 * Return: Zero on success, negative error code on error.
 */
int vmw_user_bo_alloc(struct vmw_private *dev_priv,
		      struct ttm_object_file *tfile,
		      uint32_t size,
		      bool shareable,
		      uint32_t *handle,
		      struct vmw_buffer_object **p_vbo,
		      struct ttm_base_object **p_base)
{
	struct vmw_user_buffer_object *user_bo;
	int ret;

	user_bo = kzalloc(sizeof(*user_bo), GFP_KERNEL);
	if (unlikely(!user_bo)) {
		DRM_ERROR("Failed to allocate a buffer.\n");
		return -ENOMEM;
	}

	ret = vmw_bo_init(dev_priv, &user_bo->vbo, size,
			  (dev_priv->has_mob) ?
			  &vmw_sys_placement :
			  &vmw_vram_sys_placement, true,
			  &vmw_user_bo_destroy);
	if (unlikely(ret != 0))
		return ret;

	ttm_bo_get(&user_bo->vbo.base);
	ret = ttm_prime_object_init(tfile,
				    size,
				    &user_bo->prime,
				    shareable,
				    ttm_buffer_type,
				    &vmw_user_bo_release,
				    &vmw_user_bo_ref_obj_release);
	if (unlikely(ret != 0)) {
		ttm_bo_put(&user_bo->vbo.base);
		goto out_no_base_object;
	}

	*p_vbo = &user_bo->vbo;
	if (p_base) {
		*p_base = &user_bo->prime.base;
		kref_get(&(*p_base)->refcount);
	}
	*handle = user_bo->prime.base.handle;

out_no_base_object:
	return ret;
}


/**
 * vmw_user_bo_verify_access - verify access permissions on this
 * buffer object.
 *
 * @bo: Pointer to the buffer object being accessed
 * @tfile: Identifying the caller.
 */
int vmw_user_bo_verify_access(struct ttm_buffer_object *bo,
			      struct ttm_object_file *tfile)
{
	struct vmw_user_buffer_object *vmw_user_bo;

	if (unlikely(bo->destroy != vmw_user_bo_destroy))
		return -EPERM;

	vmw_user_bo = vmw_user_buffer_object(bo);

	/* Check that the caller has opened the object. */
	if (likely(ttm_ref_object_exists(tfile, &vmw_user_bo->prime.base)))
		return 0;

	DRM_ERROR("Could not grant buffer access.\n");
	return -EPERM;
}


/**
 * vmw_user_bo_synccpu_grab - Grab a struct vmw_user_buffer_object for cpu
 * access, idling previous GPU operations on the buffer and optionally
 * blocking it for further command submissions.
 *
 * @user_bo: Pointer to the buffer object being grabbed for CPU access
 * @tfile: Identifying the caller.
 * @flags: Flags indicating how the grab should be performed.
 * Return: Zero on success, Negative error code on error. In particular,
 * -EBUSY will be returned if a dontblock operation is requested and the
 * buffer object is busy, and -ERESTARTSYS will be returned if a wait is
 * interrupted by a signal.
 *
 * A blocking grab will be automatically released when @tfile is closed.
 */
static int vmw_user_bo_synccpu_grab(struct vmw_user_buffer_object *user_bo,
				    struct ttm_object_file *tfile,
				    uint32_t flags)
{
	bool nonblock = !!(flags & drm_vmw_synccpu_dontblock);
	struct ttm_buffer_object *bo = &user_bo->vbo.base;
	bool existed;
	int ret;

	if (flags & drm_vmw_synccpu_allow_cs) {
		long lret;

		lret = dma_resv_wait_timeout_rcu
			(bo->base.resv, true, true,
			 nonblock ? 0 : MAX_SCHEDULE_TIMEOUT);
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
		atomic_inc(&user_bo->vbo.cpu_writers);

	ttm_bo_unreserve(bo);
	if (unlikely(ret != 0))
		return ret;

	ret = ttm_ref_object_add(tfile, &user_bo->prime.base,
				 TTM_REF_SYNCCPU_WRITE, &existed, false);
	if (ret != 0 || existed)
		atomic_dec(&user_bo->vbo.cpu_writers);

	return ret;
}

/**
 * vmw_user_bo_synccpu_release - Release a previous grab for CPU access,
 * and unblock command submission on the buffer if blocked.
 *
 * @handle: Handle identifying the buffer object.
 * @tfile: Identifying the caller.
 * @flags: Flags indicating the type of release.
 */
static int vmw_user_bo_synccpu_release(uint32_t handle,
					   struct ttm_object_file *tfile,
					   uint32_t flags)
{
	if (!(flags & drm_vmw_synccpu_allow_cs))
		return ttm_ref_object_base_unref(tfile, handle,
						 TTM_REF_SYNCCPU_WRITE);

	return 0;
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
	struct vmw_user_buffer_object *user_bo;
	struct ttm_object_file *tfile = vmw_fpriv(file_priv)->tfile;
	struct ttm_base_object *buffer_base;
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
		ret = vmw_user_bo_lookup(tfile, arg->handle, &vbo,
					     &buffer_base);
		if (unlikely(ret != 0))
			return ret;

		user_bo = container_of(vbo, struct vmw_user_buffer_object,
				       vbo);
		ret = vmw_user_bo_synccpu_grab(user_bo, tfile, arg->flags);
		vmw_bo_unreference(&vbo);
		ttm_base_object_unref(&buffer_base);
		if (unlikely(ret != 0 && ret != -ERESTARTSYS &&
			     ret != -EBUSY)) {
			DRM_ERROR("Failed synccpu grab on handle 0x%08x.\n",
				  (unsigned int) arg->handle);
			return ret;
		}
		break;
	case drm_vmw_synccpu_release:
		ret = vmw_user_bo_synccpu_release(arg->handle, tfile,
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
 * vmw_bo_alloc_ioctl - ioctl function implementing the buffer object
 * allocation functionality.
 *
 * @dev: Identifies the drm device.
 * @data: Pointer to the ioctl argument.
 * @file_priv: Identifies the caller.
 * Return: Zero on success, negative error code on error.
 *
 * This function checks the ioctl arguments for validity and allocates a
 * struct vmw_user_buffer_object bo.
 */
int vmw_bo_alloc_ioctl(struct drm_device *dev, void *data,
		       struct drm_file *file_priv)
{
	struct vmw_private *dev_priv = vmw_priv(dev);
	union drm_vmw_alloc_dmabuf_arg *arg =
	    (union drm_vmw_alloc_dmabuf_arg *)data;
	struct drm_vmw_alloc_dmabuf_req *req = &arg->req;
	struct drm_vmw_dmabuf_rep *rep = &arg->rep;
	struct vmw_buffer_object *vbo;
	uint32_t handle;
	int ret;

	ret = ttm_read_lock(&dev_priv->reservation_sem, true);
	if (unlikely(ret != 0))
		return ret;

	ret = vmw_user_bo_alloc(dev_priv, vmw_fpriv(file_priv)->tfile,
				req->size, false, &handle, &vbo,
				NULL);
	if (unlikely(ret != 0))
		goto out_no_bo;

	rep->handle = handle;
	rep->map_handle = drm_vma_node_offset_addr(&vbo->base.base.vma_node);
	rep->cur_gmr_id = handle;
	rep->cur_gmr_offset = 0;

	vmw_bo_unreference(&vbo);

out_no_bo:
	ttm_read_unlock(&dev_priv->reservation_sem);

	return ret;
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

	return ttm_ref_object_base_unref(vmw_fpriv(file_priv)->tfile,
					 arg->handle,
					 TTM_REF_USAGE);
}


/**
 * vmw_user_bo_lookup - Look up a vmw user buffer object from a handle.
 *
 * @tfile: The TTM object file the handle is registered with.
 * @handle: The user buffer object handle
 * @out: Pointer to a where a pointer to the embedded
 * struct vmw_buffer_object should be placed.
 * @p_base: Pointer to where a pointer to the TTM base object should be
 * placed, or NULL if no such pointer is required.
 * Return: Zero on success, Negative error code on error.
 *
 * Both the output base object pointer and the vmw buffer object pointer
 * will be refcounted.
 */
int vmw_user_bo_lookup(struct ttm_object_file *tfile,
		       uint32_t handle, struct vmw_buffer_object **out,
		       struct ttm_base_object **p_base)
{
	struct vmw_user_buffer_object *vmw_user_bo;
	struct ttm_base_object *base;

	base = ttm_base_object_lookup(tfile, handle);
	if (unlikely(base == NULL)) {
		DRM_ERROR("Invalid buffer object handle 0x%08lx.\n",
			  (unsigned long)handle);
		return -ESRCH;
	}

	if (unlikely(ttm_base_object_type(base) != ttm_buffer_type)) {
		ttm_base_object_unref(&base);
		DRM_ERROR("Invalid buffer object handle 0x%08lx.\n",
			  (unsigned long)handle);
		return -EINVAL;
	}

	vmw_user_bo = container_of(base, struct vmw_user_buffer_object,
				   prime.base);
	ttm_bo_get(&vmw_user_bo->vbo.base);
	if (p_base)
		*p_base = base;
	else
		ttm_base_object_unref(&base);
	*out = &vmw_user_bo->vbo;

	return 0;
}

/**
 * vmw_user_bo_noref_lookup - Look up a vmw user buffer object without reference
 * @tfile: The TTM object file the handle is registered with.
 * @handle: The user buffer object handle.
 *
 * This function looks up a struct vmw_user_bo and returns a pointer to the
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
vmw_user_bo_noref_lookup(struct ttm_object_file *tfile, u32 handle)
{
	struct vmw_user_buffer_object *vmw_user_bo;
	struct ttm_base_object *base;

	base = ttm_base_object_noref_lookup(tfile, handle);
	if (!base) {
		DRM_ERROR("Invalid buffer object handle 0x%08lx.\n",
			  (unsigned long)handle);
		return ERR_PTR(-ESRCH);
	}

	if (unlikely(ttm_base_object_type(base) != ttm_buffer_type)) {
		ttm_base_object_noref_release();
		DRM_ERROR("Invalid buffer object handle 0x%08lx.\n",
			  (unsigned long)handle);
		return ERR_PTR(-EINVAL);
	}

	vmw_user_bo = container_of(base, struct vmw_user_buffer_object,
				   prime.base);
	return &vmw_user_bo->vbo;
}

/**
 * vmw_user_bo_reference - Open a handle to a vmw user buffer object.
 *
 * @tfile: The TTM object file to register the handle with.
 * @vbo: The embedded vmw buffer object.
 * @handle: Pointer to where the new handle should be placed.
 * Return: Zero on success, Negative error code on error.
 */
int vmw_user_bo_reference(struct ttm_object_file *tfile,
			  struct vmw_buffer_object *vbo,
			  uint32_t *handle)
{
	struct vmw_user_buffer_object *user_bo;

	if (vbo->base.destroy != vmw_user_bo_destroy)
		return -EINVAL;

	user_bo = container_of(vbo, struct vmw_user_buffer_object, vbo);

	*handle = user_bo->prime.base.handle;
	return ttm_ref_object_add(tfile, &user_bo->prime.base,
				  TTM_REF_USAGE, NULL, false);
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
	struct ttm_bo_device *bdev = bo->bdev;

	struct vmw_private *dev_priv =
		container_of(bdev, struct vmw_private, bdev);

	if (fence == NULL) {
		vmw_execbuf_fence_commands(NULL, dev_priv, &fence, NULL);
		dma_resv_add_excl_fence(bo->base.resv, &fence->base);
		dma_fence_put(&fence->base);
	} else
		dma_resv_add_excl_fence(bo->base.resv, &fence->base);
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
	args->size = args->pitch * args->height;

	ret = ttm_read_lock(&dev_priv->reservation_sem, true);
	if (unlikely(ret != 0))
		return ret;

	ret = vmw_user_bo_alloc(dev_priv, vmw_fpriv(file_priv)->tfile,
				    args->size, false, &args->handle,
				    &vbo, NULL);
	if (unlikely(ret != 0))
		goto out_no_bo;

	vmw_bo_unreference(&vbo);
out_no_bo:
	ttm_read_unlock(&dev_priv->reservation_sem);
	return ret;
}


/**
 * vmw_dumb_map_offset - Return the address space offset of a dumb buffer
 *
 * @file_priv: Pointer to a struct drm_file identifying the caller.
 * @dev: Pointer to the drm device.
 * @handle: Handle identifying the dumb buffer.
 * @offset: The address space offset returned.
 * Return: Zero on success, negative error code on failure.
 *
 * This is a driver callback for the core drm dumb_map_offset functionality.
 */
int vmw_dumb_map_offset(struct drm_file *file_priv,
			struct drm_device *dev, uint32_t handle,
			uint64_t *offset)
{
	struct ttm_object_file *tfile = vmw_fpriv(file_priv)->tfile;
	struct vmw_buffer_object *out_buf;
	int ret;

	ret = vmw_user_bo_lookup(tfile, handle, &out_buf, NULL);
	if (ret != 0)
		return -EINVAL;

	*offset = drm_vma_node_offset_addr(&out_buf->base.base.vma_node);
	vmw_bo_unreference(&out_buf);
	return 0;
}


/**
 * vmw_dumb_destroy - Destroy a dumb boffer
 *
 * @file_priv: Pointer to a struct drm_file identifying the caller.
 * @dev: Pointer to the drm device.
 * @handle: Handle identifying the dumb buffer.
 * Return: Zero on success, negative error code on failure.
 *
 * This is a driver callback for the core drm dumb_destroy functionality.
 */
int vmw_dumb_destroy(struct drm_file *file_priv,
		     struct drm_device *dev,
		     uint32_t handle)
{
	return ttm_ref_object_base_unref(vmw_fpriv(file_priv)->tfile,
					 handle, TTM_REF_USAGE);
}


/**
 * vmw_bo_swap_notify - swapout notify callback.
 *
 * @bo: The buffer object to be swapped out.
 */
void vmw_bo_swap_notify(struct ttm_buffer_object *bo)
{
	/* Is @bo embedded in a struct vmw_buffer_object? */
	if (bo->destroy != vmw_bo_bo_free &&
	    bo->destroy != vmw_user_bo_destroy)
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

	if (mem == NULL)
		return;

	/* Make sure @bo is embedded in a struct vmw_buffer_object? */
	if (bo->destroy != vmw_bo_bo_free &&
	    bo->destroy != vmw_user_bo_destroy)
		return;

	vbo = container_of(bo, struct vmw_buffer_object, base);

	/*
	 * Kill any cached kernel maps before move to or from VRAM.
	 * With other types of moves, the underlying pages stay the same,
	 * and the map can be kept.
	 */
	if (mem->mem_type == TTM_PL_VRAM || bo->mem.mem_type == TTM_PL_VRAM)
		vmw_bo_unmap(vbo);

	/*
	 * If we're moving a backup MOB out of MOB placement, then make sure we
	 * read back all resource content first, and unbind the MOB from
	 * the resource.
	 */
	if (mem->mem_type != VMW_PL_MOB && bo->mem.mem_type == VMW_PL_MOB)
		vmw_resource_unbind_list(vbo);
}
