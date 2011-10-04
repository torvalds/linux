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

#include "ttm/ttm_placement.h"

#include "drmP.h"
#include "vmwgfx_drv.h"


/**
 * Validate a buffer to placement.
 *
 * May only be called by the current master as this function takes the
 * its lock in write mode.
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
 * Move a buffer to vram or gmr.
 *
 * May only be called by the current master as this function takes the
 * its lock in write mode.
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
 * Move a buffer to vram.
 *
 * May only be called by the current master as this function takes the
 * its lock in write mode.
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
 * Move a buffer to start of vram.
 *
 * May only be called by the current master as this function takes the
 * its lock in write mode.
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
 * Unpin the buffer given buffer, does not move the buffer.
 *
 * May only be called by the current master as this function takes the
 * its lock in write mode.
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
 * Move a buffer to system memory, does not pin the buffer.
 *
 * May only be called by the current master as this function takes the
 * its lock in write mode.
 *
 * @dev_priv:  Driver private.
 * @buf:  DMA buffer to move.
 * @interruptible:  Use interruptible wait.
 *
 * Returns
 * -ERESTARTSYS if interrupted by a signal.
 */
int vmw_dmabuf_to_system(struct vmw_private *dev_priv,
			 struct vmw_dma_buffer *buf,
			 bool interruptible)
{
	return vmw_dmabuf_to_placement(dev_priv, buf,
				       &vmw_sys_placement,
				       interruptible);
}

void vmw_dmabuf_get_id_offset(struct vmw_dma_buffer *buf,
			      uint32_t *gmrId, uint32_t *offset)
{
	if (buf->base.mem.mem_type == TTM_PL_VRAM) {
		*gmrId = SVGA_GMR_FRAMEBUFFER;
		*offset = buf->base.offset;
	} else {
		*gmrId = buf->base.mem.start;
		*offset = 0;
	}
}

void vmw_dmabuf_get_guest_ptr(struct vmw_dma_buffer *buf, SVGAGuestPtr *ptr)
{
	if (buf->base.mem.mem_type == TTM_PL_VRAM) {
		ptr->gmrId = SVGA_GMR_FRAMEBUFFER;
		ptr->offset = buf->base.offset;
	} else {
		ptr->gmrId = buf->base.mem.start;
		ptr->offset = 0;
	}
}
