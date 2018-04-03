/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author:
 *      Sandy Huang <hjc@rock-chips.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <drm/drmP.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_dp_helper.h>
#include <drm/drm_panel.h>
#include <drm/drm_of.h>

#include <linux/component.h>
#include <linux/of_graph.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>

#include "rockchip_drm_drv.h"
#include "rockchip_drm_vop.h"

#define HIWORD_UPDATE(v, l, h)	(((v) << (l)) | (GENMASK(h, l) << 16))
#define PX30_GRF_PD_VO_CON1	0x0438
#define PX30_LCDC_DCLK_INV(v)	HIWORD_UPDATE(v, 4, 4)
#define PX30_RGB_SYNC_BYPASS(v)	HIWORD_UPDATE(v, 3, 3)
#define PX30_RGB_VOP_SEL(v)	HIWORD_UPDATE(v, 2, 2)

#define connector_to_rgb(c) container_of(c, struct rockchip_rgb, connector)
#define encoder_to_rgb(c) container_of(c, struct rockchip_rgb, encoder)

struct rockchip_rgb {
	struct device *dev;
	struct drm_device *drm_dev;
	struct drm_panel *panel;
	struct drm_bridge *bridge;
	struct drm_connector connector;
	struct drm_encoder encoder;
	struct dev_pin_info *pins;
	int output_mode;
	struct regmap *grf;
};

static inline int name_to_output_mode(const char *s)
{
	static const struct {
		const char *name;
		int format;
	} formats[] = {
		{ "p888", ROCKCHIP_OUT_MODE_P888 },
		{ "p666", ROCKCHIP_OUT_MODE_P666 },
		{ "p565", ROCKCHIP_OUT_MODE_P565 },
		{ "s888", ROCKCHIP_OUT_MODE_S888 },
		{ "s888_dummy", ROCKCHIP_OUT_MODE_S888_DUMMY }
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(formats); i++)
		if (!strncmp(s, formats[i].name, strlen(formats[i].name)))
			return formats[i].format;

	return -EINVAL;
}

static enum drm_connector_status
rockchip_rgb_connector_detect(struct drm_connector *connector, bool force)
{
	return connector_status_connected;
}

static const struct drm_connector_funcs rockchip_rgb_connector_funcs = {
	.dpms = drm_atomic_helper_connector_dpms,
	.detect = rockchip_rgb_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int rockchip_rgb_connector_get_modes(struct drm_connector *connector)
{
	struct rockchip_rgb *rgb = connector_to_rgb(connector);
	struct drm_panel *panel = rgb->panel;

	return drm_panel_get_modes(panel);
}

static struct drm_encoder *
rockchip_rgb_connector_best_encoder(struct drm_connector *connector)
{
	struct rockchip_rgb *rgb = connector_to_rgb(connector);

	return &rgb->encoder;
}

static const
struct drm_connector_helper_funcs rockchip_rgb_connector_helper_funcs = {
	.get_modes = rockchip_rgb_connector_get_modes,
	.best_encoder = rockchip_rgb_connector_best_encoder,
};

static void rockchip_rgb_encoder_enable(struct drm_encoder *encoder)
{
	struct rockchip_rgb *rgb = encoder_to_rgb(encoder);

	if (rgb->grf) {
		int pipe = drm_of_encoder_active_endpoint_id(rgb->dev->of_node,
							     encoder);
		regmap_write(rgb->grf, PX30_GRF_PD_VO_CON1,
			     PX30_RGB_VOP_SEL(pipe));
		regmap_write(rgb->grf, PX30_GRF_PD_VO_CON1,
			     PX30_RGB_SYNC_BYPASS(1));
	}

	drm_panel_prepare(rgb->panel);
	/* iomux to LCD data/sync mode */
	if (rgb->pins && !IS_ERR(rgb->pins->default_state))
		pinctrl_select_state(rgb->pins->p, rgb->pins->default_state);

	drm_panel_enable(rgb->panel);
}

static void rockchip_rgb_encoder_disable(struct drm_encoder *encoder)
{
	struct rockchip_rgb *rgb = encoder_to_rgb(encoder);

	drm_panel_disable(rgb->panel);
	drm_panel_unprepare(rgb->panel);
}

static int
rockchip_rgb_encoder_atomic_check(struct drm_encoder *encoder,
				   struct drm_crtc_state *crtc_state,
				   struct drm_connector_state *conn_state)
{
	struct rockchip_crtc_state *s = to_rockchip_crtc_state(crtc_state);
	struct rockchip_rgb *rgb = encoder_to_rgb(encoder);
	struct drm_connector *connector = conn_state->connector;
	struct drm_display_info *info = &connector->display_info;

	s->output_mode = rgb->output_mode;
	s->output_type = DRM_MODE_CONNECTOR_LVDS;

	if (info->num_bus_formats)
		s->bus_format = info->bus_formats[0];
	else
		s->bus_format = MEDIA_BUS_FMT_RGB888_1X24;

	if (s->bus_format == MEDIA_BUS_FMT_RGB666_1X18)
		s->output_mode = ROCKCHIP_OUT_MODE_P666;
	else if (s->bus_format == MEDIA_BUS_FMT_RGB565_1X16)
		s->output_mode = ROCKCHIP_OUT_MODE_P565;
	else
		s->output_mode = ROCKCHIP_OUT_MODE_P888;

	return 0;
}

static const
struct drm_encoder_helper_funcs rockchip_rgb_encoder_helper_funcs = {
	.enable = rockchip_rgb_encoder_enable,
	.disable = rockchip_rgb_encoder_disable,
	.atomic_check = rockchip_rgb_encoder_atomic_check,
};

static const struct drm_encoder_funcs rockchip_rgb_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static const struct of_device_id rockchip_rgb_dt_ids[] = {
	{
		.compatible = "rockchip,px30-rgb",
	}, {
		.compatible = "rockchip,rv1108-rgb",
	}, {
		.compatible = "rockchip,rk3066-rgb",
	}, {
		.compatible = "rockchip,rk3308-rgb",
	},
	{}
};

MODULE_DEVICE_TABLE(of, rockchip_rgb_dt_ids);

static int rockchip_rgb_bind(struct device *dev, struct device *master,
			     void *data)
{
	struct rockchip_rgb *rgb = dev_get_drvdata(dev);
	struct drm_device *drm_dev = data;
	struct drm_encoder *encoder;
	struct drm_connector *connector;
	struct device_node *remote = NULL;
	struct device_node  *port, *endpoint;
	u32 endpoint_id;
	const char *name;
	int ret = 0, child_count = 0;

	rgb->drm_dev = drm_dev;
	port = of_graph_get_port_by_id(dev->of_node, 1);
	if (!port) {
		DRM_DEV_ERROR(dev,
			      "can't found port point, please init rgb panel port!\n");
		return -EINVAL;
	}
	for_each_child_of_node(port, endpoint) {
		child_count++;
		if (of_property_read_u32(endpoint, "reg", &endpoint_id))
			endpoint_id = 0;
		ret = drm_of_find_panel_or_bridge(dev->of_node, 1, endpoint_id,
						  &rgb->panel, &rgb->bridge);
		if (!ret)
			break;
	}
	if (!child_count) {
		DRM_DEV_ERROR(dev, "rgb port does not have any children\n");
		ret = -EINVAL;
		goto err_put_port;
	} else if (ret) {
		DRM_DEV_ERROR(dev, "failed to find panel and bridge node\n");
		ret = -EPROBE_DEFER;
		goto err_put_port;
	}
	if (rgb->panel)
		remote = rgb->panel->dev->of_node;
	else
		remote = rgb->bridge->of_node;
	if (of_property_read_string(remote, "rgb-mode", &name))
		/* default set it as output mode P888 */
		rgb->output_mode = ROCKCHIP_OUT_MODE_P888;
	else
		rgb->output_mode = name_to_output_mode(name);
	if (rgb->output_mode < 0) {
		DRM_DEV_ERROR(dev, "invalid rockchip,rgb-mode [%s]\n", name);
		ret = rgb->output_mode;
		goto err_put_remote;
	}

	encoder = &rgb->encoder;
	encoder->possible_crtcs = drm_of_find_possible_crtcs(drm_dev,
							     dev->of_node);

	ret = drm_encoder_init(drm_dev, encoder, &rockchip_rgb_encoder_funcs,
			       DRM_MODE_ENCODER_NONE, NULL);
	if (ret < 0) {
		DRM_DEV_ERROR(drm_dev->dev,
			      "failed to initialize encoder: %d\n", ret);
		goto err_put_remote;
	}

	drm_encoder_helper_add(encoder, &rockchip_rgb_encoder_helper_funcs);

	if (rgb->panel) {
		connector = &rgb->connector;
		connector->dpms = DRM_MODE_DPMS_OFF;
		ret = drm_connector_init(drm_dev, connector,
					 &rockchip_rgb_connector_funcs,
					 DRM_MODE_CONNECTOR_Unknown);
		if (ret < 0) {
			DRM_DEV_ERROR(drm_dev->dev,
				      "failed to initialize connector: %d\n",
				      ret);
			goto err_free_encoder;
		}

		drm_connector_helper_add(connector,
					 &rockchip_rgb_connector_helper_funcs);

		ret = drm_mode_connector_attach_encoder(connector, encoder);
		if (ret < 0) {
			DRM_DEV_ERROR(drm_dev->dev,
				      "failed to attach encoder: %d\n", ret);
			goto err_free_connector;
		}

		ret = drm_panel_attach(rgb->panel, connector);
		if (ret < 0) {
			DRM_DEV_ERROR(drm_dev->dev,
				      "failed to attach panel: %d\n", ret);
			goto err_free_connector;
		}
		connector->port = dev->of_node;
	} else {
		rgb->bridge->encoder = encoder;
		ret = drm_bridge_attach(drm_dev, rgb->bridge);
		if (ret) {
			DRM_DEV_ERROR(drm_dev->dev,
				      "failed to attach bridge: %d\n", ret);
			goto err_free_encoder;
		}
		encoder->bridge = rgb->bridge;
	}

	of_node_put(remote);
	of_node_put(port);

	return 0;

err_free_connector:
	drm_connector_cleanup(connector);
err_free_encoder:
	drm_encoder_cleanup(encoder);
err_put_remote:
	of_node_put(remote);
err_put_port:
	of_node_put(port);

	return ret;
}

static void rockchip_rgb_unbind(struct device *dev, struct device *master,
				void *data)
{
	struct rockchip_rgb *rgb = dev_get_drvdata(dev);

	rockchip_rgb_encoder_disable(&rgb->encoder);
	if (rgb->panel) {
		drm_panel_detach(rgb->panel);
		drm_connector_cleanup(&rgb->connector);
	}
	drm_encoder_cleanup(&rgb->encoder);
}

static const struct component_ops rockchip_rgb_component_ops = {
	.bind = rockchip_rgb_bind,
	.unbind = rockchip_rgb_unbind,
};

static int rockchip_rgb_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rockchip_rgb *rgb;
	const struct of_device_id *match;
	int ret;

	if (!dev->of_node) {
		DRM_DEV_ERROR(dev, "dev->of_node is null\n");
		return -ENODEV;
	}

	rgb = devm_kzalloc(&pdev->dev, sizeof(*rgb), GFP_KERNEL);
	if (!rgb)
		return -ENOMEM;

	rgb->dev = dev;
	match = of_match_node(rockchip_rgb_dt_ids, dev->of_node);
	if (!match) {
		DRM_DEV_ERROR(dev, "match node failed\n");
		return -ENODEV;
	}

	if (dev->parent && dev->parent->of_node) {
		rgb->grf = syscon_node_to_regmap(dev->parent->of_node);
		if (IS_ERR(rgb->grf)) {
			ret = PTR_ERR(rgb->grf);
			dev_err(dev, "Unable to get grf: %d\n", ret);
			return ret;
		}
	}

	rgb->pins = devm_kzalloc(rgb->dev, sizeof(*rgb->pins), GFP_KERNEL);
	if (!rgb->pins)
		return -ENOMEM;
	rgb->pins->p = devm_pinctrl_get(rgb->dev);
	if (IS_ERR(rgb->pins->p)) {
		DRM_DEV_ERROR(dev, "no pinctrl handle\n");
		devm_kfree(rgb->dev, rgb->pins);
		rgb->pins = NULL;
	} else {
		rgb->pins->default_state =
			pinctrl_lookup_state(rgb->pins->p, "lcdc");
		if (IS_ERR(rgb->pins->default_state)) {
			DRM_DEV_ERROR(dev, "no default pinctrl state\n");
			devm_kfree(rgb->dev, rgb->pins);
			rgb->pins = NULL;
		}
	}

	dev_set_drvdata(dev, rgb);
	ret = component_add(&pdev->dev, &rockchip_rgb_component_ops);
	if (ret < 0)
		DRM_DEV_ERROR(dev, "failed to add component\n");

	return ret;
}

static int rockchip_rgb_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &rockchip_rgb_component_ops);

	return 0;
}

struct platform_driver rockchip_rgb_driver = {
	.probe = rockchip_rgb_probe,
	.remove = rockchip_rgb_remove,
	.driver = {
		   .name = "rockchip-rgb",
		   .of_match_table = of_match_ptr(rockchip_rgb_dt_ids),
	},
};

module_platform_driver(rockchip_rgb_driver);

MODULE_AUTHOR("Sandy Huang <hjc@rock-chips.com>");
MODULE_DESCRIPTION("ROCKCHIP RGB Driver");
MODULE_LICENSE("GPL v2");
