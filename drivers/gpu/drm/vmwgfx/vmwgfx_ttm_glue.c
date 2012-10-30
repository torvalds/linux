/**************************************************************************
 *
 * Copyright © 2009 VMware, Inc., Palo Alto, CA., USA
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

#include <drm/drmP.h>
#include "vmwgfx_drv.h"

int vmw_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct drm_file *file_priv;
	struct vmw_private *dev_priv;

	if (unlikely(vma->vm_pgoff < VMWGFX_FILE_PAGE_OFFSET)) {
		DRM_ERROR("Illegal attempt to mmap old fifo space.\n");
		return -EINVAL;
	}

	file_priv = filp->private_data;
	dev_priv = vmw_priv(file_priv->minor->dev);
	return ttm_bo_mmap(filp, vma, &dev_priv->bdev);
}

static int vmw_ttm_mem_global_init(struct drm_global_reference *ref)
{
	DRM_INFO("global init.\n");
	return ttm_mem_global_init(ref->object);
}

static void vmw_ttm_mem_global_release(struct drm_global_reference *ref)
{
	ttm_mem_global_release(ref->object);
}

int vmw_ttm_global_init(struct vmw_private *dev_priv)
{
	struct drm_global_reference *global_ref;
	int ret;

	global_ref = &dev_priv->mem_global_ref;
	global_ref->global_type = DRM_GLOBAL_TTM_MEM;
	global_ref->size = sizeof(struct ttm_mem_global);
	global_ref->init = &vmw_ttm_mem_global_init;
	global_ref->release = &vmw_ttm_mem_global_release;

	ret = drm_global_item_ref(global_ref);
	if (unlikely(ret != 0)) {
		DRM_ERROR("Failed setting up TTM memory accounting.\n");
		return ret;
	}

	dev_priv->bo_global_ref.mem_glob =
		dev_priv->mem_global_ref.object;
	global_ref = &dev_priv->bo_global_ref.ref;
	global_ref->global_type = DRM_GLOBAL_TTM_BO;
	global_ref->size = sizeof(struct ttm_bo_global);
	global_ref->init = &ttm_bo_global_init;
	global_ref->release = &ttm_bo_global_release;
	ret = drm_global_item_ref(global_ref);

	if (unlikely(ret != 0)) {
		DRM_ERROR("Failed setting up TTM buffer objects.\n");
		goto out_no_bo;
	}

	return 0;
out_no_bo:
	drm_global_item_unref(&dev_priv->mem_global_ref);
	return ret;
}

void vmw_ttm_global_release(struct vmw_private *dev_priv)
{
	drm_global_item_unref(&dev_priv->bo_global_ref.ref);
	drm_global_item_unref(&dev_priv->mem_global_ref);
}
