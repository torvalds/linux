// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author:
 *      Sandy Huang <hjc@rock-chips.com>
 */

#include <linux/component.h>
#include <linux/media-bus-format.h>
#include <linux/of_graph.h>

#include <drm/display/drm_dp_helper.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_bridge_connector.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>

#include "rockchip_drm_drv.h"
#include "rockchip_rgb.h"

struct rockchip_rgb {
	struct device *dev;
	struct drm_device *drm_dev;
	struct drm_bridge *bridge;
	struct rockchip_encoder encoder;
	struct drm_connector connector;
	int output_mode;
};

static int
rockchip_rgb_encoder_atomic_check(struct drm_encoder *encoder,
				   struct drm_crtc_state *crtc_state,
				   struct drm_connector_state *conn_state)
{
	struct rockchip_crtc_state *s = to_rockchip_crtc_state(crtc_state);
	struct drm_connector *connector = conn_state->connector;
	struct drm_display_info *info = &connector->display_info;
	u32 bus_format;

	if (info->num_bus_formats)
		bus_format = info->bus_formats[0];
	else
		bus_format = MEDIA_BUS_FMT_RGB888_1X24;

	switch (bus_format) {
	case MEDIA_BUS_FMT_RGB666_1X18:
		s->output_mode = ROCKCHIP_OUT_MODE_P666;
		break;
	case MEDIA_BUS_FMT_RGB565_1X16:
		s->output_mode = ROCKCHIP_OUT_MODE_P565;
		break;
	case MEDIA_BUS_FMT_RGB888_1X24:
	case MEDIA_BUS_FMT_RGB666_1X24_CPADHI:
	default:
		s->output_mode = ROCKCHIP_OUT_MODE_P888;
		break;
	}

	s->output_type = DRM_MODE_CONNECTOR_LVDS;

	return 0;
}

static const
struct drm_encoder_helper_funcs rockchip_rgb_encoder_helper_funcs = {
	.atomic_check = rockchip_rgb_encoder_atomic_check,
};

struct rockchip_rgb *rockchip_rgb_init(struct device *dev,
				       struct drm_crtc *crtc,
				       struct drm_device *drm_dev,
				       int video_port)
{
	struct rockchip_rgb *rgb;
	struct drm_encoder *encoder;
	struct device_node *port, *endpoint;
	u32 endpoint_id;
	int ret = 0, child_count = 0;
	struct drm_panel *panel;
	struct drm_bridge *bridge;
	struct drm_connector *connector;

	rgb = devm_kzalloc(dev, sizeof(*rgb), GFP_KERNEL);
	if (!rgb)
		return ERR_PTR(-ENOMEM);

	rgb->dev = dev;
	rgb->drm_dev = drm_dev;

	port = of_graph_get_port_by_id(dev->of_node, video_port);
	if (!port)
		return ERR_PTR(-EINVAL);

	for_each_child_of_node(port, endpoint) {
		if (of_property_read_u32(endpoint, "reg", &endpoint_id))
			endpoint_id = 0;

		/* if subdriver (> 0) or error case (< 0), ignore entry */
		if (rockchip_drm_endpoint_is_subdriver(endpoint) != 0)
			continue;

		child_count++;
		ret = drm_of_find_panel_or_bridge(dev->of_node, video_port,
						  endpoint_id, &panel, &bridge);
		if (!ret) {
			of_node_put(endpoint);
			break;
		}
	}

	of_node_put(port);

	/* if the rgb output is not connected to anything, just return */
	if (!child_count)
		return NULL;

	if (ret < 0) {
		if (ret != -EPROBE_DEFER)
			DRM_DEV_ERROR(dev, "failed to find panel or bridge %d\n", ret);
		return ERR_PTR(ret);
	}

	encoder = &rgb->encoder.encoder;
	encoder->possible_crtcs = drm_crtc_mask(crtc);

	ret = drm_simple_encoder_init(drm_dev, encoder, DRM_MODE_ENCODER_NONE);
	if (ret < 0) {
		DRM_DEV_ERROR(drm_dev->dev,
			      "failed to initialize encoder: %d\n", ret);
		return ERR_PTR(ret);
	}

	drm_encoder_helper_add(encoder, &rockchip_rgb_encoder_helper_funcs);

	if (panel) {
		bridge = drm_panel_bridge_add_typed(panel,
						    DRM_MODE_CONNECTOR_LVDS);
		if (IS_ERR(bridge))
			return ERR_CAST(bridge);
	}

	rgb->bridge = bridge;

	ret = drm_bridge_attach(encoder, rgb->bridge, NULL,
				DRM_BRIDGE_ATTACH_NO_CONNECTOR);
	if (ret)
		goto err_free_encoder;

	connector = &rgb->connector;
	connector = drm_bridge_connector_init(rgb->drm_dev, encoder);
	if (IS_ERR(connector)) {
		DRM_DEV_ERROR(drm_dev->dev,
			      "failed to initialize bridge connector: %pe\n",
			      connector);
		ret = PTR_ERR(connector);
		goto err_free_encoder;
	}

	rgb->encoder.crtc_endpoint_id = endpoint_id;

	ret = drm_connector_attach_encoder(connector, encoder);
	if (ret < 0) {
		DRM_DEV_ERROR(drm_dev->dev,
			      "failed to attach encoder: %d\n", ret);
		goto err_free_connector;
	}

	return rgb;

err_free_connector:
	drm_connector_cleanup(connector);
err_free_encoder:
	drm_encoder_cleanup(encoder);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(rockchip_rgb_init);

void rockchip_rgb_fini(struct rockchip_rgb *rgb)
{
	drm_panel_bridge_remove(rgb->bridge);
	drm_connector_cleanup(&rgb->connector);
	drm_encoder_cleanup(&rgb->encoder.encoder);
}
EXPORT_SYMBOL_GPL(rockchip_rgb_fini);
