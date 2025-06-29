// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2024 NXP
 */

#include <linux/of.h>
#include <linux/of_graph.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_bridge_connector.h>
#include <drm/drm_connector.h>
#include <drm/drm_crtc.h>
#include <drm/drm_device.h>
#include <drm/drm_encoder.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_mode_config.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>
#include <drm/drm_vblank.h>

#include "dc-de.h"
#include "dc-drv.h"
#include "dc-kms.h"

static const struct drm_mode_config_funcs dc_drm_mode_config_funcs = {
	.fb_create = drm_gem_fb_create,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

static int dc_kms_init_encoder_per_crtc(struct dc_drm_device *dc_drm,
					int crtc_index)
{
	struct dc_crtc *dc_crtc = &dc_drm->dc_crtc[crtc_index];
	struct drm_device *drm = &dc_drm->base;
	struct drm_crtc *crtc = &dc_crtc->base;
	struct drm_connector *connector;
	struct device *dev = drm->dev;
	struct drm_encoder *encoder;
	struct drm_bridge *bridge;
	int ret;

	bridge = devm_drm_of_get_bridge(dev, dc_crtc->de->tc->dev->of_node,
					0, 0);
	if (IS_ERR(bridge)) {
		ret = PTR_ERR(bridge);
		if (ret == -ENODEV)
			return 0;

		return dev_err_probe(dev, ret,
				     "failed to find bridge for CRTC%u\n",
				     crtc->index);
	}

	encoder = &dc_drm->encoder[crtc_index];
	ret = drm_simple_encoder_init(drm, encoder, DRM_MODE_ENCODER_NONE);
	if (ret) {
		dev_err(dev, "failed to initialize encoder for CRTC%u: %d\n",
			crtc->index, ret);
		return ret;
	}

	encoder->possible_crtcs = drm_crtc_mask(crtc);

	ret = drm_bridge_attach(encoder, bridge, NULL,
				DRM_BRIDGE_ATTACH_NO_CONNECTOR);
	if (ret) {
		dev_err(dev,
			"failed to attach bridge to encoder for CRTC%u: %d\n",
			crtc->index, ret);
		return ret;
	}

	connector = drm_bridge_connector_init(drm, encoder);
	if (IS_ERR(connector)) {
		ret = PTR_ERR(connector);
		dev_err(dev, "failed to init bridge connector for CRTC%u: %d\n",
			crtc->index, ret);
		return ret;
	}

	ret = drm_connector_attach_encoder(connector, encoder);
	if (ret)
		dev_err(dev,
			"failed to attach encoder to connector for CRTC%u: %d\n",
			crtc->index, ret);

	return ret;
}

int dc_kms_init(struct dc_drm_device *dc_drm)
{
	struct drm_device *drm = &dc_drm->base;
	int ret, i;

	ret = drmm_mode_config_init(drm);
	if (ret)
		return ret;

	drm->mode_config.min_width = 60;
	drm->mode_config.min_height = 60;
	drm->mode_config.max_width = 8192;
	drm->mode_config.max_height = 8192;
	drm->mode_config.funcs = &dc_drm_mode_config_funcs;

	drm->vblank_disable_immediate = true;
	drm->max_vblank_count = DC_FRAMEGEN_MAX_FRAME_INDEX;

	for (i = 0; i < DC_DISPLAYS; i++) {
		ret = dc_crtc_init(dc_drm, i);
		if (ret)
			return ret;

		ret = dc_kms_init_encoder_per_crtc(dc_drm, i);
		if (ret)
			return ret;
	}

	for (i = 0; i < DC_DISPLAYS; i++) {
		ret = dc_crtc_post_init(dc_drm, i);
		if (ret)
			return ret;
	}

	ret = drm_vblank_init(drm, DC_DISPLAYS);
	if (ret) {
		dev_err(drm->dev, "failed to init vblank support: %d\n", ret);
		return ret;
	}

	drm_mode_config_reset(drm);

	drm_kms_helper_poll_init(drm);

	return 0;
}

void dc_kms_uninit(struct dc_drm_device *dc_drm)
{
	drm_kms_helper_poll_fini(&dc_drm->base);
}
