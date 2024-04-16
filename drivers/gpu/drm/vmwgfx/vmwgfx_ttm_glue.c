// SPDX-License-Identifier: GPL-2.0 OR MIT
/**************************************************************************
 *
 * Copyright 2009-2011 VMware, Inc., Palo Alto, CA., USA
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

#include "vmwgfx_drv.h"

static int vmw_bo_vm_lookup(struct ttm_device *bdev,
				   struct drm_file *filp,
				   unsigned long offset,
				   unsigned long pages,
				   struct ttm_buffer_object **p_bo)
{
	struct vmw_private *dev_priv = container_of(bdev, struct vmw_private, bdev);
	struct drm_device *drm = &dev_priv->drm;
	struct drm_vma_offset_node *node;
	int ret;

	*p_bo = NULL;

	drm_vma_offset_lock_lookup(bdev->vma_manager);

	node = drm_vma_offset_lookup_locked(bdev->vma_manager, offset, pages);
	if (likely(node)) {
		*p_bo = container_of(node, struct ttm_buffer_object,
				  base.vma_node);
		*p_bo = ttm_bo_get_unless_zero(*p_bo);
	}

	drm_vma_offset_unlock_lookup(bdev->vma_manager);

	if (!*p_bo) {
		drm_err(drm, "Could not find buffer object to map\n");
		return -EINVAL;
	}

	if (!drm_vma_node_is_allowed(node, filp)) {
		ret = -EACCES;
		goto out_no_access;
	}

	return 0;
out_no_access:
	ttm_bo_put(*p_bo);
	return ret;
}

int vmw_mmap(struct file *filp, struct vm_area_struct *vma)
{
	static const struct vm_operations_struct vmw_vm_ops = {
		.pfn_mkwrite = vmw_bo_vm_mkwrite,
		.page_mkwrite = vmw_bo_vm_mkwrite,
		.fault = vmw_bo_vm_fault,
		.open = ttm_bo_vm_open,
		.close = ttm_bo_vm_close,
	};
	struct drm_file *file_priv = filp->private_data;
	struct vmw_private *dev_priv = vmw_priv(file_priv->minor->dev);
	struct ttm_device *bdev = &dev_priv->bdev;
	struct ttm_buffer_object *bo;
	int ret;

	if (unlikely(vma->vm_pgoff < DRM_FILE_PAGE_OFFSET_START))
		return -EINVAL;

	ret = vmw_bo_vm_lookup(bdev, file_priv, vma->vm_pgoff, vma_pages(vma), &bo);
	if (unlikely(ret != 0))
		return ret;

	ret = ttm_bo_mmap_obj(vma, bo);
	if (unlikely(ret != 0))
		goto out_unref;

	vma->vm_ops = &vmw_vm_ops;

	/* Use VM_PFNMAP rather than VM_MIXEDMAP if not a COW mapping */
	if (!is_cow_mapping(vma->vm_flags))
		vm_flags_mod(vma, VM_PFNMAP, VM_MIXEDMAP);

	ttm_bo_put(bo); /* release extra ref taken by ttm_bo_mmap_obj() */

	return 0;

out_unref:
	ttm_bo_put(bo);
	return ret;
}

