/*
 * Copyright (C) 2010, 2012-2013 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <drm/drmP.h>
#include <drm/mali_drm.h>
#include "mali_drv.h"

#define VIDEO_TYPE 0
#define MEM_TYPE 1

#define MALI_MM_ALIGN_SHIFT 4
#define MALI_MM_ALIGN_MASK ( (1 << MALI_MM_ALIGN_SHIFT) - 1)


static void *mali_sman_mm_allocate(void *private, unsigned long size, unsigned alignment)
{
	printk(KERN_ERR "DRM: %s\n", __func__);
	return NULL;
}

static void mali_sman_mm_free(void *private, void *ref)
{
	printk(KERN_ERR "DRM: %s\n", __func__);
}

static void mali_sman_mm_destroy(void *private)
{
	printk(KERN_ERR "DRM: %s\n", __func__);
}

static unsigned long mali_sman_mm_offset(void *private, void *ref)
{
	printk(KERN_ERR "DRM: %s\n", __func__);
	return ~((unsigned long)ref);
}

static int mali_fb_init(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	drm_mali_private_t *dev_priv = dev->dev_private;
	drm_mali_fb_t *fb = data;
	int ret;
	printk(KERN_ERR "DRM: %s\n", __func__);

	mutex_lock(&dev->struct_mutex);
	{
		struct drm_sman_mm sman_mm;
		sman_mm.private = (void *)0xFFFFFFFF;
		sman_mm.allocate = mali_sman_mm_allocate;
		sman_mm.free = mali_sman_mm_free;
		sman_mm.destroy = mali_sman_mm_destroy;
		sman_mm.offset = mali_sman_mm_offset;
		ret = drm_sman_set_manager(&dev_priv->sman, VIDEO_TYPE, &sman_mm);
	}

	if (ret)
	{
		DRM_ERROR("VRAM memory manager initialisation error\n");
		mutex_unlock(&dev->struct_mutex);
		return ret;
	}

	dev_priv->vram_initialized = 1;
	dev_priv->vram_offset = fb->offset;

	mutex_unlock(&dev->struct_mutex);
	DRM_DEBUG("offset = %u, size = %u\n", fb->offset, fb->size);

	return 0;
}

static int mali_drm_alloc(struct drm_device *dev, struct drm_file *file_priv, void *data, int pool)
{
	drm_mali_private_t *dev_priv = dev->dev_private;
	drm_mali_mem_t *mem = data;
	int retval = 0;
	struct drm_memblock_item *item;
	printk(KERN_ERR "DRM: %s\n", __func__);

	mutex_lock(&dev->struct_mutex);

	if (0 == dev_priv->vram_initialized)
	{
		DRM_ERROR("Attempt to allocate from uninitialized memory manager.\n");
		mutex_unlock(&dev->struct_mutex);
		return -EINVAL;
	}

	mem->size = (mem->size + MALI_MM_ALIGN_MASK) >> MALI_MM_ALIGN_SHIFT;
	item = drm_sman_alloc(&dev_priv->sman, pool, mem->size, 0,
	                      (unsigned long)file_priv);

	mutex_unlock(&dev->struct_mutex);

	if (item)
	{
		mem->offset = dev_priv->vram_offset + (item->mm->offset(item->mm, item->mm_info) << MALI_MM_ALIGN_SHIFT);
		mem->free = item->user_hash.key;
		mem->size = mem->size << MALI_MM_ALIGN_SHIFT;
	}
	else
	{
		mem->offset = 0;
		mem->size = 0;
		mem->free = 0;
		retval = -ENOMEM;
	}

	DRM_DEBUG("alloc %d, size = %d, offset = %d\n", pool, mem->size, mem->offset);

	return retval;
}

static int mali_drm_free(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	drm_mali_private_t *dev_priv = dev->dev_private;
	drm_mali_mem_t *mem = data;
	int ret;
	printk(KERN_ERR "DRM: %s\n", __func__);

	mutex_lock(&dev->struct_mutex);
	ret = drm_sman_free_key(&dev_priv->sman, mem->free);
	mutex_unlock(&dev->struct_mutex);
	DRM_DEBUG("free = 0x%lx\n", mem->free);

	return ret;
}

static int mali_fb_alloc(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	printk(KERN_ERR "DRM: %s\n", __func__);
	return mali_drm_alloc(dev, file_priv, data, VIDEO_TYPE);
}

static int mali_ioctl_mem_init(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	drm_mali_private_t *dev_priv = dev->dev_private;
	drm_mali_mem_t *mem = data;
	int ret;
	dev_priv = dev->dev_private;
	printk(KERN_ERR "DRM: %s\n", __func__);

	mutex_lock(&dev->struct_mutex);
	ret = drm_sman_set_range(&dev_priv->sman, MEM_TYPE, 0, mem->size >> MALI_MM_ALIGN_SHIFT);

	if (ret)
	{
		DRM_ERROR("MEM memory manager initialisation error\n");
		mutex_unlock(&dev->struct_mutex);
		return ret;
	}

	mutex_unlock(&dev->struct_mutex);

	return 0;
}

static int mali_ioctl_mem_alloc(struct drm_device *dev, void *data,
                                struct drm_file *file_priv)
{

	printk(KERN_ERR "DRM: %s\n", __func__);
	return mali_drm_alloc(dev, file_priv, data, MEM_TYPE);
}

static drm_local_map_t *mem_reg_init(struct drm_device *dev)
{
	struct drm_map_list *entry;
	drm_local_map_t *map;
	printk(KERN_ERR "DRM: %s\n", __func__);

	list_for_each_entry(entry, &dev->maplist, head)
	{
		map = entry->map;

		if (!map)
		{
			continue;
		}

		if (map->type == _DRM_REGISTERS)
		{
			return map;
		}
	}
	return NULL;
}

int mali_idle(struct drm_device *dev)
{
	drm_mali_private_t *dev_priv = dev->dev_private;
	uint32_t idle_reg;
	unsigned long end;
	int i;
	printk(KERN_ERR "DRM: %s\n", __func__);

	if (dev_priv->idle_fault)
	{
		return 0;
	}

	return 0;
}


void mali_lastclose(struct drm_device *dev)
{
	drm_mali_private_t *dev_priv = dev->dev_private;
	printk(KERN_ERR "DRM: %s\n", __func__);

	if (!dev_priv)
	{
		return;
	}

	mutex_lock(&dev->struct_mutex);
	drm_sman_cleanup(&dev_priv->sman);
	dev_priv->vram_initialized = 0;
	dev_priv->mmio = NULL;
	mutex_unlock(&dev->struct_mutex);
}

void mali_reclaim_buffers_locked(struct drm_device *dev, struct drm_file *file_priv)
{
	drm_mali_private_t *dev_priv = dev->dev_private;
	printk(KERN_ERR "DRM: %s\n", __func__);

	mutex_lock(&dev->struct_mutex);

	if (drm_sman_owner_clean(&dev_priv->sman, (unsigned long)file_priv))
	{
		mutex_unlock(&dev->struct_mutex);
		return;
	}

	if (dev->driver->dma_quiescent)
	{
		dev->driver->dma_quiescent(dev);
	}

	drm_sman_owner_cleanup(&dev_priv->sman, (unsigned long)file_priv);
	mutex_unlock(&dev->struct_mutex);
	return;
}

const struct drm_ioctl_desc mali_ioctls[] =
{
	DRM_IOCTL_DEF(DRM_MALI_FB_ALLOC, mali_fb_alloc, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_MALI_FB_FREE, mali_drm_free, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_MALI_MEM_INIT, mali_ioctl_mem_init, DRM_AUTH | DRM_MASTER | DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_MALI_MEM_ALLOC, mali_ioctl_mem_alloc, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_MALI_MEM_FREE, mali_drm_free, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_MALI_FB_INIT, mali_fb_init, DRM_AUTH | DRM_MASTER | DRM_ROOT_ONLY),
};

int mali_max_ioctl = DRM_ARRAY_SIZE(mali_ioctls);
