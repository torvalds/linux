/*
 * Copyright 2006 Tungsten Graphics Inc., Bismarck, ND., USA.
 * All rights reserved.
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
 * THE AUTHORS OR COPYRIGHT HOLDERS AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
/*
 * Authors: Thomas Hellström <thomas-at-tungstengraphics-dot-com>
 */

#include <linux/slab.h>

#include <drm/drm_device.h>
#include <drm/drm_file.h>
#include <drm/via_drm.h>

#include "via_drv.h"

#define VIA_MM_ALIGN_SHIFT 4
#define VIA_MM_ALIGN_MASK ((1 << VIA_MM_ALIGN_SHIFT) - 1)

struct via_memblock {
	struct drm_mm_node mm_node;
	struct list_head owner_list;
};

int via_agp_init(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	drm_via_agp_t *agp = data;
	drm_via_private_t *dev_priv = (drm_via_private_t *) dev->dev_private;

	mutex_lock(&dev->struct_mutex);
	drm_mm_init(&dev_priv->agp_mm, 0, agp->size >> VIA_MM_ALIGN_SHIFT);

	dev_priv->agp_initialized = 1;
	dev_priv->agp_offset = agp->offset;
	mutex_unlock(&dev->struct_mutex);

	DRM_DEBUG("offset = %u, size = %u\n", agp->offset, agp->size);
	return 0;
}

int via_fb_init(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	drm_via_fb_t *fb = data;
	drm_via_private_t *dev_priv = (drm_via_private_t *) dev->dev_private;

	mutex_lock(&dev->struct_mutex);
	drm_mm_init(&dev_priv->vram_mm, 0, fb->size >> VIA_MM_ALIGN_SHIFT);

	dev_priv->vram_initialized = 1;
	dev_priv->vram_offset = fb->offset;

	mutex_unlock(&dev->struct_mutex);
	DRM_DEBUG("offset = %u, size = %u\n", fb->offset, fb->size);

	return 0;

}

int via_final_context(struct drm_device *dev, int context)
{
	drm_via_private_t *dev_priv = (drm_via_private_t *) dev->dev_private;

	via_release_futex(dev_priv, context);

	/* Linux specific until context tracking code gets ported to BSD */
	/* Last context, perform cleanup */
	if (list_is_singular(&dev->ctxlist)) {
		DRM_DEBUG("Last Context\n");
		drm_legacy_irq_uninstall(dev);
		via_cleanup_futex(dev_priv);
		via_do_cleanup_map(dev);
	}
	return 1;
}

void via_lastclose(struct drm_device *dev)
{
	drm_via_private_t *dev_priv = (drm_via_private_t *) dev->dev_private;

	if (!dev_priv)
		return;

	mutex_lock(&dev->struct_mutex);
	if (dev_priv->vram_initialized) {
		drm_mm_takedown(&dev_priv->vram_mm);
		dev_priv->vram_initialized = 0;
	}
	if (dev_priv->agp_initialized) {
		drm_mm_takedown(&dev_priv->agp_mm);
		dev_priv->agp_initialized = 0;
	}
	mutex_unlock(&dev->struct_mutex);
}

int via_mem_alloc(struct drm_device *dev, void *data,
		  struct drm_file *file)
{
	drm_via_mem_t *mem = data;
	int retval = 0, user_key;
	struct via_memblock *item;
	drm_via_private_t *dev_priv = (drm_via_private_t *) dev->dev_private;
	struct via_file_private *file_priv = file->driver_priv;
	unsigned long tmpSize;

	if (mem->type > VIA_MEM_AGP) {
		DRM_ERROR("Unknown memory type allocation\n");
		return -EINVAL;
	}
	mutex_lock(&dev->struct_mutex);
	if (0 == ((mem->type == VIA_MEM_VIDEO) ? dev_priv->vram_initialized :
		      dev_priv->agp_initialized)) {
		mutex_unlock(&dev->struct_mutex);
		DRM_ERROR
		    ("Attempt to allocate from uninitialized memory manager.\n");
		return -EINVAL;
	}

	item = kzalloc(sizeof(*item), GFP_KERNEL);
	if (!item) {
		retval = -ENOMEM;
		goto fail_alloc;
	}

	tmpSize = (mem->size + VIA_MM_ALIGN_MASK) >> VIA_MM_ALIGN_SHIFT;
	if (mem->type == VIA_MEM_AGP)
		retval = drm_mm_insert_node(&dev_priv->agp_mm,
					    &item->mm_node,
					    tmpSize);
	else
		retval = drm_mm_insert_node(&dev_priv->vram_mm,
					    &item->mm_node,
					    tmpSize);
	if (retval)
		goto fail_alloc;

	retval = idr_alloc(&dev_priv->object_idr, item, 1, 0, GFP_KERNEL);
	if (retval < 0)
		goto fail_idr;
	user_key = retval;

	list_add(&item->owner_list, &file_priv->obj_list);
	mutex_unlock(&dev->struct_mutex);

	mem->offset = ((mem->type == VIA_MEM_VIDEO) ?
		      dev_priv->vram_offset : dev_priv->agp_offset) +
	    ((item->mm_node.start) << VIA_MM_ALIGN_SHIFT);
	mem->index = user_key;

	return 0;

fail_idr:
	drm_mm_remove_node(&item->mm_node);
fail_alloc:
	kfree(item);
	mutex_unlock(&dev->struct_mutex);

	mem->offset = 0;
	mem->size = 0;
	mem->index = 0;
	DRM_DEBUG("Video memory allocation failed\n");

	return retval;
}

int via_mem_free(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	drm_via_private_t *dev_priv = dev->dev_private;
	drm_via_mem_t *mem = data;
	struct via_memblock *obj;

	mutex_lock(&dev->struct_mutex);
	obj = idr_find(&dev_priv->object_idr, mem->index);
	if (obj == NULL) {
		mutex_unlock(&dev->struct_mutex);
		return -EINVAL;
	}

	idr_remove(&dev_priv->object_idr, mem->index);
	list_del(&obj->owner_list);
	drm_mm_remove_node(&obj->mm_node);
	kfree(obj);
	mutex_unlock(&dev->struct_mutex);

	DRM_DEBUG("free = 0x%lx\n", mem->index);

	return 0;
}


void via_reclaim_buffers_locked(struct drm_device *dev,
				struct drm_file *file)
{
	struct via_file_private *file_priv = file->driver_priv;
	struct via_memblock *entry, *next;

	if (!(dev->master && file->master->lock.hw_lock))
		return;

	drm_legacy_idlelock_take(&file->master->lock);

	mutex_lock(&dev->struct_mutex);
	if (list_empty(&file_priv->obj_list)) {
		mutex_unlock(&dev->struct_mutex);
		drm_legacy_idlelock_release(&file->master->lock);

		return;
	}

	via_driver_dma_quiescent(dev);

	list_for_each_entry_safe(entry, next, &file_priv->obj_list,
				 owner_list) {
		list_del(&entry->owner_list);
		drm_mm_remove_node(&entry->mm_node);
		kfree(entry);
	}
	mutex_unlock(&dev->struct_mutex);

	drm_legacy_idlelock_release(&file->master->lock);

	return;
}
