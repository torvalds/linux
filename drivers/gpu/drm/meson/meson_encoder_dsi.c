// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2016 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 * Copyright (C) 2015 Amlogic, Inc. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_simple_kms_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_bridge_connector.h>
#include <drm/drm_device.h>
#include <drm/drm_probe_helper.h>

#include "meson_drv.h"
#include "meson_encoder_dsi.h"
#include "meson_registers.h"
#include "meson_venc.h"
#include "meson_vclk.h"

struct meson_encoder_dsi {
	struct drm_encoder encoder;
	struct drm_bridge bridge;
	struct drm_bridge *next_bridge;
	struct meson_drm *priv;
};

#define bridge_to_meson_encoder_dsi(x) \
	container_of(x, struct meson_encoder_dsi, bridge)

static int meson_encoder_dsi_attach(struct drm_bridge *bridge,
				    enum drm_bridge_attach_flags flags)
{
	struct meson_encoder_dsi *encoder_dsi = bridge_to_meson_encoder_dsi(bridge);

	return drm_bridge_attach(bridge->encoder, encoder_dsi->next_bridge,
				 &encoder_dsi->bridge, flags);
}

static void meson_encoder_dsi_atomic_enable(struct drm_bridge *bridge,
					    struct drm_bridge_state *bridge_state)
{
	struct meson_encoder_dsi *encoder_dsi = bridge_to_meson_encoder_dsi(bridge);
	struct drm_atomic_state *state = bridge_state->base.state;
	struct meson_drm *priv = encoder_dsi->priv;
	struct drm_connector_state *conn_state;
	struct drm_crtc_state *crtc_state;
	struct drm_connector *connector;

	connector = drm_atomic_get_new_connector_for_encoder(state, bridge->encoder);
	if (WARN_ON(!connector))
		return;

	conn_state = drm_atomic_get_new_connector_state(state, connector);
	if (WARN_ON(!conn_state))
		return;

	crtc_state = drm_atomic_get_new_crtc_state(state, conn_state->crtc);
	if (WARN_ON(!crtc_state))
		return;

	/* ENCL clock setup is handled by CCF */

	meson_venc_mipi_dsi_mode_set(priv, &crtc_state->adjusted_mode);
	meson_encl_load_gamma(priv);

	writel_relaxed(0, priv->io_base + _REG(ENCL_VIDEO_EN));

	writel_bits_relaxed(ENCL_VIDEO_MODE_ADV_VFIFO_EN, ENCL_VIDEO_MODE_ADV_VFIFO_EN,
			    priv->io_base + _REG(ENCL_VIDEO_MODE_ADV));
	writel_relaxed(0, priv->io_base + _REG(ENCL_TST_EN));

	writel_bits_relaxed(BIT(0), 0, priv->io_base + _REG(VPP_WRAP_OSD1_MATRIX_EN_CTRL));

	writel_relaxed(1, priv->io_base + _REG(ENCL_VIDEO_EN));
}

static void meson_encoder_dsi_atomic_disable(struct drm_bridge *bridge,
					     struct drm_bridge_state *bridge_state)
{
	struct meson_encoder_dsi *meson_encoder_dsi =
					bridge_to_meson_encoder_dsi(bridge);
	struct meson_drm *priv = meson_encoder_dsi->priv;

	writel_relaxed(0, priv->io_base + _REG(ENCL_VIDEO_EN));

	writel_bits_relaxed(BIT(0), BIT(0), priv->io_base + _REG(VPP_WRAP_OSD1_MATRIX_EN_CTRL));
}

static const struct drm_bridge_funcs meson_encoder_dsi_bridge_funcs = {
	.attach	= meson_encoder_dsi_attach,
	.atomic_enable = meson_encoder_dsi_atomic_enable,
	.atomic_disable	= meson_encoder_dsi_atomic_disable,
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
	.atomic_reset = drm_atomic_helper_bridge_reset,
};

int meson_encoder_dsi_init(struct meson_drm *priv)
{
	struct meson_encoder_dsi *meson_encoder_dsi;
	struct device_node *remote;
	int ret;

	meson_encoder_dsi = devm_kzalloc(priv->dev, sizeof(*meson_encoder_dsi), GFP_KERNEL);
	if (!meson_encoder_dsi)
		return -ENOMEM;

	/* DSI Transceiver Bridge */
	remote = of_graph_get_remote_node(priv->dev->of_node, 2, 0);
	if (!remote) {
		dev_err(priv->dev, "DSI transceiver device is disabled");
		return 0;
	}

	meson_encoder_dsi->next_bridge = of_drm_find_bridge(remote);
	if (!meson_encoder_dsi->next_bridge) {
		dev_dbg(priv->dev, "Failed to find DSI transceiver bridge\n");
		return -EPROBE_DEFER;
	}

	/* DSI Encoder Bridge */
	meson_encoder_dsi->bridge.funcs = &meson_encoder_dsi_bridge_funcs;
	meson_encoder_dsi->bridge.of_node = priv->dev->of_node;
	meson_encoder_dsi->bridge.type = DRM_MODE_CONNECTOR_DSI;

	drm_bridge_add(&meson_encoder_dsi->bridge);

	meson_encoder_dsi->priv = priv;

	/* Encoder */
	ret = drm_simple_encoder_init(priv->drm, &meson_encoder_dsi->encoder,
				      DRM_MODE_ENCODER_DSI);
	if (ret) {
		dev_err(priv->dev, "Failed to init DSI encoder: %d\n", ret);
		return ret;
	}

	meson_encoder_dsi->encoder.possible_crtcs = BIT(0);

	/* Attach DSI Encoder Bridge to Encoder */
	ret = drm_bridge_attach(&meson_encoder_dsi->encoder, &meson_encoder_dsi->bridge, NULL, 0);
	if (ret) {
		dev_err(priv->dev, "Failed to attach bridge: %d\n", ret);
		return ret;
	}

	/*
	 * We should have now in place:
	 * encoder->[dsi encoder bridge]->[dw-mipi-dsi bridge]->[panel bridge]->[panel]
	 */

	priv->encoders[MESON_ENC_DSI] = meson_encoder_dsi;

	dev_dbg(priv->dev, "DSI encoder initialized\n");

	return 0;
}

void meson_encoder_dsi_remove(struct meson_drm *priv)
{
	struct meson_encoder_dsi *meson_encoder_dsi;

	if (priv->encoders[MESON_ENC_DSI]) {
		meson_encoder_dsi = priv->encoders[MESON_ENC_DSI];
		drm_bridge_remove(&meson_encoder_dsi->bridge);
		drm_bridge_remove(meson_encoder_dsi->next_bridge);
	}
}
