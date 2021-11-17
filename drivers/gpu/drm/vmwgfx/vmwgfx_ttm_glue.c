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

static struct ttm_buffer_object *vmw_bo_vm_lookup(struct ttm_device *bdev,
						  unsigned long offset,
						  unsigned long pages)
{
	struct vmw_private *dev_priv = container_of(bdev, struct vmw_private, bdev);
	struct drm_device *drm = &dev_priv->drm;
	struct drm_vma_offset_node *node;
	struct ttm_buffer_object *bo = NULL;

	drm_vma_offset_lock_lookup(bdev->vma_manager);

	node = drm_vma_offset_lookup_locked(bdev->vma_manager, offset, pages);
	if (likely(node)) {
		bo = container_of(node, struct ttm_buffer_object,
				  base.vma_node);
		bo = ttm_bo_get_unless_zero(bo);
	}

	drm_vma_offset_unlock_lookup(bdev->vma_manager);

	if (!bo)
		drm_err(drm, "Could not find buffer object to map\n");

	return bo;
}

int vmw_mmap(struct file *filp, struct vm_area_struct *vma)
{
	static const struct vm_operations_struct vmw_vm_ops = {
		.pfn_mkwrite = vmw_bo_vm_mkwrite,
		.page_mkwrite = vmw_bo_vm_mkwrite,
		.fault = vmw_bo_vm_fault,
		.open = ttm_bo_vm_open,
		.close = ttm_bo_vm_close,
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
		.huge_fault = vmw_bo_vm_huge_fault,
#endif
	};
	struct drm_file *file_priv = filp->private_data;
	struct vmw_private *dev_priv = vmw_priv(file_priv->minor->dev);
	struct ttm_object_file *tfile = vmw_fpriv(file_priv)->tfile;
	struct ttm_device *bdev = &dev_priv->bdev;
	struct ttm_buffer_object *bo;
	int ret;

	if (unlikely(vma->vm_pgoff < DRM_FILE_PAGE_OFFSET_START))
		return -EINVAL;

	bo = vmw_bo_vm_lookup(bdev, vma->vm_pgoff, vma_pages(vma));
	if (unlikely(!bo))
		return -EINVAL;

	ret = vmw_user_bo_verify_access(bo, tfile);
	if (unlikely(ret != 0))
		goto out_unref;

	ret = ttm_bo_mmap_obj(vma, bo);
	if (unlikely(ret != 0))
		goto out_unref;

	vma->vm_ops = &vmw_vm_ops;

	/* Use VM_PFNMAP rather than VM_MIXEDMAP if not a COW mapping */
	if (!is_cow_mapping(vma->vm_flags))
		vma->vm_flags = (vma->vm_flags & ~VM_MIXEDMAP) | VM_PFNMAP;

	ttm_bo_put(bo); /* release extra ref taken by ttm_bo_mmap_obj() */

	return 0;

out_unref:
	ttm_bo_put(bo);
	return ret;
}

/* struct vmw_validation_mem callback */
static int vmw_vmt_reserve(struct vmw_validation_mem *m, size_t size)
{
	static struct ttm_operation_ctx ctx = {.interruptible = false,
					       .no_wait_gpu = false};
	struct vmw_private *dev_priv = container_of(m, struct vmw_private, vvm);

	return ttm_mem_global_alloc(vmw_mem_glob(dev_priv), size, &ctx);
}

/* struct vmw_validation_mem callback */
static void vmw_vmt_unreserve(struct vmw_validation_mem *m, size_t size)
{
	struct vmw_private *dev_priv = container_of(m, struct vmw_private, vvm);

	return ttm_mem_global_free(vmw_mem_glob(dev_priv), size);
}

/**
 * vmw_validation_mem_init_ttm - Interface the validation memory tracker
 * to ttm.
 * @dev_priv: Pointer to struct vmw_private. The reason we choose a vmw private
 * rather than a struct vmw_validation_mem is to make sure assumption in the
 * callbacks that struct vmw_private derives from struct vmw_validation_mem
 * holds true.
 * @gran: The recommended allocation granularity
 */
void vmw_validation_mem_init_ttm(struct vmw_private *dev_priv, size_t gran)
{
	struct vmw_validation_mem *vvm = &dev_priv->vvm;

	vvm->reserve_mem = vmw_vmt_reserve;
	vvm->unreserve_mem = vmw_vmt_unreserve;
	vvm->gran = gran;
}
