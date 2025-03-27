// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2024 Hisilicon Limited.

#include <linux/io.h>

#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_modes.h>
#include <drm/drm_drv.h>
#include <drm/drm_edid.h>

#include "hibmc_drm_drv.h"
#include "dp/dp_hw.h"

static int hibmc_dp_connector_get_modes(struct drm_connector *connector)
{
	int count;

	count = drm_add_modes_noedid(connector, connector->dev->mode_config.max_width,
				     connector->dev->mode_config.max_height);
	drm_set_preferred_mode(connector, 1024, 768); // temporary implementation

	return count;
}

static const struct drm_connector_helper_funcs hibmc_dp_conn_helper_funcs = {
	.get_modes = hibmc_dp_connector_get_modes,
};

static const struct drm_connector_funcs hibmc_dp_conn_funcs = {
	.reset = drm_atomic_helper_connector_reset,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static inline int hibmc_dp_prepare(struct hibmc_dp *dp, struct drm_display_mode *mode)
{
	int ret;

	hibmc_dp_display_en(dp, false);

	ret = hibmc_dp_mode_set(dp, mode);
	if (ret)
		drm_err(dp->drm_dev, "hibmc dp mode set failed: %d\n", ret);

	return ret;
}

static void hibmc_dp_encoder_enable(struct drm_encoder *drm_encoder,
				    struct drm_atomic_state *state)
{
	struct hibmc_dp *dp = container_of(drm_encoder, struct hibmc_dp, encoder);
	struct drm_display_mode *mode = &drm_encoder->crtc->state->mode;

	if (hibmc_dp_prepare(dp, mode))
		return;

	hibmc_dp_display_en(dp, true);
}

static void hibmc_dp_encoder_disable(struct drm_encoder *drm_encoder,
				     struct drm_atomic_state *state)
{
	struct hibmc_dp *dp = container_of(drm_encoder, struct hibmc_dp, encoder);

	hibmc_dp_display_en(dp, false);
}

static const struct drm_encoder_helper_funcs hibmc_dp_encoder_helper_funcs = {
	.atomic_enable = hibmc_dp_encoder_enable,
	.atomic_disable = hibmc_dp_encoder_disable,
};

int hibmc_dp_init(struct hibmc_drm_private *priv)
{
	struct drm_device *dev = &priv->dev;
	struct drm_crtc *crtc = &priv->crtc;
	struct hibmc_dp *dp = &priv->dp;
	struct drm_connector *connector = &dp->connector;
	struct drm_encoder *encoder = &dp->encoder;
	int ret;

	dp->mmio = priv->mmio;
	dp->drm_dev = dev;

	ret = hibmc_dp_hw_init(&priv->dp);
	if (ret) {
		drm_err(dev, "hibmc dp hw init failed: %d\n", ret);
		return ret;
	}

	hibmc_dp_display_en(&priv->dp, false);

	encoder->possible_crtcs = drm_crtc_mask(crtc);
	ret = drmm_encoder_init(dev, encoder, NULL, DRM_MODE_ENCODER_TMDS, NULL);
	if (ret) {
		drm_err(dev, "init dp encoder failed: %d\n", ret);
		return ret;
	}

	drm_encoder_helper_add(encoder, &hibmc_dp_encoder_helper_funcs);

	ret = drm_connector_init(dev, connector, &hibmc_dp_conn_funcs,
				 DRM_MODE_CONNECTOR_DisplayPort);
	if (ret) {
		drm_err(dev, "init dp connector failed: %d\n", ret);
		return ret;
	}

	drm_connector_helper_add(connector, &hibmc_dp_conn_helper_funcs);

	drm_connector_attach_encoder(connector, encoder);

	return 0;
}
