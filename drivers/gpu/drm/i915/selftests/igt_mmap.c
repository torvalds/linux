/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

#include <drm/drm_file.h>

#include "i915_drv.h"
#include "igt_mmap.h"

unsigned long igt_mmap_offset(struct drm_i915_private *i915,
			      u64 offset,
			      unsigned long size,
			      unsigned long prot,
			      unsigned long flags)
{
	struct drm_vma_offset_node *node;
	struct file *file;
	unsigned long addr;
	int err;

	/* no need to refcount, we own this object */
	drm_vma_offset_lock_lookup(i915->drm.vma_offset_manager);
	node = drm_vma_offset_exact_lookup_locked(i915->drm.vma_offset_manager,
						  offset / PAGE_SIZE, size / PAGE_SIZE);
	drm_vma_offset_unlock_lookup(i915->drm.vma_offset_manager);

	if (GEM_WARN_ON(!node)) {
		pr_info("Failed to lookup %llx\n", offset);
		return -ENOENT;
	}

	/* Pretend to open("/dev/dri/card0") */
	file = mock_drm_getfile(i915->drm.primary, O_RDWR);
	if (IS_ERR(file))
		return PTR_ERR(file);

	err = drm_vma_node_allow(node, file->private_data);
	if (err) {
		addr = err;
		goto out_file;
	}

	addr = vm_mmap(file, 0, drm_vma_node_size(node) << PAGE_SHIFT,
		       prot, flags, drm_vma_node_offset_addr(node));

	drm_vma_node_revoke(node, file->private_data);
out_file:
	fput(file);
	return addr;
}
