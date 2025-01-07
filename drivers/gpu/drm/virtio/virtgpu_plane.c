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

#include <drm/drm_atomic_helper.h>
#include <drm/drm_damage_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem_atomic_helper.h>
#include <linux/virtio_dma_buf.h>

#include "virtgpu_drv.h"

static const uint32_t virtio_gpu_formats[] = {
	DRM_FORMAT_HOST_XRGB8888,
};

static const uint32_t virtio_gpu_cursor_formats[] = {
	DRM_FORMAT_HOST_ARGB8888,
};

uint32_t virtio_gpu_translate_format(uint32_t drm_fourcc)
{
	uint32_t format;

	switch (drm_fourcc) {
	case DRM_FORMAT_XRGB8888:
		format = VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM;
		break;
	case DRM_FORMAT_ARGB8888:
		format = VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM;
		break;
	case DRM_FORMAT_BGRX8888:
		format = VIRTIO_GPU_FORMAT_X8R8G8B8_UNORM;
		break;
	case DRM_FORMAT_BGRA8888:
		format = VIRTIO_GPU_FORMAT_A8R8G8B8_UNORM;
		break;
	default:
		/*
		 * This should not happen, we handle everything listed
		 * in virtio_gpu_formats[].
		 */
		format = 0;
		break;
	}
	WARN_ON(format == 0);
	return format;
}

static struct
drm_plane_state *virtio_gpu_plane_duplicate_state(struct drm_plane *plane)
{
	struct virtio_gpu_plane_state *new;

	if (WARN_ON(!plane->state))
		return NULL;

	new = kzalloc(sizeof(*new), GFP_KERNEL);
	if (!new)
		return NULL;

	__drm_atomic_helper_plane_duplicate_state(plane, &new->base);

	return &new->base;
}

static const struct drm_plane_funcs virtio_gpu_plane_funcs = {
	.update_plane		= drm_atomic_helper_update_plane,
	.disable_plane		= drm_atomic_helper_disable_plane,
	.reset			= drm_atomic_helper_plane_reset,
	.atomic_duplicate_state = virtio_gpu_plane_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_plane_destroy_state,
};

static int virtio_gpu_plane_atomic_check(struct drm_plane *plane,
					 struct drm_atomic_state *state)
{
	struct drm_plane_state *new_plane_state = drm_atomic_get_new_plane_state(state,
										 plane);
	struct drm_plane_state *old_plane_state = drm_atomic_get_old_plane_state(state,
										 plane);
	bool is_cursor = plane->type == DRM_PLANE_TYPE_CURSOR;
	struct drm_crtc_state *crtc_state;
	int ret;

	if (!new_plane_state->fb || WARN_ON(!new_plane_state->crtc))
		return 0;

	/*
	 * Ignore damage clips if the framebuffer attached to the plane's state
	 * has changed since the last plane update (page-flip). In this case, a
	 * full plane update should happen because uploads are done per-buffer.
	 */
	if (old_plane_state->fb != new_plane_state->fb)
		new_plane_state->ignore_damage_clips = true;

	crtc_state = drm_atomic_get_crtc_state(state,
					       new_plane_state->crtc);
	if (IS_ERR(crtc_state))
                return PTR_ERR(crtc_state);

	ret = drm_atomic_helper_check_plane_state(new_plane_state, crtc_state,
						  DRM_PLANE_NO_SCALING,
						  DRM_PLANE_NO_SCALING,
						  is_cursor, true);
	return ret;
}

static void virtio_gpu_update_dumb_bo(struct virtio_gpu_device *vgdev,
				      struct drm_plane_state *state,
				      struct drm_rect *rect)
{
	struct virtio_gpu_object *bo =
		gem_to_virtio_gpu_obj(state->fb->obj[0]);
	struct virtio_gpu_object_array *objs;
	uint32_t w = rect->x2 - rect->x1;
	uint32_t h = rect->y2 - rect->y1;
	uint32_t x = rect->x1;
	uint32_t y = rect->y1;
	uint32_t off = x * state->fb->format->cpp[0] +
		y * state->fb->pitches[0];

	objs = virtio_gpu_array_alloc(1);
	if (!objs)
		return;
	virtio_gpu_array_add_obj(objs, &bo->base.base);

	virtio_gpu_cmd_transfer_to_host_2d(vgdev, off, w, h, x, y,
					   objs, NULL);
}

static void virtio_gpu_resource_flush(struct drm_plane *plane,
				      uint32_t x, uint32_t y,
				      uint32_t width, uint32_t height)
{
	struct drm_device *dev = plane->dev;
	struct virtio_gpu_device *vgdev = dev->dev_private;
	struct virtio_gpu_framebuffer *vgfb;
	struct virtio_gpu_plane_state *vgplane_st;
	struct virtio_gpu_object *bo;

	vgfb = to_virtio_gpu_framebuffer(plane->state->fb);
	vgplane_st = to_virtio_gpu_plane_state(plane->state);
	bo = gem_to_virtio_gpu_obj(vgfb->base.obj[0]);
	if (vgplane_st->fence) {
		struct virtio_gpu_object_array *objs;

		objs = virtio_gpu_array_alloc(1);
		if (!objs)
			return;
		virtio_gpu_array_add_obj(objs, vgfb->base.obj[0]);
		virtio_gpu_array_lock_resv(objs);
		virtio_gpu_cmd_resource_flush(vgdev, bo->hw_res_handle, x, y,
					      width, height, objs,
					      vgplane_st->fence);
		virtio_gpu_notify(vgdev);
		dma_fence_wait_timeout(&vgplane_st->fence->f, true,
				       msecs_to_jiffies(50));
	} else {
		virtio_gpu_cmd_resource_flush(vgdev, bo->hw_res_handle, x, y,
					      width, height, NULL, NULL);
		virtio_gpu_notify(vgdev);
	}
}

static void virtio_gpu_primary_plane_update(struct drm_plane *plane,
					    struct drm_atomic_state *state)
{
	struct drm_plane_state *old_state = drm_atomic_get_old_plane_state(state,
									   plane);
	struct drm_device *dev = plane->dev;
	struct virtio_gpu_device *vgdev = dev->dev_private;
	struct virtio_gpu_output *output = NULL;
	struct virtio_gpu_object *bo;
	struct drm_rect rect;

	if (plane->state->crtc)
		output = drm_crtc_to_virtio_gpu_output(plane->state->crtc);
	if (old_state->crtc)
		output = drm_crtc_to_virtio_gpu_output(old_state->crtc);
	if (WARN_ON(!output))
		return;

	if (!plane->state->fb || !output->crtc.state->active) {
		DRM_DEBUG("nofb\n");
		virtio_gpu_cmd_set_scanout(vgdev, output->index, 0,
					   plane->state->src_w >> 16,
					   plane->state->src_h >> 16,
					   0, 0);
		virtio_gpu_notify(vgdev);
		return;
	}

	if (!drm_atomic_helper_damage_merged(old_state, plane->state, &rect))
		return;

	bo = gem_to_virtio_gpu_obj(plane->state->fb->obj[0]);
	if (bo->dumb)
		virtio_gpu_update_dumb_bo(vgdev, plane->state, &rect);

	if (plane->state->fb != old_state->fb ||
	    plane->state->src_w != old_state->src_w ||
	    plane->state->src_h != old_state->src_h ||
	    plane->state->src_x != old_state->src_x ||
	    plane->state->src_y != old_state->src_y ||
	    output->needs_modeset) {
		output->needs_modeset = false;
		DRM_DEBUG("handle 0x%x, crtc %dx%d+%d+%d, src %dx%d+%d+%d\n",
			  bo->hw_res_handle,
			  plane->state->crtc_w, plane->state->crtc_h,
			  plane->state->crtc_x, plane->state->crtc_y,
			  plane->state->src_w >> 16,
			  plane->state->src_h >> 16,
			  plane->state->src_x >> 16,
			  plane->state->src_y >> 16);

		if (bo->host3d_blob || bo->guest_blob) {
			virtio_gpu_cmd_set_scanout_blob
						(vgdev, output->index, bo,
						 plane->state->fb,
						 plane->state->src_w >> 16,
						 plane->state->src_h >> 16,
						 plane->state->src_x >> 16,
						 plane->state->src_y >> 16);
		} else {
			virtio_gpu_cmd_set_scanout(vgdev, output->index,
						   bo->hw_res_handle,
						   plane->state->src_w >> 16,
						   plane->state->src_h >> 16,
						   plane->state->src_x >> 16,
						   plane->state->src_y >> 16);
		}
	}

	virtio_gpu_resource_flush(plane,
				  rect.x1,
				  rect.y1,
				  rect.x2 - rect.x1,
				  rect.y2 - rect.y1);
}

static int virtio_gpu_prepare_imported_obj(struct drm_plane *plane,
					   struct drm_plane_state *new_state,
					   struct drm_gem_object *obj)
{
	struct virtio_gpu_device *vgdev = plane->dev->dev_private;
	struct virtio_gpu_object *bo = gem_to_virtio_gpu_obj(obj);
	struct dma_buf_attachment *attach = obj->import_attach;
	struct dma_resv *resv = attach->dmabuf->resv;
	struct virtio_gpu_mem_entry *ents = NULL;
	unsigned int nents;
	int ret;

	dma_resv_lock(resv, NULL);

	ret = dma_buf_pin(attach);
	if (ret) {
		dma_resv_unlock(resv);
		return ret;
	}

	if (!bo->sgt) {
		ret = virtgpu_dma_buf_import_sgt(&ents, &nents,
						 bo, attach);
		if (ret)
			goto err;

		virtio_gpu_object_attach(vgdev, bo, ents, nents);
	}

	dma_resv_unlock(resv);
	return 0;

err:
	dma_buf_unpin(attach);
	dma_resv_unlock(resv);
	return ret;
}

static int virtio_gpu_plane_prepare_fb(struct drm_plane *plane,
				       struct drm_plane_state *new_state)
{
	struct drm_device *dev = plane->dev;
	struct virtio_gpu_device *vgdev = dev->dev_private;
	struct virtio_gpu_framebuffer *vgfb;
	struct virtio_gpu_plane_state *vgplane_st;
	struct virtio_gpu_object *bo;
	struct drm_gem_object *obj;
	int ret;

	if (!new_state->fb)
		return 0;

	vgfb = to_virtio_gpu_framebuffer(new_state->fb);
	vgplane_st = to_virtio_gpu_plane_state(new_state);
	bo = gem_to_virtio_gpu_obj(vgfb->base.obj[0]);

	drm_gem_plane_helper_prepare_fb(plane, new_state);

	if (!bo || (plane->type == DRM_PLANE_TYPE_PRIMARY && !bo->guest_blob))
		return 0;

	obj = new_state->fb->obj[0];
	if (obj->import_attach) {
		ret = virtio_gpu_prepare_imported_obj(plane, new_state, obj);
		if (ret)
			return ret;
	}

	if (bo->dumb || obj->import_attach) {
		vgplane_st->fence = virtio_gpu_fence_alloc(vgdev,
						     vgdev->fence_drv.context,
						     0);
		if (!vgplane_st->fence)
			return -ENOMEM;
	}

	return 0;
}

static void virtio_gpu_cleanup_imported_obj(struct drm_gem_object *obj)
{
	struct dma_buf_attachment *attach = obj->import_attach;
	struct dma_resv *resv = attach->dmabuf->resv;

	dma_resv_lock(resv, NULL);
	dma_buf_unpin(attach);
	dma_resv_unlock(resv);
}

static void virtio_gpu_plane_cleanup_fb(struct drm_plane *plane,
					struct drm_plane_state *state)
{
	struct virtio_gpu_plane_state *vgplane_st;
	struct drm_gem_object *obj;

	if (!state->fb)
		return;

	vgplane_st = to_virtio_gpu_plane_state(state);
	if (vgplane_st->fence) {
		dma_fence_put(&vgplane_st->fence->f);
		vgplane_st->fence = NULL;
	}

	obj = state->fb->obj[0];
	if (obj->import_attach)
		virtio_gpu_cleanup_imported_obj(obj);
}

static void virtio_gpu_cursor_plane_update(struct drm_plane *plane,
					   struct drm_atomic_state *state)
{
	struct drm_plane_state *old_state = drm_atomic_get_old_plane_state(state,
									   plane);
	struct drm_device *dev = plane->dev;
	struct virtio_gpu_device *vgdev = dev->dev_private;
	struct virtio_gpu_output *output = NULL;
	struct virtio_gpu_framebuffer *vgfb;
	struct virtio_gpu_plane_state *vgplane_st;
	struct virtio_gpu_object *bo = NULL;
	uint32_t handle;

	if (plane->state->crtc)
		output = drm_crtc_to_virtio_gpu_output(plane->state->crtc);
	if (old_state->crtc)
		output = drm_crtc_to_virtio_gpu_output(old_state->crtc);
	if (WARN_ON(!output))
		return;

	if (plane->state->fb) {
		vgfb = to_virtio_gpu_framebuffer(plane->state->fb);
		vgplane_st = to_virtio_gpu_plane_state(plane->state);
		bo = gem_to_virtio_gpu_obj(vgfb->base.obj[0]);
		handle = bo->hw_res_handle;
	} else {
		handle = 0;
	}

	if (bo && bo->dumb && (plane->state->fb != old_state->fb)) {
		/* new cursor -- update & wait */
		struct virtio_gpu_object_array *objs;

		objs = virtio_gpu_array_alloc(1);
		if (!objs)
			return;
		virtio_gpu_array_add_obj(objs, vgfb->base.obj[0]);
		virtio_gpu_array_lock_resv(objs);
		virtio_gpu_cmd_transfer_to_host_2d
			(vgdev, 0,
			 plane->state->crtc_w,
			 plane->state->crtc_h,
			 0, 0, objs, vgplane_st->fence);
		virtio_gpu_notify(vgdev);
		dma_fence_wait(&vgplane_st->fence->f, true);
	}

	if (plane->state->fb != old_state->fb) {
		DRM_DEBUG("update, handle %d, pos +%d+%d, hot %d,%d\n", handle,
			  plane->state->crtc_x,
			  plane->state->crtc_y,
			  plane->state->hotspot_x,
			  plane->state->hotspot_y);
		output->cursor.hdr.type =
			cpu_to_le32(VIRTIO_GPU_CMD_UPDATE_CURSOR);
		output->cursor.resource_id = cpu_to_le32(handle);
		if (plane->state->fb) {
			output->cursor.hot_x =
				cpu_to_le32(plane->state->hotspot_x);
			output->cursor.hot_y =
				cpu_to_le32(plane->state->hotspot_y);
		} else {
			output->cursor.hot_x = cpu_to_le32(0);
			output->cursor.hot_y = cpu_to_le32(0);
		}
	} else {
		DRM_DEBUG("move +%d+%d\n",
			  plane->state->crtc_x,
			  plane->state->crtc_y);
		output->cursor.hdr.type =
			cpu_to_le32(VIRTIO_GPU_CMD_MOVE_CURSOR);
	}
	output->cursor.pos.x = cpu_to_le32(plane->state->crtc_x);
	output->cursor.pos.y = cpu_to_le32(plane->state->crtc_y);
	virtio_gpu_cursor_ping(vgdev, output);
}

static const struct drm_plane_helper_funcs virtio_gpu_primary_helper_funcs = {
	.prepare_fb		= virtio_gpu_plane_prepare_fb,
	.cleanup_fb		= virtio_gpu_plane_cleanup_fb,
	.atomic_check		= virtio_gpu_plane_atomic_check,
	.atomic_update		= virtio_gpu_primary_plane_update,
};

static const struct drm_plane_helper_funcs virtio_gpu_cursor_helper_funcs = {
	.prepare_fb		= virtio_gpu_plane_prepare_fb,
	.cleanup_fb		= virtio_gpu_plane_cleanup_fb,
	.atomic_check		= virtio_gpu_plane_atomic_check,
	.atomic_update		= virtio_gpu_cursor_plane_update,
};

struct drm_plane *virtio_gpu_plane_init(struct virtio_gpu_device *vgdev,
					enum drm_plane_type type,
					int index)
{
	struct drm_device *dev = vgdev->ddev;
	const struct drm_plane_helper_funcs *funcs;
	struct drm_plane *plane;
	const uint32_t *formats;
	int nformats;

	if (type == DRM_PLANE_TYPE_CURSOR) {
		formats = virtio_gpu_cursor_formats;
		nformats = ARRAY_SIZE(virtio_gpu_cursor_formats);
		funcs = &virtio_gpu_cursor_helper_funcs;
	} else {
		formats = virtio_gpu_formats;
		nformats = ARRAY_SIZE(virtio_gpu_formats);
		funcs = &virtio_gpu_primary_helper_funcs;
	}

	plane = drmm_universal_plane_alloc(dev, struct drm_plane, dev,
					   1 << index, &virtio_gpu_plane_funcs,
					   formats, nformats, NULL, type, NULL);
	if (IS_ERR(plane))
		return plane;

	drm_plane_helper_add(plane, funcs);

	if (type == DRM_PLANE_TYPE_PRIMARY)
		drm_plane_enable_fb_damage_clips(plane);

	return plane;
}
