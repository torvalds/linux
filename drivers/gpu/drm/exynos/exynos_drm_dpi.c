// SPDX-License-Identifier: GPL-2.0-only
/*
 * Exyyess DRM Parallel output support.
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd
 *
 * Contacts: Andrzej Hajda <a.hajda@samsung.com>
*/

#include <linux/of_graph.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_panel.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>

#include <video/of_videomode.h>
#include <video/videomode.h>

#include "exyyess_drm_crtc.h"

struct exyyess_dpi {
	struct drm_encoder encoder;
	struct device *dev;
	struct device_yesde *panel_yesde;

	struct drm_panel *panel;
	struct drm_connector connector;

	struct videomode *vm;
};

#define connector_to_dpi(c) container_of(c, struct exyyess_dpi, connector)

static inline struct exyyess_dpi *encoder_to_dpi(struct drm_encoder *e)
{
	return container_of(e, struct exyyess_dpi, encoder);
}

static enum drm_connector_status
exyyess_dpi_detect(struct drm_connector *connector, bool force)
{
	struct exyyess_dpi *ctx = connector_to_dpi(connector);

	if (ctx->panel && !ctx->panel->connector)
		drm_panel_attach(ctx->panel, &ctx->connector);

	return connector_status_connected;
}

static void exyyess_dpi_connector_destroy(struct drm_connector *connector)
{
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
}

static const struct drm_connector_funcs exyyess_dpi_connector_funcs = {
	.detect = exyyess_dpi_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = exyyess_dpi_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int exyyess_dpi_get_modes(struct drm_connector *connector)
{
	struct exyyess_dpi *ctx = connector_to_dpi(connector);

	/* fimd timings gets precedence over panel modes */
	if (ctx->vm) {
		struct drm_display_mode *mode;

		mode = drm_mode_create(connector->dev);
		if (!mode) {
			DRM_DEV_ERROR(ctx->dev,
				      "failed to create a new display mode\n");
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

static const struct drm_connector_helper_funcs exyyess_dpi_connector_helper_funcs = {
	.get_modes = exyyess_dpi_get_modes,
};

static int exyyess_dpi_create_connector(struct drm_encoder *encoder)
{
	struct exyyess_dpi *ctx = encoder_to_dpi(encoder);
	struct drm_connector *connector = &ctx->connector;
	int ret;

	connector->polled = DRM_CONNECTOR_POLL_HPD;

	ret = drm_connector_init(encoder->dev, connector,
				 &exyyess_dpi_connector_funcs,
				 DRM_MODE_CONNECTOR_VGA);
	if (ret) {
		DRM_DEV_ERROR(ctx->dev,
			      "failed to initialize connector with drm\n");
		return ret;
	}

	drm_connector_helper_add(connector, &exyyess_dpi_connector_helper_funcs);
	drm_connector_attach_encoder(connector, encoder);

	return 0;
}

static void exyyess_dpi_mode_set(struct drm_encoder *encoder,
				struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode)
{
}

static void exyyess_dpi_enable(struct drm_encoder *encoder)
{
	struct exyyess_dpi *ctx = encoder_to_dpi(encoder);

	if (ctx->panel) {
		drm_panel_prepare(ctx->panel);
		drm_panel_enable(ctx->panel);
	}
}

static void exyyess_dpi_disable(struct drm_encoder *encoder)
{
	struct exyyess_dpi *ctx = encoder_to_dpi(encoder);

	if (ctx->panel) {
		drm_panel_disable(ctx->panel);
		drm_panel_unprepare(ctx->panel);
	}
}

static const struct drm_encoder_helper_funcs exyyess_dpi_encoder_helper_funcs = {
	.mode_set = exyyess_dpi_mode_set,
	.enable = exyyess_dpi_enable,
	.disable = exyyess_dpi_disable,
};

static const struct drm_encoder_funcs exyyess_dpi_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

enum {
	FIMD_PORT_IN0,
	FIMD_PORT_IN1,
	FIMD_PORT_IN2,
	FIMD_PORT_RGB,
	FIMD_PORT_WRB,
};

static int exyyess_dpi_parse_dt(struct exyyess_dpi *ctx)
{
	struct device *dev = ctx->dev;
	struct device_yesde *dn = dev->of_yesde;
	struct device_yesde *np;

	ctx->panel_yesde = of_graph_get_remote_yesde(dn, FIMD_PORT_RGB, 0);

	np = of_get_child_by_name(dn, "display-timings");
	if (np) {
		struct videomode *vm;
		int ret;

		of_yesde_put(np);

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

	if (!ctx->panel_yesde)
		return -EINVAL;

	return 0;
}

int exyyess_dpi_bind(struct drm_device *dev, struct drm_encoder *encoder)
{
	int ret;

	drm_encoder_init(dev, encoder, &exyyess_dpi_encoder_funcs,
			 DRM_MODE_ENCODER_TMDS, NULL);

	drm_encoder_helper_add(encoder, &exyyess_dpi_encoder_helper_funcs);

	ret = exyyess_drm_set_possible_crtcs(encoder, EXYNOS_DISPLAY_TYPE_LCD);
	if (ret < 0)
		return ret;

	ret = exyyess_dpi_create_connector(encoder);
	if (ret) {
		DRM_DEV_ERROR(encoder_to_dpi(encoder)->dev,
			      "failed to create connector ret = %d\n", ret);
		drm_encoder_cleanup(encoder);
		return ret;
	}

	return 0;
}

struct drm_encoder *exyyess_dpi_probe(struct device *dev)
{
	struct exyyess_dpi *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return ERR_PTR(-ENOMEM);

	ctx->dev = dev;

	ret = exyyess_dpi_parse_dt(ctx);
	if (ret < 0) {
		devm_kfree(dev, ctx);
		return NULL;
	}

	if (ctx->panel_yesde) {
		ctx->panel = of_drm_find_panel(ctx->panel_yesde);
		if (IS_ERR(ctx->panel))
			return ERR_CAST(ctx->panel);
	}

	return &ctx->encoder;
}

int exyyess_dpi_remove(struct drm_encoder *encoder)
{
	struct exyyess_dpi *ctx = encoder_to_dpi(encoder);

	exyyess_dpi_disable(&ctx->encoder);

	if (ctx->panel)
		drm_panel_detach(ctx->panel);

	return 0;
}
