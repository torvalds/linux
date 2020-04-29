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
#include <drm/drm_crtc_helper.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_atomic.h>

#include "qxl_drv.h"
#include "qxl_object.h"

static bool qxl_head_enabled(struct qxl_head *head)
{
	return head->width && head->height;
}

static void qxl_alloc_client_monitors_config(struct qxl_device *qdev, unsigned count)
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
		if (!qdev->client_monitors_config)
			return;
	}
	qdev->client_monitors_config->count = count;
}

enum {
	MONITORS_CONFIG_MODIFIED,
	MONITORS_CONFIG_UNCHANGED,
	MONITORS_CONFIG_BAD_CRC,
};

static int qxl_display_copy_rom_client_monitors_config(struct qxl_device *qdev)
{
	int i;
	int num_monitors;
	uint32_t crc;
	int status = MONITORS_CONFIG_UNCHANGED;

	num_monitors = qdev->rom->client_monitors_config.count;
	crc = crc32(0, (const uint8_t *)&qdev->rom->client_monitors_config,
		  sizeof(qdev->rom->client_monitors_config));
	if (crc != qdev->rom->client_monitors_config_crc)
		return MONITORS_CONFIG_BAD_CRC;
	if (!num_monitors) {
		DRM_DEBUG_KMS("no client monitors configured\n");
		return status;
	}
	if (num_monitors > qdev->monitors_config->max_allowed) {
		DRM_DEBUG_KMS("client monitors list will be truncated: %d < %d\n",
			      qdev->monitors_config->max_allowed, num_monitors);
		num_monitors = qdev->monitors_config->max_allowed;
	} else {
		num_monitors = qdev->rom->client_monitors_config.count;
	}
	if (qdev->client_monitors_config
	      && (num_monitors != qdev->client_monitors_config->count)) {
		status = MONITORS_CONFIG_MODIFIED;
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
		if (client_head->x != c_rect->left) {
			client_head->x = c_rect->left;
			status = MONITORS_CONFIG_MODIFIED;
		}
		if (client_head->y != c_rect->top) {
			client_head->y = c_rect->top;
			status = MONITORS_CONFIG_MODIFIED;
		}
		if (client_head->width != c_rect->right - c_rect->left) {
			client_head->width = c_rect->right - c_rect->left;
			status = MONITORS_CONFIG_MODIFIED;
		}
		if (client_head->height != c_rect->bottom - c_rect->top) {
			client_head->height = c_rect->bottom - c_rect->top;
			status = MONITORS_CONFIG_MODIFIED;
		}
		if (client_head->surface_id != 0) {
			client_head->surface_id = 0;
			status = MONITORS_CONFIG_MODIFIED;
		}
		if (client_head->id != i) {
			client_head->id = i;
			status = MONITORS_CONFIG_MODIFIED;
		}
		if (client_head->flags != 0) {
			client_head->flags = 0;
			status = MONITORS_CONFIG_MODIFIED;
		}
		DRM_DEBUG_KMS("read %dx%d+%d+%d\n", client_head->width, client_head->height,
			  client_head->x, client_head->y);
	}

	return status;
}

static void qxl_update_offset_props(struct qxl_device *qdev)
{
	struct drm_device *dev = &qdev->ddev;
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
	struct drm_device *dev = &qdev->ddev;
	int status, retries;

	for (retries = 0; retries < 10; retries++) {
		status = qxl_display_copy_rom_client_monitors_config(qdev);
		if (status != MONITORS_CONFIG_BAD_CRC)
			break;
		udelay(5);
	}
	if (status == MONITORS_CONFIG_BAD_CRC) {
		DRM_DEBUG_KMS("ignoring client monitors config: bad crc");
		return;
	}
	if (status == MONITORS_CONFIG_UNCHANGED) {
		DRM_DEBUG_KMS("ignoring client monitors config: unchanged");
		return;
	}

	drm_modeset_lock_all(dev);
	qxl_update_offset_props(qdev);
	drm_modeset_unlock_all(dev);
	if (!drm_helper_hpd_irq_event(dev)) {
		/* notify that the monitor configuration changed, to
		   adjust at the arbitrary resolution */
		drm_kms_helper_hotplug_event(dev);
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

	if (!qdev->monitors_config)
		return 0;
	if (h >= qdev->monitors_config->max_allowed)
		return 0;
	if (!qdev->client_monitors_config)
		return 0;
	if (h >= qdev->client_monitors_config->count)
		return 0;

	head = &qdev->client_monitors_config->heads[h];
	DRM_DEBUG_KMS("head %d is %dx%d\n", h, head->width, head->height);

	mode = drm_cvt_mode(dev, head->width, head->height, 60, false, false,
			    false);
	mode->type |= DRM_MODE_TYPE_PREFERRED;
	mode->hdisplay = head->width;
	mode->vdisplay = head->height;
	drm_mode_set_name(mode);
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

static void qxl_send_monitors_config(struct qxl_device *qdev)
{
	int i;

	BUG_ON(!qdev->ram_header->monitors_config);

	if (qdev->monitors_config->count == 0)
		return;

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

static void qxl_crtc_update_monitors_config(struct drm_crtc *crtc,
					    const char *reason)
{
	struct drm_device *dev = crtc->dev;
	struct qxl_device *qdev = dev->dev_private;
	struct qxl_crtc *qcrtc = to_qxl_crtc(crtc);
	struct qxl_head head;
	int oldcount, i = qcrtc->index;

	if (!qdev->primary_created) {
		DRM_DEBUG_KMS("no primary surface, skip (%s)\n", reason);
		return;
	}

	if (!qdev->monitors_config ||
	    qdev->monitors_config->max_allowed <= i)
		return;

	head.id = i;
	head.flags = 0;
	oldcount = qdev->monitors_config->count;
	if (crtc->state->active) {
		struct drm_display_mode *mode = &crtc->mode;
		head.width = mode->hdisplay;
		head.height = mode->vdisplay;
		head.x = crtc->x;
		head.y = crtc->y;
		if (qdev->monitors_config->count < i + 1)
			qdev->monitors_config->count = i + 1;
	} else if (i > 0) {
		head.width = 0;
		head.height = 0;
		head.x = 0;
		head.y = 0;
		if (qdev->monitors_config->count == i + 1)
			qdev->monitors_config->count = i;
	} else {
		DRM_DEBUG_KMS("inactive head 0, skip (%s)\n", reason);
		return;
	}

	if (head.width  == qdev->monitors_config->heads[i].width  &&
	    head.height == qdev->monitors_config->heads[i].height &&
	    head.x      == qdev->monitors_config->heads[i].x      &&
	    head.y      == qdev->monitors_config->heads[i].y      &&
	    oldcount    == qdev->monitors_config->count)
		return;

	DRM_DEBUG_KMS("head %d, %dx%d, at +%d+%d, %s (%s)\n",
		      i, head.width, head.height, head.x, head.y,
		      crtc->state->active ? "on" : "off", reason);
	if (oldcount != qdev->monitors_config->count)
		DRM_DEBUG_KMS("active heads %d -> %d (%d total)\n",
			      oldcount, qdev->monitors_config->count,
			      qdev->monitors_config->max_allowed);

	qdev->monitors_config->heads[i] = head;
	qxl_send_monitors_config(qdev);
}

static void qxl_crtc_atomic_flush(struct drm_crtc *crtc,
				  struct drm_crtc_state *old_crtc_state)
{
	struct drm_device *dev = crtc->dev;
	struct drm_pending_vblank_event *event;
	unsigned long flags;

	if (crtc->state && crtc->state->event) {
		event = crtc->state->event;
		crtc->state->event = NULL;

		spin_lock_irqsave(&dev->event_lock, flags);
		drm_crtc_send_vblank_event(crtc, event);
		spin_unlock_irqrestore(&dev->event_lock, flags);
	}

	qxl_crtc_update_monitors_config(crtc, "flush");
}

static void qxl_crtc_destroy(struct drm_crtc *crtc)
{
	struct qxl_crtc *qxl_crtc = to_qxl_crtc(crtc);

	qxl_bo_unref(&qxl_crtc->cursor_bo);
	drm_crtc_cleanup(crtc);
	kfree(qxl_crtc);
}

static const struct drm_crtc_funcs qxl_crtc_funcs = {
	.set_config = drm_atomic_helper_set_config,
	.destroy = qxl_crtc_destroy,
	.page_flip = drm_atomic_helper_page_flip,
	.reset = drm_atomic_helper_crtc_reset,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
};

void qxl_user_framebuffer_destroy(struct drm_framebuffer *fb)
{
	struct qxl_framebuffer *qxl_fb = to_qxl_framebuffer(fb);
	struct qxl_bo *bo = gem_to_qxl_bo(qxl_fb->obj);

	WARN_ON(bo->shadow);
	drm_gem_object_put_unlocked(qxl_fb->obj);
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
		     struct drm_gem_object *obj,
		     const struct drm_framebuffer_funcs *funcs)
{
	int ret;

	qfb->obj = obj;
	drm_helper_mode_fill_fb_struct(dev, &qfb->base, mode_cmd);
	ret = drm_framebuffer_init(dev, &qfb->base, funcs);
	if (ret) {
		qfb->obj = NULL;
		return ret;
	}
	return 0;
}

static void qxl_crtc_atomic_enable(struct drm_crtc *crtc,
				   struct drm_crtc_state *old_state)
{
	qxl_crtc_update_monitors_config(crtc, "enable");
}

static void qxl_crtc_atomic_disable(struct drm_crtc *crtc,
				    struct drm_crtc_state *old_state)
{
	qxl_crtc_update_monitors_config(crtc, "disable");
}

static const struct drm_crtc_helper_funcs qxl_crtc_helper_funcs = {
	.atomic_flush = qxl_crtc_atomic_flush,
	.atomic_enable = qxl_crtc_atomic_enable,
	.atomic_disable = qxl_crtc_atomic_disable,
};

static int qxl_primary_atomic_check(struct drm_plane *plane,
				    struct drm_plane_state *state)
{
	struct qxl_device *qdev = plane->dev->dev_private;
	struct qxl_framebuffer *qfb;
	struct qxl_bo *bo;

	if (!state->crtc || !state->fb)
		return 0;

	qfb = to_qxl_framebuffer(state->fb);
	bo = gem_to_qxl_bo(qfb->obj);

	if (bo->surf.stride * bo->surf.height > qdev->vram_size) {
		DRM_ERROR("Mode doesn't fit in vram size (vgamem)");
		return -EINVAL;
	}

	return 0;
}

static int qxl_primary_apply_cursor(struct drm_plane *plane)
{
	struct drm_device *dev = plane->dev;
	struct qxl_device *qdev = dev->dev_private;
	struct drm_framebuffer *fb = plane->state->fb;
	struct qxl_crtc *qcrtc = to_qxl_crtc(plane->state->crtc);
	struct qxl_cursor_cmd *cmd;
	struct qxl_release *release;
	int ret = 0;

	if (!qcrtc->cursor_bo)
		return 0;

	ret = qxl_alloc_release_reserved(qdev, sizeof(*cmd),
					 QXL_RELEASE_CURSOR_CMD,
					 &release, NULL);
	if (ret)
		return ret;

	ret = qxl_release_list_add(release, qcrtc->cursor_bo);
	if (ret)
		goto out_free_release;

	ret = qxl_release_reserve_list(release, false);
	if (ret)
		goto out_free_release;

	cmd = (struct qxl_cursor_cmd *)qxl_release_map(qdev, release);
	cmd->type = QXL_CURSOR_SET;
	cmd->u.set.position.x = plane->state->crtc_x + fb->hot_x;
	cmd->u.set.position.y = plane->state->crtc_y + fb->hot_y;

	cmd->u.set.shape = qxl_bo_physical_address(qdev, qcrtc->cursor_bo, 0);

	cmd->u.set.visible = 1;
	qxl_release_unmap(qdev, release, &cmd->release_info);

	qxl_release_fence_buffer_objects(release);
	qxl_push_cursor_ring_release(qdev, release, QXL_CMD_CURSOR, false);

	return ret;

out_free_release:
	qxl_release_free(qdev, release);
	return ret;
}

static void qxl_primary_atomic_update(struct drm_plane *plane,
				      struct drm_plane_state *old_state)
{
	struct qxl_device *qdev = plane->dev->dev_private;
	struct qxl_framebuffer *qfb =
		to_qxl_framebuffer(plane->state->fb);
	struct qxl_framebuffer *qfb_old;
	struct qxl_bo *bo = gem_to_qxl_bo(qfb->obj);
	struct qxl_bo *bo_old;
	struct drm_clip_rect norect = {
	    .x1 = 0,
	    .y1 = 0,
	    .x2 = qfb->base.width,
	    .y2 = qfb->base.height
	};
	int ret;
	bool same_shadow = false;

	if (old_state->fb) {
		qfb_old = to_qxl_framebuffer(old_state->fb);
		bo_old = gem_to_qxl_bo(qfb_old->obj);
	} else {
		bo_old = NULL;
	}

	if (bo == bo_old)
		return;

	if (bo_old && bo_old->shadow && bo->shadow &&
	    bo_old->shadow == bo->shadow) {
		same_shadow = true;
	}

	if (bo_old && bo_old->is_primary) {
		if (!same_shadow)
			qxl_io_destroy_primary(qdev);
		bo_old->is_primary = false;

		ret = qxl_primary_apply_cursor(plane);
		if (ret)
			DRM_ERROR(
			"could not set cursor after creating primary");
	}

	if (!bo->is_primary) {
		if (!same_shadow)
			qxl_io_create_primary(qdev, 0, bo);
		bo->is_primary = true;
	}

	qxl_draw_dirty_fb(qdev, qfb, bo, 0, 0, &norect, 1, 1);
}

static void qxl_primary_atomic_disable(struct drm_plane *plane,
				       struct drm_plane_state *old_state)
{
	struct qxl_device *qdev = plane->dev->dev_private;

	if (old_state->fb) {
		struct qxl_framebuffer *qfb =
			to_qxl_framebuffer(old_state->fb);
		struct qxl_bo *bo = gem_to_qxl_bo(qfb->obj);

		if (bo->is_primary) {
			qxl_io_destroy_primary(qdev);
			bo->is_primary = false;
		}
	}
}

static void qxl_cursor_atomic_update(struct drm_plane *plane,
				     struct drm_plane_state *old_state)
{
	struct drm_device *dev = plane->dev;
	struct qxl_device *qdev = dev->dev_private;
	struct drm_framebuffer *fb = plane->state->fb;
	struct qxl_crtc *qcrtc = to_qxl_crtc(plane->state->crtc);
	struct qxl_release *release;
	struct qxl_cursor_cmd *cmd;
	struct qxl_cursor *cursor;
	struct drm_gem_object *obj;
	struct qxl_bo *cursor_bo = NULL, *user_bo = NULL, *old_cursor_bo = NULL;
	int ret;
	void *user_ptr;
	int size = 64*64*4;

	ret = qxl_alloc_release_reserved(qdev, sizeof(*cmd),
					 QXL_RELEASE_CURSOR_CMD,
					 &release, NULL);
	if (ret)
		return;

	if (fb != old_state->fb) {
		obj = to_qxl_framebuffer(fb)->obj;
		user_bo = gem_to_qxl_bo(obj);

		/* pinning is done in the prepare/cleanup framevbuffer */
		ret = qxl_bo_kmap(user_bo, &user_ptr);
		if (ret)
			goto out_free_release;

		ret = qxl_alloc_bo_reserved(qdev, release,
					    sizeof(struct qxl_cursor) + size,
					    &cursor_bo);
		if (ret)
			goto out_kunmap;

		ret = qxl_release_reserve_list(release, true);
		if (ret)
			goto out_free_bo;

		ret = qxl_bo_kmap(cursor_bo, (void **)&cursor);
		if (ret)
			goto out_backoff;

		cursor->header.unique = 0;
		cursor->header.type = SPICE_CURSOR_TYPE_ALPHA;
		cursor->header.width = 64;
		cursor->header.height = 64;
		cursor->header.hot_spot_x = fb->hot_x;
		cursor->header.hot_spot_y = fb->hot_y;
		cursor->data_size = size;
		cursor->chunk.next_chunk = 0;
		cursor->chunk.prev_chunk = 0;
		cursor->chunk.data_size = size;
		memcpy(cursor->chunk.data, user_ptr, size);
		qxl_bo_kunmap(cursor_bo);
		qxl_bo_kunmap(user_bo);

		cmd = (struct qxl_cursor_cmd *) qxl_release_map(qdev, release);
		cmd->u.set.visible = 1;
		cmd->u.set.shape = qxl_bo_physical_address(qdev,
							   cursor_bo, 0);
		cmd->type = QXL_CURSOR_SET;

		old_cursor_bo = qcrtc->cursor_bo;
		qcrtc->cursor_bo = cursor_bo;
		cursor_bo = NULL;
	} else {

		ret = qxl_release_reserve_list(release, true);
		if (ret)
			goto out_free_release;

		cmd = (struct qxl_cursor_cmd *) qxl_release_map(qdev, release);
		cmd->type = QXL_CURSOR_MOVE;
	}

	cmd->u.position.x = plane->state->crtc_x + fb->hot_x;
	cmd->u.position.y = plane->state->crtc_y + fb->hot_y;

	qxl_release_unmap(qdev, release, &cmd->release_info);
	qxl_release_fence_buffer_objects(release);
	qxl_push_cursor_ring_release(qdev, release, QXL_CMD_CURSOR, false);

	if (old_cursor_bo)
		qxl_bo_unref(&old_cursor_bo);

	qxl_bo_unref(&cursor_bo);

	return;

out_backoff:
	qxl_release_backoff_reserve_list(release);
out_free_bo:
	qxl_bo_unref(&cursor_bo);
out_kunmap:
	qxl_bo_kunmap(user_bo);
out_free_release:
	qxl_release_free(qdev, release);
	return;

}

static void qxl_cursor_atomic_disable(struct drm_plane *plane,
				      struct drm_plane_state *old_state)
{
	struct qxl_device *qdev = plane->dev->dev_private;
	struct qxl_release *release;
	struct qxl_cursor_cmd *cmd;
	int ret;

	ret = qxl_alloc_release_reserved(qdev, sizeof(*cmd),
					 QXL_RELEASE_CURSOR_CMD,
					 &release, NULL);
	if (ret)
		return;

	ret = qxl_release_reserve_list(release, true);
	if (ret) {
		qxl_release_free(qdev, release);
		return;
	}

	cmd = (struct qxl_cursor_cmd *)qxl_release_map(qdev, release);
	cmd->type = QXL_CURSOR_HIDE;
	qxl_release_unmap(qdev, release, &cmd->release_info);

	qxl_release_fence_buffer_objects(release);
	qxl_push_cursor_ring_release(qdev, release, QXL_CMD_CURSOR, false);
}

static int qxl_plane_prepare_fb(struct drm_plane *plane,
				struct drm_plane_state *new_state)
{
	struct qxl_device *qdev = plane->dev->dev_private;
	struct drm_gem_object *obj;
	struct qxl_bo *user_bo, *old_bo = NULL;
	int ret;

	if (!new_state->fb)
		return 0;

	obj = to_qxl_framebuffer(new_state->fb)->obj;
	user_bo = gem_to_qxl_bo(obj);

	if (plane->type == DRM_PLANE_TYPE_PRIMARY &&
	    user_bo->is_dumb && !user_bo->shadow) {
		if (plane->state->fb) {
			obj = to_qxl_framebuffer(plane->state->fb)->obj;
			old_bo = gem_to_qxl_bo(obj);
		}
		if (old_bo && old_bo->shadow &&
		    user_bo->gem_base.size == old_bo->gem_base.size &&
		    plane->state->crtc     == new_state->crtc &&
		    plane->state->crtc_w   == new_state->crtc_w &&
		    plane->state->crtc_h   == new_state->crtc_h &&
		    plane->state->src_x    == new_state->src_x &&
		    plane->state->src_y    == new_state->src_y &&
		    plane->state->src_w    == new_state->src_w &&
		    plane->state->src_h    == new_state->src_h &&
		    plane->state->rotation == new_state->rotation &&
		    plane->state->zpos     == new_state->zpos) {
			drm_gem_object_get(&old_bo->shadow->gem_base);
			user_bo->shadow = old_bo->shadow;
		} else {
			qxl_bo_create(qdev, user_bo->gem_base.size,
				      true, true, QXL_GEM_DOMAIN_VRAM, NULL,
				      &user_bo->shadow);
		}
	}

	ret = qxl_bo_pin(user_bo, QXL_GEM_DOMAIN_CPU, NULL);
	if (ret)
		return ret;

	return 0;
}

static void qxl_plane_cleanup_fb(struct drm_plane *plane,
				 struct drm_plane_state *old_state)
{
	struct drm_gem_object *obj;
	struct qxl_bo *user_bo;

	if (!old_state->fb) {
		/*
		 * we never executed prepare_fb, so there's nothing to
		 * unpin.
		 */
		return;
	}

	obj = to_qxl_framebuffer(old_state->fb)->obj;
	user_bo = gem_to_qxl_bo(obj);
	qxl_bo_unpin(user_bo);

	if (user_bo->shadow && !user_bo->is_primary) {
		drm_gem_object_put_unlocked(&user_bo->shadow->gem_base);
		user_bo->shadow = NULL;
	}
}

static const uint32_t qxl_cursor_plane_formats[] = {
	DRM_FORMAT_ARGB8888,
};

static const struct drm_plane_helper_funcs qxl_cursor_helper_funcs = {
	.atomic_update = qxl_cursor_atomic_update,
	.atomic_disable = qxl_cursor_atomic_disable,
	.prepare_fb = qxl_plane_prepare_fb,
	.cleanup_fb = qxl_plane_cleanup_fb,
};

static const struct drm_plane_funcs qxl_cursor_plane_funcs = {
	.update_plane	= drm_atomic_helper_update_plane,
	.disable_plane	= drm_atomic_helper_disable_plane,
	.destroy	= drm_primary_helper_destroy,
	.reset		= drm_atomic_helper_plane_reset,
	.atomic_duplicate_state = drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_plane_destroy_state,
};

static const uint32_t qxl_primary_plane_formats[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
};

static const struct drm_plane_helper_funcs primary_helper_funcs = {
	.atomic_check = qxl_primary_atomic_check,
	.atomic_update = qxl_primary_atomic_update,
	.atomic_disable = qxl_primary_atomic_disable,
	.prepare_fb = qxl_plane_prepare_fb,
	.cleanup_fb = qxl_plane_cleanup_fb,
};

static const struct drm_plane_funcs qxl_primary_plane_funcs = {
	.update_plane	= drm_atomic_helper_update_plane,
	.disable_plane	= drm_atomic_helper_disable_plane,
	.destroy	= drm_primary_helper_destroy,
	.reset		= drm_atomic_helper_plane_reset,
	.atomic_duplicate_state = drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_plane_destroy_state,
};

static struct drm_plane *qxl_create_plane(struct qxl_device *qdev,
					  unsigned int possible_crtcs,
					  enum drm_plane_type type)
{
	const struct drm_plane_helper_funcs *helper_funcs = NULL;
	struct drm_plane *plane;
	const struct drm_plane_funcs *funcs;
	const uint32_t *formats;
	int num_formats;
	int err;

	if (type == DRM_PLANE_TYPE_PRIMARY) {
		funcs = &qxl_primary_plane_funcs;
		formats = qxl_primary_plane_formats;
		num_formats = ARRAY_SIZE(qxl_primary_plane_formats);
		helper_funcs = &primary_helper_funcs;
	} else if (type == DRM_PLANE_TYPE_CURSOR) {
		funcs = &qxl_cursor_plane_funcs;
		formats = qxl_cursor_plane_formats;
		helper_funcs = &qxl_cursor_helper_funcs;
		num_formats = ARRAY_SIZE(qxl_cursor_plane_formats);
	} else {
		return ERR_PTR(-EINVAL);
	}

	plane = kzalloc(sizeof(*plane), GFP_KERNEL);
	if (!plane)
		return ERR_PTR(-ENOMEM);

	err = drm_universal_plane_init(&qdev->ddev, plane, possible_crtcs,
				       funcs, formats, num_formats,
				       NULL, type, NULL);
	if (err)
		goto free_plane;

	drm_plane_helper_add(plane, helper_funcs);

	return plane;

free_plane:
	kfree(plane);
	return ERR_PTR(-EINVAL);
}

static int qdev_crtc_init(struct drm_device *dev, int crtc_id)
{
	struct qxl_crtc *qxl_crtc;
	struct drm_plane *primary, *cursor;
	struct qxl_device *qdev = dev->dev_private;
	int r;

	qxl_crtc = kzalloc(sizeof(struct qxl_crtc), GFP_KERNEL);
	if (!qxl_crtc)
		return -ENOMEM;

	primary = qxl_create_plane(qdev, 1 << crtc_id, DRM_PLANE_TYPE_PRIMARY);
	if (IS_ERR(primary)) {
		r = -ENOMEM;
		goto free_mem;
	}

	cursor = qxl_create_plane(qdev, 1 << crtc_id, DRM_PLANE_TYPE_CURSOR);
	if (IS_ERR(cursor)) {
		r = -ENOMEM;
		goto clean_primary;
	}

	r = drm_crtc_init_with_planes(dev, &qxl_crtc->base, primary, cursor,
				      &qxl_crtc_funcs, NULL);
	if (r)
		goto clean_cursor;

	qxl_crtc->index = crtc_id;
	drm_crtc_helper_add(&qxl_crtc->base, &qxl_crtc_helper_funcs);
	return 0;

clean_cursor:
	drm_plane_cleanup(cursor);
	kfree(cursor);
clean_primary:
	drm_plane_cleanup(primary);
	kfree(primary);
free_mem:
	kfree(qxl_crtc);
	return r;
}

static int qxl_conn_get_modes(struct drm_connector *connector)
{
	unsigned pwidth = 1024;
	unsigned pheight = 768;
	int ret = 0;

	ret = qxl_add_monitors_config_modes(connector, &pwidth, &pheight);
	if (ret < 0)
		return ret;
	ret += qxl_add_common_modes(connector, pwidth, pheight);
	return ret;
}

static enum drm_mode_status qxl_conn_mode_valid(struct drm_connector *connector,
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

	return connected ? connector_status_connected
			 : connector_status_disconnected;
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
	.destroy = qxl_conn_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
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
		drm_property_create_range(&qdev->ddev, DRM_MODE_PROP_IMMUTABLE,
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
	drm_connector_attach_encoder(&qxl_output->base,
					  &qxl_output->enc);
	drm_encoder_helper_add(encoder, &qxl_enc_helper_funcs);
	drm_connector_helper_add(connector, &qxl_connector_helper_funcs);

	drm_object_attach_property(&connector->base,
				   qdev->hotplug_mode_update_property, 0);
	drm_object_attach_property(&connector->base,
				   dev->mode_config.suggested_x_property, 0);
	drm_object_attach_property(&connector->base,
				   dev->mode_config.suggested_y_property, 0);
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

	obj = drm_gem_object_lookup(file_priv, mode_cmd->handles[0]);
	if (!obj)
		return NULL;

	qxl_fb = kzalloc(sizeof(*qxl_fb), GFP_KERNEL);
	if (qxl_fb == NULL)
		return NULL;

	ret = qxl_framebuffer_init(dev, qxl_fb, mode_cmd, obj, &qxl_fb_funcs);
	if (ret) {
		kfree(qxl_fb);
		drm_gem_object_put_unlocked(obj);
		return NULL;
	}

	return &qxl_fb->base;
}

static const struct drm_mode_config_funcs qxl_mode_funcs = {
	.fb_create = qxl_user_framebuffer_create,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
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

	ret = qxl_bo_pin(qdev->monitors_config_bo, QXL_GEM_DOMAIN_VRAM, NULL);
	if (ret)
		return ret;

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
	ret = qxl_bo_unpin(qdev->monitors_config_bo);
	if (ret)
		return ret;

	qxl_bo_unref(&qdev->monitors_config_bo);
	return 0;
}

int qxl_modeset_init(struct qxl_device *qdev)
{
	int i;
	int ret;

	drm_mode_config_init(&qdev->ddev);

	ret = qxl_create_monitors_object(qdev);
	if (ret)
		return ret;

	qdev->ddev.mode_config.funcs = (void *)&qxl_mode_funcs;

	/* modes will be validated against the framebuffer size */
	qdev->ddev.mode_config.min_width = 0;
	qdev->ddev.mode_config.min_height = 0;
	qdev->ddev.mode_config.max_width = 8192;
	qdev->ddev.mode_config.max_height = 8192;

	qdev->ddev.mode_config.fb_base = qdev->vram_base;

	drm_mode_create_suggested_offset_properties(&qdev->ddev);
	qxl_mode_create_hotplug_mode_update_property(qdev);

	for (i = 0 ; i < qxl_num_crtc; ++i) {
		qdev_crtc_init(&qdev->ddev, i);
		qdev_output_init(&qdev->ddev, i);
	}

	qxl_display_read_client_monitors_config(qdev);
	qdev->mode_info.mode_config_initialized = true;

	drm_mode_config_reset(&qdev->ddev);

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
		drm_mode_config_cleanup(&qdev->ddev);
		qdev->mode_info.mode_config_initialized = false;
	}
}
