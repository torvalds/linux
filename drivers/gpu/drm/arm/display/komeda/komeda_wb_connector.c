// SPDX-License-Identifier: GPL-2.0
/*
 * (C) COPYRIGHT 2018 ARM Limited. All rights reserved.
 * Author: James.Qian.Wang <james.qian.wang@arm.com>
 *
 */
#include "komeda_dev.h"
#include "komeda_kms.h"

static int
komeda_wb_init_data_flow(struct komeda_layer *wb_layer,
			 struct drm_connector_state *conn_st,
			 struct komeda_crtc_state *kcrtc_st,
			 struct komeda_data_flow_cfg *dflow)
{
	struct drm_framebuffer *fb = conn_st->writeback_job->fb;

	memset(dflow, 0, sizeof(*dflow));

	dflow->out_w = fb->width;
	dflow->out_h = fb->height;

	/* the write back data comes from the compiz */
	pipeline_composition_size(kcrtc_st, &dflow->in_w, &dflow->in_h);
	dflow->input.component = &wb_layer->base.pipeline->compiz->base;
	/* compiz doesn't output alpha */
	dflow->pixel_blend_mode = DRM_MODE_BLEND_PIXEL_NONE;
	dflow->rot = DRM_MODE_ROTATE_0;

	komeda_complete_data_flow_cfg(wb_layer, dflow, fb);

	return 0;
}

static int
komeda_wb_encoder_atomic_check(struct drm_encoder *encoder,
			       struct drm_crtc_state *crtc_st,
			       struct drm_connector_state *conn_st)
{
	struct komeda_crtc_state *kcrtc_st = to_kcrtc_st(crtc_st);
	struct drm_writeback_job *writeback_job = conn_st->writeback_job;
	struct komeda_layer *wb_layer;
	struct komeda_data_flow_cfg dflow;
	int err;

	if (!writeback_job || !writeback_job->fb) {
		return 0;
	}

	if (!crtc_st->active) {
		DRM_DEBUG_ATOMIC("Cannot write the composition result out on a inactive CRTC.\n");
		return -EINVAL;
	}

	wb_layer = to_kconn(to_wb_conn(conn_st->connector))->wb_layer;

	/*
	 * No need for a full modested when the only connector changed is the
	 * writeback connector.
	 */
	if (crtc_st->connectors_changed &&
	    is_only_changed_connector(crtc_st, conn_st->connector))
		crtc_st->connectors_changed = false;

	err = komeda_wb_init_data_flow(wb_layer, conn_st, kcrtc_st, &dflow);
	if (err)
		return err;

	if (dflow.en_split)
		err = komeda_build_wb_split_data_flow(wb_layer,
				conn_st, kcrtc_st, &dflow);
	else
		err = komeda_build_wb_data_flow(wb_layer,
				conn_st, kcrtc_st, &dflow);

	return err;
}

static const struct drm_encoder_helper_funcs komeda_wb_encoder_helper_funcs = {
	.atomic_check = komeda_wb_encoder_atomic_check,
};

static int
komeda_wb_connector_get_modes(struct drm_connector *connector)
{
	return 0;
}

static enum drm_mode_status
komeda_wb_connector_mode_valid(struct drm_connector *connector,
			       struct drm_display_mode *mode)
{
	struct drm_device *dev = connector->dev;
	struct drm_mode_config *mode_config = &dev->mode_config;
	int w = mode->hdisplay, h = mode->vdisplay;

	if ((w < mode_config->min_width) || (w > mode_config->max_width))
		return MODE_BAD_HVALUE;

	if ((h < mode_config->min_height) || (h > mode_config->max_height))
		return MODE_BAD_VVALUE;

	return MODE_OK;
}

static const struct drm_connector_helper_funcs komeda_wb_conn_helper_funcs = {
	.get_modes	= komeda_wb_connector_get_modes,
	.mode_valid	= komeda_wb_connector_mode_valid,
};

static enum drm_connector_status
komeda_wb_connector_detect(struct drm_connector *connector, bool force)
{
	return connector_status_connected;
}

static int
komeda_wb_connector_fill_modes(struct drm_connector *connector,
			       uint32_t maxX, uint32_t maxY)
{
	return 0;
}

static void komeda_wb_connector_destroy(struct drm_connector *connector)
{
	drm_connector_cleanup(connector);
	kfree(to_kconn(to_wb_conn(connector)));
}

static const struct drm_connector_funcs komeda_wb_connector_funcs = {
	.reset			= drm_atomic_helper_connector_reset,
	.detect			= komeda_wb_connector_detect,
	.fill_modes		= komeda_wb_connector_fill_modes,
	.destroy		= komeda_wb_connector_destroy,
	.atomic_duplicate_state	= drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_connector_destroy_state,
};

static int komeda_wb_connector_add(struct komeda_kms_dev *kms,
				   struct komeda_crtc *kcrtc)
{
	struct komeda_dev *mdev = kms->base.dev_private;
	struct komeda_wb_connector *kwb_conn;
	struct drm_writeback_connector *wb_conn;
	u32 *formats, n_formats = 0;
	int err;

	if (!kcrtc->master->wb_layer)
		return 0;

	kwb_conn = kzalloc(sizeof(*kwb_conn), GFP_KERNEL);
	if (!kwb_conn)
		return -ENOMEM;

	kwb_conn->wb_layer = kcrtc->master->wb_layer;

	wb_conn = &kwb_conn->base;
	wb_conn->encoder.possible_crtcs = BIT(drm_crtc_index(&kcrtc->base));

	formats = komeda_get_layer_fourcc_list(&mdev->fmt_tbl,
					       kwb_conn->wb_layer->layer_type,
					       &n_formats);

	err = drm_writeback_connector_init(&kms->base, wb_conn,
					   &komeda_wb_connector_funcs,
					   &komeda_wb_encoder_helper_funcs,
					   formats, n_formats);
	komeda_put_fourcc_list(formats);
	if (err)
		return err;

	drm_connector_helper_add(&wb_conn->base, &komeda_wb_conn_helper_funcs);

	kcrtc->wb_conn = kwb_conn;

	return 0;
}

int komeda_kms_add_wb_connectors(struct komeda_kms_dev *kms,
				 struct komeda_dev *mdev)
{
	int i, err;

	for (i = 0; i < kms->n_crtcs; i++) {
		err = komeda_wb_connector_add(kms, &kms->crtcs[i]);
		if (err)
			return err;
	}

	return 0;
}
