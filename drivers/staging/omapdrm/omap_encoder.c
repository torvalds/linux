/*
 * drivers/staging/omapdrm/omap_encoder.c
 *
 * Copyright (C) 2011 Texas Instruments
 * Author: Rob Clark <rob@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "omap_drv.h"

#include "drm_crtc.h"
#include "drm_crtc_helper.h"

/*
 * encoder funcs
 */

#define to_omap_encoder(x) container_of(x, struct omap_encoder, base)

struct omap_encoder {
	struct drm_encoder base;
	struct omap_overlay_manager *mgr;
};

static void omap_encoder_destroy(struct drm_encoder *encoder)
{
	struct omap_encoder *omap_encoder = to_omap_encoder(encoder);
	DBG("%s", omap_encoder->mgr->name);
	drm_encoder_cleanup(encoder);
	kfree(omap_encoder);
}

static void omap_encoder_dpms(struct drm_encoder *encoder, int mode)
{
	struct omap_encoder *omap_encoder = to_omap_encoder(encoder);
	DBG("%s: %d", omap_encoder->mgr->name, mode);
}

static bool omap_encoder_mode_fixup(struct drm_encoder *encoder,
				  const struct drm_display_mode *mode,
				  struct drm_display_mode *adjusted_mode)
{
	struct omap_encoder *omap_encoder = to_omap_encoder(encoder);
	DBG("%s", omap_encoder->mgr->name);
	return true;
}

static void omap_encoder_mode_set(struct drm_encoder *encoder,
				struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode)
{
	struct omap_encoder *omap_encoder = to_omap_encoder(encoder);
	struct drm_device *dev = encoder->dev;
	struct omap_drm_private *priv = dev->dev_private;
	int i;

	mode = adjusted_mode;

	DBG("%s: set mode: %dx%d", omap_encoder->mgr->name,
			mode->hdisplay, mode->vdisplay);

	for (i = 0; i < priv->num_connectors; i++) {
		struct drm_connector *connector = priv->connectors[i];
		if (connector->encoder == encoder)
			omap_connector_mode_set(connector, mode);

	}
}

static void omap_encoder_prepare(struct drm_encoder *encoder)
{
	struct omap_encoder *omap_encoder = to_omap_encoder(encoder);
	struct drm_encoder_helper_funcs *encoder_funcs =
				encoder->helper_private;
	DBG("%s", omap_encoder->mgr->name);
	encoder_funcs->dpms(encoder, DRM_MODE_DPMS_OFF);
}

static void omap_encoder_commit(struct drm_encoder *encoder)
{
	struct omap_encoder *omap_encoder = to_omap_encoder(encoder);
	struct drm_encoder_helper_funcs *encoder_funcs =
				encoder->helper_private;
	DBG("%s", omap_encoder->mgr->name);
	omap_encoder->mgr->apply(omap_encoder->mgr);
	encoder_funcs->dpms(encoder, DRM_MODE_DPMS_ON);
}

static const struct drm_encoder_funcs omap_encoder_funcs = {
	.destroy = omap_encoder_destroy,
};

static const struct drm_encoder_helper_funcs omap_encoder_helper_funcs = {
	.dpms = omap_encoder_dpms,
	.mode_fixup = omap_encoder_mode_fixup,
	.mode_set = omap_encoder_mode_set,
	.prepare = omap_encoder_prepare,
	.commit = omap_encoder_commit,
};

struct omap_overlay_manager *omap_encoder_get_manager(
		struct drm_encoder *encoder)
{
	struct omap_encoder *omap_encoder = to_omap_encoder(encoder);
	return omap_encoder->mgr;
}

/* initialize encoder */
struct drm_encoder *omap_encoder_init(struct drm_device *dev,
		struct omap_overlay_manager *mgr)
{
	struct drm_encoder *encoder = NULL;
	struct omap_encoder *omap_encoder;
	struct omap_overlay_manager_info info;
	int ret;

	DBG("%s", mgr->name);

	omap_encoder = kzalloc(sizeof(*omap_encoder), GFP_KERNEL);
	if (!omap_encoder) {
		dev_err(dev->dev, "could not allocate encoder\n");
		goto fail;
	}

	omap_encoder->mgr = mgr;
	encoder = &omap_encoder->base;

	drm_encoder_init(dev, encoder, &omap_encoder_funcs,
			 DRM_MODE_ENCODER_TMDS);
	drm_encoder_helper_add(encoder, &omap_encoder_helper_funcs);

	mgr->get_manager_info(mgr, &info);

	/* TODO: fix hard-coded setup.. */
	info.default_color = 0x00000000;
	info.trans_key = 0x00000000;
	info.trans_key_type = OMAP_DSS_COLOR_KEY_GFX_DST;
	info.trans_enabled = false;

	ret = mgr->set_manager_info(mgr, &info);
	if (ret) {
		dev_err(dev->dev, "could not set manager info\n");
		goto fail;
	}

	ret = mgr->apply(mgr);
	if (ret) {
		dev_err(dev->dev, "could not apply\n");
		goto fail;
	}

	return encoder;

fail:
	if (encoder)
		omap_encoder_destroy(encoder);

	return NULL;
}
