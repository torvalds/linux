/**************************************************************************
 * Copyright (c) 2007, Intel Corporation.
 * All Rights Reserved.
 * Copyright (c) 2008, Tungsten Graphics, Inc. Cedar Park, TX. USA.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 **************************************************************************/

#include <drm/drmP.h>
#include "psb_drv.h"
#include "psb_drm.h"
#include "psb_reg.h"
#include "ttm/ttm_bo_api.h"
#include "ttm/ttm_execbuf_util.h"
#include "psb_ttm_userobj_api.h"
#include "ttm/ttm_placement.h"
#include "psb_sgx.h"
#include "psb_intel_reg.h"
#include "psb_powermgmt.h"


static inline int psb_same_page(unsigned long offset,
				unsigned long offset2)
{
	return (offset & PAGE_MASK) == (offset2 & PAGE_MASK);
}

static inline unsigned long psb_offset_end(unsigned long offset,
					      unsigned long end)
{
	offset = (offset + PAGE_SIZE) & PAGE_MASK;
	return (end < offset) ? end : offset;
}

struct psb_dstbuf_cache {
	unsigned int dst;
	struct ttm_buffer_object *dst_buf;
	unsigned long dst_offset;
	uint32_t *dst_page;
	unsigned int dst_page_offset;
	struct ttm_bo_kmap_obj dst_kmap;
	bool dst_is_iomem;
};

struct psb_validate_buffer {
	struct ttm_validate_buffer base;
	struct psb_validate_req req;
	int ret;
	struct psb_validate_arg __user *user_val_arg;
	uint32_t flags;
	uint32_t offset;
	int po_correct;
};
static int
psb_placement_fence_type(struct ttm_buffer_object *bo,
			 uint64_t set_val_flags,
			 uint64_t clr_val_flags,
			 uint32_t new_fence_class,
			 uint32_t *new_fence_type)
{
	int ret;
	uint32_t n_fence_type;
	/*
	uint32_t set_flags = set_val_flags & 0xFFFFFFFF;
	uint32_t clr_flags = clr_val_flags & 0xFFFFFFFF;
	*/
	struct ttm_fence_object *old_fence;
	uint32_t old_fence_type;
	struct ttm_placement placement;

	if (unlikely
	    (!(set_val_flags &
	       (PSB_GPU_ACCESS_READ | PSB_GPU_ACCESS_WRITE)))) {
		DRM_ERROR
		    ("GPU access type (read / write) is not indicated.\n");
		return -EINVAL;
	}

	/* User space driver doesn't set any TTM placement flags in
					set_val_flags or clr_val_flags */
	placement.num_placement = 0;/* FIXME  */
	placement.num_busy_placement = 0;
	placement.fpfn = 0;
	placement.lpfn = 0;
	ret = psb_ttm_bo_check_placement(bo, &placement);
	if (unlikely(ret != 0))
		return ret;

	switch (new_fence_class) {
	default:
		n_fence_type = _PSB_FENCE_TYPE_EXE;
	}

	*new_fence_type = n_fence_type;
	old_fence = (struct ttm_fence_object *) bo->sync_obj;
	old_fence_type = (uint32_t) (unsigned long) bo->sync_obj_arg;

	if (old_fence && ((new_fence_class != old_fence->fence_class) ||
			  ((n_fence_type ^ old_fence_type) &
			   old_fence_type))) {
		ret = ttm_bo_wait(bo, 0, 1, 0);
		if (unlikely(ret != 0))
			return ret;
	}
	/*
	bo->proposed_flags = (bo->proposed_flags | set_flags)
		& ~clr_flags & TTM_PL_MASK_MEMTYPE;
	*/
	return 0;
}

int psb_validate_kernel_buffer(struct psb_context *context,
			       struct ttm_buffer_object *bo,
			       uint32_t fence_class,
			       uint64_t set_flags, uint64_t clr_flags)
{
	struct psb_validate_buffer *item;
	uint32_t cur_fence_type;
	int ret;

	if (unlikely(context->used_buffers >= PSB_NUM_VALIDATE_BUFFERS)) {
		DRM_ERROR("Out of free validation buffer entries for "
			  "kernel buffer validation.\n");
		return -ENOMEM;
	}

	item = &context->buffers[context->used_buffers];
	item->user_val_arg = NULL;
	item->base.reserved = 0;

	ret = ttm_bo_reserve(bo, 1, 0, 1, context->val_seq);
	if (unlikely(ret != 0))
	        return ret;

	ret = psb_placement_fence_type(bo, set_flags, clr_flags, fence_class,
				       &cur_fence_type);
	if (unlikely(ret != 0)) {
		ttm_bo_unreserve(bo);
		return ret;
	}

	item->base.bo = ttm_bo_reference(bo);
	item->base.new_sync_obj_arg = (void *) (unsigned long) cur_fence_type;
	item->base.reserved = 1;

	/* Internal locking ??? FIXMEAC */
	list_add_tail(&item->base.head, &context->kern_validate_list);
	context->used_buffers++;
	/*
	ret = ttm_bo_validate(bo, 1, 0, 0);
	if (unlikely(ret != 0))
		goto out_unlock;
	*/
	item->offset = bo->offset;
	item->flags = bo->mem.placement;
	context->fence_types |= cur_fence_type;

	return ret;
}

void psb_fence_or_sync(struct drm_file *file_priv,
		       uint32_t engine,
		       uint32_t fence_types,
		       uint32_t fence_flags,
		       struct list_head *list,
		       struct psb_ttm_fence_rep *fence_arg,
		       struct ttm_fence_object **fence_p)
{
	struct drm_device *dev = file_priv->minor->dev;
	struct drm_psb_private *dev_priv = psb_priv(dev);
	struct ttm_fence_device *fdev = &dev_priv->fdev;
	int ret;
	struct ttm_fence_object *fence;
	struct ttm_object_file *tfile = psb_fpriv(file_priv)->tfile;
	uint32_t handle;

	ret = ttm_fence_user_create(fdev, tfile,
				    engine, fence_types,
				    TTM_FENCE_FLAG_EMIT, &fence, &handle);
	if (ret) {

		/*
		 * Fence creation failed.
		 * Fall back to synchronous operation and idle the engine.
		 */

		if (!(fence_flags & DRM_PSB_FENCE_NO_USER)) {

			/*
			 * Communicate to user-space that
			 * fence creation has failed and that
			 * the engine is idle.
			 */

			fence_arg->handle = ~0;
			fence_arg->error = ret;
		}

		ttm_eu_backoff_reservation(list);
		if (fence_p)
			*fence_p = NULL;
		return;
	}

	ttm_eu_fence_buffer_objects(list, fence);
	if (!(fence_flags & DRM_PSB_FENCE_NO_USER)) {
		struct ttm_fence_info info = ttm_fence_get_info(fence);
		fence_arg->handle = handle;
		fence_arg->fence_class = ttm_fence_class(fence);
		fence_arg->fence_type = ttm_fence_types(fence);
		fence_arg->signaled_types = info.signaled_types;
		fence_arg->error = 0;
	} else {
		ret =
		    ttm_ref_object_base_unref(tfile, handle,
					      ttm_fence_type);
		BUG_ON(ret);
	}

	if (fence_p)
		*fence_p = fence;
	else if (fence)
		ttm_fence_object_unref(&fence);
}

