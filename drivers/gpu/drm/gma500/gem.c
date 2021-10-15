// SPDX-License-Identifier: GPL-2.0-only
/*
 *  psb GEM interface
 *
 * Copyright (c) 2011, Intel Corporation.
 *
 * Authors: Alan Cox
 *
 * TODO:
 *	-	we need to work out if the MMU is relevant (eg for
 *		accelerated operations on a GEM object)
 */

#include <linux/pagemap.h>

#include <asm/set_memory.h>

#include <drm/drm.h>
#include <drm/drm_vma_manager.h>

#include "gem.h"
#include "psb_drv.h"

int psb_gem_pin(struct gtt_range *gt)
{
	struct drm_device *dev = gt->gem.dev;
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);
	u32 gpu_base = dev_priv->gtt.gatt_start;
	struct page **pages;
	unsigned int npages;
	int ret;

	mutex_lock(&dev_priv->gtt_mutex);

	if (gt->in_gart || gt->stolen)
		goto out; /* already mapped */

	pages = drm_gem_get_pages(&gt->gem);
	if (IS_ERR(pages)) {
		ret = PTR_ERR(pages);
		goto err_mutex_unlock;
	}

	npages = gt->gem.size / PAGE_SIZE;

	set_pages_array_wc(pages, npages);

	psb_gtt_insert_pages(dev_priv, &gt->resource, pages);
	psb_mmu_insert_pages(psb_mmu_get_default_pd(dev_priv->mmu), pages,
			     (gpu_base + gt->offset), npages, 0, 0,
			     PSB_MMU_CACHED_MEMORY);

	gt->npage = npages;
	gt->pages = pages;

out:
	++gt->in_gart;
	mutex_unlock(&dev_priv->gtt_mutex);

	return 0;

err_mutex_unlock:
	mutex_unlock(&dev_priv->gtt_mutex);
	return ret;
}

void psb_gem_unpin(struct gtt_range *gt)
{
	struct drm_device *dev = gt->gem.dev;
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);
	u32 gpu_base = dev_priv->gtt.gatt_start;

	mutex_lock(&dev_priv->gtt_mutex);

	WARN_ON(!gt->in_gart);

	--gt->in_gart;

	if (gt->in_gart || gt->stolen)
		goto out;

	psb_mmu_remove_pages(psb_mmu_get_default_pd(dev_priv->mmu),
				     (gpu_base + gt->offset), gt->npage, 0, 0);
	psb_gtt_remove_pages(dev_priv, &gt->resource);

	/* Reset caching flags */
	set_pages_array_wb(gt->pages, gt->npage);

	drm_gem_put_pages(&gt->gem, gt->pages, true, false);
	gt->pages = NULL;
	gt->npage = 0;

out:
	mutex_unlock(&dev_priv->gtt_mutex);
}

static vm_fault_t psb_gem_fault(struct vm_fault *vmf);

static void psb_gem_free_object(struct drm_gem_object *obj)
{
	struct gtt_range *gt = to_gtt_range(obj);

	drm_gem_object_release(obj);

	/* Undo the mmap pin if we are destroying the object */
	if (gt->mmapping)
		psb_gem_unpin(gt);

	WARN_ON(gt->in_gart && !gt->stolen);

	release_resource(&gt->resource);
	kfree(gt);
}

static const struct vm_operations_struct psb_gem_vm_ops = {
	.fault = psb_gem_fault,
	.open = drm_gem_vm_open,
	.close = drm_gem_vm_close,
};

static const struct drm_gem_object_funcs psb_gem_object_funcs = {
	.free = psb_gem_free_object,
	.vm_ops = &psb_gem_vm_ops,
};

struct gtt_range *
psb_gem_create(struct drm_device *dev, u64 size, const char *name, bool stolen, u32 align)
{
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);
	struct gtt_range *gt;
	struct drm_gem_object *obj;
	int ret;

	size = roundup(size, PAGE_SIZE);

	gt = kzalloc(sizeof(*gt), GFP_KERNEL);
	if (!gt)
		return ERR_PTR(-ENOMEM);
	obj = &gt->gem;

	/* GTT resource */

	ret = psb_gtt_allocate_resource(dev_priv, &gt->resource, name, size, align, stolen,
					&gt->offset);
	if (ret)
		goto err_kfree;

	if (stolen) {
		gt->stolen = true;
		gt->in_gart = 1;
	}

	/* GEM object */

	obj->funcs = &psb_gem_object_funcs;

	if (stolen) {
		drm_gem_private_object_init(dev, obj, size);
	} else {
		ret = drm_gem_object_init(dev, obj, size);
		if (ret)
			goto err_release_resource;

		/* Limit the object to 32-bit mappings */
		mapping_set_gfp_mask(obj->filp->f_mapping, GFP_KERNEL | __GFP_DMA32);
	}

	return gt;

err_release_resource:
	release_resource(&gt->resource);
err_kfree:
	kfree(gt);
	return ERR_PTR(ret);
}

/**
 *	psb_gem_dumb_create	-	create a dumb buffer
 *	@file: our client file
 *	@dev: our device
 *	@args: the requested arguments copied from userspace
 *
 *	Allocate a buffer suitable for use for a frame buffer of the
 *	form described by user space. Give userspace a handle by which
 *	to reference it.
 */
int psb_gem_dumb_create(struct drm_file *file, struct drm_device *dev,
			struct drm_mode_create_dumb *args)
{
	size_t pitch, size;
	struct gtt_range *gt;
	struct drm_gem_object *obj;
	u32 handle;
	int ret;

	pitch = args->width * DIV_ROUND_UP(args->bpp, 8);
	pitch = ALIGN(pitch, 64);

	size = pitch * args->height;
	size = roundup(size, PAGE_SIZE);
	if (!size)
		return -EINVAL;

	gt = psb_gem_create(dev, size, "gem", false, PAGE_SIZE);
	if (IS_ERR(gt))
		return PTR_ERR(gt);
	obj = &gt->gem;

	ret = drm_gem_handle_create(file, obj, &handle);
	if (ret)
		goto err_drm_gem_object_put;

	drm_gem_object_put(obj);

	args->pitch = pitch;
	args->size = size;
	args->handle = handle;

	return 0;

err_drm_gem_object_put:
	drm_gem_object_put(obj);
	return ret;
}

/**
 *	psb_gem_fault		-	pagefault handler for GEM objects
 *	@vmf: fault detail
 *
 *	Invoked when a fault occurs on an mmap of a GEM managed area. GEM
 *	does most of the work for us including the actual map/unmap calls
 *	but we need to do the actual page work.
 *
 *	This code eventually needs to handle faulting objects in and out
 *	of the GTT and repacking it when we run out of space. We can put
 *	that off for now and for our simple uses
 *
 *	The VMA was set up by GEM. In doing so it also ensured that the
 *	vma->vm_private_data points to the GEM object that is backing this
 *	mapping.
 */
static vm_fault_t psb_gem_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct drm_gem_object *obj;
	struct gtt_range *r;
	int err;
	vm_fault_t ret;
	unsigned long pfn;
	pgoff_t page_offset;
	struct drm_device *dev;
	struct drm_psb_private *dev_priv;

	obj = vma->vm_private_data;	/* GEM object */
	dev = obj->dev;
	dev_priv = to_drm_psb_private(dev);

	r = to_gtt_range(obj);

	/* Make sure we don't parallel update on a fault, nor move or remove
	   something from beneath our feet */
	mutex_lock(&dev_priv->mmap_mutex);

	/* For now the mmap pins the object and it stays pinned. As things
	   stand that will do us no harm */
	if (r->mmapping == 0) {
		err = psb_gem_pin(r);
		if (err < 0) {
			dev_err(dev->dev, "gma500: pin failed: %d\n", err);
			ret = vmf_error(err);
			goto fail;
		}
		r->mmapping = 1;
	}

	/* Page relative to the VMA start - we must calculate this ourselves
	   because vmf->pgoff is the fake GEM offset */
	page_offset = (vmf->address - vma->vm_start) >> PAGE_SHIFT;

	/* CPU view of the page, don't go via the GART for CPU writes */
	if (r->stolen)
		pfn = (dev_priv->stolen_base + r->offset) >> PAGE_SHIFT;
	else
		pfn = page_to_pfn(r->pages[page_offset]);
	ret = vmf_insert_pfn(vma, vmf->address, pfn);
fail:
	mutex_unlock(&dev_priv->mmap_mutex);

	return ret;
}
