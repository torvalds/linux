/**************************************************************************
 *
 * Copyright © 2011 VMware, Inc., Palo Alto, CA., USA
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

#include <drm/drmP.h>
#include "vmwgfx_drv.h"


/**
 * vmw_dmabuf_to_placement - Validate a buffer to placement.
 *
 * @dev_priv:  Driver private.
 * @buf:  DMA buffer to move.
 * @pin:  Pin buffer if true.
 * @interruptible:  Use interruptible wait.
 *
 * May only be called by the current master since it assumes that the
 * master lock is the current master's lock.
 * This function takes the master's lock in write mode.
 * Flushes and unpins the query bo to avoid failures.
 *
 * Returns
 *  -ERESTARTSYS if interrupted by a signal.
 */
int vmw_dmabuf_to_placement(struct vmw_private *dev_priv,
			    struct vmw_dma_buffer *buf,
			    struct ttm_placement *placement,
			    bool interruptible)
{
	struct vmw_master *vmaster = dev_priv->active_master;
	struct ttm_buffer_object *bo = &buf->base;
	int ret;

	ret = ttm_write_lock(&vmaster->lock, interruptible);
	if (unlikely(ret != 0))
		return ret;

	vmw_execbuf_release_pinned_bo(dev_priv);

	ret = ttm_bo_reserve(bo, interruptible, false, false, 0);
	if (unlikely(ret != 0))
		goto err;

	ret = ttm_bo_validate(bo, placement, interruptible, false, false);

	ttm_bo_unreserve(bo);

err:
	ttm_write_unlock(&vmaster->lock);
	return ret;
}

/**
 * vmw_dmabuf_to_vram_or_gmr - Move a buffer to vram or gmr.
 *
 * May only be called by the current master since it assumes that the
 * master lock is the current master's lock.
 * This function takes the master's lock in write mode.
 * Flushes and unpins the query bo if @pin == true to avoid failures.
 *
 * @dev_priv:  Driver private.
 * @buf:  DMA buffer to move.
 * @pin:  Pin buffer if true.
 * @interruptible:  Use interruptible wait.
 *
 * Returns
 * -ERESTARTSYS if interrupted by a signal.
 */
int vmw_dmabuf_to_vram_or_gmr(struct vmw_private *dev_priv,
			      struct vmw_dma_buffer *buf,
			      bool pin, bool interruptible)
{
	struct vmw_master *vmaster = dev_priv->active_master;
	struct ttm_buffer_object *bo = &buf->base;
	struct ttm_placement *placement;
	int ret;

	ret = ttm_write_lock(&vmaster->lock, interruptible);
	if (unlikely(ret != 0))
		return ret;

	if (pin)
		vmw_execbuf_release_pinned_bo(dev_priv);

	ret = ttm_bo_reserve(bo, interruptible, false, false, 0);
	if (unlikely(ret != 0))
		goto err;

	/**
	 * Put BO in VRAM if there is space, otherwise as a GMR.
	 * If there is no space in VRAM and GMR ids are all used up,
	 * start evicting GMRs to make room. If the DMA buffer can't be
	 * used as a GMR, this will return -ENOMEM.
	 */

	if (pin)
		placement = &vmw_vram_gmr_ne_placement;
	else
		placement = &vmw_vram_gmr_placement;

	ret = ttm_bo_validate(bo, placement, interruptible, false, false);
	if (likely(ret == 0) || ret == -ERESTARTSYS)
		goto err_unreserve;


	/**
	 * If that failed, try VRAM again, this time evicting
	 * previous contents.
	 */

	if (pin)
		placement = &vmw_vram_ne_placement;
	else
		placement = &vmw_vram_placement;

	ret = ttm_bo_validate(bo, placement, interruptible, false, false);

err_unreserve:
	ttm_bo_unreserve(bo);
err:
	ttm_write_unlock(&vmaster->lock);
	return ret;
}

/**
 * vmw_dmabuf_to_vram - Move a buffer to vram.
 *
 * May only be called by the current master since it assumes that the
 * master lock is the current master's lock.
 * This function takes the master's lock in write mode.
 *
 * @dev_priv:  Driver private.
 * @buf:  DMA buffer to move.
 * @pin:  Pin buffer in vram if true.
 * @interruptible:  Use interruptible wait.
 *
 * Returns
 * -ERESTARTSYS if interrupted by a signal.
 */
int vmw_dmabuf_to_vram(struct vmw_private *dev_priv,
		       struct vmw_dma_buffer *buf,
		       bool pin, bool interruptible)
{
	struct ttm_placement *placement;

	if (pin)
		placement = &vmw_vram_ne_placement;
	else
		placement = &vmw_vram_placement;

	return vmw_dmabuf_to_placement(dev_priv, buf,
				       placement,
				       interruptible);
}

/**
 * vmw_dmabuf_to_start_of_vram - Move a buffer to start of vram.
 *
 * May only be called by the current master since it assumes that the
 * master lock is the current master's lock.
 * This function takes the master's lock in write mode.
 * Flushes and unpins the query bo if @pin == true to avoid failures.
 *
 * @dev_priv:  Driver private.
 * @buf:  DMA buffer to move.
 * @pin:  Pin buffer in vram if true.
 * @interruptible:  Use interruptible wait.
 *
 * Returns
 * -ERESTARTSYS if interrupted by a signal.
 */
int vmw_dmabuf_to_start_of_vram(struct vmw_private *dev_priv,
				struct vmw_dma_buffer *buf,
				bool pin, bool interruptible)
{
	struct vmw_master *vmaster = dev_priv->active_master;
	struct ttm_buffer_object *bo = &buf->base;
	struct ttm_placement placement;
	int ret = 0;

	if (pin)
		placement = vmw_vram_ne_placement;
	else
		placement = vmw_vram_placement;
	placement.lpfn = bo->num_pages;

	ret = ttm_write_lock(&vmaster->lock, interruptible);
	if (unlikely(ret != 0))
		return ret;

	if (pin)
		vmw_execbuf_release_pinned_bo(dev_priv);
	ret = ttm_bo_reserve(bo, interruptible, false, false, 0);
	if (unlikely(ret != 0))
		goto err_unlock;

	/* Is this buffer already in vram but not at the start of it? */
	if (bo->mem.mem_type == TTM_PL_VRAM &&
	    bo->mem.start < bo->num_pages &&
	    bo->mem.start > 0)
		(void) ttm_bo_validate(bo, &vmw_sys_placement, false,
				       false, false);

	ret = ttm_bo_validate(bo, &placement, interruptible, false, false);

	/* For some reason we didn't up at the start of vram */
	WARN_ON(ret == 0 && bo->offset != 0);

	ttm_bo_unreserve(bo);
err_unlock:
	ttm_write_unlock(&vmaster->lock);

	return ret;
}


/**
 * vmw_dmabuf_upin - Unpin the buffer given buffer, does not move the buffer.
 *
 * May only be called by the current master since it assumes that the
 * master lock is the current master's lock.
 * This function takes the master's lock in write mode.
 *
 * @dev_priv:  Driver private.
 * @buf:  DMA buffer to unpin.
 * @interruptible:  Use interruptible wait.
 *
 * Returns
 * -ERESTARTSYS if interrupted by a signal.
 */
int vmw_dmabuf_unpin(struct vmw_private *dev_priv,
		     struct vmw_dma_buffer *buf,
		     bool interruptible)
{
	/*
	 * We could in theory early out if the buffer is
	 * unpinned but we need to lock and reserve the buffer
	 * anyways so we don't gain much by that.
	 */
	return vmw_dmabuf_to_placement(dev_priv, buf,
				       &vmw_evictable_placement,
				       interruptible);
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
		ptr->offset = bo->offset;
	} else {
		ptr->gmrId = bo->mem.start;
		ptr->offset = 0;
	}
}


/**
 * vmw_bo_pin - Pin or unpin a buffer object without moving it.
 *
 * @bo: The buffer object. Must be reserved, and present either in VRAM
 * or GMR memory.
 * @pin: Whether to pin or unpin.
 *
 */
void vmw_bo_pin(struct ttm_buffer_object *bo, bool pin)
{
	uint32_t pl_flags;
	struct ttm_placement placement;
	uint32_t old_mem_type = bo->mem.mem_type;
	int ret;

	BUG_ON(!ttm_bo_is_reserved(bo));
	BUG_ON(old_mem_type != TTM_PL_VRAM &&
	       old_mem_type != VMW_PL_FLAG_GMR);

	pl_flags = TTM_PL_FLAG_VRAM | VMW_PL_FLAG_GMR | TTM_PL_FLAG_CACHED;
	if (pin)
		pl_flags |= TTM_PL_FLAG_NO_EVICT;

	memset(&placement, 0, sizeof(placement));
	placement.num_placement = 1;
	placement.placement = &pl_flags;

	ret = ttm_bo_validate(bo, &placement, false, true, true);

	BUG_ON(ret != 0 || bo->mem.mem_type != old_mem_type);
}
