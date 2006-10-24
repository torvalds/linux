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
int drm_adddraw(DRM_IOCTL_ARGS)
{
	DRM_DEVICE;
	unsigned long irqflags;
	int i, j;
	u32 *bitfield = dev->drw_bitfield;
	unsigned int bitfield_length = dev->drw_bitfield_length;
	drm_drawable_info_t **info = dev->drw_info;
	unsigned int info_length = dev->drw_info_length;
	drm_draw_t draw;

	for (i = 0, j = 0; i < bitfield_length; i++) {
		if (bitfield[i] == ~0)
			continue;

		for (; j < 8 * sizeof(*bitfield); j++)
			if (!(bitfield[i] & (1 << j)))
				goto done;
	}
done:

	if (i == bitfield_length) {
		bitfield_length++;

		bitfield = drm_alloc(bitfield_length * sizeof(*bitfield),
				     DRM_MEM_BUFS);

		if (!bitfield) {
			DRM_ERROR("Failed to allocate new drawable bitfield\n");
			return DRM_ERR(ENOMEM);
		}

		if (8 * sizeof(*bitfield) * bitfield_length > info_length) {
			info_length += 8 * sizeof(*bitfield);

			info = drm_alloc(info_length * sizeof(*info),
					 DRM_MEM_BUFS);

			if (!info) {
				DRM_ERROR("Failed to allocate new drawable info"
					  " array\n");

				drm_free(bitfield,
					 bitfield_length * sizeof(*bitfield),
					 DRM_MEM_BUFS);
				return DRM_ERR(ENOMEM);
			}
		}

		bitfield[i] = 0;
	}

	draw.handle = i * 8 * sizeof(*bitfield) + j + 1;
	DRM_DEBUG("%d\n", draw.handle);

	spin_lock_irqsave(&dev->drw_lock, irqflags);

	bitfield[i] |= 1 << j;
	info[draw.handle - 1] = NULL;

	if (bitfield != dev->drw_bitfield) {
		memcpy(bitfield, dev->drw_bitfield, dev->drw_bitfield_length *
		       sizeof(*bitfield));
		drm_free(dev->drw_bitfield, sizeof(*bitfield) *
			 dev->drw_bitfield_length, DRM_MEM_BUFS);
		dev->drw_bitfield = bitfield;
		dev->drw_bitfield_length = bitfield_length;
	}

	if (info != dev->drw_info) {
		memcpy(info, dev->drw_info, dev->drw_info_length *
		       sizeof(*info));
		drm_free(dev->drw_info, sizeof(*info) * dev->drw_info_length,
			 DRM_MEM_BUFS);
		dev->drw_info = info;
		dev->drw_info_length = info_length;
	}

	spin_unlock_irqrestore(&dev->drw_lock, irqflags);

	DRM_COPY_TO_USER_IOCTL((drm_draw_t __user *)data, draw, sizeof(draw));

	return 0;
}

/**
 * Free drawable ID and memory to store information about it.
 */
int drm_rmdraw(DRM_IOCTL_ARGS)
{
	DRM_DEVICE;
	drm_draw_t draw;
 	int id, idx;
 	unsigned int shift;
	unsigned long irqflags;
	u32 *bitfield = dev->drw_bitfield;
	unsigned int bitfield_length = dev->drw_bitfield_length;
	drm_drawable_info_t **info = dev->drw_info;
	unsigned int info_length = dev->drw_info_length;

	DRM_COPY_FROM_USER_IOCTL(draw, (drm_draw_t __user *) data,
				 sizeof(draw));

	id = draw.handle - 1;
	idx = id / (8 * sizeof(*bitfield));
	shift = id % (8 * sizeof(*bitfield));

	if (idx < 0 || idx >= bitfield_length ||
	    !(bitfield[idx] & (1 << shift))) {
		DRM_DEBUG("No such drawable %d\n", draw.handle);
		return 0;
	}

	spin_lock_irqsave(&dev->drw_lock, irqflags);

	bitfield[idx] &= ~(1 << shift);

	spin_unlock_irqrestore(&dev->drw_lock, irqflags);

	if (info[id]) {
		drm_free(info[id]->rects, info[id]->num_rects *
			 sizeof(drm_clip_rect_t), DRM_MEM_BUFS);
		drm_free(info[id], sizeof(**info), DRM_MEM_BUFS);
	}

	/* Can we shrink the arrays? */
	if (idx == bitfield_length - 1) {
		while (idx >= 0 && !bitfield[idx])
			--idx;

		bitfield_length = idx + 1;

		if (idx != id / (8 * sizeof(*bitfield)))
			bitfield = drm_alloc(bitfield_length *
					     sizeof(*bitfield), DRM_MEM_BUFS);

		if (!bitfield && bitfield_length) {
			bitfield = dev->drw_bitfield;
			bitfield_length = dev->drw_bitfield_length;
		}
	}

	if (bitfield != dev->drw_bitfield) {
		info_length = 8 * sizeof(*bitfield) * bitfield_length;

		info = drm_alloc(info_length * sizeof(*info), DRM_MEM_BUFS);

		if (!info && info_length) {
			info = dev->drw_info;
			info_length = dev->drw_info_length;
		}

		spin_lock_irqsave(&dev->drw_lock, irqflags);

		memcpy(bitfield, dev->drw_bitfield, bitfield_length *
		       sizeof(*bitfield));
		drm_free(dev->drw_bitfield, sizeof(*bitfield) *
			 dev->drw_bitfield_length, DRM_MEM_BUFS);
		dev->drw_bitfield = bitfield;
		dev->drw_bitfield_length = bitfield_length;

		if (info != dev->drw_info) {
			memcpy(info, dev->drw_info, info_length *
			       sizeof(*info));
			drm_free(dev->drw_info, sizeof(*info) *
				 dev->drw_info_length, DRM_MEM_BUFS);
			dev->drw_info = info;
			dev->drw_info_length = info_length;
		}

		spin_unlock_irqrestore(&dev->drw_lock, irqflags);
	}

	DRM_DEBUG("%d\n", draw.handle);
	return 0;
}

int drm_update_drawable_info(DRM_IOCTL_ARGS) {
	DRM_DEVICE;
	drm_update_draw_t update;
	unsigned int id, idx, shift;
	u32 *bitfield = dev->drw_bitfield;
	unsigned long irqflags, bitfield_length = dev->drw_bitfield_length;
	drm_drawable_info_t *info;
	drm_clip_rect_t *rects;
	int err;

	DRM_COPY_FROM_USER_IOCTL(update, (drm_update_draw_t __user *) data,
				 sizeof(update));

	id = update.handle - 1;
	idx = id / (8 * sizeof(*bitfield));
	shift = id % (8 * sizeof(*bitfield));

	if (idx < 0 || idx >= bitfield_length ||
	    !(bitfield[idx] & (1 << shift))) {
		DRM_ERROR("No such drawable %d\n", update.handle);
		return DRM_ERR(EINVAL);
	}

	info = dev->drw_info[id];

	if (!info) {
		info = drm_calloc(1, sizeof(drm_drawable_info_t), DRM_MEM_BUFS);

		if (!info) {
			DRM_ERROR("Failed to allocate drawable info memory\n");
			return DRM_ERR(ENOMEM);
		}
	}

	switch (update.type) {
	case DRM_DRAWABLE_CLIPRECTS:
		if (update.num != info->num_rects) {
			rects = drm_alloc(update.num * sizeof(drm_clip_rect_t),
					 DRM_MEM_BUFS);
		} else
			rects = info->rects;

		if (update.num && !rects) {
			DRM_ERROR("Failed to allocate cliprect memory\n");
			err = DRM_ERR(ENOMEM);
			goto error;
		}

		if (update.num && DRM_COPY_FROM_USER(rects,
						     (drm_clip_rect_t __user *)
						     (unsigned long)update.data,
						     update.num *
						     sizeof(*rects))) {
			DRM_ERROR("Failed to copy cliprects from userspace\n");
			err = DRM_ERR(EFAULT);
			goto error;
		}

		spin_lock_irqsave(&dev->drw_lock, irqflags);

		if (rects != info->rects) {
			drm_free(info->rects, info->num_rects *
				 sizeof(drm_clip_rect_t), DRM_MEM_BUFS);
		}

		info->rects = rects;
		info->num_rects = update.num;
		dev->drw_info[id] = info;

		spin_unlock_irqrestore(&dev->drw_lock, irqflags);

		DRM_DEBUG("Updated %d cliprects for drawable %d\n",
			  info->num_rects, id);
		break;
	default:
		DRM_ERROR("Invalid update type %d\n", update.type);
		return DRM_ERR(EINVAL);
	}

	return 0;

error:
	if (!dev->drw_info[id])
		drm_free(info, sizeof(*info), DRM_MEM_BUFS);
	else if (rects != dev->drw_info[id]->rects)
		drm_free(rects, update.num *
			 sizeof(drm_clip_rect_t), DRM_MEM_BUFS);

	return err;
}

/**
 * Caller must hold the drawable spinlock!
 */
drm_drawable_info_t *drm_get_drawable_info(drm_device_t *dev, drm_drawable_t id) {
	u32 *bitfield = dev->drw_bitfield;
	unsigned int idx, shift;

	id--;
	idx = id / (8 * sizeof(*bitfield));
	shift = id % (8 * sizeof(*bitfield));

	if (idx < 0 || idx >= dev->drw_bitfield_length ||
	    !(bitfield[idx] & (1 << shift))) {
		DRM_DEBUG("No such drawable %d\n", id);
		return NULL;
	}

	return dev->drw_info[id];
}
EXPORT_SYMBOL(drm_get_drawable_info);
