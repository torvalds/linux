/*
 * Copyright (C) 2015 Red Hat, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <drm/drmP.h>
#include <drm/drm_fb_helper.h>
#include "virtgpu_drv.h"

static int virtio_gpu_dirty_update(struct virtio_gpu_framebuffer *fb,
				   bool store, int x, int y,
				   int width, int height)
{
	struct drm_device *dev = fb->base.dev;
	struct virtio_gpu_device *vgdev = dev->dev_private;
	bool store_for_later = false;
	int bpp = fb->base.format->cpp[0];
	int x2, y2;
	unsigned long flags;
	struct virtio_gpu_object *obj = gem_to_virtio_gpu_obj(fb->base.obj[0]);

	if ((width <= 0) ||
	    (x + width > fb->base.width) ||
	    (y + height > fb->base.height)) {
		DRM_DEBUG("values out of range %dx%d+%d+%d, fb %dx%d\n",
			  width, height, x, y,
			  fb->base.width, fb->base.height);
		return -EINVAL;
	}

	/*
	 * Can be called with pretty much any context (console output
	 * path).  If we are in atomic just store the dirty rect info
	 * to send out the update later.
	 *
	 * Can't test inside spin lock.
	 */
	if (in_atomic() || store)
		store_for_later = true;

	x2 = x + width - 1;
	y2 = y + height - 1;

	spin_lock_irqsave(&fb->dirty_lock, flags);

	if (fb->y1 < y)
		y = fb->y1;
	if (fb->y2 > y2)
		y2 = fb->y2;
	if (fb->x1 < x)
		x = fb->x1;
	if (fb->x2 > x2)
		x2 = fb->x2;

	if (store_for_later) {
		fb->x1 = x;
		fb->x2 = x2;
		fb->y1 = y;
		fb->y2 = y2;
		spin_unlock_irqrestore(&fb->dirty_lock, flags);
		return 0;
	}

	fb->x1 = fb->y1 = INT_MAX;
	fb->x2 = fb->y2 = 0;

	spin_unlock_irqrestore(&fb->dirty_lock, flags);

	{
		uint32_t offset;
		uint32_t w = x2 - x + 1;
		uint32_t h = y2 - y + 1;

		offset = (y * fb->base.pitches[0]) + x * bpp;

		virtio_gpu_cmd_transfer_to_host_2d(vgdev, obj,
						   offset,
						   cpu_to_le32(w),
						   cpu_to_le32(h),
						   cpu_to_le32(x),
						   cpu_to_le32(y),
						   NULL);

	}
	virtio_gpu_cmd_resource_flush(vgdev, obj->hw_res_handle,
				      x, y, x2 - x + 1, y2 - y + 1);
	return 0;
}

int virtio_gpu_surface_dirty(struct virtio_gpu_framebuffer *vgfb,
			     struct drm_clip_rect *clips,
			     unsigned int num_clips)
{
	struct virtio_gpu_device *vgdev = vgfb->base.dev->dev_private;
	struct virtio_gpu_object *obj = gem_to_virtio_gpu_obj(vgfb->base.obj[0]);
	struct drm_clip_rect norect;
	struct drm_clip_rect *clips_ptr;
	int left, right, top, bottom;
	int i;
	int inc = 1;

	if (!num_clips) {
		num_clips = 1;
		clips = &norect;
		norect.x1 = norect.y1 = 0;
		norect.x2 = vgfb->base.width;
		norect.y2 = vgfb->base.height;
	}
	left = clips->x1;
	right = clips->x2;
	top = clips->y1;
	bottom = clips->y2;

	/* skip the first clip rect */
	for (i = 1, clips_ptr = clips + inc;
	     i < num_clips; i++, clips_ptr += inc) {
		left = min_t(int, left, (int)clips_ptr->x1);
		right = max_t(int, right, (int)clips_ptr->x2);
		top = min_t(int, top, (int)clips_ptr->y1);
		bottom = max_t(int, bottom, (int)clips_ptr->y2);
	}

	if (obj->dumb)
		return virtio_gpu_dirty_update(vgfb, false, left, top,
					       right - left, bottom - top);

	virtio_gpu_cmd_resource_flush(vgdev, obj->hw_res_handle,
				      left, top, right - left, bottom - top);
	return 0;
}
