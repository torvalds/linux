/*
 * Copyright (C) 2016 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 * Copyright (C) 2015 Amlogic, Inc. All rights reserved.
 * Copyright (C) 2014 Endless Mobile
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Written by:
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_graph.h>

#include <drm/drmP.h>
#include <drm/drm_edid.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_atomic_helper.h>

#include "meson_venc_cvbs.h"
#include "meson_venc.h"
#include "meson_registers.h"

/* HHI VDAC Registers */
#define HHI_VDAC_CNTL0		0x2F4 /* 0xbd offset in data sheet */
#define HHI_VDAC_CNTL1		0x2F8 /* 0xbe offset in data sheet */

struct meson_venc_cvbs {
	struct drm_encoder	encoder;
	struct drm_connector	connector;
	struct meson_drm	*priv;
};
#define encoder_to_meson_venc_cvbs(x) \
	container_of(x, struct meson_venc_cvbs, encoder)

#define connector_to_meson_venc_cvbs(x) \
	container_of(x, struct meson_venc_cvbs, connector)

/* Supported Modes */

struct meson_cvbs_mode meson_cvbs_modes[MESON_CVBS_MODES_COUNT] = {
	{ /* PAL */
		.enci = &meson_cvbs_enci_pal,
		.mode = {
			DRM_MODE("720x576i", DRM_MODE_TYPE_DRIVER, 13500,
				 720, 732, 795, 864, 0, 576, 580, 586, 625, 0,
				 DRM_MODE_FLAG_INTERLACE),
			.vrefresh = 50,
			.picture_aspect_ratio = HDMI_PICTURE_ASPECT_4_3,
		},
	},
	{ /* NTSC */
		.enci = &meson_cvbs_enci_ntsc,
		.mode = {
			DRM_MODE("720x480i", DRM_MODE_TYPE_DRIVER, 13500,
				720, 739, 801, 858, 0, 480, 488, 494, 525, 0,
				DRM_MODE_FLAG_INTERLACE),
			.vrefresh = 60,
			.picture_aspect_ratio = HDMI_PICTURE_ASPECT_4_3,
		},
	},
};

/* Connector */

static void meson_cvbs_connector_destroy(struct drm_connector *connector)
{
	drm_connector_cleanup(connector);
}

static enum drm_connector_status
meson_cvbs_connector_detect(struct drm_connector *connector, bool force)
{
	/* FIXME: Add load-detect or jack-detect if possible */
	return connector_status_connected;
}

static int meson_cvbs_connector_get_modes(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct drm_display_mode *mode;
	int i;

	for (i = 0; i < MESON_CVBS_MODES_COUNT; ++i) {
		struct meson_cvbs_mode *meson_mode = &meson_cvbs_modes[i];

		mode = drm_mode_duplicate(dev, &meson_mode->mode);
		if (!mode) {
			DRM_ERROR("Failed to create a new display mode\n");
			return 0;
		}

		drm_mode_probed_add(connector, mode);
	}

	return i;
}

static int meson_cvbs_connector_mode_valid(struct drm_connector *connector,
					   struct drm_display_mode *mode)
{
	/* Validate the modes added in get_modes */
	return MODE_OK;
}

static const struct drm_connector_funcs meson_cvbs_connector_funcs = {
	.dpms			= drm_atomic_helper_connector_dpms,
	.detect			= meson_cvbs_connector_detect,
	.fill_modes		= drm_helper_probe_single_connector_modes,
	.destroy		= meson_cvbs_connector_destroy,
	.reset			= drm_atomic_helper_connector_reset,
	.atomic_duplicate_state	= drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_connector_destroy_state,
};

static const
struct drm_connector_helper_funcs meson_cvbs_connector_helper_funcs = {
	.get_modes	= meson_cvbs_connector_get_modes,
	.mode_valid	= meson_cvbs_connector_mode_valid,
};

/* Encoder */

static void meson_venc_cvbs_encoder_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
}

static const struct drm_encoder_funcs meson_venc_cvbs_encoder_funcs = {
	.destroy        = meson_venc_cvbs_encoder_destroy,
};

static int meson_venc_cvbs_encoder_atomic_check(struct drm_encoder *encoder,
					struct drm_crtc_state *crtc_state,
					struct drm_connector_state *conn_state)
{
	int i;

	for (i = 0; i < MESON_CVBS_MODES_COUNT; ++i) {
		struct meson_cvbs_mode *meson_mode = &meson_cvbs_modes[i];

		if (drm_mode_equal(&crtc_state->mode, &meson_mode->mode))
			return 0;
	}

	return -EINVAL;
}

static void meson_venc_cvbs_encoder_disable(struct drm_encoder *encoder)
{
	struct meson_venc_cvbs *meson_venc_cvbs =
					encoder_to_meson_venc_cvbs(encoder);
	struct meson_drm *priv = meson_venc_cvbs->priv;

	/* Disable CVBS VDAC */
	regmap_write(priv->hhi, HHI_VDAC_CNTL0, 0);
	regmap_write(priv->hhi, HHI_VDAC_CNTL1, 0);
}

static void meson_venc_cvbs_encoder_enable(struct drm_encoder *encoder)
{
	struct meson_venc_cvbs *meson_venc_cvbs =
					encoder_to_meson_venc_cvbs(encoder);
	struct meson_drm *priv = meson_venc_cvbs->priv;

	/* VDAC0 source is not from ATV */
	writel_bits_relaxed(BIT(5), 0, priv->io_base + _REG(VENC_VDAC_DACSEL0));

	if (meson_vpu_is_compatible(priv, "amlogic,meson-gxbb-vpu"))
		regmap_write(priv->hhi, HHI_VDAC_CNTL0, 1);
	else if (meson_vpu_is_compatible(priv, "amlogic,meson-gxm-vpu") ||
		 meson_vpu_is_compatible(priv, "amlogic,meson-gxl-vpu"))
		regmap_write(priv->hhi, HHI_VDAC_CNTL0, 0xf0001);

	regmap_write(priv->hhi, HHI_VDAC_CNTL1, 0);
}

static void meson_venc_cvbs_encoder_mode_set(struct drm_encoder *encoder,
				   struct drm_display_mode *mode,
				   struct drm_display_mode *adjusted_mode)
{
	struct meson_venc_cvbs *meson_venc_cvbs =
					encoder_to_meson_venc_cvbs(encoder);
	int i;

	for (i = 0; i < MESON_CVBS_MODES_COUNT; ++i) {
		struct meson_cvbs_mode *meson_mode = &meson_cvbs_modes[i];

		if (drm_mode_equal(mode, &meson_mode->mode)) {
			meson_venci_cvbs_mode_set(meson_venc_cvbs->priv,
						  meson_mode->enci);
			break;
		}
	}
}

static const struct drm_encoder_helper_funcs
				meson_venc_cvbs_encoder_helper_funcs = {
	.atomic_check	= meson_venc_cvbs_encoder_atomic_check,
	.disable	= meson_venc_cvbs_encoder_disable,
	.enable		= meson_venc_cvbs_encoder_enable,
	.mode_set	= meson_venc_cvbs_encoder_mode_set,
};

static bool meson_venc_cvbs_connector_is_available(struct meson_drm *priv)
{
	struct device_node *ep, *remote;

	/* CVBS VDAC output is on the first port, first endpoint */
	ep = of_graph_get_endpoint_by_regs(priv->dev->of_node, 0, 0);
	if (!ep)
		return false;


	/* If the endpoint node exists, consider it enabled */
	remote = of_graph_get_remote_port(ep);
	if (remote) {
		of_node_put(ep);
		return true;
	}

	of_node_put(ep);
	of_node_put(remote);

	return false;
}

int meson_venc_cvbs_create(struct meson_drm *priv)
{
	struct drm_device *drm = priv->drm;
	struct meson_venc_cvbs *meson_venc_cvbs;
	struct drm_connector *connector;
	struct drm_encoder *encoder;
	int ret;

	if (!meson_venc_cvbs_connector_is_available(priv)) {
		dev_info(drm->dev, "CVBS Output connector not available\n");
		return -ENODEV;
	}

	meson_venc_cvbs = devm_kzalloc(priv->dev, sizeof(*meson_venc_cvbs),
				       GFP_KERNEL);
	if (!meson_venc_cvbs)
		return -ENOMEM;

	meson_venc_cvbs->priv = priv;
	encoder = &meson_venc_cvbs->encoder;
	connector = &meson_venc_cvbs->connector;

	/* Connector */

	drm_connector_helper_add(connector,
				 &meson_cvbs_connector_helper_funcs);

	ret = drm_connector_init(drm, connector, &meson_cvbs_connector_funcs,
				 DRM_MODE_CONNECTOR_Composite);
	if (ret) {
		dev_err(priv->dev, "Failed to init CVBS connector\n");
		return ret;
	}

	connector->interlace_allowed = 1;

	/* Encoder */

	drm_encoder_helper_add(encoder, &meson_venc_cvbs_encoder_helper_funcs);

	ret = drm_encoder_init(drm, encoder, &meson_venc_cvbs_encoder_funcs,
			       DRM_MODE_ENCODER_TVDAC, "meson_venc_cvbs");
	if (ret) {
		dev_err(priv->dev, "Failed to init CVBS encoder\n");
		return ret;
	}

	encoder->possible_crtcs = BIT(0);

	drm_mode_connector_attach_encoder(connector, encoder);

	return 0;
}
