/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2014-2016 Intel Corporation
 */

#include <linux/highmem.h>
#include <linux/shmem_fs.h>
#include <linux/swap.h>

#include <drm/drm.h> /* for drm_legacy.h! */
#include <drm/drm_cache.h>
#include <drm/drm_legacy.h> /* for drm_pci.h! */
#include <drm/drm_pci.h>

#include "gt/intel_gt.h"
#include "i915_drv.h"
#include "i915_gem_object.h"
#include "i915_scatterlist.h"

static int i915_gem_object_get_pages_phys(struct drm_i915_gem_object *obj)
{
	struct address_space *mapping = obj->base.filp->f_mapping;
	struct drm_dma_handle *phys;
	struct sg_table *st;
	struct scatterlist *sg;
	char *vaddr;
	int i;
	int err;

	if (WARN_ON(i915_gem_object_needs_bit17_swizzle(obj)))
		return -EINVAL;

	/* Always aligning to the object size, allows a single allocation
	 * to handle all possible callers, and given typical object sizes,
	 * the alignment of the buddy allocation will naturally match.
	 */
	phys = drm_pci_alloc(obj->base.dev,
			     roundup_pow_of_two(obj->base.size),
			     roundup_pow_of_two(obj->base.size));
	if (!phys)
		return -ENOMEM;

	vaddr = phys->vaddr;
	for (i = 0; i < obj->base.size / PAGE_SIZE; i++) {
		struct page *page;
		char *src;

		page = shmem_read_mapping_page(mapping, i);
		if (IS_ERR(page)) {
			err = PTR_ERR(page);
			goto err_phys;
		}

		src = kmap_atomic(page);
		memcpy(vaddr, src, PAGE_SIZE);
		drm_clflush_virt_range(vaddr, PAGE_SIZE);
		kunmap_atomic(src);

		put_page(page);
		vaddr += PAGE_SIZE;
	}

	intel_gt_chipset_flush(&to_i915(obj->base.dev)->gt);

	st = kmalloc(sizeof(*st), GFP_KERNEL);
	if (!st) {
		err = -ENOMEM;
		goto err_phys;
	}

	if (sg_alloc_table(st, 1, GFP_KERNEL)) {
		kfree(st);
		err = -ENOMEM;
		goto err_phys;
	}

	sg = st->sgl;
	sg->offset = 0;
	sg->length = obj->base.size;

	sg_dma_address(sg) = phys->busaddr;
	sg_dma_len(sg) = obj->base.size;

	obj->phys_handle = phys;

	__i915_gem_object_set_pages(obj, st, sg->length);

	return 0;

err_phys:
	drm_pci_free(obj->base.dev, phys);

	return err;
}

static void
i915_gem_object_put_pages_phys(struct drm_i915_gem_object *obj,
			       struct sg_table *pages)
{
	__i915_gem_object_release_shmem(obj, pages, false);

	if (obj->mm.dirty) {
		struct address_space *mapping = obj->base.filp->f_mapping;
		char *vaddr = obj->phys_handle->vaddr;
		int i;

		for (i = 0; i < obj->base.size / PAGE_SIZE; i++) {
			struct page *page;
			char *dst;

			page = shmem_read_mapping_page(mapping, i);
			if (IS_ERR(page))
				continue;

			dst = kmap_atomic(page);
			drm_clflush_virt_range(vaddr, PAGE_SIZE);
			memcpy(dst, vaddr, PAGE_SIZE);
			kunmap_atomic(dst);

			set_page_dirty(page);
			if (obj->mm.madv == I915_MADV_WILLNEED)
				mark_page_accessed(page);
			put_page(page);
			vaddr += PAGE_SIZE;
		}
		obj->mm.dirty = false;
	}

	sg_free_table(pages);
	kfree(pages);

	drm_pci_free(obj->base.dev, obj->phys_handle);
}

static const struct drm_i915_gem_object_ops i915_gem_phys_ops = {
	.get_pages = i915_gem_object_get_pages_phys,
	.put_pages = i915_gem_object_put_pages_phys,
};

int i915_gem_object_attach_phys(struct drm_i915_gem_object *obj, int align)
{
	struct sg_table *pages;
	int err;

	if (align > obj->base.size)
		return -EINVAL;

	if (obj->ops == &i915_gem_phys_ops)
		return 0;

	if (obj->ops != &i915_gem_shmem_ops)
		return -EINVAL;

	err = i915_gem_object_unbind(obj, I915_GEM_OBJECT_UNBIND_ACTIVE);
	if (err)
		return err;

	mutex_lock(&obj->mm.lock);

	if (obj->mm.madv != I915_MADV_WILLNEED) {
		err = -EFAULT;
		goto err_unlock;
	}

	if (obj->mm.quirked) {
		err = -EFAULT;
		goto err_unlock;
	}

	if (obj->mm.mapping) {
		err = -EBUSY;
		goto err_unlock;
	}

	pages = __i915_gem_object_unset_pages(obj);

	obj->ops = &i915_gem_phys_ops;

	err = ____i915_gem_object_get_pages(obj);
	if (err)
		goto err_xfer;

	/* Perma-pin (until release) the physical set of pages */
	__i915_gem_object_pin_pages(obj);

	if (!IS_ERR_OR_NULL(pages))
		i915_gem_shmem_ops.put_pages(obj, pages);
	mutex_unlock(&obj->mm.lock);
	return 0;

err_xfer:
	obj->ops = &i915_gem_shmem_ops;
	if (!IS_ERR_OR_NULL(pages)) {
		unsigned int sg_page_sizes = i915_sg_page_sizes(pages->sgl);

		__i915_gem_object_set_pages(obj, pages, sg_page_sizes);
	}
err_unlock:
	mutex_unlock(&obj->mm.lock);
	return err;
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftests/i915_gem_phys.c"
#endif
