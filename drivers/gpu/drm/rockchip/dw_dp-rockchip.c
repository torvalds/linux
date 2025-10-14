// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Rockchip Electronics Co., Ltd.
 *
 * Author: Zhang Yubing <yubing.zhang@rock-chips.com>
 * Author: Andy Yan <andy.yan@rock-chips.com>
 */

#include <linux/component.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <drm/bridge/dw_dp.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_bridge_connector.h>
#include <drm/drm_of.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>

#include <linux/media-bus-format.h>
#include <linux/videodev2.h>

#include "rockchip_drm_drv.h"
#include "rockchip_drm_vop.h"

struct rockchip_dw_dp {
	struct dw_dp *base;
	struct device *dev;
	struct rockchip_encoder encoder;
};

static int dw_dp_encoder_atomic_check(struct drm_encoder *encoder,
				      struct drm_crtc_state *crtc_state,
				      struct drm_connector_state *conn_state)
{
	struct rockchip_crtc_state *s = to_rockchip_crtc_state(crtc_state);
	struct drm_atomic_state *state = conn_state->state;
	struct drm_display_info *di = &conn_state->connector->display_info;
	struct drm_bridge *bridge  = drm_bridge_chain_get_first_bridge(encoder);
	struct drm_bridge_state *bridge_state = drm_atomic_get_new_bridge_state(state, bridge);
	u32 bus_format = bridge_state->input_bus_cfg.format;

	switch (bus_format) {
	case MEDIA_BUS_FMT_UYYVYY10_0_5X30:
	case MEDIA_BUS_FMT_UYYVYY8_0_5X24:
		s->output_mode = ROCKCHIP_OUT_MODE_YUV420;
		break;
	case MEDIA_BUS_FMT_YUYV10_1X20:
	case MEDIA_BUS_FMT_YUYV8_1X16:
		s->output_mode = ROCKCHIP_OUT_MODE_S888_DUMMY;
		break;
	case MEDIA_BUS_FMT_RGB101010_1X30:
	case MEDIA_BUS_FMT_RGB888_1X24:
	case MEDIA_BUS_FMT_RGB666_1X24_CPADHI:
	case MEDIA_BUS_FMT_YUV10_1X30:
	case MEDIA_BUS_FMT_YUV8_1X24:
	default:
		s->output_mode = ROCKCHIP_OUT_MODE_AAAA;
		break;
	}

	s->output_type = DRM_MODE_CONNECTOR_DisplayPort;
	s->bus_format = bus_format;
	s->bus_flags = di->bus_flags;
	s->color_space = V4L2_COLORSPACE_DEFAULT;

	return 0;
}

static const struct drm_encoder_helper_funcs dw_dp_encoder_helper_funcs = {
	.atomic_check		= dw_dp_encoder_atomic_check,
};

static int dw_dp_rockchip_bind(struct device *dev, struct device *master, void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct dw_dp_plat_data plat_data;
	struct drm_device *drm_dev = data;
	struct rockchip_dw_dp *dp;
	struct drm_encoder *encoder;
	struct drm_connector *connector;
	int ret;

	dp = devm_kzalloc(dev, sizeof(*dp), GFP_KERNEL);
	if (!dp)
		return -ENOMEM;

	dp->dev = dev;
	platform_set_drvdata(pdev, dp);

	plat_data.max_link_rate = 810000;
	encoder = &dp->encoder.encoder;
	encoder->possible_crtcs = drm_of_find_possible_crtcs(drm_dev, dev->of_node);
	rockchip_drm_encoder_set_crtc_endpoint_id(&dp->encoder, dev->of_node, 0, 0);

	ret = drmm_encoder_init(drm_dev, encoder, NULL, DRM_MODE_ENCODER_TMDS, NULL);
	if (ret)
		return ret;
	drm_encoder_helper_add(encoder, &dw_dp_encoder_helper_funcs);

	dp->base = dw_dp_bind(dev, encoder, &plat_data);
	if (IS_ERR(dp->base)) {
		ret = PTR_ERR(dp->base);
		return ret;
	}

	connector = drm_bridge_connector_init(drm_dev, encoder);
	if (IS_ERR(connector)) {
		ret = PTR_ERR(connector);
		return dev_err_probe(dev, ret, "Failed to init bridge connector");
	}

	drm_connector_attach_encoder(connector, encoder);

	return 0;
}

static const struct component_ops dw_dp_rockchip_component_ops = {
	.bind = dw_dp_rockchip_bind,
};

static int dw_dp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	return component_add(dev, &dw_dp_rockchip_component_ops);
}

static void dw_dp_remove(struct platform_device *pdev)
{
	struct rockchip_dw_dp *dp = platform_get_drvdata(pdev);

	component_del(dp->dev, &dw_dp_rockchip_component_ops);
}

static const struct of_device_id dw_dp_of_match[] = {
	{ .compatible = "rockchip,rk3588-dp", },
	{}
};
MODULE_DEVICE_TABLE(of, dw_dp_of_match);

struct platform_driver dw_dp_driver = {
	.probe	= dw_dp_probe,
	.remove = dw_dp_remove,
	.driver = {
		.name = "dw-dp",
		.of_match_table = dw_dp_of_match,
	},
};
