// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2016 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 * Copyright (C) 2015 Amlogic, Inc. All rights reserved.
 * Copyright (C) 2014 Endless Mobile
 *
 * Written by:
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

#include <linux/export.h>
#include <linux/of_graph.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_bridge_connector.h>
#include <drm/drm_device.h>
#include <drm/drm_edid.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>

#include "meson_registers.h"
#include "meson_vclk.h"
#include "meson_encoder_cvbs.h"

/* HHI VDAC Registers */
#define HHI_VDAC_CNTL0		0x2F4 /* 0xbd offset in data sheet */
#define HHI_VDAC_CNTL0_G12A	0x2EC /* 0xbd offset in data sheet */
#define HHI_VDAC_CNTL1		0x2F8 /* 0xbe offset in data sheet */
#define HHI_VDAC_CNTL1_G12A	0x2F0 /* 0xbe offset in data sheet */

struct meson_encoder_cvbs {
	struct drm_encoder	encoder;
	struct drm_bridge	bridge;
	struct drm_bridge	*next_bridge;
	struct meson_drm	*priv;
};

#define bridge_to_meson_encoder_cvbs(x) \
	container_of(x, struct meson_encoder_cvbs, bridge)

/* Supported Modes */

struct meson_cvbs_mode meson_cvbs_modes[MESON_CVBS_MODES_COUNT] = {
	{ /* PAL */
		.enci = &meson_cvbs_enci_pal,
		.mode = {
			DRM_MODE("720x576i", DRM_MODE_TYPE_DRIVER, 13500,
				 720, 732, 795, 864, 0, 576, 580, 586, 625, 0,
				 DRM_MODE_FLAG_INTERLACE),
			.picture_aspect_ratio = HDMI_PICTURE_ASPECT_4_3,
		},
	},
	{ /* NTSC */
		.enci = &meson_cvbs_enci_ntsc,
		.mode = {
			DRM_MODE("720x480i", DRM_MODE_TYPE_DRIVER, 13500,
				720, 739, 801, 858, 0, 480, 488, 494, 525, 0,
				DRM_MODE_FLAG_INTERLACE),
			.picture_aspect_ratio = HDMI_PICTURE_ASPECT_4_3,
		},
	},
};

static const struct meson_cvbs_mode *
meson_cvbs_get_mode(const struct drm_display_mode *req_mode)
{
	int i;

	for (i = 0; i < MESON_CVBS_MODES_COUNT; ++i) {
		struct meson_cvbs_mode *meson_mode = &meson_cvbs_modes[i];

		if (drm_mode_match(req_mode, &meson_mode->mode,
				   DRM_MODE_MATCH_TIMINGS |
				   DRM_MODE_MATCH_CLOCK |
				   DRM_MODE_MATCH_FLAGS |
				   DRM_MODE_MATCH_3D_FLAGS))
			return meson_mode;
	}

	return NULL;
}

static int meson_encoder_cvbs_attach(struct drm_bridge *bridge,
				     struct drm_encoder *encoder,
				     enum drm_bridge_attach_flags flags)
{
	struct meson_encoder_cvbs *meson_encoder_cvbs =
					bridge_to_meson_encoder_cvbs(bridge);

	return drm_bridge_attach(encoder, meson_encoder_cvbs->next_bridge,
				 &meson_encoder_cvbs->bridge, flags);
}

static int meson_encoder_cvbs_get_modes(struct drm_bridge *bridge,
					struct drm_connector *connector)
{
	struct meson_encoder_cvbs *meson_encoder_cvbs =
					bridge_to_meson_encoder_cvbs(bridge);
	struct meson_drm *priv = meson_encoder_cvbs->priv;
	struct drm_display_mode *mode;
	int i;

	for (i = 0; i < MESON_CVBS_MODES_COUNT; ++i) {
		struct meson_cvbs_mode *meson_mode = &meson_cvbs_modes[i];

		mode = drm_mode_duplicate(priv->drm, &meson_mode->mode);
		if (!mode) {
			dev_err(priv->dev, "Failed to create a new display mode\n");
			return 0;
		}

		drm_mode_probed_add(connector, mode);
	}

	return i;
}

static enum drm_mode_status
meson_encoder_cvbs_mode_valid(struct drm_bridge *bridge,
			      const struct drm_display_info *display_info,
			      const struct drm_display_mode *mode)
{
	if (meson_cvbs_get_mode(mode))
		return MODE_OK;

	return MODE_BAD;
}

static int meson_encoder_cvbs_atomic_check(struct drm_bridge *bridge,
					struct drm_bridge_state *bridge_state,
					struct drm_crtc_state *crtc_state,
					struct drm_connector_state *conn_state)
{
	if (meson_cvbs_get_mode(&crtc_state->mode))
		return 0;

	return -EINVAL;
}

static void meson_encoder_cvbs_atomic_enable(struct drm_bridge *bridge,
					     struct drm_atomic_state *state)
{
	struct meson_encoder_cvbs *encoder_cvbs = bridge_to_meson_encoder_cvbs(bridge);
	struct meson_drm *priv = encoder_cvbs->priv;
	const struct meson_cvbs_mode *meson_mode;
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

	meson_mode = meson_cvbs_get_mode(&crtc_state->adjusted_mode);
	if (WARN_ON(!meson_mode))
		return;

	meson_venci_cvbs_mode_set(priv, meson_mode->enci);

	/* Setup 27MHz vclk2 for ENCI and VDAC */
	meson_vclk_setup(priv, MESON_VCLK_TARGET_CVBS,
			 MESON_VCLK_CVBS, MESON_VCLK_CVBS,
			 MESON_VCLK_CVBS, MESON_VCLK_CVBS,
			 true);

	/* VDAC0 source is not from ATV */
	writel_bits_relaxed(VENC_VDAC_SEL_ATV_DMD, 0,
			    priv->io_base + _REG(VENC_VDAC_DACSEL0));

	if (meson_vpu_is_compatible(priv, VPU_COMPATIBLE_GXBB)) {
		regmap_write(priv->hhi, HHI_VDAC_CNTL0, 1);
		regmap_write(priv->hhi, HHI_VDAC_CNTL1, 0);
	} else if (meson_vpu_is_compatible(priv, VPU_COMPATIBLE_GXM) ||
		 meson_vpu_is_compatible(priv, VPU_COMPATIBLE_GXL)) {
		regmap_write(priv->hhi, HHI_VDAC_CNTL0, 0xf0001);
		regmap_write(priv->hhi, HHI_VDAC_CNTL1, 0);
	} else if (meson_vpu_is_compatible(priv, VPU_COMPATIBLE_G12A)) {
		regmap_write(priv->hhi, HHI_VDAC_CNTL0_G12A, 0x906001);
		regmap_write(priv->hhi, HHI_VDAC_CNTL1_G12A, 0);
	}
}

static void meson_encoder_cvbs_atomic_disable(struct drm_bridge *bridge,
					      struct drm_atomic_state *state)
{
	struct meson_encoder_cvbs *meson_encoder_cvbs =
					bridge_to_meson_encoder_cvbs(bridge);
	struct meson_drm *priv = meson_encoder_cvbs->priv;

	/* Disable CVBS VDAC */
	if (meson_vpu_is_compatible(priv, VPU_COMPATIBLE_G12A)) {
		regmap_write(priv->hhi, HHI_VDAC_CNTL0_G12A, 0);
		regmap_write(priv->hhi, HHI_VDAC_CNTL1_G12A, 0);
	} else {
		regmap_write(priv->hhi, HHI_VDAC_CNTL0, 0);
		regmap_write(priv->hhi, HHI_VDAC_CNTL1, 8);
	}
}

static const struct drm_bridge_funcs meson_encoder_cvbs_bridge_funcs = {
	.attach = meson_encoder_cvbs_attach,
	.mode_valid = meson_encoder_cvbs_mode_valid,
	.get_modes = meson_encoder_cvbs_get_modes,
	.atomic_enable = meson_encoder_cvbs_atomic_enable,
	.atomic_disable = meson_encoder_cvbs_atomic_disable,
	.atomic_check = meson_encoder_cvbs_atomic_check,
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
	.atomic_reset = drm_atomic_helper_bridge_reset,
};

int meson_encoder_cvbs_probe(struct meson_drm *priv)
{
	struct drm_device *drm = priv->drm;
	struct meson_encoder_cvbs *meson_encoder_cvbs;
	struct drm_connector *connector;
	struct device_node *remote;
	int ret;

	meson_encoder_cvbs = devm_drm_bridge_alloc(priv->dev,
						   struct meson_encoder_cvbs,
						   bridge,
						   &meson_encoder_cvbs_bridge_funcs);
	if (IS_ERR(meson_encoder_cvbs))
		return PTR_ERR(meson_encoder_cvbs);

	/* CVBS Connector Bridge */
	remote = of_graph_get_remote_node(priv->dev->of_node, 0, 0);
	if (!remote) {
		dev_info(drm->dev, "CVBS Output connector not available\n");
		return 0;
	}

	meson_encoder_cvbs->next_bridge = of_drm_find_bridge(remote);
	of_node_put(remote);
	if (!meson_encoder_cvbs->next_bridge)
		return dev_err_probe(priv->dev, -EPROBE_DEFER,
				     "Failed to find CVBS Connector bridge\n");

	/* CVBS Encoder Bridge */
	meson_encoder_cvbs->bridge.of_node = priv->dev->of_node;
	meson_encoder_cvbs->bridge.type = DRM_MODE_CONNECTOR_Composite;
	meson_encoder_cvbs->bridge.ops = DRM_BRIDGE_OP_MODES;
	meson_encoder_cvbs->bridge.interlace_allowed = true;

	drm_bridge_add(&meson_encoder_cvbs->bridge);

	meson_encoder_cvbs->priv = priv;

	/* Encoder */
	ret = drm_simple_encoder_init(priv->drm, &meson_encoder_cvbs->encoder,
				      DRM_MODE_ENCODER_TVDAC);
	if (ret)
		return dev_err_probe(priv->dev, ret,
				     "Failed to init CVBS encoder\n");

	meson_encoder_cvbs->encoder.possible_crtcs = BIT(0);

	/* Attach CVBS Encoder Bridge to Encoder */
	ret = drm_bridge_attach(&meson_encoder_cvbs->encoder, &meson_encoder_cvbs->bridge, NULL,
				DRM_BRIDGE_ATTACH_NO_CONNECTOR);
	if (ret) {
		dev_err(priv->dev, "Failed to attach bridge: %d\n", ret);
		return ret;
	}

	/* Initialize & attach Bridge Connector */
	connector = drm_bridge_connector_init(priv->drm, &meson_encoder_cvbs->encoder);
	if (IS_ERR(connector))
		return dev_err_probe(priv->dev, PTR_ERR(connector),
				     "Unable to create CVBS bridge connector\n");

	drm_connector_attach_encoder(connector, &meson_encoder_cvbs->encoder);

	priv->encoders[MESON_ENC_CVBS] = meson_encoder_cvbs;

	return 0;
}

void meson_encoder_cvbs_remove(struct meson_drm *priv)
{
	struct meson_encoder_cvbs *meson_encoder_cvbs;

	if (priv->encoders[MESON_ENC_CVBS]) {
		meson_encoder_cvbs = priv->encoders[MESON_ENC_CVBS];
		drm_bridge_remove(&meson_encoder_cvbs->bridge);
	}
}
