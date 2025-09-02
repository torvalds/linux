// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include "xe_mmio_gem.h"

#include <drm/drm_drv.h>
#include <drm/drm_gem.h>
#include <drm/drm_managed.h>

#include "xe_device_types.h"

/**
 * DOC: Exposing MMIO regions to userspace
 *
 * In certain cases, the driver may allow userspace to mmap a portion of the hardware registers.
 *
 * This can be done as follows:
 * 1. Call xe_mmio_gem_create() to create a GEM object with an mmap-able fake offset.
 * 2. Use xe_mmio_gem_mmap_offset() on the created GEM object to retrieve the fake offset.
 * 3. Provide the fake offset to userspace.
 * 4. Userspace can call mmap with the fake offset. The length provided to mmap
 *    must match the size of the GEM object.
 * 5. When the region is no longer needed, call xe_mmio_gem_destroy() to release the GEM object.
 *
 * NOTE: The exposed MMIO region must be page-aligned with regards to its BAR offset and size.
 *
 * WARNING: Exposing MMIO regions to userspace can have security and stability implications.
 * Make sure not to expose any sensitive registers.
 */

static void xe_mmio_gem_free(struct drm_gem_object *);
static int xe_mmio_gem_mmap(struct drm_gem_object *, struct vm_area_struct *);
static vm_fault_t xe_mmio_gem_vm_fault(struct vm_fault *);

struct xe_mmio_gem {
	struct drm_gem_object base;
	phys_addr_t phys_addr;
};

static const struct vm_operations_struct vm_ops = {
	.open = drm_gem_vm_open,
	.close = drm_gem_vm_close,
	.fault = xe_mmio_gem_vm_fault,
};

static const struct drm_gem_object_funcs xe_mmio_gem_funcs = {
	.free = xe_mmio_gem_free,
	.mmap = xe_mmio_gem_mmap,
	.vm_ops = &vm_ops,
};

static inline struct xe_mmio_gem *to_xe_mmio_gem(struct drm_gem_object *obj)
{
	return container_of(obj, struct xe_mmio_gem, base);
}

/**
 * xe_mmio_gem_create - Expose an MMIO region to userspace
 * @xe: The xe device
 * @file: DRM file descriptor
 * @phys_addr: Start of the exposed MMIO region
 * @size: The size of the exposed MMIO region
 *
 * This function creates a GEM object that exposes an MMIO region with an mmap-able
 * fake offset.
 *
 * See: "Exposing MMIO regions to userspace"
 */
struct xe_mmio_gem *xe_mmio_gem_create(struct xe_device *xe, struct drm_file *file,
				       phys_addr_t phys_addr, size_t size)
{
	struct xe_mmio_gem *obj;
	struct drm_gem_object *base;
	int err;

	if ((phys_addr % PAGE_SIZE != 0) || (size % PAGE_SIZE != 0))
		return ERR_PTR(-EINVAL);

	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	if (!obj)
		return ERR_PTR(-ENOMEM);

	base = &obj->base;
	base->funcs = &xe_mmio_gem_funcs;
	obj->phys_addr = phys_addr;

	drm_gem_private_object_init(&xe->drm, base, size);

	err = drm_gem_create_mmap_offset(base);
	if (err)
		goto free_gem;

	err = drm_vma_node_allow(&base->vma_node, file);
	if (err)
		goto free_gem;

	return obj;

free_gem:
	xe_mmio_gem_free(base);
	return ERR_PTR(err);
}

/**
 * xe_mmio_gem_mmap_offset - Return the mmap-able fake offset
 * @gem: the GEM object created with xe_mmio_gem_create()
 *
 * This function returns the mmap-able fake offset allocated during
 * xe_mmio_gem_create().
 *
 * See: "Exposing MMIO regions to userspace"
 */
u64 xe_mmio_gem_mmap_offset(struct xe_mmio_gem *gem)
{
	return drm_vma_node_offset_addr(&gem->base.vma_node);
}

static void xe_mmio_gem_free(struct drm_gem_object *base)
{
	struct xe_mmio_gem *obj = to_xe_mmio_gem(base);

	drm_gem_object_release(base);
	kfree(obj);
}

/**
 * xe_mmio_gem_destroy - Destroy the GEM object that exposes an MMIO region
 * @gem: the GEM object to destroy
 *
 * This function releases resources associated with the GEM object created by
 * xe_mmio_gem_create().
 *
 * See: "Exposing MMIO regions to userspace"
 */
void xe_mmio_gem_destroy(struct xe_mmio_gem *gem)
{
	xe_mmio_gem_free(&gem->base);
}

static int xe_mmio_gem_mmap(struct drm_gem_object *base, struct vm_area_struct *vma)
{
	if (vma->vm_end - vma->vm_start != base->size)
		return -EINVAL;

	if ((vma->vm_flags & VM_SHARED) == 0)
		return -EINVAL;

	/* Set vm_pgoff (used as a fake buffer offset by DRM) to 0 */
	vma->vm_pgoff = 0;
	vma->vm_page_prot = pgprot_noncached(vm_get_page_prot(vma->vm_flags));
	vm_flags_set(vma, VM_IO | VM_PFNMAP | VM_DONTEXPAND | VM_DONTDUMP |
		     VM_DONTCOPY | VM_NORESERVE);

	/* Defer actual mapping to the fault handler. */
	return 0;
}

static void xe_mmio_gem_release_dummy_page(struct drm_device *dev, void *res)
{
	__free_page((struct page *)res);
}

static vm_fault_t xe_mmio_gem_vm_fault_dummy_page(struct vm_area_struct *vma)
{
	struct drm_gem_object *base = vma->vm_private_data;
	struct drm_device *dev = base->dev;
	vm_fault_t ret = VM_FAULT_NOPAGE;
	struct page *page;
	unsigned long pfn;
	unsigned long i;

	page = alloc_page(GFP_KERNEL | __GFP_ZERO);
	if (!page)
		return VM_FAULT_OOM;

	if (drmm_add_action_or_reset(dev, xe_mmio_gem_release_dummy_page, page))
		return VM_FAULT_OOM;

	pfn = page_to_pfn(page);

	/* Map the entire VMA to the same dummy page */
	for (i = 0; i < base->size; i += PAGE_SIZE) {
		unsigned long addr = vma->vm_start + i;

		ret = vmf_insert_pfn(vma, addr, pfn);
		if (ret & VM_FAULT_ERROR)
			break;
	}

	return ret;
}

static vm_fault_t xe_mmio_gem_vm_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct drm_gem_object *base = vma->vm_private_data;
	struct xe_mmio_gem *obj = to_xe_mmio_gem(base);
	struct drm_device *dev = base->dev;
	vm_fault_t ret = VM_FAULT_NOPAGE;
	unsigned long i;
	int idx;

	if (!drm_dev_enter(dev, &idx)) {
		/*
		 * Provide a dummy page to avoid SIGBUS for events such as hot-unplug.
		 * This gives the userspace the option to recover instead of crashing.
		 * It is assumed the userspace will receive the notification via some
		 * other channel (e.g. drm uevent).
		 */
		return xe_mmio_gem_vm_fault_dummy_page(vma);
	}

	for (i = 0; i < base->size; i += PAGE_SIZE) {
		unsigned long addr = vma->vm_start + i;
		unsigned long phys_addr = obj->phys_addr + i;

		ret = vmf_insert_pfn(vma, addr, PHYS_PFN(phys_addr));
		if (ret & VM_FAULT_ERROR)
			break;
	}

	drm_dev_exit(idx);
	return ret;
}
