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

#include <drm/drm.h>
#include <drm/drm_vma_manager.h>

#include "gem.h"
#include "psb_drv.h"

/*
 * Pin and build an in-kernel list of the pages that back our GEM object.
 * While we hold this the pages cannot be swapped out. This is protected
 * via the gtt mutex which the caller must hold.
 */
static int psb_gtt_attach_pages(struct gtt_range *gt)
{
	struct page **pages;

	WARN_ON(gt->pages);

	pages = drm_gem_get_pages(&gt->gem);
	if (IS_ERR(pages))
		return PTR_ERR(pages);

	gt->npage = gt->gem.size / PAGE_SIZE;
	gt->pages = pages;

	return 0;
}

/*
 * Undo the effect of psb_gtt_attach_pages. At this point the pages
 * must have been removed from the GTT as they could now be paged out
 * and move bus address. This is protected via the gtt mutex which the
 * caller must hold.
 */
static void psb_gtt_detach_pages(struct gtt_range *gt)
{
	drm_gem_put_pages(&gt->gem, gt->pages, true, false);
	gt->pages = NULL;
}

int psb_gtt_pin(struct gtt_range *gt)
{
	int ret = 0;
	struct drm_device *dev = gt->gem.dev;
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);
	u32 gpu_base = dev_priv->gtt.gatt_start;

	mutex_lock(&dev_priv->gtt_mutex);

	if (gt->in_gart == 0 && gt->stolen == 0) {
		ret = psb_gtt_attach_pages(gt);
		if (ret < 0)
			goto out;
		ret = psb_gtt_insert(dev, gt, 0);
		if (ret < 0) {
			psb_gtt_detach_pages(gt);
			goto out;
		}
		psb_mmu_insert_pages(psb_mmu_get_default_pd(dev_priv->mmu),
				     gt->pages, (gpu_base + gt->offset),
				     gt->npage, 0, 0, PSB_MMU_CACHED_MEMORY);
	}
	gt->in_gart++;
out:
	mutex_unlock(&dev_priv->gtt_mutex);
	return ret;
}

void psb_gtt_unpin(struct gtt_range *gt)
{
	struct drm_device *dev = gt->gem.dev;
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);
	u32 gpu_base = dev_priv->gtt.gatt_start;

	mutex_lock(&dev_priv->gtt_mutex);

	WARN_ON(!gt->in_gart);

	gt->in_gart--;
	if (gt->in_gart == 0 && gt->stolen == 0) {
		psb_mmu_remove_pages(psb_mmu_get_default_pd(dev_priv->mmu),
				     (gpu_base + gt->offset), gt->npage, 0, 0);
		psb_gtt_remove(dev, gt);
		psb_gtt_detach_pages(gt);
	}

	mutex_unlock(&dev_priv->gtt_mutex);
}

static void psb_gtt_free_range(struct drm_device *dev, struct gtt_range *gt)
{
	/* Undo the mmap pin if we are destroying the object */
	if (gt->mmapping) {
		psb_gtt_unpin(gt);
		gt->mmapping = 0;
	}
	WARN_ON(gt->in_gart && !gt->stolen);
	release_resource(&gt->resource);
	kfree(gt);
}

static vm_fault_t psb_gem_fault(struct vm_fault *vmf);

static void psb_gem_free_object(struct drm_gem_object *obj)
{
	struct gtt_range *gtt = to_gtt_range(obj);

	/* Remove the list map if one is present */
	drm_gem_free_mmap_offset(obj);
	drm_gem_object_release(obj);

	/* This must occur last as it frees up the memory of the GEM object */
	psb_gtt_free_range(obj->dev, gtt);
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

static struct gtt_range *psb_gtt_alloc_range(struct drm_device *dev, int len,
					     const char *name, int backed, u32 align)
{
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);
	struct gtt_range *gt;
	struct resource *r = dev_priv->gtt_mem;
	int ret;
	unsigned long start, end;

	if (backed) {
		/* The start of the GTT is the stolen pages */
		start = r->start;
		end = r->start + dev_priv->gtt.stolen_size - 1;
	} else {
		/* The rest we will use for GEM backed objects */
		start = r->start + dev_priv->gtt.stolen_size;
		end = r->end;
	}

	gt = kzalloc(sizeof(struct gtt_range), GFP_KERNEL);
	if (gt == NULL)
		return NULL;
	gt->resource.name = name;
	gt->stolen = backed;
	gt->in_gart = backed;
	/* Ensure this is set for non GEM objects */
	gt->gem.dev = dev;
	ret = allocate_resource(dev_priv->gtt_mem, &gt->resource,
				len, start, end, align, NULL, NULL);
	if (ret == 0) {
		gt->offset = gt->resource.start - r->start;
		return gt;
	}
	kfree(gt);
	return NULL;
}

struct gtt_range *
psb_gem_create(struct drm_device *dev, u64 size, const char *name, bool stolen, u32 align)
{
	struct gtt_range *gt;
	struct drm_gem_object *obj;
	int ret;

	size = roundup(size, PAGE_SIZE);

	gt = psb_gtt_alloc_range(dev, size, name, stolen, align);
	if (!gt) {
		dev_err(dev->dev, "no memory for %lld byte GEM object\n", size);
		return ERR_PTR(-ENOSPC);
	}
	obj = &gt->gem;

	obj->funcs = &psb_gem_object_funcs;

	if (stolen) {
		drm_gem_private_object_init(dev, obj, size);
	} else {
		ret = drm_gem_object_init(dev, obj, size);
		if (ret)
			goto err_psb_gtt_free_range;

		/* Limit the object to 32-bit mappings */
		mapping_set_gfp_mask(obj->filp->f_mapping, GFP_KERNEL | __GFP_DMA32);
	}

	return gt;

err_psb_gtt_free_range:
	psb_gtt_free_range(dev, gt);
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
		err = psb_gtt_pin(r);
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
