// SPDX-License-Identifier: GPL-2.0 OR MIT

/*
 *  Xen para-virtual DRM device
 *
 * Copyright (C) 2016-2018 EPAM Systems Inc.
 *
 * Author: Oleksandr Andrushchenko <oleksandr_andrushchenko@epam.com>
 */

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>

#include "xen_drm_front.h"
#include "xen_drm_front_conn.h"
#include "xen_drm_front_kms.h"

/*
 * Timeout in ms to wait for frame done event from the backend:
 * must be a bit more than IO time-out
 */
#define FRAME_DONE_TO_MS	(XEN_DRM_FRONT_WAIT_BACK_MS + 100)

static struct xen_drm_front_drm_pipeline *
to_xen_drm_pipeline(struct drm_simple_display_pipe *pipe)
{
	return container_of(pipe, struct xen_drm_front_drm_pipeline, pipe);
}

static void fb_destroy(struct drm_framebuffer *fb)
{
	struct xen_drm_front_drm_info *drm_info = fb->dev->dev_private;
	int idx;

	if (drm_dev_enter(fb->dev, &idx)) {
		xen_drm_front_fb_detach(drm_info->front_info,
					xen_drm_front_fb_to_cookie(fb));
		drm_dev_exit(idx);
	}
	drm_gem_fb_destroy(fb);
}

static const struct drm_framebuffer_funcs fb_funcs = {
	.destroy = fb_destroy,
};

static struct drm_framebuffer *
fb_create(struct drm_device *dev, struct drm_file *filp,
	  const struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct xen_drm_front_drm_info *drm_info = dev->dev_private;
	struct drm_framebuffer *fb;
	struct drm_gem_object *gem_obj;
	int ret;

	fb = drm_gem_fb_create_with_funcs(dev, filp, mode_cmd, &fb_funcs);
	if (IS_ERR_OR_NULL(fb))
		return fb;

	gem_obj = fb->obj[0];

	ret = xen_drm_front_fb_attach(drm_info->front_info,
				      xen_drm_front_dbuf_to_cookie(gem_obj),
				      xen_drm_front_fb_to_cookie(fb),
				      fb->width, fb->height,
				      fb->format->format);
	if (ret < 0) {
		DRM_ERROR("Back failed to attach FB %p: %d\n", fb, ret);
		goto fail;
	}

	return fb;

fail:
	drm_gem_fb_destroy(fb);
	return ERR_PTR(ret);
}

static const struct drm_mode_config_funcs mode_config_funcs = {
	.fb_create = fb_create,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

static void send_pending_event(struct xen_drm_front_drm_pipeline *pipeline)
{
	struct drm_crtc *crtc = &pipeline->pipe.crtc;
	struct drm_device *dev = crtc->dev;
	unsigned long flags;

	spin_lock_irqsave(&dev->event_lock, flags);
	if (pipeline->pending_event)
		drm_crtc_send_vblank_event(crtc, pipeline->pending_event);
	pipeline->pending_event = NULL;
	spin_unlock_irqrestore(&dev->event_lock, flags);
}

static void display_enable(struct drm_simple_display_pipe *pipe,
			   struct drm_crtc_state *crtc_state,
			   struct drm_plane_state *plane_state)
{
	struct xen_drm_front_drm_pipeline *pipeline =
			to_xen_drm_pipeline(pipe);
	struct drm_crtc *crtc = &pipe->crtc;
	struct drm_framebuffer *fb = plane_state->fb;
	int ret, idx;

	if (!drm_dev_enter(pipe->crtc.dev, &idx))
		return;

	ret = xen_drm_front_mode_set(pipeline, crtc->x, crtc->y,
				     fb->width, fb->height,
				     fb->format->cpp[0] * 8,
				     xen_drm_front_fb_to_cookie(fb));

	if (ret) {
		DRM_ERROR("Failed to enable display: %d\n", ret);
		pipeline->conn_connected = false;
	}

	drm_dev_exit(idx);
}

static void display_disable(struct drm_simple_display_pipe *pipe)
{
	struct xen_drm_front_drm_pipeline *pipeline =
			to_xen_drm_pipeline(pipe);
	int ret = 0, idx;

	if (drm_dev_enter(pipe->crtc.dev, &idx)) {
		ret = xen_drm_front_mode_set(pipeline, 0, 0, 0, 0, 0,
					     xen_drm_front_fb_to_cookie(NULL));
		drm_dev_exit(idx);
	}
	if (ret)
		DRM_ERROR("Failed to disable display: %d\n", ret);

	/* Make sure we can restart with enabled connector next time */
	pipeline->conn_connected = true;

	/* release stalled event if any */
	send_pending_event(pipeline);
}

void xen_drm_front_kms_on_frame_done(struct xen_drm_front_drm_pipeline *pipeline,
				     u64 fb_cookie)
{
	/*
	 * This runs in interrupt context, e.g. under
	 * drm_info->front_info->io_lock, so we cannot call _sync version
	 * to cancel the work
	 */
	cancel_delayed_work(&pipeline->pflip_to_worker);

	send_pending_event(pipeline);
}

static void pflip_to_worker(struct work_struct *work)
{
	struct delayed_work *delayed_work = to_delayed_work(work);
	struct xen_drm_front_drm_pipeline *pipeline =
			container_of(delayed_work,
				     struct xen_drm_front_drm_pipeline,
				     pflip_to_worker);

	DRM_ERROR("Frame done timed-out, releasing");
	send_pending_event(pipeline);
}

static bool display_send_page_flip(struct drm_simple_display_pipe *pipe,
				   struct drm_plane_state *old_plane_state)
{
	struct drm_plane_state *plane_state =
			drm_atomic_get_new_plane_state(old_plane_state->state,
						       &pipe->plane);

	/*
	 * If old_plane_state->fb is NULL and plane_state->fb is not,
	 * then this is an atomic commit which will enable display.
	 * If old_plane_state->fb is not NULL and plane_state->fb is,
	 * then this is an atomic commit which will disable display.
	 * Ignore these and do not send page flip as this framebuffer will be
	 * sent to the backend as a part of display_set_config call.
	 */
	if (old_plane_state->fb && plane_state->fb) {
		struct xen_drm_front_drm_pipeline *pipeline =
				to_xen_drm_pipeline(pipe);
		struct xen_drm_front_drm_info *drm_info = pipeline->drm_info;
		int ret;

		schedule_delayed_work(&pipeline->pflip_to_worker,
				      msecs_to_jiffies(FRAME_DONE_TO_MS));

		ret = xen_drm_front_page_flip(drm_info->front_info,
					      pipeline->index,
					      xen_drm_front_fb_to_cookie(plane_state->fb));
		if (ret) {
			DRM_ERROR("Failed to send page flip request to backend: %d\n", ret);

			pipeline->conn_connected = false;
			/*
			 * Report the flip not handled, so pending event is
			 * sent, unblocking user-space.
			 */
			return false;
		}
		/*
		 * Signal that page flip was handled, pending event will be sent
		 * on frame done event from the backend.
		 */
		return true;
	}

	return false;
}

static int display_check(struct drm_simple_display_pipe *pipe,
			 struct drm_plane_state *plane_state,
			 struct drm_crtc_state *crtc_state)
{
	/*
	 * Xen doesn't initialize vblanking via drm_vblank_init(), so
	 * DRM helpers assume that it doesn't handle vblanking and start
	 * sending out fake VBLANK events automatically.
	 *
	 * As xen contains it's own logic for sending out VBLANK events
	 * in send_pending_event(), disable no_vblank (i.e., the xen
	 * driver has vblanking support).
	 */
	crtc_state->no_vblank = false;

	return 0;
}

static void display_update(struct drm_simple_display_pipe *pipe,
			   struct drm_plane_state *old_plane_state)
{
	struct xen_drm_front_drm_pipeline *pipeline =
			to_xen_drm_pipeline(pipe);
	struct drm_crtc *crtc = &pipe->crtc;
	struct drm_pending_vblank_event *event;
	int idx;

	event = crtc->state->event;
	if (event) {
		struct drm_device *dev = crtc->dev;
		unsigned long flags;

		WARN_ON(pipeline->pending_event);

		spin_lock_irqsave(&dev->event_lock, flags);
		crtc->state->event = NULL;

		pipeline->pending_event = event;
		spin_unlock_irqrestore(&dev->event_lock, flags);
	}

	if (!drm_dev_enter(pipe->crtc.dev, &idx)) {
		send_pending_event(pipeline);
		return;
	}

	/*
	 * Send page flip request to the backend *after* we have event cached
	 * above, so on page flip done event from the backend we can
	 * deliver it and there is no race condition between this code and
	 * event from the backend.
	 * If this is not a page flip, e.g. no flip done event from the backend
	 * is expected, then send now.
	 */
	if (!display_send_page_flip(pipe, old_plane_state))
		send_pending_event(pipeline);

	drm_dev_exit(idx);
}

static enum drm_mode_status
display_mode_valid(struct drm_simple_display_pipe *pipe,
		   const struct drm_display_mode *mode)
{
	struct xen_drm_front_drm_pipeline *pipeline =
			container_of(pipe, struct xen_drm_front_drm_pipeline,
				     pipe);

	if (mode->hdisplay != pipeline->width)
		return MODE_ERROR;

	if (mode->vdisplay != pipeline->height)
		return MODE_ERROR;

	return MODE_OK;
}

static const struct drm_simple_display_pipe_funcs display_funcs = {
	.mode_valid = display_mode_valid,
	.enable = display_enable,
	.disable = display_disable,
	.prepare_fb = drm_gem_fb_simple_display_pipe_prepare_fb,
	.check = display_check,
	.update = display_update,
};

static int display_pipe_init(struct xen_drm_front_drm_info *drm_info,
			     int index, struct xen_drm_front_cfg_connector *cfg,
			     struct xen_drm_front_drm_pipeline *pipeline)
{
	struct drm_device *dev = drm_info->drm_dev;
	const u32 *formats;
	int format_count;
	int ret;

	pipeline->drm_info = drm_info;
	pipeline->index = index;
	pipeline->height = cfg->height;
	pipeline->width = cfg->width;

	INIT_DELAYED_WORK(&pipeline->pflip_to_worker, pflip_to_worker);

	ret = xen_drm_front_conn_init(drm_info, &pipeline->conn);
	if (ret)
		return ret;

	formats = xen_drm_front_conn_get_formats(&format_count);

	return drm_simple_display_pipe_init(dev, &pipeline->pipe,
					    &display_funcs, formats,
					    format_count, NULL,
					    &pipeline->conn);
}

int xen_drm_front_kms_init(struct xen_drm_front_drm_info *drm_info)
{
	struct drm_device *dev = drm_info->drm_dev;
	int i, ret;

	drm_mode_config_init(dev);

	dev->mode_config.min_width = 0;
	dev->mode_config.min_height = 0;
	dev->mode_config.max_width = 4095;
	dev->mode_config.max_height = 2047;
	dev->mode_config.funcs = &mode_config_funcs;

	for (i = 0; i < drm_info->front_info->cfg.num_connectors; i++) {
		struct xen_drm_front_cfg_connector *cfg =
				&drm_info->front_info->cfg.connectors[i];
		struct xen_drm_front_drm_pipeline *pipeline =
				&drm_info->pipeline[i];

		ret = display_pipe_init(drm_info, i, cfg, pipeline);
		if (ret) {
			drm_mode_config_cleanup(dev);
			return ret;
		}
	}

	drm_mode_config_reset(dev);
	drm_kms_helper_poll_init(dev);
	return 0;
}

void xen_drm_front_kms_fini(struct xen_drm_front_drm_info *drm_info)
{
	int i;

	for (i = 0; i < drm_info->front_info->cfg.num_connectors; i++) {
		struct xen_drm_front_drm_pipeline *pipeline =
				&drm_info->pipeline[i];

		cancel_delayed_work_sync(&pipeline->pflip_to_worker);

		send_pending_event(pipeline);
	}
}
