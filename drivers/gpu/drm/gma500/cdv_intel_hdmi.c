/*
 * Copyright © 2006-2011 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *	jim liu <jim.liu@intel.com>
 */

#include <linux/pm_runtime.h>

#include <drm/drm.h>
#include <drm/drm_crtc.h>
#include <drm/drm_edid.h>
#include <drm/drm_simple_kms_helper.h>

#include "cdv_device.h"
#include "psb_drv.h"
#include "psb_intel_drv.h"
#include "psb_intel_reg.h"

/* hdmi control bits */
#define HDMI_NULL_PACKETS_DURING_VSYNC	(1 << 9)
#define HDMI_BORDER_ENABLE		(1 << 7)
#define HDMI_AUDIO_ENABLE		(1 << 6)
#define HDMI_VSYNC_ACTIVE_HIGH		(1 << 4)
#define HDMI_HSYNC_ACTIVE_HIGH		(1 << 3)
/* hdmi-b control bits */
#define	HDMIB_PIPE_B_SELECT		(1 << 30)


struct mid_intel_hdmi_priv {
	u32 hdmi_reg;
	u32 save_HDMIB;
	bool has_hdmi_sink;
	bool has_hdmi_audio;
	/* Should set this when detect hotplug */
	bool hdmi_device_connected;
	struct i2c_adapter *hdmi_i2c_adapter;	/* for control functions */
	struct drm_device *dev;
};

static void cdv_hdmi_mode_set(struct drm_encoder *encoder,
			struct drm_display_mode *mode,
			struct drm_display_mode *adjusted_mode)
{
	struct drm_device *dev = encoder->dev;
	struct gma_encoder *gma_encoder = to_gma_encoder(encoder);
	struct mid_intel_hdmi_priv *hdmi_priv = gma_encoder->dev_priv;
	u32 hdmib;
	struct drm_crtc *crtc = encoder->crtc;
	struct gma_crtc *gma_crtc = to_gma_crtc(crtc);

	hdmib = (2 << 10);

	if (adjusted_mode->flags & DRM_MODE_FLAG_PVSYNC)
		hdmib |= HDMI_VSYNC_ACTIVE_HIGH;
	if (adjusted_mode->flags & DRM_MODE_FLAG_PHSYNC)
		hdmib |= HDMI_HSYNC_ACTIVE_HIGH;

	if (gma_crtc->pipe == 1)
		hdmib |= HDMIB_PIPE_B_SELECT;

	if (hdmi_priv->has_hdmi_audio) {
		hdmib |= HDMI_AUDIO_ENABLE;
		hdmib |= HDMI_NULL_PACKETS_DURING_VSYNC;
	}

	REG_WRITE(hdmi_priv->hdmi_reg, hdmib);
	REG_READ(hdmi_priv->hdmi_reg);
}

static void cdv_hdmi_dpms(struct drm_encoder *encoder, int mode)
{
	struct drm_device *dev = encoder->dev;
	struct gma_encoder *gma_encoder = to_gma_encoder(encoder);
	struct mid_intel_hdmi_priv *hdmi_priv = gma_encoder->dev_priv;
	u32 hdmib;

	hdmib = REG_READ(hdmi_priv->hdmi_reg);

	if (mode != DRM_MODE_DPMS_ON)
		REG_WRITE(hdmi_priv->hdmi_reg, hdmib & ~HDMIB_PORT_EN);
	else
		REG_WRITE(hdmi_priv->hdmi_reg, hdmib | HDMIB_PORT_EN);
	REG_READ(hdmi_priv->hdmi_reg);
}

static void cdv_hdmi_save(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct gma_encoder *gma_encoder = gma_attached_encoder(connector);
	struct mid_intel_hdmi_priv *hdmi_priv = gma_encoder->dev_priv;

	hdmi_priv->save_HDMIB = REG_READ(hdmi_priv->hdmi_reg);
}

static void cdv_hdmi_restore(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct gma_encoder *gma_encoder = gma_attached_encoder(connector);
	struct mid_intel_hdmi_priv *hdmi_priv = gma_encoder->dev_priv;

	REG_WRITE(hdmi_priv->hdmi_reg, hdmi_priv->save_HDMIB);
	REG_READ(hdmi_priv->hdmi_reg);
}

static enum drm_connector_status cdv_hdmi_detect(
				struct drm_connector *connector, bool force)
{
	struct gma_encoder *gma_encoder = gma_attached_encoder(connector);
	struct mid_intel_hdmi_priv *hdmi_priv = gma_encoder->dev_priv;
	struct edid *edid = NULL;
	enum drm_connector_status status = connector_status_disconnected;

	edid = drm_get_edid(connector, &gma_encoder->i2c_bus->adapter);

	hdmi_priv->has_hdmi_sink = false;
	hdmi_priv->has_hdmi_audio = false;
	if (edid) {
		if (edid->input & DRM_EDID_INPUT_DIGITAL) {
			status = connector_status_connected;
			hdmi_priv->has_hdmi_sink =
						drm_detect_hdmi_monitor(edid);
			hdmi_priv->has_hdmi_audio =
						drm_detect_monitor_audio(edid);
		}
		kfree(edid);
	}
	return status;
}

static int cdv_hdmi_set_property(struct drm_connector *connector,
				       struct drm_property *property,
				       uint64_t value)
{
	struct drm_encoder *encoder = connector->encoder;

	if (!strcmp(property->name, "scaling mode") && encoder) {
		struct gma_crtc *crtc = to_gma_crtc(encoder->crtc);
		bool centre;
		uint64_t curValue;

		if (!crtc)
			return -1;

		switch (value) {
		case DRM_MODE_SCALE_FULLSCREEN:
			break;
		case DRM_MODE_SCALE_NO_SCALE:
			break;
		case DRM_MODE_SCALE_ASPECT:
			break;
		default:
			return -1;
		}

		if (drm_object_property_get_value(&connector->base,
							property, &curValue))
			return -1;

		if (curValue == value)
			return 0;

		if (drm_object_property_set_value(&connector->base,
							property, value))
			return -1;

		centre = (curValue == DRM_MODE_SCALE_NO_SCALE) ||
			(value == DRM_MODE_SCALE_NO_SCALE);

		if (crtc->saved_mode.hdisplay != 0 &&
		    crtc->saved_mode.vdisplay != 0) {
			if (centre) {
				if (!drm_crtc_helper_set_mode(encoder->crtc, &crtc->saved_mode,
					    encoder->crtc->x, encoder->crtc->y, encoder->crtc->primary->fb))
					return -1;
			} else {
				const struct drm_encoder_helper_funcs *helpers
						    = encoder->helper_private;
				helpers->mode_set(encoder, &crtc->saved_mode,
					     &crtc->saved_adjusted_mode);
			}
		}
	}
	return 0;
}

/*
 * Return the list of HDMI DDC modes if available.
 */
static int cdv_hdmi_get_modes(struct drm_connector *connector)
{
	struct gma_encoder *gma_encoder = gma_attached_encoder(connector);
	struct edid *edid = NULL;
	int ret = 0;

	edid = drm_get_edid(connector, &gma_encoder->i2c_bus->adapter);
	if (edid) {
		drm_connector_update_edid_property(connector, edid);
		ret = drm_add_edid_modes(connector, edid);
		kfree(edid);
	}
	return ret;
}

static enum drm_mode_status cdv_hdmi_mode_valid(struct drm_connector *connector,
				 struct drm_display_mode *mode)
{
	if (mode->clock > 165000)
		return MODE_CLOCK_HIGH;
	if (mode->clock < 20000)
		return MODE_CLOCK_HIGH;

	/* just in case */
	if (mode->flags & DRM_MODE_FLAG_DBLSCAN)
		return MODE_NO_DBLESCAN;

	/* just in case */
	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		return MODE_NO_INTERLACE;

	return MODE_OK;
}

static void cdv_hdmi_destroy(struct drm_connector *connector)
{
	struct gma_encoder *gma_encoder = gma_attached_encoder(connector);

	psb_intel_i2c_destroy(gma_encoder->i2c_bus);
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
	kfree(connector);
}

static const struct drm_encoder_helper_funcs cdv_hdmi_helper_funcs = {
	.dpms = cdv_hdmi_dpms,
	.prepare = gma_encoder_prepare,
	.mode_set = cdv_hdmi_mode_set,
	.commit = gma_encoder_commit,
};

static const struct drm_connector_helper_funcs
					cdv_hdmi_connector_helper_funcs = {
	.get_modes = cdv_hdmi_get_modes,
	.mode_valid = cdv_hdmi_mode_valid,
	.best_encoder = gma_best_encoder,
};

static const struct drm_connector_funcs cdv_hdmi_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.detect = cdv_hdmi_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.set_property = cdv_hdmi_set_property,
	.destroy = cdv_hdmi_destroy,
};

void cdv_hdmi_init(struct drm_device *dev,
			struct psb_intel_mode_device *mode_dev, int reg)
{
	struct gma_encoder *gma_encoder;
	struct gma_connector *gma_connector;
	struct drm_connector *connector;
	struct drm_encoder *encoder;
	struct mid_intel_hdmi_priv *hdmi_priv;
	int ddc_bus;

	gma_encoder = kzalloc(sizeof(struct gma_encoder), GFP_KERNEL);

	if (!gma_encoder)
		return;

	gma_connector = kzalloc(sizeof(struct gma_connector),
				      GFP_KERNEL);

	if (!gma_connector)
		goto err_connector;

	hdmi_priv = kzalloc(sizeof(struct mid_intel_hdmi_priv), GFP_KERNEL);

	if (!hdmi_priv)
		goto err_priv;

	connector = &gma_connector->base;
	connector->polled = DRM_CONNECTOR_POLL_HPD;
	gma_connector->save = cdv_hdmi_save;
	gma_connector->restore = cdv_hdmi_restore;

	encoder = &gma_encoder->base;
	drm_connector_init(dev, connector,
			   &cdv_hdmi_connector_funcs,
			   DRM_MODE_CONNECTOR_DVID);

	drm_simple_encoder_init(dev, encoder, DRM_MODE_ENCODER_TMDS);

	gma_connector_attach_encoder(gma_connector, gma_encoder);
	gma_encoder->type = INTEL_OUTPUT_HDMI;
	hdmi_priv->hdmi_reg = reg;
	hdmi_priv->has_hdmi_sink = false;
	gma_encoder->dev_priv = hdmi_priv;

	drm_encoder_helper_add(encoder, &cdv_hdmi_helper_funcs);
	drm_connector_helper_add(connector,
				 &cdv_hdmi_connector_helper_funcs);
	connector->display_info.subpixel_order = SubPixelHorizontalRGB;
	connector->interlace_allowed = false;
	connector->doublescan_allowed = false;

	drm_object_attach_property(&connector->base,
				      dev->mode_config.scaling_mode_property,
				      DRM_MODE_SCALE_FULLSCREEN);

	switch (reg) {
	case SDVOB:
		ddc_bus = GPIOE;
		gma_encoder->ddi_select = DDI0_SELECT;
		break;
	case SDVOC:
		ddc_bus = GPIOD;
		gma_encoder->ddi_select = DDI1_SELECT;
		break;
	default:
		DRM_ERROR("unknown reg 0x%x for HDMI\n", reg);
		goto failed_ddc;
		break;
	}

	gma_encoder->i2c_bus = psb_intel_i2c_create(dev,
				ddc_bus, (reg == SDVOB) ? "HDMIB" : "HDMIC");

	if (!gma_encoder->i2c_bus) {
		dev_err(dev->dev, "No ddc adapter available!\n");
		goto failed_ddc;
	}

	hdmi_priv->hdmi_i2c_adapter = &(gma_encoder->i2c_bus->adapter);
	hdmi_priv->dev = dev;
	drm_connector_register(connector);
	return;

failed_ddc:
	drm_encoder_cleanup(encoder);
	drm_connector_cleanup(connector);
err_priv:
	kfree(gma_connector);
err_connector:
	kfree(gma_encoder);
}
