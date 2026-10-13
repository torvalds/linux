// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include "xe_mmio_gem.h"

#include <linux/dma-resv.h>
#include <drm/drm_drv.h>
#include <drm/drm_gem.h>

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
	struct page *dummy_page; /* protected by the GEM's dma_resv */
	bool destroyed; /* protected by the GEM's dma_resv */
};

static int xe_mmio_gem_vm_may_split(struct vm_area_struct *area, unsigned long addr)
{
	/*
	 * Forbid splitting. Together with VM_DONTEXPAND, this keeps the VMA
	 * matching the GEM object exactly.
	 */
	return -EINVAL;
}

static const struct vm_operations_struct vm_ops = {
	.open = drm_gem_vm_open,
	.close = drm_gem_vm_close,
	.fault = xe_mmio_gem_vm_fault,
	.may_split = xe_mmio_gem_vm_may_split,
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

	obj = kzalloc_obj(*obj);
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

	if (obj->dummy_page)
		__free_page(obj->dummy_page);
	drm_gem_object_release(base);
	kfree(obj);
}

/**
 * xe_mmio_gem_destroy - Destroy the GEM object that exposes an MMIO region
 * @gem: the GEM object to destroy
 * @file: DRM file descriptor previously passed to xe_mmio_gem_create()
 *
 * This function releases resources associated with the GEM object created by
 * xe_mmio_gem_create().
 *
 * See: "Exposing MMIO regions to userspace"
 */
void xe_mmio_gem_destroy(struct xe_mmio_gem *gem, struct drm_file *file)
{
	struct drm_gem_object *base = &gem->base;
	struct drm_device *dev = base->dev;

	drm_vma_node_revoke(&base->vma_node, file);

	dma_resv_lock(base->resv, NULL);
	gem->destroyed = true;
	dma_resv_unlock(base->resv);
	/*
	 * Setting 'destroyed' under lock takes care of the subsequent faults.
	 * Zap the existing PTEs to cut off access to the real MMIO through
	 * currently mapped pages.
	 */
	drm_vma_node_unmap(&base->vma_node, dev->anon_inode->i_mapping);

	drm_gem_object_put(base);
}

static int xe_mmio_gem_mmap(struct drm_gem_object *base, struct vm_area_struct *vma)
{
	if (vma->vm_end - vma->vm_start != base->size)
		return -EINVAL;

	if ((vma->vm_flags & VM_SHARED) == 0)
		return -EINVAL;

	vma->vm_page_prot = pgprot_noncached(vma_get_page_prot(vma));
	vm_flags_set(vma, VM_IO | VM_PFNMAP | VM_DONTEXPAND | VM_DONTDUMP |
		     VM_DONTCOPY | VM_NORESERVE);

	/* Defer actual mapping to the fault handler. */
	return 0;
}

static int alloc_dummy_page_if_needed(struct drm_gem_object *base)
{
	struct xe_mmio_gem *obj = to_xe_mmio_gem(base);

	dma_resv_assert_held(base->resv);
	if (!obj->dummy_page)
		obj->dummy_page = alloc_page(GFP_KERNEL | __GFP_ZERO);

	return obj->dummy_page ? 0 : -ENOMEM;
}

static vm_fault_t xe_mmio_gem_vm_fault_dummy_page(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct drm_gem_object *base = vma->vm_private_data;
	struct xe_mmio_gem *obj = to_xe_mmio_gem(base);
	unsigned long pfn;

	if (alloc_dummy_page_if_needed(base))
		return VM_FAULT_OOM;

	pfn = page_to_pfn(obj->dummy_page);

	return vmf_insert_pfn_prot(vma, vmf->address, pfn,
				   vm_get_page_prot(vma->vm_flags));
}

static vm_fault_t xe_mmio_gem_vm_fault_locked(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct drm_gem_object *base = vma->vm_private_data;
	struct xe_mmio_gem *obj = to_xe_mmio_gem(base);
	struct drm_device *dev = base->dev;
	vm_fault_t ret = VM_FAULT_NOPAGE;
	unsigned long addr, pfn;
	int idx;

	dma_resv_assert_held(base->resv);
	if (obj->destroyed)
		return VM_FAULT_SIGBUS;

	if (!drm_dev_enter(dev, &idx)) {
		/*
		 * Provide a dummy page to avoid SIGBUS for events such as hot-unplug.
		 * This gives the userspace the option to recover instead of crashing.
		 * It is assumed the userspace will receive the notification via some
		 * other channel (e.g. drm uevent).
		 */
		return xe_mmio_gem_vm_fault_dummy_page(vmf);
	}

	pfn = PHYS_PFN(obj->phys_addr);
	for (addr = vma->vm_start; addr < vma->vm_end; addr += PAGE_SIZE) {
		ret = vmf_insert_pfn(vma, addr, pfn);
		if (ret & VM_FAULT_ERROR)
			break;

		pfn++;
	}

	drm_dev_exit(idx);
	return ret;
}

static vm_fault_t xe_mmio_gem_vm_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct drm_gem_object *base = vma->vm_private_data;
	vm_fault_t ret;

	dma_resv_lock(base->resv, NULL);
	ret = xe_mmio_gem_vm_fault_locked(vmf);
	dma_resv_unlock(base->resv);
	return ret;
}
