/*
 * Copyright (c) 2007-2008 Tungsten Graphics, Inc., Cedar Park, TX., USA,
 * All Rights Reserved.
 * Copyright (c) 2009 VMware, Inc., Palo Alto, CA., USA,
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
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
 */

#include "drmP.h"

#include "nouveau_drv.h"

int
nouveau_ttm_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct drm_file *file_priv = filp->private_data;
	struct drm_nouveau_private *dev_priv =
		file_priv->minor->dev->dev_private;

	if (unlikely(vma->vm_pgoff < DRM_FILE_PAGE_OFFSET))
		return drm_mmap(filp, vma);

	return ttm_bo_mmap(filp, vma, &dev_priv->ttm.bdev);
}

static int
nouveau_ttm_mem_global_init(struct ttm_global_reference *ref)
{
	return ttm_mem_global_init(ref->object);
}

static void
nouveau_ttm_mem_global_release(struct ttm_global_reference *ref)
{
	ttm_mem_global_release(ref->object);
}

int
nouveau_ttm_global_init(struct drm_nouveau_private *dev_priv)
{
	struct ttm_global_reference *global_ref;
	int ret;

	global_ref = &dev_priv->ttm.mem_global_ref;
	global_ref->global_type = TTM_GLOBAL_TTM_MEM;
	global_ref->size = sizeof(struct ttm_mem_global);
	global_ref->init = &nouveau_ttm_mem_global_init;
	global_ref->release = &nouveau_ttm_mem_global_release;

	ret = ttm_global_item_ref(global_ref);
	if (unlikely(ret != 0)) {
		DRM_ERROR("Failed setting up TTM memory accounting\n");
		dev_priv->ttm.mem_global_ref.release = NULL;
		return ret;
	}

	dev_priv->ttm.bo_global_ref.mem_glob = global_ref->object;
	global_ref = &dev_priv->ttm.bo_global_ref.ref;
	global_ref->global_type = TTM_GLOBAL_TTM_BO;
	global_ref->size = sizeof(struct ttm_bo_global);
	global_ref->init = &ttm_bo_global_init;
	global_ref->release = &ttm_bo_global_release;

	ret = ttm_global_item_ref(global_ref);
	if (unlikely(ret != 0)) {
		DRM_ERROR("Failed setting up TTM BO subsystem\n");
		ttm_global_item_unref(&dev_priv->ttm.mem_global_ref);
		dev_priv->ttm.mem_global_ref.release = NULL;
		return ret;
	}

	return 0;
}

void
nouveau_ttm_global_release(struct drm_nouveau_private *dev_priv)
{
	if (dev_priv->ttm.mem_global_ref.release == NULL)
		return;

	ttm_global_item_unref(&dev_priv->ttm.bo_global_ref.ref);
	ttm_global_item_unref(&dev_priv->ttm.mem_global_ref);
	dev_priv->ttm.mem_global_ref.release = NULL;
}

