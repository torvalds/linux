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
#include <linux/delay.h>
#include <linux/iosys-map.h>

#include <drm/drm_drv.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>
#include <drm/drm_gem_atomic_helper.h>

#include "qxl_drv.h"
#include "qxl_object.h"

static bool qxl_head_enabled(struct qxl_head *head)
{
	return head->width && head->height;
}

static int qxl_alloc_client_monitors_config(struct qxl_device *qdev,
		unsigned int count)
{
	if (qdev->client_monitors_config &&
	    count > qdev->client_monitors_config->count) {
		kfree(qdev->client_monitors_config);
		qdev->client_monitors_config = NULL;
	}
	if (!qdev->client_monitors_config) {
		qdev->client_monitors_config = kzalloc(
				struct_size(qdev->client_monitors_config,
				heads, count), GFP_KERNEL);
		if (!qdev->client_monitors_config)
			return -ENOMEM;
	}
	qdev->client_monitors_config->count = count;
	return 0;
}

enum {
	MONITORS_CONFIG_MODIFIED,
	MONITORS_CONFIG_UNCHANGED,
	MONITORS_CONFIG_BAD_CRC,
	MONITORS_CONFIG_ERROR,
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
	if (num_monitors > qxl_num_crtc) {
		DRM_DEBUG_KMS("client monitors list will be truncated: %d < %d\n",
			      qxl_num_crtc, num_monitors);
		num_monitors = qxl_num_crtc;
	} else {
		num_monitors = qdev->rom->client_monitors_config.count;
	}
	if (qdev->client_monitors_config
	      && (num_monitors != qdev->client_monitors_config->count)) {
		status = MONITORS_CONFIG_MODIFIED;
	}
	if (qxl_alloc_client_monitors_config(qdev, num_monitors)) {
		status = MONITORS_CONFIG_ERROR;
		return status;
	}
	/* we copy max from the client but it isn't used */
	qdev->client_monitors_config->max_allowed = qxl_num_crtc;
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
	struct drm_modeset_acquire_ctx ctx;
	int status, retries, ret;

	for (retries = 0; retries < 10; retries++) {
		status = qxl_display_copy_rom_client_monitors_config(qdev);
		if (status != MONITORS_CONFIG_BAD_CRC)
			break;
		udelay(5);
	}
	if (status == MONITORS_CONFIG_ERROR) {
		DRM_DEBUG_KMS("ignoring client monitors config: error");
		return;
	}
	if (status == MONITORS_CONFIG_BAD_CRC) {
		DRM_DEBUG_KMS("ignoring client monitors config: bad crc");
		return;
	}
	if (status == MONITORS_CONFIG_UNCHANGED) {
		DRM_DEBUG_KMS("ignoring client monitors config: unchanged");
		return;
	}

	DRM_MODESET_LOCK_ALL_BEGIN(dev, ctx, DRM_MODESET_ACQUIRE_INTERRUPTIBLE, ret);
	qxl_update_offset_props(qdev);
	DRM_MODESET_LOCK_ALL_END(dev, ctx, ret);
	if (!drm_helper_hpd_irq_event(dev)) {
		/* notify that the monitor configuration changed, to
		   adjust at the arbitrary resolution */
		drm_kms_helper_hotplug_event(dev);
	}
}

static int qxl_check_mode(struct qxl_device *qdev,
			  unsigned int width,
			  unsigned int height)
{
	unsigned int stride;
	unsigned int size;

	if (check_mul_overflow(width, 4u, &stride))
		return -EINVAL;
	if (check_mul_overflow(stride, height, &size))
		return -EINVAL;
	if (size > qdev->vram_size)
		return -ENOMEM;
	return 0;
}

static int qxl_check_framebuffer(struct qxl_device *qdev,
				 struct qxl_bo *bo)
{
	return qxl_check_mode(qdev, bo->surf.width, bo->surf.height);
}

static int qxl_add_mode(struct drm_connector *connector,
			unsigned int width,
			unsigned int height,
			bool preferred)
{
	struct drm_device *dev = connector->dev;
	struct qxl_device *qdev = to_qxl(dev);
	struct drm_display_mode *mode = NULL;
	int rc;

	rc = qxl_check_mode(qdev, width, height);
	if (rc != 0)
		return 0;

	mode = drm_cvt_mode(dev, width, height, 60, false, false, false);
	if (!mode)
		return 0;

	if (preferred)
		mode->type |= DRM_MODE_TYPE_PREFERRED;
	mode->hdisplay = width;
	mode->vdisplay = height;
	drm_mode_set_name(mode);
	drm_mode_probed_add(connector, mode);
	return 1;
}

static int qxl_add_monitors_config_modes(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct qxl_device *qdev = to_qxl(dev);
	struct qxl_output *output = drm_connector_to_qxl_output(connector);
	int h = output->index;
	struct qxl_head *head;

	if (!qdev->monitors_config)
		return 0;
	if (h >= qxl_num_crtc)
		return 0;
	if (!qdev->client_monitors_config)
		return 0;
	if (h >= qdev->client_monitors_config->count)
		return 0;

	head = &qdev->client_monitors_config->heads[h];
	DRM_DEBUG_KMS("head %d is %dx%d\n", h, head->width, head->height);

	return qxl_add_mode(connector, head->width, head->height, true);
}

static struct mode_size {
	int w;
	int h;
} extra_modes[] = {
	{ 720,  480},
	{1152,  768},
	{1280,  854},
};

static int qxl_add_extra_modes(struct drm_connector *connector)
{
	int i, ret = 0;

	for (i = 0; i < ARRAY_SIZE(extra_modes); i++)
		ret += qxl_add_mode(connector,
				    extra_modes[i].w,
				    extra_modes[i].h,
				    false);
	return ret;
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
	struct qxl_device *qdev = to_qxl(dev);
	struct qxl_crtc *qcrtc = to_qxl_crtc(crtc);
	struct qxl_head head;
	int oldcount, i = qcrtc->index;

	if (!qdev->primary_bo) {
		DRM_DEBUG_KMS("no primary surface, skip (%s)\n", reason);
		return;
	}

	if (!qdev->monitors_config || qxl_num_crtc <= i)
		return;

	head.id = i;
	head.flags = 0;
	head.surface_id = 0;
	oldcount = qdev->monitors_config->count;
	if (crtc->state->active) {
		struct drm_display_mode *mode = &crtc->mode;

		head.width = mode->hdisplay;
		head.height = mode->vdisplay;
		head.x = crtc->x;
		head.y = crtc->y;
		if (qdev->monitors_config->count < i + 1)
			qdev->monitors_config->count = i + 1;
		if (qdev->primary_bo == qdev->dumb_shadow_bo)
			head.x += qdev->dumb_heads[i].x;
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
			      qxl_num_crtc);

	qdev->monitors_config->heads[i] = head;
	qdev->monitors_config->max_allowed = qxl_num_crtc;
	qxl_send_monitors_config(qdev);
}

static void qxl_crtc_atomic_flush(struct drm_crtc *crtc,
				  struct drm_atomic_state *state)
{
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

static int qxl_framebuffer_surface_dirty(struct drm_framebuffer *fb,
					 struct drm_file *file_priv,
					 unsigned int flags, unsigned int color,
					 struct drm_clip_rect *clips,
					 unsigned int num_clips)
{
	/* TODO: vmwgfx where this was cribbed from had locking. Why? */
	struct qxl_device *qdev = to_qxl(fb->dev);
	struct drm_clip_rect norect;
	struct qxl_bo *qobj;
	struct drm_modeset_acquire_ctx ctx;
	bool is_primary;
	int inc = 1, ret;

	DRM_MODESET_LOCK_ALL_BEGIN(fb->dev, ctx, DRM_MODESET_ACQUIRE_INTERRUPTIBLE, ret);

	qobj = gem_to_qxl_bo(fb->obj[0]);
	/* if we aren't primary surface ignore this */
	is_primary = qobj->shadow ? qobj->shadow->is_primary : qobj->is_primary;
	if (!is_primary)
		goto out_lock_end;

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

	qxl_draw_dirty_fb(qdev, fb, qobj, flags, color,
			  clips, num_clips, inc, 0);

out_lock_end:
	DRM_MODESET_LOCK_ALL_END(fb->dev, ctx, ret);

	return 0;
}

static const struct drm_framebuffer_funcs qxl_fb_funcs = {
	.destroy = drm_gem_fb_destroy,
	.dirty = qxl_framebuffer_surface_dirty,
	.create_handle = drm_gem_fb_create_handle,
};

static void qxl_crtc_atomic_enable(struct drm_crtc *crtc,
				   struct drm_atomic_state *state)
{
	qxl_crtc_update_monitors_config(crtc, "enable");
}

static void qxl_crtc_atomic_disable(struct drm_crtc *crtc,
				    struct drm_atomic_state *state)
{
	qxl_crtc_update_monitors_config(crtc, "disable");
}

static const struct drm_crtc_helper_funcs qxl_crtc_helper_funcs = {
	.atomic_flush = qxl_crtc_atomic_flush,
	.atomic_enable = qxl_crtc_atomic_enable,
	.atomic_disable = qxl_crtc_atomic_disable,
};

static int qxl_primary_atomic_check(struct drm_plane *plane,
				    struct drm_atomic_state *state)
{
	struct drm_plane_state *new_plane_state = drm_atomic_get_new_plane_state(state,
										 plane);
	struct qxl_device *qdev = to_qxl(plane->dev);
	struct qxl_bo *bo;

	if (!new_plane_state->crtc || !new_plane_state->fb)
		return 0;

	bo = gem_to_qxl_bo(new_plane_state->fb->obj[0]);

	return qxl_check_framebuffer(qdev, bo);
}

static int qxl_primary_apply_cursor(struct qxl_device *qdev,
				    struct drm_plane_state *plane_state)
{
	struct qxl_crtc *qcrtc = to_qxl_crtc(plane_state->crtc);
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
	cmd->u.set.position.x = plane_state->crtc_x + plane_state->hotspot_x;
	cmd->u.set.position.y = plane_state->crtc_y + plane_state->hotspot_y;

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

static int qxl_primary_move_cursor(struct qxl_device *qdev,
				   struct drm_plane_state *plane_state)
{
	struct qxl_crtc *qcrtc = to_qxl_crtc(plane_state->crtc);
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

	ret = qxl_release_reserve_list(release, true);
	if (ret) {
		qxl_release_free(qdev, release);
		return ret;
	}

	cmd = (struct qxl_cursor_cmd *)qxl_release_map(qdev, release);
	cmd->type = QXL_CURSOR_MOVE;
	cmd->u.position.x = plane_state->crtc_x + plane_state->hotspot_x;
	cmd->u.position.y = plane_state->crtc_y + plane_state->hotspot_y;
	qxl_release_unmap(qdev, release, &cmd->release_info);

	qxl_release_fence_buffer_objects(release);
	qxl_push_cursor_ring_release(qdev, release, QXL_CMD_CURSOR, false);
	return ret;
}

static struct qxl_bo *qxl_create_cursor(struct qxl_device *qdev,
					struct qxl_bo *user_bo,
					int hot_x, int hot_y)
{
	static const u32 size = 64 * 64 * 4;
	struct qxl_bo *cursor_bo;
	struct iosys_map cursor_map;
	struct iosys_map user_map;
	struct qxl_cursor cursor;
	int ret;

	if (!user_bo)
		return NULL;

	ret = qxl_bo_create(qdev, sizeof(struct qxl_cursor) + size,
			    false, true, QXL_GEM_DOMAIN_VRAM, 1,
			    NULL, &cursor_bo);
	if (ret)
		goto err;

	ret = qxl_bo_vmap(cursor_bo, &cursor_map);
	if (ret)
		goto err_unref;

	ret = qxl_bo_vmap(user_bo, &user_map);
	if (ret)
		goto err_unmap;

	cursor.header.unique = 0;
	cursor.header.type = SPICE_CURSOR_TYPE_ALPHA;
	cursor.header.width = 64;
	cursor.header.height = 64;
	cursor.header.hot_spot_x = hot_x;
	cursor.header.hot_spot_y = hot_y;
	cursor.data_size = size;
	cursor.chunk.next_chunk = 0;
	cursor.chunk.prev_chunk = 0;
	cursor.chunk.data_size = size;
	if (cursor_map.is_iomem) {
		memcpy_toio(cursor_map.vaddr_iomem,
			    &cursor, sizeof(cursor));
		memcpy_toio(cursor_map.vaddr_iomem + sizeof(cursor),
			    user_map.vaddr, size);
	} else {
		memcpy(cursor_map.vaddr,
		       &cursor, sizeof(cursor));
		memcpy(cursor_map.vaddr + sizeof(cursor),
		       user_map.vaddr, size);
	}

	qxl_bo_vunmap(user_bo);
	qxl_bo_vunmap(cursor_bo);
	return cursor_bo;

err_unmap:
	qxl_bo_vunmap(cursor_bo);
err_unref:
	qxl_bo_unpin(cursor_bo);
	qxl_bo_unref(&cursor_bo);
err:
	return NULL;
}

static void qxl_free_cursor(struct qxl_bo *cursor_bo)
{
	if (!cursor_bo)
		return;

	qxl_bo_unpin(cursor_bo);
	qxl_bo_unref(&cursor_bo);
}

static void qxl_primary_atomic_update(struct drm_plane *plane,
				      struct drm_atomic_state *state)
{
	struct drm_plane_state *new_state = drm_atomic_get_new_plane_state(state,
									   plane);
	struct qxl_device *qdev = to_qxl(plane->dev);
	struct qxl_bo *bo = gem_to_qxl_bo(new_state->fb->obj[0]);
	struct qxl_bo *primary;
	struct drm_clip_rect norect = {
	    .x1 = 0,
	    .y1 = 0,
	    .x2 = new_state->fb->width,
	    .y2 = new_state->fb->height
	};
	uint32_t dumb_shadow_offset = 0;

	primary = bo->shadow ? bo->shadow : bo;

	if (!primary->is_primary) {
		if (qdev->primary_bo)
			qxl_io_destroy_primary(qdev);
		qxl_io_create_primary(qdev, primary);
		qxl_primary_apply_cursor(qdev, plane->state);
	}

	if (bo->is_dumb)
		dumb_shadow_offset =
			qdev->dumb_heads[new_state->crtc->index].x;

	qxl_draw_dirty_fb(qdev, new_state->fb, bo, 0, 0, &norect, 1, 1,
			  dumb_shadow_offset);
}

static void qxl_primary_atomic_disable(struct drm_plane *plane,
				       struct drm_atomic_state *state)
{
	struct drm_plane_state *old_state = drm_atomic_get_old_plane_state(state,
									   plane);
	struct qxl_device *qdev = to_qxl(plane->dev);

	if (old_state->fb) {
		struct qxl_bo *bo = gem_to_qxl_bo(old_state->fb->obj[0]);

		if (bo->shadow)
			bo = bo->shadow;
		if (bo->is_primary)
			qxl_io_destroy_primary(qdev);
	}
}

static void qxl_cursor_atomic_update(struct drm_plane *plane,
				     struct drm_atomic_state *state)
{
	struct drm_plane_state *old_state = drm_atomic_get_old_plane_state(state,
									   plane);
	struct drm_plane_state *new_state = drm_atomic_get_new_plane_state(state,
									   plane);
	struct qxl_device *qdev = to_qxl(plane->dev);
	struct drm_framebuffer *fb = new_state->fb;

	if (fb != old_state->fb) {
		qxl_primary_apply_cursor(qdev, new_state);
	} else {
		qxl_primary_move_cursor(qdev, new_state);
	}
}

static void qxl_cursor_atomic_disable(struct drm_plane *plane,
				      struct drm_atomic_state *state)
{
	struct drm_plane_state *old_state = drm_atomic_get_old_plane_state(state,
									   plane);
	struct qxl_device *qdev = to_qxl(plane->dev);
	struct qxl_crtc *qcrtc;
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

	qcrtc = to_qxl_crtc(old_state->crtc);
	qxl_free_cursor(qcrtc->cursor_bo);
	qcrtc->cursor_bo = NULL;
}

static void qxl_update_dumb_head(struct qxl_device *qdev,
				 int index, struct qxl_bo *bo)
{
	uint32_t width, height;

	if (index >= qdev->monitors_config->max_allowed)
		return;

	if (bo && bo->is_dumb) {
		width = bo->surf.width;
		height = bo->surf.height;
	} else {
		width = 0;
		height = 0;
	}

	if (qdev->dumb_heads[index].width == width &&
	    qdev->dumb_heads[index].height == height)
		return;

	DRM_DEBUG("#%d: %dx%d -> %dx%d\n", index,
		  qdev->dumb_heads[index].width,
		  qdev->dumb_heads[index].height,
		  width, height);
	qdev->dumb_heads[index].width = width;
	qdev->dumb_heads[index].height = height;
}

static void qxl_calc_dumb_shadow(struct qxl_device *qdev,
				 struct qxl_surface *surf)
{
	struct qxl_head *head;
	int i;

	memset(surf, 0, sizeof(*surf));
	for (i = 0; i < qdev->monitors_config->max_allowed; i++) {
		head = qdev->dumb_heads + i;
		head->x = surf->width;
		surf->width += head->width;
		if (surf->height < head->height)
			surf->height = head->height;
	}
	if (surf->width < 64)
		surf->width = 64;
	if (surf->height < 64)
		surf->height = 64;
	surf->format = SPICE_SURFACE_FMT_32_xRGB;
	surf->stride = surf->width * 4;

	if (!qdev->dumb_shadow_bo ||
	    qdev->dumb_shadow_bo->surf.width != surf->width ||
	    qdev->dumb_shadow_bo->surf.height != surf->height)
		DRM_DEBUG("%dx%d\n", surf->width, surf->height);
}

static void qxl_prepare_shadow(struct qxl_device *qdev, struct qxl_bo *user_bo,
			       int crtc_index)
{
	struct qxl_surface surf;

	qxl_update_dumb_head(qdev, crtc_index,
			     user_bo);
	qxl_calc_dumb_shadow(qdev, &surf);
	if (!qdev->dumb_shadow_bo ||
	    qdev->dumb_shadow_bo->surf.width  != surf.width ||
	    qdev->dumb_shadow_bo->surf.height != surf.height) {
		if (qdev->dumb_shadow_bo) {
			qxl_bo_unpin(qdev->dumb_shadow_bo);
			drm_gem_object_put
				(&qdev->dumb_shadow_bo->tbo.base);
			qdev->dumb_shadow_bo = NULL;
		}
		qxl_bo_create(qdev, surf.height * surf.stride,
			      true, true, QXL_GEM_DOMAIN_SURFACE, 0,
			      &surf, &qdev->dumb_shadow_bo);
	}
	if (user_bo->shadow != qdev->dumb_shadow_bo) {
		if (user_bo->shadow) {
			qxl_bo_unpin(user_bo->shadow);
			drm_gem_object_put
				(&user_bo->shadow->tbo.base);
			user_bo->shadow = NULL;
		}
		drm_gem_object_get(&qdev->dumb_shadow_bo->tbo.base);
		user_bo->shadow = qdev->dumb_shadow_bo;
		qxl_bo_pin(user_bo->shadow);
	}
}

static int qxl_plane_prepare_fb(struct drm_plane *plane,
				struct drm_plane_state *new_state)
{
	struct qxl_device *qdev = to_qxl(plane->dev);
	struct drm_gem_object *obj;
	struct qxl_bo *user_bo;
	int ret;

	if (!new_state->fb)
		return 0;

	obj = new_state->fb->obj[0];
	user_bo = gem_to_qxl_bo(obj);

	if (plane->type == DRM_PLANE_TYPE_PRIMARY &&
	    user_bo->is_dumb) {
		qxl_prepare_shadow(qdev, user_bo, new_state->crtc->index);
	}

	if (plane->type == DRM_PLANE_TYPE_CURSOR &&
	    plane->state->fb != new_state->fb) {
		struct qxl_crtc *qcrtc = to_qxl_crtc(new_state->crtc);
		struct qxl_bo *old_cursor_bo = qcrtc->cursor_bo;

		qcrtc->cursor_bo = qxl_create_cursor(qdev, user_bo,
						     new_state->hotspot_x,
						     new_state->hotspot_y);
		qxl_free_cursor(old_cursor_bo);
	}

	ret = qxl_bo_pin(user_bo);
	if (ret)
		return ret;

	return drm_gem_plane_helper_prepare_fb(plane, new_state);
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

	obj = old_state->fb->obj[0];
	user_bo = gem_to_qxl_bo(obj);
	qxl_bo_unpin(user_bo);

	if (old_state->fb != plane->state->fb && user_bo->shadow) {
		qxl_bo_unpin(user_bo->shadow);
		drm_gem_object_put(&user_bo->shadow->tbo.base);
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
	.destroy	= drm_plane_helper_destroy,
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
	.destroy	= drm_plane_helper_destroy,
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
	struct qxl_device *qdev = to_qxl(dev);
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
	struct drm_device *dev = connector->dev;
	struct qxl_device *qdev = to_qxl(dev);
	struct qxl_output *output = drm_connector_to_qxl_output(connector);
	unsigned int pwidth = 1024;
	unsigned int pheight = 768;
	int ret = 0;

	if (qdev->client_monitors_config) {
		struct qxl_head *head;
		head = &qdev->client_monitors_config->heads[output->index];
		if (head->width)
			pwidth = head->width;
		if (head->height)
			pheight = head->height;
	}

	ret += drm_add_modes_noedid(connector, 8192, 8192);
	ret += qxl_add_extra_modes(connector);
	ret += qxl_add_monitors_config_modes(connector);
	drm_set_preferred_mode(connector, pwidth, pheight);
	return ret;
}

static enum drm_mode_status qxl_conn_mode_valid(struct drm_connector *connector,
			       struct drm_display_mode *mode)
{
	struct drm_device *ddev = connector->dev;
	struct qxl_device *qdev = to_qxl(ddev);

	if (qxl_check_mode(qdev, mode->hdisplay, mode->vdisplay) != 0)
		return MODE_BAD;

	return MODE_OK;
}

static struct drm_encoder *qxl_best_encoder(struct drm_connector *connector)
{
	struct qxl_output *qxl_output =
		drm_connector_to_qxl_output(connector);

	DRM_DEBUG("\n");
	return &qxl_output->enc;
}

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
	struct qxl_device *qdev = to_qxl(ddev);
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
	.detect = qxl_conn_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = qxl_conn_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
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
	struct qxl_device *qdev = to_qxl(dev);
	struct qxl_output *qxl_output;
	struct drm_connector *connector;
	struct drm_encoder *encoder;
	int ret;

	qxl_output = kzalloc(sizeof(struct qxl_output), GFP_KERNEL);
	if (!qxl_output)
		return -ENOMEM;

	qxl_output->index = num_output;

	connector = &qxl_output->base;
	encoder = &qxl_output->enc;
	drm_connector_init(dev, &qxl_output->base,
			   &qxl_connector_funcs, DRM_MODE_CONNECTOR_VIRTUAL);

	ret = drm_simple_encoder_init(dev, &qxl_output->enc,
				      DRM_MODE_ENCODER_VIRTUAL);
	if (ret) {
		drm_err(dev, "drm_simple_encoder_init() failed, error %d\n",
			ret);
		goto err_drm_connector_cleanup;
	}

	/* we get HPD via client monitors config */
	connector->polled = DRM_CONNECTOR_POLL_HPD;
	encoder->possible_crtcs = 1 << num_output;
	drm_connector_attach_encoder(&qxl_output->base,
					  &qxl_output->enc);
	drm_connector_helper_add(connector, &qxl_connector_helper_funcs);

	drm_object_attach_property(&connector->base,
				   qdev->hotplug_mode_update_property, 0);
	drm_object_attach_property(&connector->base,
				   dev->mode_config.suggested_x_property, 0);
	drm_object_attach_property(&connector->base,
				   dev->mode_config.suggested_y_property, 0);
	return 0;

err_drm_connector_cleanup:
	drm_connector_cleanup(&qxl_output->base);
	kfree(qxl_output);
	return ret;
}

static struct drm_framebuffer *
qxl_user_framebuffer_create(struct drm_device *dev,
			    struct drm_file *file_priv,
			    const struct drm_mode_fb_cmd2 *mode_cmd)
{
	return drm_gem_fb_create_with_funcs(dev, file_priv, mode_cmd,
					    &qxl_fb_funcs);
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
	struct iosys_map map;
	int monitors_config_size = sizeof(struct qxl_monitors_config) +
		qxl_num_crtc * sizeof(struct qxl_head);

	ret = qxl_gem_object_create(qdev, monitors_config_size, 0,
				    QXL_GEM_DOMAIN_VRAM,
				    false, false, NULL, &gobj);
	if (ret) {
		DRM_ERROR("%s: failed to create gem ret=%d\n", __func__, ret);
		return -ENOMEM;
	}
	qdev->monitors_config_bo = gem_to_qxl_bo(gobj);

	ret = qxl_bo_vmap(qdev->monitors_config_bo, &map);
	if (ret)
		return ret;

	qdev->monitors_config = qdev->monitors_config_bo->kptr;
	qdev->ram_header->monitors_config =
		qxl_bo_physical_address(qdev, qdev->monitors_config_bo, 0);

	memset(qdev->monitors_config, 0, monitors_config_size);
	qdev->dumb_heads = kcalloc(qxl_num_crtc, sizeof(qdev->dumb_heads[0]),
				   GFP_KERNEL);
	if (!qdev->dumb_heads) {
		qxl_destroy_monitors_object(qdev);
		return -ENOMEM;
	}
	return 0;
}

int qxl_destroy_monitors_object(struct qxl_device *qdev)
{
	int ret;

	if (!qdev->monitors_config_bo)
		return 0;

	kfree(qdev->dumb_heads);
	qdev->dumb_heads = NULL;

	qdev->monitors_config = NULL;
	qdev->ram_header->monitors_config = 0;

	ret = qxl_bo_vunmap(qdev->monitors_config_bo);
	if (ret)
		return ret;

	qxl_bo_unref(&qdev->monitors_config_bo);
	return 0;
}

int qxl_modeset_init(struct qxl_device *qdev)
{
	int i;
	int ret;

	ret = drmm_mode_config_init(&qdev->ddev);
	if (ret)
		return ret;

	ret = qxl_create_monitors_object(qdev);
	if (ret)
		return ret;

	qdev->ddev.mode_config.funcs = (void *)&qxl_mode_funcs;

	/* modes will be validated against the framebuffer size */
	qdev->ddev.mode_config.min_width = 0;
	qdev->ddev.mode_config.min_height = 0;
	qdev->ddev.mode_config.max_width = 8192;
	qdev->ddev.mode_config.max_height = 8192;

	drm_mode_create_suggested_offset_properties(&qdev->ddev);
	qxl_mode_create_hotplug_mode_update_property(qdev);

	for (i = 0 ; i < qxl_num_crtc; ++i) {
		qdev_crtc_init(&qdev->ddev, i);
		qdev_output_init(&qdev->ddev, i);
	}

	qxl_display_read_client_monitors_config(qdev);

	drm_mode_config_reset(&qdev->ddev);
	return 0;
}

void qxl_modeset_fini(struct qxl_device *qdev)
{
	if (qdev->dumb_shadow_bo) {
		qxl_bo_unpin(qdev->dumb_shadow_bo);
		drm_gem_object_put(&qdev->dumb_shadow_bo->tbo.base);
		qdev->dumb_shadow_bo = NULL;
	}
	qxl_destroy_monitors_object(qdev);
}
