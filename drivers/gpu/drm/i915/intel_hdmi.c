/*
 * Copyright 2006 Dave Airlie <airlied@linux.ie>
 * Copyright © 2006-2009 Intel Corporation
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
 *	Eric Anholt <eric@anholt.net>
 *	Jesse Barnes <jesse.barnes@intel.com>
 */

#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include "drmP.h"
#include "drm.h"
#include "drm_crtc.h"
#include "drm_edid.h"
#include "intel_drv.h"
#include "i915_drm.h"
#include "i915_drv.h"

struct intel_hdmi_priv {
	u32 sdvox_reg;
	u32 save_SDVOX;
	bool has_hdmi_sink;
};

static void intel_hdmi_mode_set(struct drm_encoder *encoder,
				struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode)
{
	struct drm_device *dev = encoder->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_crtc *crtc = encoder->crtc;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct intel_encoder *intel_encoder = enc_to_intel_encoder(encoder);
	struct intel_hdmi_priv *hdmi_priv = intel_encoder->dev_priv;
	u32 sdvox;

	sdvox = SDVO_ENCODING_HDMI |
		SDVO_BORDER_ENABLE |
		SDVO_VSYNC_ACTIVE_HIGH |
		SDVO_HSYNC_ACTIVE_HIGH;

	if (hdmi_priv->has_hdmi_sink)
		sdvox |= SDVO_AUDIO_ENABLE;

	if (intel_crtc->pipe == 1)
		sdvox |= SDVO_PIPE_B_SELECT;

	I915_WRITE(hdmi_priv->sdvox_reg, sdvox);
	POSTING_READ(hdmi_priv->sdvox_reg);
}

static void intel_hdmi_dpms(struct drm_encoder *encoder, int mode)
{
	struct drm_device *dev = encoder->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_encoder *intel_encoder = enc_to_intel_encoder(encoder);
	struct intel_hdmi_priv *hdmi_priv = intel_encoder->dev_priv;
	u32 temp;

	temp = I915_READ(hdmi_priv->sdvox_reg);

	/* HW workaround, need to toggle enable bit off and on for 12bpc, but
	 * we do this anyway which shows more stable in testing.
	 */
	if (HAS_PCH_SPLIT(dev)) {
		I915_WRITE(hdmi_priv->sdvox_reg, temp & ~SDVO_ENABLE);
		POSTING_READ(hdmi_priv->sdvox_reg);
	}

	if (mode != DRM_MODE_DPMS_ON) {
		temp &= ~SDVO_ENABLE;
	} else {
		temp |= SDVO_ENABLE;
	}

	I915_WRITE(hdmi_priv->sdvox_reg, temp);
	POSTING_READ(hdmi_priv->sdvox_reg);

	/* HW workaround, need to write this twice for issue that may result
	 * in first write getting masked.
	 */
	if (HAS_PCH_SPLIT(dev)) {
		I915_WRITE(hdmi_priv->sdvox_reg, temp);
		POSTING_READ(hdmi_priv->sdvox_reg);
	}
}

static void intel_hdmi_save(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_encoder *intel_encoder = to_intel_encoder(connector);
	struct intel_hdmi_priv *hdmi_priv = intel_encoder->dev_priv;

	hdmi_priv->save_SDVOX = I915_READ(hdmi_priv->sdvox_reg);
}

static void intel_hdmi_restore(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_encoder *intel_encoder = to_intel_encoder(connector);
	struct intel_hdmi_priv *hdmi_priv = intel_encoder->dev_priv;

	I915_WRITE(hdmi_priv->sdvox_reg, hdmi_priv->save_SDVOX);
	POSTING_READ(hdmi_priv->sdvox_reg);
}

static int intel_hdmi_mode_valid(struct drm_connector *connector,
				 struct drm_display_mode *mode)
{
	if (mode->clock > 165000)
		return MODE_CLOCK_HIGH;
	if (mode->clock < 20000)
		return MODE_CLOCK_HIGH;

	if (mode->flags & DRM_MODE_FLAG_DBLSCAN)
		return MODE_NO_DBLESCAN;

	return MODE_OK;
}

static bool intel_hdmi_mode_fixup(struct drm_encoder *encoder,
				  struct drm_display_mode *mode,
				  struct drm_display_mode *adjusted_mode)
{
	return true;
}

static enum drm_connector_status
intel_hdmi_detect(struct drm_connector *connector)
{
	struct intel_encoder *intel_encoder = to_intel_encoder(connector);
	struct intel_hdmi_priv *hdmi_priv = intel_encoder->dev_priv;
	struct edid *edid = NULL;
	enum drm_connector_status status = connector_status_disconnected;

	hdmi_priv->has_hdmi_sink = false;
	edid = drm_get_edid(&intel_encoder->base,
			    intel_encoder->ddc_bus);

	if (edid) {
		if (edid->input & DRM_EDID_INPUT_DIGITAL) {
			status = connector_status_connected;
			hdmi_priv->has_hdmi_sink = drm_detect_hdmi_monitor(edid);
		}
		intel_encoder->base.display_info.raw_edid = NULL;
		kfree(edid);
	}

	return status;
}

static int intel_hdmi_get_modes(struct drm_connector *connector)
{
	struct intel_encoder *intel_encoder = to_intel_encoder(connector);

	/* We should parse the EDID data and find out if it's an HDMI sink so
	 * we can send audio to it.
	 */

	return intel_ddc_get_modes(intel_encoder);
}

static void intel_hdmi_destroy(struct drm_connector *connector)
{
	struct intel_encoder *intel_encoder = to_intel_encoder(connector);

	if (intel_encoder->i2c_bus)
		intel_i2c_destroy(intel_encoder->i2c_bus);
	drm_sysfs_connector_remove(connector);
	drm_connector_cleanup(connector);
	kfree(intel_encoder);
}

static const struct drm_encoder_helper_funcs intel_hdmi_helper_funcs = {
	.dpms = intel_hdmi_dpms,
	.mode_fixup = intel_hdmi_mode_fixup,
	.prepare = intel_encoder_prepare,
	.mode_set = intel_hdmi_mode_set,
	.commit = intel_encoder_commit,
};

static const struct drm_connector_funcs intel_hdmi_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.save = intel_hdmi_save,
	.restore = intel_hdmi_restore,
	.detect = intel_hdmi_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = intel_hdmi_destroy,
};

static const struct drm_connector_helper_funcs intel_hdmi_connector_helper_funcs = {
	.get_modes = intel_hdmi_get_modes,
	.mode_valid = intel_hdmi_mode_valid,
	.best_encoder = intel_best_encoder,
};

static void intel_hdmi_enc_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
}

static const struct drm_encoder_funcs intel_hdmi_enc_funcs = {
	.destroy = intel_hdmi_enc_destroy,
};

void intel_hdmi_init(struct drm_device *dev, int sdvox_reg)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_connector *connector;
	struct intel_encoder *intel_encoder;
	struct intel_hdmi_priv *hdmi_priv;

	intel_encoder = kcalloc(sizeof(struct intel_encoder) +
			       sizeof(struct intel_hdmi_priv), 1, GFP_KERNEL);
	if (!intel_encoder)
		return;
	hdmi_priv = (struct intel_hdmi_priv *)(intel_encoder + 1);

	connector = &intel_encoder->base;
	drm_connector_init(dev, connector, &intel_hdmi_connector_funcs,
			   DRM_MODE_CONNECTOR_HDMIA);
	drm_connector_helper_add(connector, &intel_hdmi_connector_helper_funcs);

	intel_encoder->type = INTEL_OUTPUT_HDMI;

	connector->interlace_allowed = 0;
	connector->doublescan_allowed = 0;
	intel_encoder->crtc_mask = (1 << 0) | (1 << 1);

	/* Set up the DDC bus. */
	if (sdvox_reg == SDVOB) {
		intel_encoder->clone_mask = (1 << INTEL_HDMIB_CLONE_BIT);
		intel_encoder->ddc_bus = intel_i2c_create(dev, GPIOE, "HDMIB");
		dev_priv->hotplug_supported_mask |= HDMIB_HOTPLUG_INT_STATUS;
	} else if (sdvox_reg == SDVOC) {
		intel_encoder->clone_mask = (1 << INTEL_HDMIC_CLONE_BIT);
		intel_encoder->ddc_bus = intel_i2c_create(dev, GPIOD, "HDMIC");
		dev_priv->hotplug_supported_mask |= HDMIC_HOTPLUG_INT_STATUS;
	} else if (sdvox_reg == HDMIB) {
		intel_encoder->clone_mask = (1 << INTEL_HDMID_CLONE_BIT);
		intel_encoder->ddc_bus = intel_i2c_create(dev, PCH_GPIOE,
								"HDMIB");
		dev_priv->hotplug_supported_mask |= HDMIB_HOTPLUG_INT_STATUS;
	} else if (sdvox_reg == HDMIC) {
		intel_encoder->clone_mask = (1 << INTEL_HDMIE_CLONE_BIT);
		intel_encoder->ddc_bus = intel_i2c_create(dev, PCH_GPIOD,
								"HDMIC");
		dev_priv->hotplug_supported_mask |= HDMIC_HOTPLUG_INT_STATUS;
	} else if (sdvox_reg == HDMID) {
		intel_encoder->clone_mask = (1 << INTEL_HDMIF_CLONE_BIT);
		intel_encoder->ddc_bus = intel_i2c_create(dev, PCH_GPIOF,
								"HDMID");
		dev_priv->hotplug_supported_mask |= HDMID_HOTPLUG_INT_STATUS;
	}
	if (!intel_encoder->ddc_bus)
		goto err_connector;

	hdmi_priv->sdvox_reg = sdvox_reg;
	intel_encoder->dev_priv = hdmi_priv;

	drm_encoder_init(dev, &intel_encoder->enc, &intel_hdmi_enc_funcs,
			 DRM_MODE_ENCODER_TMDS);
	drm_encoder_helper_add(&intel_encoder->enc, &intel_hdmi_helper_funcs);

	drm_mode_connector_attach_encoder(&intel_encoder->base,
					  &intel_encoder->enc);
	drm_sysfs_connector_add(connector);

	/* For G4X desktop chip, PEG_BAND_GAP_DATA 3:0 must first be written
	 * 0xd.  Failure to do so will result in spurious interrupts being
	 * generated on the port when a cable is not attached.
	 */
	if (IS_G4X(dev) && !IS_GM45(dev)) {
		u32 temp = I915_READ(PEG_BAND_GAP_DATA);
		I915_WRITE(PEG_BAND_GAP_DATA, (temp & ~0xf) | 0xd);
	}

	return;

err_connector:
	drm_connector_cleanup(connector);
	kfree(intel_encoder);

	return;
}
