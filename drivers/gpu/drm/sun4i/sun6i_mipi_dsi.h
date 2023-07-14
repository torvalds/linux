// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2016 Allwinnertech Co., Ltd.
 * Copyright (C) 2017-2018 Bootlin
 *
 * Maxime Ripard <maxime.ripard@bootlin.com>
 */

#ifndef _SUN6I_MIPI_DSI_H_
#define _SUN6I_MIPI_DSI_H_

#include <drm/drm_connector.h>
#include <drm/drm_encoder.h>
#include <drm/drm_mipi_dsi.h>

#define SUN6I_DSI_TCON_DIV	4

struct sun6i_dsi_variant {
	bool			has_mod_clk;
	bool			set_mod_clk;
};

struct sun6i_dsi {
	struct drm_connector	connector;
	struct drm_encoder	encoder;
	struct mipi_dsi_host	host;

	struct clk		*bus_clk;
	struct clk		*mod_clk;
	struct regmap		*regs;
	struct regulator	*regulator;
	struct reset_control	*reset;
	struct phy		*dphy;

	struct device		*dev;
	struct mipi_dsi_device	*device;
	struct drm_device	*drm;
	struct drm_panel	*panel;

	const struct sun6i_dsi_variant *variant;
};

static inline struct sun6i_dsi *host_to_sun6i_dsi(struct mipi_dsi_host *host)
{
	return container_of(host, struct sun6i_dsi, host);
};

static inline struct sun6i_dsi *connector_to_sun6i_dsi(struct drm_connector *connector)
{
	return container_of(connector, struct sun6i_dsi, connector);
};

static inline struct sun6i_dsi *encoder_to_sun6i_dsi(const struct drm_encoder *encoder)
{
	return container_of(encoder, struct sun6i_dsi, encoder);
};

#endif /* _SUN6I_MIPI_DSI_H_ */
