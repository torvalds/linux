/*
 * Copyright (c) 2017 Rockchip Electronics Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "rk618_output.h"

static inline struct rk618_output *encoder_to_output(struct drm_encoder *e)
{
	return container_of(e, struct rk618_output, encoder);
}

static inline
struct rk618_output *connector_to_output(struct drm_connector *c)
{
	return container_of(c, struct rk618_output, connector);
}

static inline struct rk618_output *bridge_to_output(struct drm_bridge *b)
{
	return container_of(b, struct rk618_output, bridge);
}

static struct drm_encoder *
rk618_output_connector_best_encoder(struct drm_connector *connector)
{
	struct rk618_output *output = connector_to_output(connector);

	return &output->encoder;
}

static int rk618_output_connector_get_modes(struct drm_connector *connector)
{
	struct rk618_output *output = connector_to_output(connector);
	struct drm_display_info *info = &connector->display_info;
	struct drm_display_mode *mode;
	int num_modes;

	num_modes = drm_panel_get_modes(output->panel);

	list_for_each_entry(mode, &connector->probed_modes, head) {
		if (mode->type & DRM_MODE_TYPE_PREFERRED)
			drm_mode_copy(&output->panel_mode, mode);
	}

	if (info->num_bus_formats)
		output->bus_format = info->bus_formats[0];

	return num_modes;
}

static const struct drm_connector_helper_funcs
rk618_output_connector_helper_funcs = {
	.get_modes = rk618_output_connector_get_modes,
	.best_encoder = rk618_output_connector_best_encoder,
};

static enum drm_connector_status
rk618_output_connector_detect(struct drm_connector *connector, bool force)
{
	return connector_status_connected;
}

static void rk618_output_connector_destroy(struct drm_connector *connector)
{
	struct rk618_output *output = connector_to_output(connector);

	drm_panel_detach(output->panel);
	drm_connector_cleanup(connector);
}

static const struct drm_connector_funcs rk618_output_connector_funcs = {
	.dpms = drm_atomic_helper_connector_dpms,
	.detect = rk618_output_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = rk618_output_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static void rk618_output_encoder_enable(struct drm_encoder *encoder)
{
	struct rk618_output *output = encoder_to_output(encoder);

	rk618_dither_frc_dclk_invert(output->parent);

	clk_set_parent(output->dither_clk, output->vif_clk);

	if (output->funcs->pre_enable)
		output->funcs->pre_enable(output);

	drm_panel_prepare(output->panel);

	if (output->funcs->enable)
		output->funcs->enable(output);

	drm_panel_enable(output->panel);
}

static void rk618_output_encoder_disable(struct drm_encoder *encoder)
{
	struct rk618_output *output = encoder_to_output(encoder);

	drm_panel_disable(output->panel);

	if (output->funcs->disable)
		output->funcs->disable(output);

	drm_panel_unprepare(output->panel);

	if (output->funcs->post_disable)
		output->funcs->post_disable(output);
}

static void rk618_output_encoder_mode_set(struct drm_encoder *encoder,
					  struct drm_display_mode *mode,
					  struct drm_display_mode *adjusted)
{
	struct rk618_output *output = encoder_to_output(encoder);

	drm_mode_copy(&output->panel_mode, adjusted);

	if (output->funcs->mode_set)
		output->funcs->mode_set(output, adjusted);
}

static int
rk618_output_encoder_atomic_check(struct drm_encoder *encoder,
				  struct drm_crtc_state *crtc_state,
				  struct drm_connector_state *conn_state)
{
	struct rockchip_crtc_state *s = to_rockchip_crtc_state(crtc_state);

	s->output_mode = ROCKCHIP_OUT_MODE_P888;
	s->output_type = DRM_MODE_CONNECTOR_LVDS;

	return 0;
}

static const struct drm_encoder_helper_funcs
rk618_output_encoder_helper_funcs = {
	.enable = rk618_output_encoder_enable,
	.disable = rk618_output_encoder_disable,
	.mode_set = rk618_output_encoder_mode_set,
	.atomic_check = rk618_output_encoder_atomic_check,
};

static const struct drm_encoder_funcs rk618_output_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static void rk618_output_bridge_pre_enable(struct drm_bridge *bridge)
{
	struct rk618_output *output = bridge_to_output(bridge);
	const struct drm_display_mode *src = &output->scale_mode;
	const struct drm_display_mode *dst = &output->panel_mode;
	unsigned long sclk_rate, dclk_rate = src->clock * 1000;
	long rate;

	sclk_rate = dclk_rate * dst->vdisplay * dst->htotal;
	do_div(sclk_rate, src->vdisplay * src->htotal);

	dev_dbg(output->dev, "dclk rate: %ld, sclk rate: %ld\n",
		dclk_rate, sclk_rate);

	clk_set_parent(output->dither_clk, output->scaler_clk);

	rate = clk_round_rate(output->scaler_clk, sclk_rate);
	clk_set_rate(output->scaler_clk, rate);

	clk_prepare_enable(output->scaler_clk);

	rk618_scaler_configure(output->parent,
			       &output->scale_mode, &output->panel_mode);
	rk618_scaler_enable(output->parent);

	if (output->funcs->pre_enable)
		output->funcs->pre_enable(output);

	drm_panel_prepare(output->panel);
}

static void rk618_output_bridge_enable(struct drm_bridge *bridge)
{
	struct rk618_output *output = bridge_to_output(bridge);

	if (output->funcs->enable)
		output->funcs->enable(output);

	drm_panel_enable(output->panel);
}

static void rk618_output_bridge_disable(struct drm_bridge *bridge)
{
	struct rk618_output *output = bridge_to_output(bridge);

	drm_panel_disable(output->panel);

	if (output->funcs->disable)
		output->funcs->disable(output);
}

static void rk618_output_bridge_post_disable(struct drm_bridge *bridge)
{
	struct rk618_output *output = bridge_to_output(bridge);

	drm_panel_unprepare(output->panel);

	if (output->funcs->post_disable)
		output->funcs->post_disable(output);

	rk618_scaler_disable(output->parent);
	clk_disable_unprepare(output->scaler_clk);
}

static void rk618_output_bridge_mode_set(struct drm_bridge *bridge,
					 struct drm_display_mode *mode,
					 struct drm_display_mode *adjusted)
{
	struct rk618_output *output = bridge_to_output(bridge);

	drm_mode_copy(&output->scale_mode, adjusted);

	if (output->funcs->mode_set)
		output->funcs->mode_set(output, &output->panel_mode);
}

static const struct drm_bridge_funcs rk618_output_bridge_funcs = {
	.pre_enable = rk618_output_bridge_pre_enable,
	.enable = rk618_output_bridge_enable,
	.disable = rk618_output_bridge_disable,
	.post_disable = rk618_output_bridge_post_disable,
	.mode_set = rk618_output_bridge_mode_set,
};

int rk618_output_register(struct rk618_output *output)
{
	struct device *dev = output->dev;
	struct device_node *endpoint, *remote;
	int ret;

	output->dither_clk = devm_clk_get(dev, "dither");
	if (IS_ERR(output->dither_clk)) {
		ret = PTR_ERR(output->dither_clk);
		dev_err(dev, "failed to get dither clock: %d\n", ret);
		return ret;
	}

	output->vif_clk = devm_clk_get(dev, "vif");
	if (IS_ERR(output->vif_clk)) {
		ret = PTR_ERR(output->vif_clk);
		dev_err(dev, "failed to get vif clock: %d\n", ret);
		return ret;
	}

	output->scaler_clk = devm_clk_get(dev, "scaler");
	if (IS_ERR(output->scaler_clk)) {
		ret = PTR_ERR(output->scaler_clk);
		dev_err(dev, "failed to get scaler clock: %d\n", ret);
		return ret;
	}

	endpoint = of_graph_get_endpoint_by_regs(dev->of_node, 1, -1);
	if (!endpoint) {
		dev_err(dev, "no valid endpoint\n");
		return -ENODEV;
	}

	remote = of_graph_get_remote_port_parent(endpoint);
	of_node_put(endpoint);
	if (!remote) {
		dev_err(dev, "no valid remote node\n");
		return -ENODEV;
	}

	output->panel_node = remote;

	return 0;
}
EXPORT_SYMBOL(rk618_output_register);

int rk618_output_bind(struct rk618_output *output, struct drm_device *drm,
		      int encoder_type, int connector_type)
{
	struct device *dev = output->dev;
	struct drm_encoder *encoder = &output->encoder;
	struct drm_connector *connector = &output->connector;
	struct drm_bridge *bridge = &output->bridge;
	int ret;

	output->panel = of_drm_find_panel(output->panel_node);
	if (!output->panel)
		return -EPROBE_DEFER;

	encoder->possible_crtcs = drm_of_find_possible_crtcs(drm,
							     dev->of_node);
	drm_encoder_init(drm, encoder, &rk618_output_encoder_funcs,
			 encoder_type, NULL);
	drm_encoder_helper_add(encoder, &rk618_output_encoder_helper_funcs);

	connector->port = dev->of_node;
	drm_connector_init(drm, connector, &rk618_output_connector_funcs,
			   connector_type);
	drm_connector_helper_add(connector,
				 &rk618_output_connector_helper_funcs);

	drm_mode_connector_attach_encoder(connector, encoder);

	drm_panel_attach(output->panel, connector);

	bridge->funcs = &rk618_output_bridge_funcs;
	bridge->of_node = dev->of_node;
	ret = drm_bridge_add(bridge);
	if (ret) {
		DRM_ERROR("failed to add bridge\n");
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(rk618_output_bind);

void rk618_output_unbind(struct rk618_output *output)
{
	drm_bridge_remove(&output->bridge);
	drm_connector_cleanup(&output->connector);
	drm_encoder_cleanup(&output->encoder);
}
EXPORT_SYMBOL(rk618_output_unbind);
