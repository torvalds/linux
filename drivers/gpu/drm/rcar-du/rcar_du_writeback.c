// SPDX-License-Identifier: GPL-2.0
/*
 * rcar_du_writeback.c  --  R-Car Display Unit Writeback Support
 *
 * Copyright (C) 2019 Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 */

#include <drm/drm_atomic_helper.h>
#include <drm/drm_device.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_writeback.h>

#include "rcar_du_crtc.h"
#include "rcar_du_drv.h"
#include "rcar_du_kms.h"
#include "rcar_du_writeback.h"

/**
 * struct rcar_du_wb_conn_state - Driver-specific writeback connector state
 * @state: base DRM connector state
 * @format: format of the writeback framebuffer
 */
struct rcar_du_wb_conn_state {
	struct drm_connector_state state;
	const struct rcar_du_format_info *format;
};

#define to_rcar_wb_conn_state(s) \
	container_of(s, struct rcar_du_wb_conn_state, state)

/**
 * struct rcar_du_wb_job - Driver-private data for writeback jobs
 * @sg_tables: scatter-gather tables for the framebuffer memory
 */
struct rcar_du_wb_job {
	struct sg_table sg_tables[3];
};

static int rcar_du_wb_conn_get_modes(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;

	return drm_add_modes_noedid(connector, dev->mode_config.max_width,
				    dev->mode_config.max_height);
}

static int rcar_du_wb_prepare_job(struct drm_writeback_connector *connector,
				  struct drm_writeback_job *job)
{
	struct rcar_du_crtc *rcrtc = wb_to_rcar_crtc(connector);
	struct rcar_du_wb_job *rjob;
	int ret;

	if (!job->fb)
		return 0;

	rjob = kzalloc(sizeof(*rjob), GFP_KERNEL);
	if (!rjob)
		return -ENOMEM;

	/* Map the framebuffer to the VSP. */
	ret = rcar_du_vsp_map_fb(rcrtc->vsp, job->fb, rjob->sg_tables);
	if (ret < 0) {
		kfree(rjob);
		return ret;
	}

	job->priv = rjob;
	return 0;
}

static void rcar_du_wb_cleanup_job(struct drm_writeback_connector *connector,
				   struct drm_writeback_job *job)
{
	struct rcar_du_crtc *rcrtc = wb_to_rcar_crtc(connector);
	struct rcar_du_wb_job *rjob = job->priv;

	if (!job->fb)
		return;

	rcar_du_vsp_unmap_fb(rcrtc->vsp, job->fb, rjob->sg_tables);
	kfree(rjob);
}

static const struct drm_connector_helper_funcs rcar_du_wb_conn_helper_funcs = {
	.get_modes = rcar_du_wb_conn_get_modes,
	.prepare_writeback_job = rcar_du_wb_prepare_job,
	.cleanup_writeback_job = rcar_du_wb_cleanup_job,
};

static struct drm_connector_state *
rcar_du_wb_conn_duplicate_state(struct drm_connector *connector)
{
	struct rcar_du_wb_conn_state *copy;

	if (WARN_ON(!connector->state))
		return NULL;

	copy = kzalloc(sizeof(*copy), GFP_KERNEL);
	if (!copy)
		return NULL;

	__drm_atomic_helper_connector_duplicate_state(connector, &copy->state);

	return &copy->state;
}

static void rcar_du_wb_conn_destroy_state(struct drm_connector *connector,
					  struct drm_connector_state *state)
{
	__drm_atomic_helper_connector_destroy_state(state);
	kfree(to_rcar_wb_conn_state(state));
}

static void rcar_du_wb_conn_reset(struct drm_connector *connector)
{
	struct rcar_du_wb_conn_state *state;

	if (connector->state) {
		rcar_du_wb_conn_destroy_state(connector, connector->state);
		connector->state = NULL;
	}

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (state == NULL)
		return;

	__drm_atomic_helper_connector_reset(connector, &state->state);
}

static const struct drm_connector_funcs rcar_du_wb_conn_funcs = {
	.reset = rcar_du_wb_conn_reset,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.atomic_duplicate_state = rcar_du_wb_conn_duplicate_state,
	.atomic_destroy_state = rcar_du_wb_conn_destroy_state,
};

static int rcar_du_wb_enc_atomic_check(struct drm_encoder *encoder,
				       struct drm_crtc_state *crtc_state,
				       struct drm_connector_state *conn_state)
{
	struct rcar_du_wb_conn_state *wb_state =
		to_rcar_wb_conn_state(conn_state);
	const struct drm_display_mode *mode = &crtc_state->mode;
	struct drm_device *dev = encoder->dev;
	struct drm_framebuffer *fb;

	if (!conn_state->writeback_job || !conn_state->writeback_job->fb)
		return 0;

	fb = conn_state->writeback_job->fb;

	/*
	 * Verify that the framebuffer format is supported and that its size
	 * matches the current mode.
	 */
	if (fb->width != mode->hdisplay || fb->height != mode->vdisplay) {
		dev_dbg(dev->dev, "%s: invalid framebuffer size %ux%u\n",
			__func__, fb->width, fb->height);
		return -EINVAL;
	}

	wb_state->format = rcar_du_format_info(fb->format->format);
	if (wb_state->format == NULL) {
		dev_dbg(dev->dev, "%s: unsupported format %08x\n", __func__,
			fb->format->format);
		return -EINVAL;
	}

	return 0;
}

static const struct drm_encoder_helper_funcs rcar_du_wb_enc_helper_funcs = {
	.atomic_check = rcar_du_wb_enc_atomic_check,
};

/*
 * Only RGB formats are currently supported as the VSP outputs RGB to the DU
 * and can't convert to YUV separately for writeback.
 */
static const u32 writeback_formats[] = {
	DRM_FORMAT_RGB332,
	DRM_FORMAT_ARGB4444,
	DRM_FORMAT_XRGB4444,
	DRM_FORMAT_ARGB1555,
	DRM_FORMAT_XRGB1555,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_BGR888,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_BGRA8888,
	DRM_FORMAT_BGRX8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_XRGB8888,
};

int rcar_du_writeback_init(struct rcar_du_device *rcdu,
			   struct rcar_du_crtc *rcrtc)
{
	struct drm_writeback_connector *wb_conn = &rcrtc->writeback;

	wb_conn->encoder.possible_crtcs = 1 << drm_crtc_index(&rcrtc->crtc);
	drm_connector_helper_add(&wb_conn->base,
				 &rcar_du_wb_conn_helper_funcs);

	return drm_writeback_connector_init(rcdu->ddev, wb_conn,
					    &rcar_du_wb_conn_funcs,
					    &rcar_du_wb_enc_helper_funcs,
					    writeback_formats,
					    ARRAY_SIZE(writeback_formats));
}

void rcar_du_writeback_setup(struct rcar_du_crtc *rcrtc,
			     struct vsp1_du_writeback_config *cfg)
{
	struct rcar_du_wb_conn_state *wb_state;
	struct drm_connector_state *state;
	struct rcar_du_wb_job *rjob;
	struct drm_framebuffer *fb;
	unsigned int i;

	state = rcrtc->writeback.base.state;
	if (!state || !state->writeback_job || !state->writeback_job->fb)
		return;

	fb = state->writeback_job->fb;
	rjob = state->writeback_job->priv;
	wb_state = to_rcar_wb_conn_state(state);

	cfg->pixelformat = wb_state->format->v4l2;
	cfg->pitch = fb->pitches[0];

	for (i = 0; i < wb_state->format->planes; ++i)
		cfg->mem[i] = sg_dma_address(rjob->sg_tables[i].sgl)
			    + fb->offsets[i];

	drm_writeback_queue_job(&rcrtc->writeback, state);
}

void rcar_du_writeback_complete(struct rcar_du_crtc *rcrtc)
{
	drm_writeback_signal_completion(&rcrtc->writeback, 0);
}
