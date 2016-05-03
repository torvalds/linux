/*
 * Copyright 2013 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Dave Airlie
 *          Alon Levy
 */


#include <linux/crc32.h>

#include "qxl_drv.h"
#include "qxl_object.h"
#include "drm_crtc_helper.h"
#include <drm/drm_plane_helper.h>

static bool qxl_head_enabled(struct qxl_head *head)
{
	return head->width && head->height;
}

void qxl_alloc_client_monitors_config(struct qxl_device *qdev, unsigned count)
{
	if (qdev->client_monitors_config &&
	    count > qdev->client_monitors_config->count) {
		kfree(qdev->client_monitors_config);
		qdev->client_monitors_config = NULL;
	}
	if (!qdev->client_monitors_config) {
		qdev->client_monitors_config = kzalloc(
				sizeof(struct qxl_monitors_config) +
				sizeof(struct qxl_head) * count, GFP_KERNEL);
		if (!qdev->client_monitors_config) {
			qxl_io_log(qdev,
				   "%s: allocation failure for %u heads\n",
				   __func__, count);
			return;
		}
	}
	qdev->client_monitors_config->count = count;
}

static int qxl_display_copy_rom_client_monitors_config(struct qxl_device *qdev)
{
	int i;
	int num_monitors;
	uint32_t crc;

	num_monitors = qdev->rom->client_monitors_config.count;
	crc = crc32(0, (const uint8_t *)&qdev->rom->client_monitors_config,
		  sizeof(qdev->rom->client_monitors_config));
	if (crc != qdev->rom->client_monitors_config_crc) {
		qxl_io_log(qdev, "crc mismatch: have %X (%zd) != %X\n", crc,
			   sizeof(qdev->rom->client_monitors_config),
			   qdev->rom->client_monitors_config_crc);
		return 1;
	}
	if (num_monitors > qdev->monitors_config->max_allowed) {
		DRM_DEBUG_KMS("client monitors list will be truncated: %d < %d\n",
			      qdev->monitors_config->max_allowed, num_monitors);
		num_monitors = qdev->monitors_config->max_allowed;
	} else {
		num_monitors = qdev->rom->client_monitors_config.count;
	}
	qxl_alloc_client_monitors_config(qdev, num_monitors);
	/* we copy max from the client but it isn't used */
	qdev->client_monitors_config->max_allowed =
				qdev->monitors_config->max_allowed;
	for (i = 0 ; i < qdev->client_monitors_config->count ; ++i) {
		struct qxl_urect *c_rect =
			&qdev->rom->client_monitors_config.heads[i];
		struct qxl_head *client_head =
			&qdev->client_monitors_config->heads[i];
		client_head->x = c_rect->left;
		client_head->y = c_rect->top;
		client_head->width = c_rect->right - c_rect->left;
		client_head->height = c_rect->bottom - c_rect->top;
		client_head->surface_id = 0;
		client_head->id = i;
		client_head->flags = 0;
		DRM_DEBUG_KMS("read %dx%d+%d+%d\n", client_head->width, client_head->height,
			  client_head->x, client_head->y);
	}
	return 0;
}

static void qxl_update_offset_props(struct qxl_device *qdev)
{
	struct drm_device *dev = qdev->ddev;
	struct drm_connector *connector;
	struct qxl_output *output;
	struct qxl_head *head;

	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		output = drm_connector_to_qxl_output(connector);

		head = &qdev->client_monitors_config->heads[output->index];

		drm_object_property_set_value(&connector->base,
			dev->mode_config.suggested_x_property, head->x);
		drm_object_property_set_value(&connector->base,
			dev->mode_config.suggested_y_property, head->y);
	}
}

void qxl_display_read_client_monitors_config(struct qxl_device *qdev)
{

	struct drm_device *dev = qdev->ddev;
	while (qxl_display_copy_rom_client_monitors_config(qdev)) {
		qxl_io_log(qdev, "failed crc check for client_monitors_config,"
				 " retrying\n");
	}

	drm_modeset_lock_all(dev);
	qxl_update_offset_props(qdev);
	drm_modeset_unlock_all(dev);
	if (!drm_helper_hpd_irq_event(qdev->ddev)) {
		/* notify that the monitor configuration changed, to
		   adjust at the arbitrary resolution */
		drm_kms_helper_hotplug_event(qdev->ddev);
	}
}

static int qxl_add_monitors_config_modes(struct drm_connector *connector,
                                         unsigned *pwidth,
                                         unsigned *pheight)
{
	struct drm_device *dev = connector->dev;
	struct qxl_device *qdev = dev->dev_private;
	struct qxl_output *output = drm_connector_to_qxl_output(connector);
	int h = output->index;
	struct drm_display_mode *mode = NULL;
	struct qxl_head *head;

	if (!qdev->client_monitors_config)
		return 0;
	head = &qdev->client_monitors_config->heads[h];

	mode = drm_cvt_mode(dev, head->width, head->height, 60, false, false,
			    false);
	mode->type |= DRM_MODE_TYPE_PREFERRED;
	*pwidth = head->width;
	*pheight = head->height;
	drm_mode_probed_add(connector, mode);
	/* remember the last custom size for mode validation */
	qdev->monitors_config_width = mode->hdisplay;
	qdev->monitors_config_height = mode->vdisplay;
	return 1;
}

static struct mode_size {
	int w;
	int h;
} common_modes[] = {
	{ 640,  480},
	{ 720,  480},
	{ 800,  600},
	{ 848,  480},
	{1024,  768},
	{1152,  768},
	{1280,  720},
	{1280,  800},
	{1280,  854},
	{1280,  960},
	{1280, 1024},
	{1440,  900},
	{1400, 1050},
	{1680, 1050},
	{1600, 1200},
	{1920, 1080},
	{1920, 1200}
};

static int qxl_add_common_modes(struct drm_connector *connector,
                                unsigned pwidth,
                                unsigned pheight)
{
	struct drm_device *dev = connector->dev;
	struct drm_display_mode *mode = NULL;
	int i;
	for (i = 0; i < ARRAY_SIZE(common_modes); i++) {
		mode = drm_cvt_mode(dev, common_modes[i].w, common_modes[i].h,
				    60, false, false, false);
		if (common_modes[i].w == pwidth && common_modes[i].h == pheight)
			mode->type |= DRM_MODE_TYPE_PREFERRED;
		drm_mode_probed_add(connector, mode);
	}
	return i - 1;
}

static void qxl_crtc_destroy(struct drm_crtc *crtc)
{
	struct qxl_crtc *qxl_crtc = to_qxl_crtc(crtc);

	drm_crtc_cleanup(crtc);
	kfree(qxl_crtc);
}

static int qxl_crtc_page_flip(struct drm_crtc *crtc,
                              struct drm_framebuffer *fb,
                              struct drm_pending_vblank_event *event,
                              uint32_t page_flip_flags)
{
	struct drm_device *dev = crtc->dev;
	struct qxl_device *qdev = dev->dev_private;
	struct qxl_crtc *qcrtc = to_qxl_crtc(crtc);
	struct qxl_framebuffer *qfb_src = to_qxl_framebuffer(fb);
	struct qxl_framebuffer *qfb_old = to_qxl_framebuffer(crtc->primary->fb);
	struct qxl_bo *bo_old = gem_to_qxl_bo(qfb_old->obj);
	struct qxl_bo *bo = gem_to_qxl_bo(qfb_src->obj);
	unsigned long flags;
	struct drm_clip_rect norect = {
	    .x1 = 0,
	    .y1 = 0,
	    .x2 = fb->width,
	    .y2 = fb->height
	};
	int inc = 1;
	int one_clip_rect = 1;
	int ret = 0;

	crtc->primary->fb = fb;
	bo_old->is_primary = false;
	bo->is_primary = true;

	ret = qxl_bo_reserve(bo, false);
	if (ret)
		return ret;
	ret = qxl_bo_pin(bo, bo->type, NULL);
	qxl_bo_unreserve(bo);
	if (ret)
		return ret;

	qxl_draw_dirty_fb(qdev, qfb_src, bo, 0, 0,
			  &norect, one_clip_rect, inc);

	drm_vblank_get(dev, qcrtc->index);

	if (event) {
		spin_lock_irqsave(&dev->event_lock, flags);
		drm_send_vblank_event(dev, qcrtc->index, event);
		spin_unlock_irqrestore(&dev->event_lock, flags);
	}
	drm_vblank_put(dev, qcrtc->index);

	ret = qxl_bo_reserve(bo, false);
	if (!ret) {
		qxl_bo_unpin(bo);
		qxl_bo_unreserve(bo);
	}

	return 0;
}

static int
qxl_hide_cursor(struct qxl_device *qdev)
{
	struct qxl_release *release;
	struct qxl_cursor_cmd *cmd;
	int ret;

	ret = qxl_alloc_release_reserved(qdev, sizeof(*cmd), QXL_RELEASE_CURSOR_CMD,
					 &release, NULL);
	if (ret)
		return ret;

	ret = qxl_release_reserve_list(release, true);
	if (ret) {
		qxl_release_free(qdev, release);
		return ret;
	}

	cmd = (struct qxl_cursor_cmd *)qxl_release_map(qdev, release);
	cmd->type = QXL_CURSOR_HIDE;
	qxl_release_unmap(qdev, release, &cmd->release_info);

	qxl_push_cursor_ring_release(qdev, release, QXL_CMD_CURSOR, false);
	qxl_release_fence_buffer_objects(release);
	return 0;
}

static int qxl_crtc_cursor_set2(struct drm_crtc *crtc,
				struct drm_file *file_priv,
				uint32_t handle,
				uint32_t width,
				uint32_t height, int32_t hot_x, int32_t hot_y)
{
	struct drm_device *dev = crtc->dev;
	struct qxl_device *qdev = dev->dev_private;
	struct qxl_crtc *qcrtc = to_qxl_crtc(crtc);
	struct drm_gem_object *obj;
	struct qxl_cursor *cursor;
	struct qxl_cursor_cmd *cmd;
	struct qxl_bo *cursor_bo, *user_bo;
	struct qxl_release *release;
	void *user_ptr;

	int size = 64*64*4;
	int ret = 0;
	if (!handle)
		return qxl_hide_cursor(qdev);

	obj = drm_gem_object_lookup(crtc->dev, file_priv, handle);
	if (!obj) {
		DRM_ERROR("cannot find cursor object\n");
		return -ENOENT;
	}

	user_bo = gem_to_qxl_bo(obj);

	ret = qxl_bo_reserve(user_bo, false);
	if (ret)
		goto out_unref;

	ret = qxl_bo_pin(user_bo, QXL_GEM_DOMAIN_CPU, NULL);
	qxl_bo_unreserve(user_bo);
	if (ret)
		goto out_unref;

	ret = qxl_bo_kmap(user_bo, &user_ptr);
	if (ret)
		goto out_unpin;

	ret = qxl_alloc_release_reserved(qdev, sizeof(*cmd),
					 QXL_RELEASE_CURSOR_CMD,
					 &release, NULL);
	if (ret)
		goto out_kunmap;

	ret = qxl_alloc_bo_reserved(qdev, release, sizeof(struct qxl_cursor) + size,
			   &cursor_bo);
	if (ret)
		goto out_free_release;

	ret = qxl_release_reserve_list(release, false);
	if (ret)
		goto out_free_bo;

	ret = qxl_bo_kmap(cursor_bo, (void **)&cursor);
	if (ret)
		goto out_backoff;

	cursor->header.unique = 0;
	cursor->header.type = SPICE_CURSOR_TYPE_ALPHA;
	cursor->header.width = 64;
	cursor->header.height = 64;
	cursor->header.hot_spot_x = hot_x;
	cursor->header.hot_spot_y = hot_y;
	cursor->data_size = size;
	cursor->chunk.next_chunk = 0;
	cursor->chunk.prev_chunk = 0;
	cursor->chunk.data_size = size;

	memcpy(cursor->chunk.data, user_ptr, size);

	qxl_bo_kunmap(cursor_bo);

	qxl_bo_kunmap(user_bo);

	cmd = (struct qxl_cursor_cmd *)qxl_release_map(qdev, release);
	cmd->type = QXL_CURSOR_SET;
	cmd->u.set.position.x = qcrtc->cur_x;
	cmd->u.set.position.y = qcrtc->cur_y;

	cmd->u.set.shape = qxl_bo_physical_address(qdev, cursor_bo, 0);

	cmd->u.set.visible = 1;
	qxl_release_unmap(qdev, release, &cmd->release_info);

	qxl_push_cursor_ring_release(qdev, release, QXL_CMD_CURSOR, false);
	qxl_release_fence_buffer_objects(release);

	/* finish with the userspace bo */
	ret = qxl_bo_reserve(user_bo, false);
	if (!ret) {
		qxl_bo_unpin(user_bo);
		qxl_bo_unreserve(user_bo);
	}
	drm_gem_object_unreference_unlocked(obj);

	qxl_bo_unref(&cursor_bo);

	return ret;

out_backoff:
	qxl_release_backoff_reserve_list(release);
out_free_bo:
	qxl_bo_unref(&cursor_bo);
out_free_release:
	qxl_release_free(qdev, release);
out_kunmap:
	qxl_bo_kunmap(user_bo);
out_unpin:
	qxl_bo_unpin(user_bo);
out_unref:
	drm_gem_object_unreference_unlocked(obj);
	return ret;
}

static int qxl_crtc_cursor_move(struct drm_crtc *crtc,
				int x, int y)
{
	struct drm_device *dev = crtc->dev;
	struct qxl_device *qdev = dev->dev_private;
	struct qxl_crtc *qcrtc = to_qxl_crtc(crtc);
	struct qxl_release *release;
	struct qxl_cursor_cmd *cmd;
	int ret;

	ret = qxl_alloc_release_reserved(qdev, sizeof(*cmd), QXL_RELEASE_CURSOR_CMD,
				   &release, NULL);
	if (ret)
		return ret;

	ret = qxl_release_reserve_list(release, true);
	if (ret) {
		qxl_release_free(qdev, release);
		return ret;
	}

	qcrtc->cur_x = x;
	qcrtc->cur_y = y;

	cmd = (struct qxl_cursor_cmd *)qxl_release_map(qdev, release);
	cmd->type = QXL_CURSOR_MOVE;
	cmd->u.position.x = qcrtc->cur_x;
	cmd->u.position.y = qcrtc->cur_y;
	qxl_release_unmap(qdev, release, &cmd->release_info);

	qxl_push_cursor_ring_release(qdev, release, QXL_CMD_CURSOR, false);
	qxl_release_fence_buffer_objects(release);

	return 0;
}


static const struct drm_crtc_funcs qxl_crtc_funcs = {
	.cursor_set2 = qxl_crtc_cursor_set2,
	.cursor_move = qxl_crtc_cursor_move,
	.set_config = drm_crtc_helper_set_config,
	.destroy = qxl_crtc_destroy,
	.page_flip = qxl_crtc_page_flip,
};

static void qxl_user_framebuffer_destroy(struct drm_framebuffer *fb)
{
	struct qxl_framebuffer *qxl_fb = to_qxl_framebuffer(fb);

	if (qxl_fb->obj)
		drm_gem_object_unreference_unlocked(qxl_fb->obj);
	drm_framebuffer_cleanup(fb);
	kfree(qxl_fb);
}

static int qxl_framebuffer_surface_dirty(struct drm_framebuffer *fb,
					 struct drm_file *file_priv,
					 unsigned flags, unsigned color,
					 struct drm_clip_rect *clips,
					 unsigned num_clips)
{
	/* TODO: vmwgfx where this was cribbed from had locking. Why? */
	struct qxl_framebuffer *qxl_fb = to_qxl_framebuffer(fb);
	struct qxl_device *qdev = qxl_fb->base.dev->dev_private;
	struct drm_clip_rect norect;
	struct qxl_bo *qobj;
	int inc = 1;

	drm_modeset_lock_all(fb->dev);

	qobj = gem_to_qxl_bo(qxl_fb->obj);
	/* if we aren't primary surface ignore this */
	if (!qobj->is_primary) {
		drm_modeset_unlock_all(fb->dev);
		return 0;
	}

	if (!num_clips) {
		num_clips = 1;
		clips = &norect;
		norect.x1 = norect.y1 = 0;
		norect.x2 = fb->width;
		norect.y2 = fb->height;
	} else if (flags & DRM_MODE_FB_DIRTY_ANNOTATE_COPY) {
		num_clips /= 2;
		inc = 2; /* skip source rects */
	}

	qxl_draw_dirty_fb(qdev, qxl_fb, qobj, flags, color,
			  clips, num_clips, inc);

	drm_modeset_unlock_all(fb->dev);

	return 0;
}

static const struct drm_framebuffer_funcs qxl_fb_funcs = {
	.destroy = qxl_user_framebuffer_destroy,
	.dirty = qxl_framebuffer_surface_dirty,
/*	TODO?
 *	.create_handle = qxl_user_framebuffer_create_handle, */
};

int
qxl_framebuffer_init(struct drm_device *dev,
		     struct qxl_framebuffer *qfb,
		     const struct drm_mode_fb_cmd2 *mode_cmd,
		     struct drm_gem_object *obj)
{
	int ret;

	qfb->obj = obj;
	ret = drm_framebuffer_init(dev, &qfb->base, &qxl_fb_funcs);
	if (ret) {
		qfb->obj = NULL;
		return ret;
	}
	drm_helper_mode_fill_fb_struct(&qfb->base, mode_cmd);
	return 0;
}

static void qxl_crtc_dpms(struct drm_crtc *crtc, int mode)
{
}

static bool qxl_crtc_mode_fixup(struct drm_crtc *crtc,
				  const struct drm_display_mode *mode,
				  struct drm_display_mode *adjusted_mode)
{
	struct drm_device *dev = crtc->dev;
	struct qxl_device *qdev = dev->dev_private;

	qxl_io_log(qdev, "%s: (%d,%d) => (%d,%d)\n",
		   __func__,
		   mode->hdisplay, mode->vdisplay,
		   adjusted_mode->hdisplay,
		   adjusted_mode->vdisplay);
	return true;
}

void
qxl_send_monitors_config(struct qxl_device *qdev)
{
	int i;

	BUG_ON(!qdev->ram_header->monitors_config);

	if (qdev->monitors_config->count == 0) {
		qxl_io_log(qdev, "%s: 0 monitors??\n", __func__);
		return;
	}
	for (i = 0 ; i < qdev->monitors_config->count ; ++i) {
		struct qxl_head *head = &qdev->monitors_config->heads[i];

		if (head->y > 8192 || head->x > 8192 ||
		    head->width > 8192 || head->height > 8192) {
			DRM_ERROR("head %d wrong: %dx%d+%d+%d\n",
				  i, head->width, head->height,
				  head->x, head->y);
			return;
		}
	}
	qxl_io_monitors_config(qdev);
}

static void qxl_monitors_config_set(struct qxl_device *qdev,
				    int index,
				    unsigned x, unsigned y,
				    unsigned width, unsigned height,
				    unsigned surf_id)
{
	DRM_DEBUG_KMS("%d:%dx%d+%d+%d\n", index, width, height, x, y);
	qdev->monitors_config->heads[index].x = x;
	qdev->monitors_config->heads[index].y = y;
	qdev->monitors_config->heads[index].width = width;
	qdev->monitors_config->heads[index].height = height;
	qdev->monitors_config->heads[index].surface_id = surf_id;

}

static int qxl_crtc_mode_set(struct drm_crtc *crtc,
			       struct drm_display_mode *mode,
			       struct drm_display_mode *adjusted_mode,
			       int x, int y,
			       struct drm_framebuffer *old_fb)
{
	struct drm_device *dev = crtc->dev;
	struct qxl_device *qdev = dev->dev_private;
	struct qxl_framebuffer *qfb;
	struct qxl_bo *bo, *old_bo = NULL;
	struct qxl_crtc *qcrtc = to_qxl_crtc(crtc);
	bool recreate_primary = false;
	int ret;
	int surf_id;
	if (!crtc->primary->fb) {
		DRM_DEBUG_KMS("No FB bound\n");
		return 0;
	}

	if (old_fb) {
		qfb = to_qxl_framebuffer(old_fb);
		old_bo = gem_to_qxl_bo(qfb->obj);
	}
	qfb = to_qxl_framebuffer(crtc->primary->fb);
	bo = gem_to_qxl_bo(qfb->obj);
	DRM_DEBUG("+%d+%d (%d,%d) => (%d,%d)\n",
		  x, y,
		  mode->hdisplay, mode->vdisplay,
		  adjusted_mode->hdisplay,
		  adjusted_mode->vdisplay);

	if (bo->is_primary == false)
		recreate_primary = true;

	if (bo->surf.stride * bo->surf.height > qdev->vram_size) {
		DRM_ERROR("Mode doesn't fit in vram size (vgamem)");
		return -EINVAL;
        }

	ret = qxl_bo_reserve(bo, false);
	if (ret != 0)
		return ret;
	ret = qxl_bo_pin(bo, bo->type, NULL);
	if (ret != 0) {
		qxl_bo_unreserve(bo);
		return -EINVAL;
	}
	qxl_bo_unreserve(bo);
	if (recreate_primary) {
		qxl_io_destroy_primary(qdev);
		qxl_io_log(qdev,
			   "recreate primary: %dx%d,%d,%d\n",
			   bo->surf.width, bo->surf.height,
			   bo->surf.stride, bo->surf.format);
		qxl_io_create_primary(qdev, 0, bo);
		bo->is_primary = true;
	}

	if (bo->is_primary) {
		DRM_DEBUG_KMS("setting surface_id to 0 for primary surface %d on crtc %d\n", bo->surface_id, qcrtc->index);
		surf_id = 0;
	} else {
		surf_id = bo->surface_id;
	}

	if (old_bo && old_bo != bo) {
		old_bo->is_primary = false;
		ret = qxl_bo_reserve(old_bo, false);
		qxl_bo_unpin(old_bo);
		qxl_bo_unreserve(old_bo);
	}

	qxl_monitors_config_set(qdev, qcrtc->index, x, y,
				mode->hdisplay,
				mode->vdisplay, surf_id);
	return 0;
}

static void qxl_crtc_prepare(struct drm_crtc *crtc)
{
	DRM_DEBUG("current: %dx%d+%d+%d (%d).\n",
		  crtc->mode.hdisplay, crtc->mode.vdisplay,
		  crtc->x, crtc->y, crtc->enabled);
}

static void qxl_crtc_commit(struct drm_crtc *crtc)
{
	DRM_DEBUG("\n");
}

static void qxl_crtc_disable(struct drm_crtc *crtc)
{
	struct qxl_crtc *qcrtc = to_qxl_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct qxl_device *qdev = dev->dev_private;
	if (crtc->primary->fb) {
		struct qxl_framebuffer *qfb = to_qxl_framebuffer(crtc->primary->fb);
		struct qxl_bo *bo = gem_to_qxl_bo(qfb->obj);
		int ret;
		ret = qxl_bo_reserve(bo, false);
		qxl_bo_unpin(bo);
		qxl_bo_unreserve(bo);
		crtc->primary->fb = NULL;
	}

	qxl_monitors_config_set(qdev, qcrtc->index, 0, 0, 0, 0, 0);

	qxl_send_monitors_config(qdev);
}

static const struct drm_crtc_helper_funcs qxl_crtc_helper_funcs = {
	.dpms = qxl_crtc_dpms,
	.disable = qxl_crtc_disable,
	.mode_fixup = qxl_crtc_mode_fixup,
	.mode_set = qxl_crtc_mode_set,
	.prepare = qxl_crtc_prepare,
	.commit = qxl_crtc_commit,
};

static int qdev_crtc_init(struct drm_device *dev, int crtc_id)
{
	struct qxl_crtc *qxl_crtc;

	qxl_crtc = kzalloc(sizeof(struct qxl_crtc), GFP_KERNEL);
	if (!qxl_crtc)
		return -ENOMEM;

	drm_crtc_init(dev, &qxl_crtc->base, &qxl_crtc_funcs);
	qxl_crtc->index = crtc_id;
	drm_mode_crtc_set_gamma_size(&qxl_crtc->base, 256);
	drm_crtc_helper_add(&qxl_crtc->base, &qxl_crtc_helper_funcs);
	return 0;
}

static void qxl_enc_dpms(struct drm_encoder *encoder, int mode)
{
	DRM_DEBUG("\n");
}

static void qxl_enc_prepare(struct drm_encoder *encoder)
{
	DRM_DEBUG("\n");
}

static void qxl_write_monitors_config_for_encoder(struct qxl_device *qdev,
		struct drm_encoder *encoder)
{
	int i;
	struct qxl_output *output = drm_encoder_to_qxl_output(encoder);
	struct qxl_head *head;
	struct drm_display_mode *mode;

	BUG_ON(!encoder);
	/* TODO: ugly, do better */
	i = output->index;
	if (!qdev->monitors_config ||
	    qdev->monitors_config->max_allowed <= i) {
		DRM_ERROR(
		"head number too large or missing monitors config: %p, %d",
		qdev->monitors_config,
		qdev->monitors_config ?
			qdev->monitors_config->max_allowed : -1);
		return;
	}
	if (!encoder->crtc) {
		DRM_ERROR("missing crtc on encoder %p\n", encoder);
		return;
	}
	if (i != 0)
		DRM_DEBUG("missing for multiple monitors: no head holes\n");
	head = &qdev->monitors_config->heads[i];
	head->id = i;
	if (encoder->crtc->enabled) {
		mode = &encoder->crtc->mode;
		head->width = mode->hdisplay;
		head->height = mode->vdisplay;
		head->x = encoder->crtc->x;
		head->y = encoder->crtc->y;
		if (qdev->monitors_config->count < i + 1)
			qdev->monitors_config->count = i + 1;
	} else {
		head->width = 0;
		head->height = 0;
		head->x = 0;
		head->y = 0;
	}
	DRM_DEBUG_KMS("setting head %d to +%d+%d %dx%d out of %d\n",
		      i, head->x, head->y, head->width, head->height, qdev->monitors_config->count);
	head->flags = 0;
	/* TODO - somewhere else to call this for multiple monitors
	 * (config_commit?) */
	qxl_send_monitors_config(qdev);
}

static void qxl_enc_commit(struct drm_encoder *encoder)
{
	struct qxl_device *qdev = encoder->dev->dev_private;

	qxl_write_monitors_config_for_encoder(qdev, encoder);
	DRM_DEBUG("\n");
}

static void qxl_enc_mode_set(struct drm_encoder *encoder,
				struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode)
{
	DRM_DEBUG("\n");
}

static int qxl_conn_get_modes(struct drm_connector *connector)
{
	int ret = 0;
	struct qxl_device *qdev = connector->dev->dev_private;
	unsigned pwidth = 1024;
	unsigned pheight = 768;

	DRM_DEBUG_KMS("monitors_config=%p\n", qdev->monitors_config);
	/* TODO: what should we do here? only show the configured modes for the
	 * device, or allow the full list, or both? */
	if (qdev->monitors_config && qdev->monitors_config->count) {
		ret = qxl_add_monitors_config_modes(connector, &pwidth, &pheight);
		if (ret < 0)
			return ret;
	}
	ret += qxl_add_common_modes(connector, pwidth, pheight);
	return ret;
}

static int qxl_conn_mode_valid(struct drm_connector *connector,
			       struct drm_display_mode *mode)
{
	struct drm_device *ddev = connector->dev;
	struct qxl_device *qdev = ddev->dev_private;
	int i;

	/* TODO: is this called for user defined modes? (xrandr --add-mode)
	 * TODO: check that the mode fits in the framebuffer */

	if(qdev->monitors_config_width == mode->hdisplay &&
	   qdev->monitors_config_height == mode->vdisplay)
		return MODE_OK;

	for (i = 0; i < ARRAY_SIZE(common_modes); i++) {
		if (common_modes[i].w == mode->hdisplay && common_modes[i].h == mode->vdisplay)
			return MODE_OK;
	}
	return MODE_BAD;
}

static struct drm_encoder *qxl_best_encoder(struct drm_connector *connector)
{
	struct qxl_output *qxl_output =
		drm_connector_to_qxl_output(connector);

	DRM_DEBUG("\n");
	return &qxl_output->enc;
}


static const struct drm_encoder_helper_funcs qxl_enc_helper_funcs = {
	.dpms = qxl_enc_dpms,
	.prepare = qxl_enc_prepare,
	.mode_set = qxl_enc_mode_set,
	.commit = qxl_enc_commit,
};

static const struct drm_connector_helper_funcs qxl_connector_helper_funcs = {
	.get_modes = qxl_conn_get_modes,
	.mode_valid = qxl_conn_mode_valid,
	.best_encoder = qxl_best_encoder,
};

static enum drm_connector_status qxl_conn_detect(
			struct drm_connector *connector,
			bool force)
{
	struct qxl_output *output =
		drm_connector_to_qxl_output(connector);
	struct drm_device *ddev = connector->dev;
	struct qxl_device *qdev = ddev->dev_private;
	bool connected = false;

	/* The first monitor is always connected */
	if (!qdev->client_monitors_config) {
		if (output->index == 0)
			connected = true;
	} else
		connected = qdev->client_monitors_config->count > output->index &&
		     qxl_head_enabled(&qdev->client_monitors_config->heads[output->index]);

	DRM_DEBUG("#%d connected: %d\n", output->index, connected);
	if (!connected)
		qxl_monitors_config_set(qdev, output->index, 0, 0, 0, 0, 0);

	return connected ? connector_status_connected
			 : connector_status_disconnected;
}

static int qxl_conn_set_property(struct drm_connector *connector,
				   struct drm_property *property,
				   uint64_t value)
{
	DRM_DEBUG("\n");
	return 0;
}

static void qxl_conn_destroy(struct drm_connector *connector)
{
	struct qxl_output *qxl_output =
		drm_connector_to_qxl_output(connector);

	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
	kfree(qxl_output);
}

static const struct drm_connector_funcs qxl_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.detect = qxl_conn_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.set_property = qxl_conn_set_property,
	.destroy = qxl_conn_destroy,
};

static void qxl_enc_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
}

static const struct drm_encoder_funcs qxl_enc_funcs = {
	.destroy = qxl_enc_destroy,
};

static int qxl_mode_create_hotplug_mode_update_property(struct qxl_device *qdev)
{
	if (qdev->hotplug_mode_update_property)
		return 0;

	qdev->hotplug_mode_update_property =
		drm_property_create_range(qdev->ddev, DRM_MODE_PROP_IMMUTABLE,
					  "hotplug_mode_update", 0, 1);

	return 0;
}

static int qdev_output_init(struct drm_device *dev, int num_output)
{
	struct qxl_device *qdev = dev->dev_private;
	struct qxl_output *qxl_output;
	struct drm_connector *connector;
	struct drm_encoder *encoder;

	qxl_output = kzalloc(sizeof(struct qxl_output), GFP_KERNEL);
	if (!qxl_output)
		return -ENOMEM;

	qxl_output->index = num_output;

	connector = &qxl_output->base;
	encoder = &qxl_output->enc;
	drm_connector_init(dev, &qxl_output->base,
			   &qxl_connector_funcs, DRM_MODE_CONNECTOR_VIRTUAL);

	drm_encoder_init(dev, &qxl_output->enc, &qxl_enc_funcs,
			 DRM_MODE_ENCODER_VIRTUAL, NULL);

	/* we get HPD via client monitors config */
	connector->polled = DRM_CONNECTOR_POLL_HPD;
	encoder->possible_crtcs = 1 << num_output;
	drm_mode_connector_attach_encoder(&qxl_output->base,
					  &qxl_output->enc);
	drm_encoder_helper_add(encoder, &qxl_enc_helper_funcs);
	drm_connector_helper_add(connector, &qxl_connector_helper_funcs);

	drm_object_attach_property(&connector->base,
				   qdev->hotplug_mode_update_property, 0);
	drm_object_attach_property(&connector->base,
				   dev->mode_config.suggested_x_property, 0);
	drm_object_attach_property(&connector->base,
				   dev->mode_config.suggested_y_property, 0);
	drm_connector_register(connector);
	return 0;
}

static struct drm_framebuffer *
qxl_user_framebuffer_create(struct drm_device *dev,
			    struct drm_file *file_priv,
			    const struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct drm_gem_object *obj;
	struct qxl_framebuffer *qxl_fb;
	int ret;

	obj = drm_gem_object_lookup(dev, file_priv, mode_cmd->handles[0]);

	qxl_fb = kzalloc(sizeof(*qxl_fb), GFP_KERNEL);
	if (qxl_fb == NULL)
		return NULL;

	ret = qxl_framebuffer_init(dev, qxl_fb, mode_cmd, obj);
	if (ret) {
		kfree(qxl_fb);
		drm_gem_object_unreference_unlocked(obj);
		return NULL;
	}

	return &qxl_fb->base;
}

static const struct drm_mode_config_funcs qxl_mode_funcs = {
	.fb_create = qxl_user_framebuffer_create,
};

int qxl_create_monitors_object(struct qxl_device *qdev)
{
	int ret;
	struct drm_gem_object *gobj;
	int max_allowed = qxl_num_crtc;
	int monitors_config_size = sizeof(struct qxl_monitors_config) +
		max_allowed * sizeof(struct qxl_head);

	ret = qxl_gem_object_create(qdev, monitors_config_size, 0,
				    QXL_GEM_DOMAIN_VRAM,
				    false, false, NULL, &gobj);
	if (ret) {
		DRM_ERROR("%s: failed to create gem ret=%d\n", __func__, ret);
		return -ENOMEM;
	}
	qdev->monitors_config_bo = gem_to_qxl_bo(gobj);

	ret = qxl_bo_reserve(qdev->monitors_config_bo, false);
	if (ret)
		return ret;

	ret = qxl_bo_pin(qdev->monitors_config_bo, QXL_GEM_DOMAIN_VRAM, NULL);
	if (ret) {
		qxl_bo_unreserve(qdev->monitors_config_bo);
		return ret;
	}

	qxl_bo_unreserve(qdev->monitors_config_bo);

	qxl_bo_kmap(qdev->monitors_config_bo, NULL);

	qdev->monitors_config = qdev->monitors_config_bo->kptr;
	qdev->ram_header->monitors_config =
		qxl_bo_physical_address(qdev, qdev->monitors_config_bo, 0);

	memset(qdev->monitors_config, 0, monitors_config_size);
	qdev->monitors_config->max_allowed = max_allowed;
	return 0;
}

int qxl_destroy_monitors_object(struct qxl_device *qdev)
{
	int ret;

	qdev->monitors_config = NULL;
	qdev->ram_header->monitors_config = 0;

	qxl_bo_kunmap(qdev->monitors_config_bo);
	ret = qxl_bo_reserve(qdev->monitors_config_bo, false);
	if (ret)
		return ret;

	qxl_bo_unpin(qdev->monitors_config_bo);
	qxl_bo_unreserve(qdev->monitors_config_bo);

	qxl_bo_unref(&qdev->monitors_config_bo);
	return 0;
}

int qxl_modeset_init(struct qxl_device *qdev)
{
	int i;
	int ret;

	drm_mode_config_init(qdev->ddev);

	ret = qxl_create_monitors_object(qdev);
	if (ret)
		return ret;

	qdev->ddev->mode_config.funcs = (void *)&qxl_mode_funcs;

	/* modes will be validated against the framebuffer size */
	qdev->ddev->mode_config.min_width = 320;
	qdev->ddev->mode_config.min_height = 200;
	qdev->ddev->mode_config.max_width = 8192;
	qdev->ddev->mode_config.max_height = 8192;

	qdev->ddev->mode_config.fb_base = qdev->vram_base;

	drm_mode_create_suggested_offset_properties(qdev->ddev);
	qxl_mode_create_hotplug_mode_update_property(qdev);

	for (i = 0 ; i < qxl_num_crtc; ++i) {
		qdev_crtc_init(qdev->ddev, i);
		qdev_output_init(qdev->ddev, i);
	}

	qdev->mode_info.mode_config_initialized = true;

	/* primary surface must be created by this point, to allow
	 * issuing command queue commands and having them read by
	 * spice server. */
	qxl_fbdev_init(qdev);
	return 0;
}

void qxl_modeset_fini(struct qxl_device *qdev)
{
	qxl_fbdev_fini(qdev);

	qxl_destroy_monitors_object(qdev);
	if (qdev->mode_info.mode_config_initialized) {
		drm_mode_config_cleanup(qdev->ddev);
		qdev->mode_info.mode_config_initialized = false;
	}
}
