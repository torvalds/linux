/**************************************************************************
 *
 * Copyright 2006 Tungsten Graphics, Inc., Bismarck, ND., USA.
 * All Rights Reserved.
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
 *
 **************************************************************************/

/*
 * Authors:
 *    Thomas Hellström <thomas-at-tungstengraphics-dot-com>
 */

#include "drmP.h"
#include "sis_drm.h"
#include "sis_drv.h"

#include <video/sisfb.h>

#define VIDEO_TYPE 0
#define AGP_TYPE 1


#if defined(CONFIG_FB_SIS)
/* fb management via fb device */

#define SIS_MM_ALIGN_SHIFT 0
#define SIS_MM_ALIGN_MASK 0

static void *sis_sman_mm_allocate(void *private, unsigned long size,
				  unsigned alignment)
{
	struct sis_memreq req;

	req.size = size;
	sis_malloc(&req);
	if (req.size == 0)
		return NULL;
	else
		return (void *)~req.offset;
}

static void sis_sman_mm_free(void *private, void *ref)
{
	sis_free(~((unsigned long)ref));
}

static void sis_sman_mm_destroy(void *private)
{
	;
}

static unsigned long sis_sman_mm_offset(void *private, void *ref)
{
	return ~((unsigned long)ref);
}

#else /* CONFIG_FB_SIS */

#define SIS_MM_ALIGN_SHIFT 4
#define SIS_MM_ALIGN_MASK ( (1 << SIS_MM_ALIGN_SHIFT) - 1)

#endif /* CONFIG_FB_SIS */

static int sis_fb_init(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	drm_sis_private_t *dev_priv = dev->dev_private;
	drm_sis_fb_t *fb = data;
	int ret;

	mutex_lock(&dev->struct_mutex);
#if defined(CONFIG_FB_SIS)
	{
		struct drm_sman_mm sman_mm;
		sman_mm.private = (void *)0xFFFFFFFF;
		sman_mm.allocate = sis_sman_mm_allocate;
		sman_mm.free = sis_sman_mm_free;
		sman_mm.destroy = sis_sman_mm_destroy;
		sman_mm.offset = sis_sman_mm_offset;
		ret =
		    drm_sman_set_manager(&dev_priv->sman, VIDEO_TYPE, &sman_mm);
	}
#else
	ret = drm_sman_set_range(&dev_priv->sman, VIDEO_TYPE, 0,
				 fb->size >> SIS_MM_ALIGN_SHIFT);
#endif

	if (ret) {
		DRM_ERROR("VRAM memory manager initialisation error\n");
		mutex_unlock(&dev->struct_mutex);
		return ret;
	}

	dev_priv->vram_initialized = 1;
	dev_priv->vram_offset = fb->offset;

	mutex_unlock(&dev->struct_mutex);
	DRM_DEBUG("offset = %u, size = %u", fb->offset, fb->size);

	return 0;
}

static int sis_drm_alloc(struct drm_device *dev, struct drm_file *file_priv,
			 void *data, int pool)
{
	drm_sis_private_t *dev_priv = dev->dev_private;
	drm_sis_mem_t *mem = data;
	int retval = 0;
	struct drm_memblock_item *item;

	mutex_lock(&dev->struct_mutex);

	if (0 == ((pool == 0) ? dev_priv->vram_initialized :
		      dev_priv->agp_initialized)) {
		DRM_ERROR
		    ("Attempt to allocate from uninitialized memory manager.\n");
		return -EINVAL;
	}

	mem->size = (mem->size + SIS_MM_ALIGN_MASK) >> SIS_MM_ALIGN_SHIFT;
	item = drm_sman_alloc(&dev_priv->sman, pool, mem->size, 0,
			      (unsigned long)file_priv);

	mutex_unlock(&dev->struct_mutex);
	if (item) {
		mem->offset = ((pool == 0) ?
			      dev_priv->vram_offset : dev_priv->agp_offset) +
		    (item->mm->
		     offset(item->mm, item->mm_info) << SIS_MM_ALIGN_SHIFT);
		mem->free = item->user_hash.key;
		mem->size = mem->size << SIS_MM_ALIGN_SHIFT;
	} else {
		mem->offset = 0;
		mem->size = 0;
		mem->free = 0;
		retval = -ENOMEM;
	}

	DRM_DEBUG("alloc %d, size = %d, offset = %d\n", pool, mem->size,
		  mem->offset);

	return retval;
}

static int sis_drm_free(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	drm_sis_private_t *dev_priv = dev->dev_private;
	drm_sis_mem_t *mem = data;
	int ret;

	mutex_lock(&dev->struct_mutex);
	ret = drm_sman_free_key(&dev_priv->sman, mem->free);
	mutex_unlock(&dev->struct_mutex);
	DRM_DEBUG("free = 0x%lx\n", mem->free);

	return ret;
}

static int sis_fb_alloc(struct drm_device *dev, void *data,
			struct drm_file *file_priv)
{
	return sis_drm_alloc(dev, file_priv, data, VIDEO_TYPE);
}

static int sis_ioctl_agp_init(struct drm_device *dev, void *data,
			      struct drm_file *file_priv)
{
	drm_sis_private_t *dev_priv = dev->dev_private;
	drm_sis_agp_t *agp = data;
	int ret;
	dev_priv = dev->dev_private;

	mutex_lock(&dev->struct_mutex);
	ret = drm_sman_set_range(&dev_priv->sman, AGP_TYPE, 0,
				 agp->size >> SIS_MM_ALIGN_SHIFT);

	if (ret) {
		DRM_ERROR("AGP memory manager initialisation error\n");
		mutex_unlock(&dev->struct_mutex);
		return ret;
	}

	dev_priv->agp_initialized = 1;
	dev_priv->agp_offset = agp->offset;
	mutex_unlock(&dev->struct_mutex);

	DRM_DEBUG("offset = %u, size = %u", agp->offset, agp->size);
	return 0;
}

static int sis_ioctl_agp_alloc(struct drm_device *dev, void *data,
			       struct drm_file *file_priv)
{

	return sis_drm_alloc(dev, file_priv, data, AGP_TYPE);
}

static drm_local_map_t *sis_reg_init(struct drm_device *dev)
{
	struct drm_map_list *entry;
	drm_local_map_t *map;

	list_for_each_entry(entry, &dev->maplist, head) {
		map = entry->map;
		if (!map)
			continue;
		if (map->type == _DRM_REGISTERS) {
			return map;
		}
	}
	return NULL;
}

int sis_idle(struct drm_device *dev)
{
	drm_sis_private_t *dev_priv = dev->dev_private;
	uint32_t idle_reg;
	unsigned long end;
	int i;

	if (dev_priv->idle_fault)
		return 0;

	if (dev_priv->mmio == NULL) {
		dev_priv->mmio = sis_reg_init(dev);
		if (dev_priv->mmio == NULL) {
			DRM_ERROR("Could not find register map.\n");
			return 0;
		}
	}
	
	/*
	 * Implement a device switch here if needed
	 */

	if (dev_priv->chipset != SIS_CHIP_315)
		return 0;

	/*
	 * Timeout after 3 seconds. We cannot use DRM_WAIT_ON here
	 * because its polling frequency is too low.
	 */

	end = jiffies + (DRM_HZ * 3);

	for (i=0; i<4; ++i) {
		do {
			idle_reg = SIS_READ(0x85cc);
		} while ( !time_after_eq(jiffies, end) &&
			  ((idle_reg & 0x80000000) != 0x80000000));
	}

	if (time_after_eq(jiffies, end)) {
		DRM_ERROR("Graphics engine idle timeout. "
			  "Disabling idle check\n");
		dev_priv->idle_fault = 1;
	}

	/*
	 * The caller never sees an error code. It gets trapped
	 * in libdrm.
	 */

	return 0;
}


void sis_lastclose(struct drm_device *dev)
{
	drm_sis_private_t *dev_priv = dev->dev_private;

	if (!dev_priv)
		return;

	mutex_lock(&dev->struct_mutex);
	drm_sman_cleanup(&dev_priv->sman);
	dev_priv->vram_initialized = 0;
	dev_priv->agp_initialized = 0;
	dev_priv->mmio = NULL;
	mutex_unlock(&dev->struct_mutex);
}

void sis_reclaim_buffers_locked(struct drm_device * dev,
				struct drm_file *file_priv)
{
	drm_sis_private_t *dev_priv = dev->dev_private;

	mutex_lock(&dev->struct_mutex);
	if (drm_sman_owner_clean(&dev_priv->sman, (unsigned long)file_priv)) {
		mutex_unlock(&dev->struct_mutex);
		return;
	}

	if (dev->driver->dma_quiescent) {
		dev->driver->dma_quiescent(dev);
	}

	drm_sman_owner_cleanup(&dev_priv->sman, (unsigned long)file_priv);
	mutex_unlock(&dev->struct_mutex);
	return;
}

struct drm_ioctl_desc sis_ioctls[] = {
	DRM_IOCTL_DEF(DRM_SIS_FB_ALLOC, sis_fb_alloc, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_SIS_FB_FREE, sis_drm_free, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_SIS_AGP_INIT, sis_ioctl_agp_init, DRM_AUTH | DRM_MASTER | DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_SIS_AGP_ALLOC, sis_ioctl_agp_alloc, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_SIS_AGP_FREE, sis_drm_free, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_SIS_FB_INIT, sis_fb_init, DRM_AUTH | DRM_MASTER | DRM_ROOT_ONLY),
};

int sis_max_ioctl = DRM_ARRAY_SIZE(sis_ioctls);
