/**
 * \file drm_drawable.c
 * IOCTLs for drawables
 *
 * \author Rickard E. (Rik) Faith <faith@valinux.com>
 * \author Gareth Hughes <gareth@valinux.com>
 * \author Michel Dänzer <michel@tungstengraphics.com>
 */

/*
 * Created: Tue Feb  2 08:37:54 1999 by faith@valinux.com
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * Copyright 2006 Tungsten Graphics, Inc., Bismarck, North Dakota.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "drmP.h"

/**
 * Allocate drawable ID and memory to store information about it.
 */
int drm_adddraw(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	unsigned long irqflags;
	struct drm_draw *draw = data;
	int new_id = 0;
	int ret;

again:
	if (idr_pre_get(&dev->drw_idr, GFP_KERNEL) == 0) {
		DRM_ERROR("Out of memory expanding drawable idr\n");
		return -ENOMEM;
	}

	spin_lock_irqsave(&dev->drw_lock, irqflags);
	ret = idr_get_new_above(&dev->drw_idr, NULL, 1, &new_id);
	if (ret == -EAGAIN) {
		spin_unlock_irqrestore(&dev->drw_lock, irqflags);
		goto again;
	}

	spin_unlock_irqrestore(&dev->drw_lock, irqflags);

	draw->handle = new_id;

	DRM_DEBUG("%d\n", draw->handle);

	return 0;
}

/**
 * Free drawable ID and memory to store information about it.
 */
int drm_rmdraw(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct drm_draw *draw = data;
	unsigned long irqflags;
	struct drm_drawable_info *info;

	spin_lock_irqsave(&dev->drw_lock, irqflags);

	info = drm_get_drawable_info(dev, draw->handle);
	if (info == NULL) {
		spin_unlock_irqrestore(&dev->drw_lock, irqflags);
		return -EINVAL;
	}
	kfree(info->rects);
	kfree(info);

	idr_remove(&dev->drw_idr, draw->handle);

	spin_unlock_irqrestore(&dev->drw_lock, irqflags);
	DRM_DEBUG("%d\n", draw->handle);
	return 0;
}

int drm_update_drawable_info(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct drm_update_draw *update = data;
	unsigned long irqflags;
	struct drm_clip_rect *rects;
	struct drm_drawable_info *info;
	int err;

	info = idr_find(&dev->drw_idr, update->handle);
	if (!info) {
		info = kzalloc(sizeof(*info), GFP_KERNEL);
		if (!info)
			return -ENOMEM;
		if (IS_ERR(idr_replace(&dev->drw_idr, info, update->handle))) {
			DRM_ERROR("No such drawable %d\n", update->handle);
			kfree(info);
			return -EINVAL;
		}
	}

	switch (update->type) {
	case DRM_DRAWABLE_CLIPRECTS:
		if (update->num == 0)
			rects = NULL;
		else if (update->num != info->num_rects) {
			rects = kmalloc(update->num *
					sizeof(struct drm_clip_rect),
					GFP_KERNEL);
		} else
			rects = info->rects;

		if (update->num && !rects) {
			DRM_ERROR("Failed to allocate cliprect memory\n");
			err = -ENOMEM;
			goto error;
		}

		if (update->num && DRM_COPY_FROM_USER(rects,
						     (struct drm_clip_rect __user *)
						     (unsigned long)update->data,
						     update->num *
						     sizeof(*rects))) {
			DRM_ERROR("Failed to copy cliprects from userspace\n");
			err = -EFAULT;
			goto error;
		}

		spin_lock_irqsave(&dev->drw_lock, irqflags);

		if (rects != info->rects) {
			kfree(info->rects);
		}

		info->rects = rects;
		info->num_rects = update->num;

		spin_unlock_irqrestore(&dev->drw_lock, irqflags);

		DRM_DEBUG("Updated %d cliprects for drawable %d\n",
			  info->num_rects, update->handle);
		break;
	default:
		DRM_ERROR("Invalid update type %d\n", update->type);
		return -EINVAL;
	}

	return 0;

error:
	if (rects != info->rects)
		kfree(rects);

	return err;
}

/**
 * Caller must hold the drawable spinlock!
 */
struct drm_drawable_info *drm_get_drawable_info(struct drm_device *dev, drm_drawable_t id)
{
	return idr_find(&dev->drw_idr, id);
}
EXPORT_SYMBOL(drm_get_drawable_info);

static int drm_drawable_free(int idr, void *p, void *data)
{
	struct drm_drawable_info *info = p;

	if (info) {
		kfree(info->rects);
		kfree(info);
	}

	return 0;
}

void drm_drawable_free_all(struct drm_device *dev)
{
	idr_for_each(&dev->drw_idr, drm_drawable_free, NULL);
	idr_remove_all(&dev->drw_idr);
}
