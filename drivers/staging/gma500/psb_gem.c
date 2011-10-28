/*
 *  psb GEM interface
 *
 * Copyright (c) 2011, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Authors: Alan Cox
 *
 * TODO:
 *	-	we don't actually put GEM objects into the GART yet
 *	-	we need to work out if the MMU is relevant as well (eg for
 *		accelerated operations on a GEM object)
 *	-	cache coherency
 *
 * ie this is just an initial framework to get us going.
 */

#include <drm/drmP.h>
#include <drm/drm.h>
#include "psb_drm.h"
#include "psb_drv.h"

int psb_gem_init_object(struct drm_gem_object *obj)
{
	return -EINVAL;
}

void psb_gem_free_object(struct drm_gem_object *obj)
{
	struct gtt_range *gtt = container_of(obj, struct gtt_range, gem);
	psb_gtt_free_range(obj->dev, gtt);
	if (obj->map_list.map) {
		/* Do things GEM should do for us */
		struct drm_gem_mm *mm = obj->dev->mm_private;
		struct drm_map_list *list = &obj->map_list;
		drm_ht_remove_item(&mm->offset_hash, &list->hash);
		drm_mm_put_block(list->file_offset_node);
		kfree(list->map);
		list->map = NULL;
	}
	drm_gem_object_release(obj);
}

int psb_gem_get_aperture(struct drm_device *dev, void *data,
				struct drm_file *file)
{
	return -EINVAL;
}

/**
 *	psb_gem_create_mmap_offset	-	invent an mmap offset
 *	@obj: our object
 *
 *	This is basically doing by hand a pile of ugly crap which should
 *	be done automatically by the GEM library code but isn't
 */
static int psb_gem_create_mmap_offset(struct drm_gem_object *obj)
{
	struct drm_device *dev = obj->dev;
	struct drm_gem_mm *mm = dev->mm_private;
	struct drm_map_list *list;
	struct drm_local_map *map;
	int ret;

	list = &obj->map_list;
	list->map = kzalloc(sizeof(struct drm_map_list), GFP_KERNEL);
	if (list->map == NULL)
		return -ENOMEM;
	map = list->map;
	map->type = _DRM_GEM;
	map->size = obj->size;
	map->handle =obj;

	list->file_offset_node = drm_mm_search_free(&mm->offset_manager,
					obj->size / PAGE_SIZE, 0, 0);
	if (!list->file_offset_node) {
		DRM_ERROR("failed to allocate offset for bo %d\n", obj->name);
		ret = -ENOSPC;
		goto free_it;
	}
	list->file_offset_node = drm_mm_get_block(list->file_offset_node,
					obj->size / PAGE_SIZE, 0);
	if (!list->file_offset_node) {
		ret = -ENOMEM;
		goto free_it;
	}
	list->hash.key = list->file_offset_node->start;
	ret = drm_ht_insert_item(&mm->offset_hash, &list->hash);
	if (ret) {
		DRM_ERROR("failed to add to map hash\n");
		goto free_mm;
	}
	return 0;

free_mm:
	drm_mm_put_block(list->file_offset_node);
free_it:
	kfree(list->map);
	list->map = NULL;
	return ret;
}

/**
 *	psb_gem_dumb_map_gtt	-	buffer mapping for dumb interface
 *	@file: our drm client file
 *	@dev: drm device
 *	@handle: GEM handle to the object (from dumb_create)
 *
 *	Do the necessary setup to allow the mapping of the frame buffer
 *	into user memory. We don't have to do much here at the moment.
 */
int psb_gem_dumb_map_gtt(struct drm_file *file, struct drm_device *dev,
			 uint32_t handle, uint64_t *offset)
{
	int ret = 0;
	struct drm_gem_object *obj;

	if (!(dev->driver->driver_features & DRIVER_GEM))
		return -ENODEV;
		
	mutex_lock(&dev->struct_mutex);

	/* GEM does all our handle to object mapping */
	obj = drm_gem_object_lookup(dev, file, handle);
	if (obj == NULL) {
		ret = -ENOENT;
		goto unlock;
	}
	/* What validation is needed here ? */
	
	/* Make it mmapable */
	if (!obj->map_list.map) {
		ret = psb_gem_create_mmap_offset(obj);
		if (ret)
			goto out;
	}
	/* GEM should really work out the hash offsets for us */
	*offset = (u64)obj->map_list.hash.key << PAGE_SHIFT;
out:
	drm_gem_object_unreference(obj);
unlock:
	mutex_unlock(&dev->struct_mutex);
	return ret;
}

/**
 *	psb_gem_create		-	create a mappable object
 *	@file: the DRM file of the client
 *	@dev: our device
 *	@size: the size requested
 *	@handlep: returned handle (opaque number)
 *
 *	Create a GEM object, fill in the boilerplate and attach a handle to
 *	it so that userspace can speak about it. This does the core work
 *	for the various methods that do/will create GEM objects for things
 */
static int psb_gem_create(struct drm_file *file,
	struct drm_device *dev, uint64_t size, uint32_t *handlep)
{
	struct gtt_range *r;
	int ret;
	u32 handle;

	size = roundup(size, PAGE_SIZE);

	/* Allocate our object - for now a direct gtt range which is not 
	   stolen memory backed */
	r = psb_gtt_alloc_range(dev, size, "gem", 0);
	if (r == NULL)
		return -ENOSPC;
	/* Initialize the extra goodies GEM needs to do all the hard work */
	if (drm_gem_object_init(dev, &r->gem, size) != 0) {
		psb_gtt_free_range(dev, r);
		/* GEM doesn't give an error code and we don't have an
		   EGEMSUCKS so make something up for now - FIXME */
		return -ENOMEM;
	}
	/* Give the object a handle so we can carry it more easily */
	ret = drm_gem_handle_create(file, &r->gem, &handle);
	if (ret) {
		drm_gem_object_release(&r->gem);
		psb_gtt_free_range(dev, r);
		return ret;
	}
	/* We have the initial and handle reference but need only one now */
	drm_gem_object_unreference(&r->gem);
	*handlep = handle;
	return 0;
}

/**
 *	psb_gem_dumb_create	-	create a dumb buffer
 *	@drm_file: our client file
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
	args->pitch = ALIGN(args->width * ((args->bpp + 7) / 8), 64);
	args->size = args->pitch * args->height;
	return psb_gem_create(file, dev, args->size, &args->handle);
}

/**
 *	psb_gem_dumb_destroy	-	destroy a dumb buffer
 *	@file: client file
 *	@dev: our DRM device
 *	@handle: the object handle
 *
 *	Destroy a handle that was created via psb_gem_dumb_create, at least
 *	we hope it was created that way. i915 seems to assume the caller
 *	does the checking but that might be worth review ! FIXME
 */
int psb_gem_dumb_destroy(struct drm_file *file, struct drm_device *dev,
			uint32_t handle)
{
	/* No special work needed, drop the reference and see what falls out */
	return drm_gem_handle_delete(file, handle);
}

/**
 *	psb_gem_fault		-	pagefault handler for GEM objects
 *	@vma: the VMA of the GEM object
 *	@vmf: fault detail
 *
 *	Invoked when a fault occurs on an mmap of a GEM managed area. GEM
 *	does most of the work for us including the actual map/unmap calls
 *	but we need to do the actual page work.
 *
 *	This code eventually needs to handle faulting objects in and out
 *	of the GART and repacking it when we run out of space. We can put
 *	that off for now and for our simple uses
 *
 *	The VMA was set up by GEM. In doing so it also ensured that the
 *	vma->vm_private_data points to the GEM object that is backing this
 *	mapping.
 *
 *	To avoid aliasing and cache funnies we want to map the object
 *	through the GART. For the moment this is slightly hackish. It would
 *	be nicer if GEM provided mmap opened/closed hooks for us giving
 *	the object so that we could track things nicely. That needs changes
 *	to the core GEM code so must be tackled post staging
 *
 *	FIXME
 */
int psb_gem_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct drm_gem_object *obj;
	struct gtt_range *r;
	int ret;
	unsigned long pfn;
	pgoff_t page_offset;
	struct drm_device *dev;

	obj = vma->vm_private_data;	/* GEM object */
	dev = obj->dev;

	r = container_of(obj, struct gtt_range, gem);	/* Get the gtt range */

	/* Make sure we don't parallel update on a fault, nor move or remove
	   something from beneath our feet */
	mutex_lock(&dev->struct_mutex);

	/* For now the mmap pins the object and it stays pinned. As things
	   stand that will do us no harm */
	if (r->mmapping == 0) {
		ret = psb_gtt_pin(r);
		if (ret < 0) {
		        DRM_ERROR("gma500: pin failed: %d\n", ret);
		        goto fail;
                }
		r->mmapping = 1;
	}

	/* FIXME: Locking. We may also need to repack the GART sometimes */

	/* Page relative to the VMA start */
	page_offset = ((unsigned long) vmf->virtual_address - vma->vm_start)
				>> PAGE_SHIFT;

	/* Bus address of the page is gart + object offset + page offset */
	/* Assumes gtt allocations are page aligned */
	pfn = (r->resource.start >> PAGE_SHIFT) + page_offset;

	pr_debug("Object GTT base at %p\n", (void *)(r->resource.start));
	pr_debug("Inserting %p pfn %lx, pa %lx\n", vmf->virtual_address,
	        pfn, pfn << PAGE_SHIFT);

	ret = vm_insert_pfn(vma, (unsigned long)vmf->virtual_address, pfn);

fail:
        mutex_unlock(&dev->struct_mutex);
	switch (ret) {
	case 0:
	case -ERESTARTSYS:
	case -EINTR:
		return VM_FAULT_NOPAGE;
	case -ENOMEM:
		return VM_FAULT_OOM;
	default:
		return VM_FAULT_SIGBUS;
	}
}
