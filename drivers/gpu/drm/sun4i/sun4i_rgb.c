/*
 * Copyright (C) 2015 Free Electrons
 * Copyright (C) 2015 NextThing Co
 *
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#include <linux/clk.h>

#include <drm/drmP.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_panel.h>

#include "sun4i_drv.h"
#include "sun4i_tcon.h"
#include "sun4i_rgb.h"

struct sun4i_rgb {
	struct drm_connector	connector;
	struct drm_encoder	encoder;

	struct sun4i_drv	*drv;
};

static inline struct sun4i_rgb *
drm_connector_to_sun4i_rgb(struct drm_connector *connector)
{
	return container_of(connector, struct sun4i_rgb,
			    connector);
}

static inline struct sun4i_rgb *
drm_encoder_to_sun4i_rgb(struct drm_encoder *encoder)
{
	return container_of(encoder, struct sun4i_rgb,
			    encoder);
}

static int sun4i_rgb_get_modes(struct drm_connector *connector)
{
	struct sun4i_rgb *rgb =
		drm_connector_to_sun4i_rgb(connector);
	struct sun4i_drv *drv = rgb->drv;
	struct sun4i_tcon *tcon = drv->tcon;

	return drm_panel_get_modes(tcon->panel);
}

static int sun4i_rgb_mode_valid(struct drm_connector *connector,
				struct drm_display_mode *mode)
{
	struct sun4i_rgb *rgb = drm_connector_to_sun4i_rgb(connector);
	struct sun4i_drv *drv = rgb->drv;
	struct sun4i_tcon *tcon = drv->tcon;
	u32 hsync = mode->hsync_end - mode->hsync_start;
	u32 vsync = mode->vsync_end - mode->vsync_start;
	unsigned long rate = mode->clock * 1000;
	long rounded_rate;

	DRM_DEBUG_DRIVER("Validating modes...\n");

	if (hsync < 1)
		return MODE_HSYNC_NARROW;

	if (hsync > 0x3ff)
		return MODE_HSYNC_WIDE;

	if ((mode->hdisplay < 1) || (mode->htotal < 1))
		return MODE_H_ILLEGAL;

	if ((mode->hdisplay > 0x7ff) || (mode->htotal > 0xfff))
		return MODE_BAD_HVALUE;

	DRM_DEBUG_DRIVER("Horizontal parameters OK\n");

	if (vsync < 1)
		return MODE_VSYNC_NARROW;

	if (vsync > 0x3ff)
		return MODE_VSYNC_WIDE;

	if ((mode->vdisplay < 1) || (mode->vtotal < 1))
		return MODE_V_ILLEGAL;

	if ((mode->vdisplay > 0x7ff) || (mode->vtotal > 0xfff))
		return MODE_BAD_VVALUE;

	DRM_DEBUG_DRIVER("Vertical parameters OK\n");

	rounded_rate = clk_round_rate(tcon->dclk, rate);
	if (rounded_rate < rate)
		return MODE_CLOCK_LOW;

	if (rounded_rate > rate)
		return MODE_CLOCK_HIGH;

	DRM_DEBUG_DRIVER("Clock rate OK\n");

	return MODE_OK;
}

static struct drm_connector_helper_funcs sun4i_rgb_con_helper_funcs = {
	.get_modes	= sun4i_rgb_get_modes,
	.mode_valid	= sun4i_rgb_mode_valid,
};

static enum drm_connector_status
sun4i_rgb_connector_detect(struct drm_connector *connector, bool force)
{
	return connector_status_connected;
}

static void
sun4i_rgb_connector_destroy(struct drm_connector *connector)
{
	struct sun4i_rgb *rgb = drm_connector_to_sun4i_rgb(connector);
	struct sun4i_drv *drv = rgb->drv;
	struct sun4i_tcon *tcon = drv->tcon;

	drm_panel_detach(tcon->panel);
	drm_connector_cleanup(connector);
}

static struct drm_connector_funcs sun4i_rgb_con_funcs = {
	.dpms			= drm_atomic_helper_connector_dpms,
	.detect			= sun4i_rgb_connector_detect,
	.fill_modes		= drm_helper_probe_single_connector_modes,
	.destroy		= sun4i_rgb_connector_destroy,
	.reset			= drm_atomic_helper_connector_reset,
	.atomic_duplicate_state	= drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_connector_destroy_state,
};

static int sun4i_rgb_atomic_check(struct drm_encoder *encoder,
				  struct drm_crtc_state *crtc_state,
				  struct drm_connector_state *conn_state)
{
	return 0;
}

static void sun4i_rgb_encoder_enable(struct drm_encoder *encoder)
{
	struct sun4i_rgb *rgb = drm_encoder_to_sun4i_rgb(encoder);
	struct sun4i_drv *drv = rgb->drv;
	struct sun4i_tcon *tcon = drv->tcon;

	DRM_DEBUG_DRIVER("Enabling RGB output\n");

	if (!IS_ERR(tcon->panel))
		drm_panel_prepare(tcon->panel);

	sun4i_tcon_channel_enable(tcon, 0);

	if (!IS_ERR(tcon->panel))
		drm_panel_enable(tcon->panel);
}

static void sun4i_rgb_encoder_disable(struct drm_encoder *encoder)
{
	struct sun4i_rgb *rgb = drm_encoder_to_sun4i_rgb(encoder);
	struct sun4i_drv *drv = rgb->drv;
	struct sun4i_tcon *tcon = drv->tcon;

	DRM_DEBUG_DRIVER("Disabling RGB output\n");

	if (!IS_ERR(tcon->panel))
		drm_panel_disable(tcon->panel);

	sun4i_tcon_channel_disable(tcon, 0);

	if (!IS_ERR(tcon->panel))
		drm_panel_unprepare(tcon->panel);
}

static void sun4i_rgb_encoder_mode_set(struct drm_encoder *encoder,
				       struct drm_display_mode *mode,
				       struct drm_display_mode *adjusted_mode)
{
	struct sun4i_rgb *rgb = drm_encoder_to_sun4i_rgb(encoder);
	struct sun4i_drv *drv = rgb->drv;
	struct sun4i_tcon *tcon = drv->tcon;

	sun4i_tcon0_mode_set(tcon, mode);

	clk_set_rate(tcon->dclk, mode->crtc_clock * 1000);

	/* FIXME: This seems to be board specific */
	clk_set_phase(tcon->dclk, 120);
}

static struct drm_encoder_helper_funcs sun4i_rgb_enc_helper_funcs = {
	.atomic_check	= sun4i_rgb_atomic_check,
	.mode_set	= sun4i_rgb_encoder_mode_set,
	.disable	= sun4i_rgb_encoder_disable,
	.enable		= sun4i_rgb_encoder_enable,
};

static void sun4i_rgb_enc_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
}

static struct drm_encoder_funcs sun4i_rgb_enc_funcs = {
	.destroy	= sun4i_rgb_enc_destroy,
};

int sun4i_rgb_init(struct drm_device *drm)
{
	struct sun4i_drv *drv = drm->dev_private;
	struct sun4i_tcon *tcon = drv->tcon;
	struct drm_encoder *encoder;
	struct sun4i_rgb *rgb;
	int ret;

	rgb = devm_kzalloc(drm->dev, sizeof(*rgb), GFP_KERNEL);
	if (!rgb)
		return -ENOMEM;
	rgb->drv = drv;
	encoder = &rgb->encoder;

	tcon->panel = sun4i_tcon_find_panel(tcon->dev->of_node);
	encoder->bridge = sun4i_tcon_find_bridge(tcon->dev->of_node);
	if (IS_ERR(tcon->panel) && IS_ERR(encoder->bridge)) {
		dev_info(drm->dev, "No panel or bridge found... RGB output disabled\n");
		return 0;
	}

	drm_encoder_helper_add(&rgb->encoder,
			       &sun4i_rgb_enc_helper_funcs);
	ret = drm_encoder_init(drm,
			       &rgb->encoder,
			       &sun4i_rgb_enc_funcs,
			       DRM_MODE_ENCODER_NONE,
			       NULL);
	if (ret) {
		dev_err(drm->dev, "Couldn't initialise the rgb encoder\n");
		goto err_out;
	}

	/* The RGB encoder can only work with the TCON channel 0 */
	rgb->encoder.possible_crtcs = BIT(0);

	if (!IS_ERR(tcon->panel)) {
		drm_connector_helper_add(&rgb->connector,
					 &sun4i_rgb_con_helper_funcs);
		ret = drm_connector_init(drm, &rgb->connector,
					 &sun4i_rgb_con_funcs,
					 DRM_MODE_CONNECTOR_Unknown);
		if (ret) {
			dev_err(drm->dev, "Couldn't initialise the rgb connector\n");
			goto err_cleanup_connector;
		}

		drm_mode_connector_attach_encoder(&rgb->connector,
						  &rgb->encoder);

		ret = drm_panel_attach(tcon->panel, &rgb->connector);
		if (ret) {
			dev_err(drm->dev, "Couldn't attach our panel\n");
			goto err_cleanup_connector;
		}
	}

	if (!IS_ERR(encoder->bridge)) {
		encoder->bridge->encoder = &rgb->encoder;

		ret = drm_bridge_attach(drm, encoder->bridge);
		if (ret) {
			dev_err(drm->dev, "Couldn't attach our bridge\n");
			goto err_cleanup_connector;
		}
	} else {
		encoder->bridge = NULL;
	}

	return 0;

err_cleanup_connector:
	drm_encoder_cleanup(&rgb->encoder);
err_out:
	return ret;
}
EXPORT_SYMBOL(sun4i_rgb_init);
