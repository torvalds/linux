// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <drm/drm_edid.h>

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
	.prepare_writeback_job = dpu_wb_conn_prepare_job,
	.cleanup_writeback_job = dpu_wb_conn_cleanup_job,
};

int dpu_writeback_init(struct drm_device *dev, struct drm_encoder *enc,
		const u32 *format_list, u32 num_formats)
{
	struct dpu_wb_connector *dpu_wb_conn;
	int rc = 0;

	dpu_wb_conn = devm_kzalloc(dev->dev, sizeof(*dpu_wb_conn), GFP_KERNEL);

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
