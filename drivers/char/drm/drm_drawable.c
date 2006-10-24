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

/** No-op. */
int drm_adddraw(DRM_IOCTL_ARGS)
{
	DRM_DEVICE;
	unsigned long irqflags;
	int i, j = 0;
	drm_draw_t draw;

	spin_lock_irqsave(&dev->drw_lock, irqflags);

	for (i = 0; i < dev->drw_bitfield_length; i++) {
		u32 bitfield = dev->drw_bitfield[i];

		if (bitfield == ~0)
			continue;

		for (; j < sizeof(bitfield); j++)
			if (!(bitfield & (1 << j)))
				goto done;
	}
done:

	if (i == dev->drw_bitfield_length) {
		u32 *new_bitfield = drm_realloc(dev->drw_bitfield, i * 4,
						(i + 1) * 4, DRM_MEM_BUFS);

		if (!new_bitfield) {
			DRM_ERROR("Failed to allocate new drawable bitfield\n");
			spin_unlock_irqrestore(&dev->drw_lock, irqflags);
			return DRM_ERR(ENOMEM);
		}

		if (32 * (i + 1) > dev->drw_info_length) {
			void *new_info = drm_realloc(dev->drw_info,
						     dev->drw_info_length *
						     sizeof(drm_drawable_info_t*),
						     32 * (i + 1) *
						     sizeof(drm_drawable_info_t*),
						     DRM_MEM_BUFS);

			if (!new_info) {
				DRM_ERROR("Failed to allocate new drawable info"
					  " array\n");

				drm_free(new_bitfield, (i + 1) * 4, DRM_MEM_BUFS);
				spin_unlock_irqrestore(&dev->drw_lock, irqflags);
				return DRM_ERR(ENOMEM);
			}

			dev->drw_info = (drm_drawable_info_t**)new_info;
		}

		new_bitfield[i] = 0;

		dev->drw_bitfield = new_bitfield;
		dev->drw_bitfield_length++;
	}

	dev->drw_bitfield[i] |= 1 << j;

	draw.handle = i * sizeof(u32) + j;
	DRM_DEBUG("%d\n", draw.handle);

	dev->drw_info[draw.handle] = NULL;

	spin_unlock_irqrestore(&dev->drw_lock, irqflags);

	DRM_COPY_TO_USER_IOCTL((drm_draw_t __user *)data, draw, sizeof(draw));

	return 0;
}

/** No-op. */
int drm_rmdraw(DRM_IOCTL_ARGS)
{
	DRM_DEVICE;
	drm_draw_t draw;
	unsigned int idx, mod;
	unsigned long irqflags;

	DRM_COPY_FROM_USER_IOCTL(draw, (drm_draw_t __user *) data,
				 sizeof(draw));

	idx = draw.handle / 32;
	mod = draw.handle % 32;

	spin_lock_irqsave(&dev->drw_lock, irqflags);

	if (idx >= dev->drw_bitfield_length ||
	    !(dev->drw_bitfield[idx] & (1 << mod))) {
		DRM_DEBUG("No such drawable %d\n", draw.handle);
		spin_unlock_irqrestore(&dev->drw_lock, irqflags);
		return 0;
	}

	dev->drw_bitfield[idx] &= ~(1 << mod);

	if (idx == (dev->drw_bitfield_length - 1)) {
		while (idx >= 0 && !dev->drw_bitfield[idx])
			--idx;

		if (idx != draw.handle / 32) {
			u32 *new_bitfield = drm_realloc(dev->drw_bitfield,
							dev->drw_bitfield_length * 4,
							(idx + 1) * 4,
							DRM_MEM_BUFS);

			if (new_bitfield || idx == -1) {
				dev->drw_bitfield = new_bitfield;
				dev->drw_bitfield_length = idx + 1;
			}
		}
	}

	if (32 * dev->drw_bitfield_length < dev->drw_info_length) {
		void *new_info = drm_realloc(dev->drw_info,
					     dev->drw_info_length *
					     sizeof(drm_drawable_info_t*),
					     32 * dev->drw_bitfield_length *
					     sizeof(drm_drawable_info_t*),
					     DRM_MEM_BUFS);

		if (new_info || !dev->drw_bitfield_length) {
			dev->drw_info = (drm_drawable_info_t**)new_info;
			dev->drw_info_length = 32 * dev->drw_bitfield_length;
		}
	}

	spin_unlock_irqrestore(&dev->drw_lock, irqflags);

	DRM_DEBUG("%d\n", draw.handle);
	return 0;
}

int drm_update_drawable_info(DRM_IOCTL_ARGS) {
	DRM_DEVICE;
	drm_update_draw_t update;
	unsigned int id, idx, mod;
	unsigned long irqflags;
	drm_drawable_info_t *info;
	void *new_data;

	DRM_COPY_FROM_USER_IOCTL(update, (drm_update_draw_t __user *) data,
				 sizeof(update));

	id = update.handle;
	idx = id / 32;
	mod = id % 32;

	spin_lock_irqsave(&dev->drw_lock, irqflags);

	if (idx >= dev->drw_bitfield_length ||
	    !(dev->drw_bitfield[idx] & (1 << mod))) {
		DRM_ERROR("No such drawable %d\n", update.handle);
		spin_unlock_irqrestore(&dev->drw_lock, irqflags);
		return DRM_ERR(EINVAL);
	}

	info = dev->drw_info[id];

	if (!info) {
		info = drm_calloc(1, sizeof(drm_drawable_info_t), DRM_MEM_BUFS);

		if (!info) {
			DRM_ERROR("Failed to allocate drawable info memory\n");
			spin_unlock_irqrestore(&dev->drw_lock, irqflags);
			return DRM_ERR(ENOMEM);
		}

		dev->drw_info[id] = info;
	}

	switch (update.type) {
	case DRM_DRAWABLE_CLIPRECTS:
		if (update.num != info->num_rects) {
			new_data = drm_alloc(update.num *
					     sizeof(drm_clip_rect_t),
					     DRM_MEM_BUFS);

			if (!new_data) {
				DRM_ERROR("Can't allocate cliprect memory\n");
				spin_unlock_irqrestore(&dev->drw_lock, irqflags);
				return DRM_ERR(ENOMEM);
			}

			info->rects = new_data;
		}

		if (DRM_COPY_FROM_USER(info->rects,
				       (drm_clip_rect_t __user *)
				       (unsigned long)update.data,
				       update.num * sizeof(drm_clip_rect_t))) {
			DRM_ERROR("Can't copy cliprects from userspace\n");
			spin_unlock_irqrestore(&dev->drw_lock, irqflags);
			return DRM_ERR(EFAULT);
		}

		if (update.num != info->num_rects) {
			drm_free(info->rects, info->num_rects *
				 sizeof(drm_clip_rect_t), DRM_MEM_BUFS);
			info->num_rects = update.num;
		}

		DRM_DEBUG("Updated %d cliprects for drawable %d\n",
			  info->num_rects, id);
		break;
	default:
		DRM_ERROR("Invalid update type %d\n", update.type);
		spin_unlock_irqrestore(&dev->drw_lock, irqflags);
		return DRM_ERR(EINVAL);
	}

	spin_unlock_irqrestore(&dev->drw_lock, irqflags);

	return 0;
}

/**
 * Caller must hold the drawable spinlock!
 */
drm_drawable_info_t *drm_get_drawable_info(drm_device_t *dev, drm_drawable_t id) {
	unsigned int idx = id / 32, mod = id % 32;

	if (idx >= dev->drw_bitfield_length ||
	    !(dev->drw_bitfield[idx] & (1 << mod))) {
		DRM_DEBUG("No such drawable %d\n", id);
		return NULL;
	}

	return dev->drw_info[id];
}
EXPORT_SYMBOL(drm_get_drawable_info);
