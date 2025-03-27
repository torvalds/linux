// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <drm/drm_edid.h>
#include <drm/drm_framebuffer.h>

#include "dpu_writeback.h"

static int dpu_wb_conn_get_modes(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct msm_drm_private *priv = dev->dev_private;
	struct dpu_kms *dpu_kms = to_dpu_kms(priv->kms);

	/*
	 * We should ideally be limiting the modes only to the maxlinewidth but
	 * on some chipsets this will allow even 4k modes to be added which will
	 * fail the per SSPP bandwidth checks. So, till we have dual-SSPP support
	 * and source split support added lets limit the modes based on max_mixer_width
	 * as 4K modes can then be supported.
	 */
	return drm_add_modes_noedid(connector, dpu_kms->catalog->caps->max_mixer_width,
			dev->mode_config.max_height);
}

static int dpu_wb_conn_atomic_check(struct drm_connector *connector,
				    struct drm_atomic_state *state)
{
	struct drm_writeback_connector *wb_conn = drm_connector_to_writeback(connector);
	struct dpu_wb_connector *dpu_wb_conn = to_dpu_wb_conn(wb_conn);
	struct drm_connector_state *conn_state =
		drm_atomic_get_new_connector_state(state, connector);
	struct drm_crtc *crtc;
	struct drm_crtc_state *crtc_state;
	const struct drm_display_mode *mode;
	struct drm_framebuffer *fb;

	DPU_DEBUG("[atomic_check:%d]\n", connector->base.id);

	if (!conn_state || !conn_state->connector) {
		DPU_ERROR("invalid connector state\n");
		return -EINVAL;
	}

	crtc = conn_state->crtc;
	if (!crtc)
		return 0;

	if (!conn_state->writeback_job || !conn_state->writeback_job->fb)
		return 0;

	crtc_state = drm_atomic_get_crtc_state(state, crtc);
	if (IS_ERR(crtc_state))
		return PTR_ERR(crtc_state);

	mode = &crtc_state->mode;

	fb = conn_state->writeback_job->fb;

	DPU_DEBUG("[fb_id:%u][fb:%u,%u][mode:\"%s\":%ux%u]\n", fb->base.id, fb->width, fb->height,
		  mode->name, mode->hdisplay, mode->vdisplay);

	if (fb->width != mode->hdisplay) {
		DPU_ERROR("invalid fb w=%d, mode w=%d\n", fb->width, mode->hdisplay);
		return -EINVAL;
	} else if (fb->height != mode->vdisplay) {
		DPU_ERROR("invalid fb h=%d, mode h=%d\n", fb->height, mode->vdisplay);
		return -EINVAL;
	} else if (fb->width > dpu_wb_conn->maxlinewidth) {
		DPU_ERROR("invalid fb w=%d, maxlinewidth=%u\n",
			  fb->width, dpu_wb_conn->maxlinewidth);
		return -EINVAL;
	}

	return drm_atomic_helper_check_wb_connector_state(conn_state->connector, conn_state->state);
}

static const struct drm_connector_funcs dpu_wb_conn_funcs = {
	.reset = drm_atomic_helper_connector_reset,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int dpu_wb_conn_prepare_job(struct drm_writeback_connector *connector,
		struct drm_writeback_job *job)
{

	struct dpu_wb_connector *dpu_wb_conn = to_dpu_wb_conn(connector);

	if (!job->fb)
		return 0;

	dpu_encoder_prepare_wb_job(dpu_wb_conn->wb_enc, job);

	return 0;
}

static void dpu_wb_conn_cleanup_job(struct drm_writeback_connector *connector,
		struct drm_writeback_job *job)
{
	struct dpu_wb_connector *dpu_wb_conn = to_dpu_wb_conn(connector);

	if (!job->fb)
		return;

	dpu_encoder_cleanup_wb_job(dpu_wb_conn->wb_enc, job);
}

static const struct drm_connector_helper_funcs dpu_wb_conn_helper_funcs = {
	.get_modes = dpu_wb_conn_get_modes,
	.atomic_check = dpu_wb_conn_atomic_check,
	.prepare_writeback_job = dpu_wb_conn_prepare_job,
	.cleanup_writeback_job = dpu_wb_conn_cleanup_job,
};

int dpu_writeback_init(struct drm_device *dev, struct drm_encoder *enc,
		const u32 *format_list, u32 num_formats, u32 maxlinewidth)
{
	struct dpu_wb_connector *dpu_wb_conn;
	int rc = 0;

	dpu_wb_conn = devm_kzalloc(dev->dev, sizeof(*dpu_wb_conn), GFP_KERNEL);
	if (!dpu_wb_conn)
		return -ENOMEM;

	dpu_wb_conn->maxlinewidth = maxlinewidth;

	drm_connector_helper_add(&dpu_wb_conn->base.base, &dpu_wb_conn_helper_funcs);

	/* DPU initializes the encoder and sets it up completely for writeback
	 * cases and hence should use the new API drm_writeback_connector_init_with_encoder
	 * to initialize the writeback connector
	 */
	rc = drm_writeback_connector_init_with_encoder(dev, &dpu_wb_conn->base, enc,
			&dpu_wb_conn_funcs, format_list, num_formats);

	if (!rc)
		dpu_wb_conn->wb_enc = enc;

	return rc;
}
