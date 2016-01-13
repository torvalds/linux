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

#include "virtgpu_drv.h"
#include <drm/drm_plane_helper.h>
#include <drm/drm_atomic_helper.h>

static const uint32_t virtio_gpu_formats[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_BGRX8888,
	DRM_FORMAT_BGRA8888,
	DRM_FORMAT_RGBX8888,
	DRM_FORMAT_RGBA8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_ABGR8888,
};

static void virtio_gpu_plane_destroy(struct drm_plane *plane)
{
	kfree(plane);
}

static const struct drm_plane_funcs virtio_gpu_plane_funcs = {
	.update_plane		= drm_atomic_helper_update_plane,
	.disable_plane		= drm_atomic_helper_disable_plane,
	.destroy		= virtio_gpu_plane_destroy,
	.reset			= drm_atomic_helper_plane_reset,
	.atomic_duplicate_state = drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_plane_destroy_state,
};

static int virtio_gpu_plane_atomic_check(struct drm_plane *plane,
					 struct drm_plane_state *state)
{
	return 0;
}

static void virtio_gpu_plane_atomic_update(struct drm_plane *plane,
					   struct drm_plane_state *old_state)
{
	struct drm_device *dev = plane->dev;
	struct virtio_gpu_device *vgdev = dev->dev_private;
	struct virtio_gpu_output *output = drm_crtc_to_virtio_gpu_output(plane->crtc);
	struct virtio_gpu_framebuffer *vgfb;
	struct virtio_gpu_object *bo;
	uint32_t handle;

	if (plane->state->fb) {
		vgfb = to_virtio_gpu_framebuffer(plane->state->fb);
		bo = gem_to_virtio_gpu_obj(vgfb->obj);
		handle = bo->hw_res_handle;
		if (bo->dumb) {
			virtio_gpu_cmd_transfer_to_host_2d
				(vgdev, handle, 0,
				 cpu_to_le32(plane->state->crtc_w),
				 cpu_to_le32(plane->state->crtc_h),
				 plane->state->crtc_x, plane->state->crtc_y, NULL);
		}
	} else {
		handle = 0;
	}

	DRM_DEBUG("handle 0x%x, crtc %dx%d+%d+%d\n", handle,
		  plane->state->crtc_w, plane->state->crtc_h,
		  plane->state->crtc_x, plane->state->crtc_y);
	virtio_gpu_cmd_set_scanout(vgdev, output->index, handle,
				   plane->state->crtc_w,
				   plane->state->crtc_h,
				   plane->state->crtc_x,
				   plane->state->crtc_y);
	virtio_gpu_cmd_resource_flush(vgdev, handle,
				      plane->state->crtc_x,
				      plane->state->crtc_y,
				      plane->state->crtc_w,
				      plane->state->crtc_h);
}


static const struct drm_plane_helper_funcs virtio_gpu_plane_helper_funcs = {
	.atomic_check		= virtio_gpu_plane_atomic_check,
	.atomic_update		= virtio_gpu_plane_atomic_update,
};

struct drm_plane *virtio_gpu_plane_init(struct virtio_gpu_device *vgdev,
					int index)
{
	struct drm_device *dev = vgdev->ddev;
	struct drm_plane *plane;
	int ret;

	plane = kzalloc(sizeof(*plane), GFP_KERNEL);
	if (!plane)
		return ERR_PTR(-ENOMEM);

	ret = drm_universal_plane_init(dev, plane, 1 << index,
				       &virtio_gpu_plane_funcs,
				       virtio_gpu_formats,
				       ARRAY_SIZE(virtio_gpu_formats),
				       DRM_PLANE_TYPE_PRIMARY);
	if (ret)
		goto err_plane_init;

	drm_plane_helper_add(plane, &virtio_gpu_plane_helper_funcs);
	return plane;

err_plane_init:
	kfree(plane);
	return ERR_PTR(ret);
}
