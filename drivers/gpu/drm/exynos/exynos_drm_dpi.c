/*
 * Exynos DRM Parallel output support.
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd
 *
 * Contacts: Andrzej Hajda <a.hajda@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_panel.h>
#include <drm/drm_atomic_helper.h>

#include <linux/regulator/consumer.h>

#include <video/of_videomode.h>
#include <video/videomode.h>

#include "exynos_drm_encoder.h"
#include "exynos_drm_crtc.h"

struct exynos_dpi {
	struct exynos_drm_encoder encoder;
	struct device *dev;
	struct device_node *panel_node;

	struct drm_panel *panel;
	struct drm_connector connector;

	struct videomode *vm;
};

#define connector_to_dpi(c) container_of(c, struct exynos_dpi, connector)

static inline struct exynos_dpi *encoder_to_dpi(struct exynos_drm_encoder *e)
{
	return container_of(e, struct exynos_dpi, encoder);
}

static enum drm_connector_status
exynos_dpi_detect(struct drm_connector *connector, bool force)
{
	struct exynos_dpi *ctx = connector_to_dpi(connector);

	if (ctx->panel && !ctx->panel->connector)
		drm_panel_attach(ctx->panel, &ctx->connector);

	return connector_status_connected;
}

static void exynos_dpi_connector_destroy(struct drm_connector *connector)
{
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
}

static struct drm_connector_funcs exynos_dpi_connector_funcs = {
	.dpms = drm_atomic_helper_connector_dpms,
	.detect = exynos_dpi_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = exynos_dpi_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int exynos_dpi_get_modes(struct drm_connector *connector)
{
	struct exynos_dpi *ctx = connector_to_dpi(connector);

	/* fimd timings gets precedence over panel modes */
	if (ctx->vm) {
		struct drm_display_mode *mode;

		mode = drm_mode_create(connector->dev);
		if (!mode) {
			DRM_ERROR("failed to create a new display mode\n");
			return 0;
		}
		drm_display_mode_from_videomode(ctx->vm, mode);
		mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
		drm_mode_probed_add(connector, mode);
		return 1;
	}

	if (ctx->panel)
		return ctx->panel->funcs->get_modes(ctx->panel);

	return 0;
}

static struct drm_encoder *
exynos_dpi_best_encoder(struct drm_connector *connector)
{
	struct exynos_dpi *ctx = connector_to_dpi(connector);

	return &ctx->encoder.base;
}

static struct drm_connector_helper_funcs exynos_dpi_connector_helper_funcs = {
	.get_modes = exynos_dpi_get_modes,
	.best_encoder = exynos_dpi_best_encoder,
};

static int exynos_dpi_create_connector(
				struct exynos_drm_encoder *exynos_encoder)
{
	struct exynos_dpi *ctx = encoder_to_dpi(exynos_encoder);
	struct drm_encoder *encoder = &exynos_encoder->base;
	struct drm_connector *connector = &ctx->connector;
	int ret;

	connector->polled = DRM_CONNECTOR_POLL_HPD;

	ret = drm_connector_init(encoder->dev, connector,
				 &exynos_dpi_connector_funcs,
				 DRM_MODE_CONNECTOR_VGA);
	if (ret) {
		DRM_ERROR("failed to initialize connector with drm\n");
		return ret;
	}

	drm_connector_helper_add(connector, &exynos_dpi_connector_helper_funcs);
	drm_connector_register(connector);
	drm_mode_connector_attach_encoder(connector, encoder);

	return 0;
}

static void exynos_dpi_enable(struct exynos_drm_encoder *encoder)
{
	struct exynos_dpi *ctx = encoder_to_dpi(encoder);

	if (ctx->panel) {
		drm_panel_prepare(ctx->panel);
		drm_panel_enable(ctx->panel);
	}
}

static void exynos_dpi_disable(struct exynos_drm_encoder *encoder)
{
	struct exynos_dpi *ctx = encoder_to_dpi(encoder);

	if (ctx->panel) {
		drm_panel_disable(ctx->panel);
		drm_panel_unprepare(ctx->panel);
	}
}

static struct exynos_drm_encoder_ops exynos_dpi_encoder_ops = {
	.enable = exynos_dpi_enable,
	.disable = exynos_dpi_disable,
};

/* of_* functions will be removed after merge of of_graph patches */
static struct device_node *
of_get_child_by_name_reg(struct device_node *parent, const char *name, u32 reg)
{
	struct device_node *np;

	for_each_child_of_node(parent, np) {
		u32 r;

		if (!np->name || of_node_cmp(np->name, name))
			continue;

		if (of_property_read_u32(np, "reg", &r) < 0)
			r = 0;

		if (reg == r)
			break;
	}

	return np;
}

static struct device_node *of_graph_get_port_by_reg(struct device_node *parent,
						    u32 reg)
{
	struct device_node *ports, *port;

	ports = of_get_child_by_name(parent, "ports");
	if (ports)
		parent = ports;

	port = of_get_child_by_name_reg(parent, "port", reg);

	of_node_put(ports);

	return port;
}

static struct device_node *
of_graph_get_endpoint_by_reg(struct device_node *port, u32 reg)
{
	return of_get_child_by_name_reg(port, "endpoint", reg);
}

static struct device_node *
of_graph_get_remote_port_parent(const struct device_node *node)
{
	struct device_node *np;
	unsigned int depth;

	np = of_parse_phandle(node, "remote-endpoint", 0);

	/* Walk 3 levels up only if there is 'ports' node. */
	for (depth = 3; depth && np; depth--) {
		np = of_get_next_parent(np);
		if (depth == 2 && of_node_cmp(np->name, "ports"))
			break;
	}
	return np;
}

enum {
	FIMD_PORT_IN0,
	FIMD_PORT_IN1,
	FIMD_PORT_IN2,
	FIMD_PORT_RGB,
	FIMD_PORT_WRB,
};

static struct device_node *exynos_dpi_of_find_panel_node(struct device *dev)
{
	struct device_node *np, *ep;

	np = of_graph_get_port_by_reg(dev->of_node, FIMD_PORT_RGB);
	if (!np)
		return NULL;

	ep = of_graph_get_endpoint_by_reg(np, 0);
	of_node_put(np);
	if (!ep)
		return NULL;

	np = of_graph_get_remote_port_parent(ep);
	of_node_put(ep);

	return np;
}

static int exynos_dpi_parse_dt(struct exynos_dpi *ctx)
{
	struct device *dev = ctx->dev;
	struct device_node *dn = dev->of_node;
	struct device_node *np;

	ctx->panel_node = exynos_dpi_of_find_panel_node(dev);

	np = of_get_child_by_name(dn, "display-timings");
	if (np) {
		struct videomode *vm;
		int ret;

		of_node_put(np);

		vm = devm_kzalloc(dev, sizeof(*ctx->vm), GFP_KERNEL);
		if (!vm)
			return -ENOMEM;

		ret = of_get_videomode(dn, vm, 0);
		if (ret < 0) {
			devm_kfree(dev, vm);
			return ret;
		}

		ctx->vm = vm;

		return 0;
	}

	if (!ctx->panel_node)
		return -EINVAL;

	return 0;
}

int exynos_dpi_bind(struct drm_device *dev,
		    struct exynos_drm_encoder *exynos_encoder)
{
	int ret;

	ret = exynos_drm_encoder_create(dev, exynos_encoder,
					EXYNOS_DISPLAY_TYPE_LCD);
	if (ret) {
		DRM_ERROR("failed to create encoder\n");
		return ret;
	}

	ret = exynos_dpi_create_connector(exynos_encoder);
	if (ret) {
		DRM_ERROR("failed to create connector ret = %d\n", ret);
		drm_encoder_cleanup(&exynos_encoder->base);
		return ret;
	}

	return 0;
}

struct exynos_drm_encoder *exynos_dpi_probe(struct device *dev)
{
	struct exynos_dpi *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return ERR_PTR(-ENOMEM);

	ctx->encoder.ops = &exynos_dpi_encoder_ops;
	ctx->dev = dev;

	ret = exynos_dpi_parse_dt(ctx);
	if (ret < 0) {
		devm_kfree(dev, ctx);
		return NULL;
	}

	if (ctx->panel_node) {
		ctx->panel = of_drm_find_panel(ctx->panel_node);
		if (!ctx->panel)
			return ERR_PTR(-EPROBE_DEFER);
	}

	return &ctx->encoder;
}

int exynos_dpi_remove(struct exynos_drm_encoder *encoder)
{
	struct exynos_dpi *ctx = encoder_to_dpi(encoder);

	exynos_dpi_disable(&ctx->encoder);

	if (ctx->panel)
		drm_panel_detach(ctx->panel);

	return 0;
}
