/*
 * Copyright © 2006-2007 Intel Corporation
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
 */

#include <linux/i2c.h>
#include "drmP.h"
#include "drm.h"
#include "drm_crtc.h"
#include "drm_crtc_helper.h"
#include "intel_drv.h"
#include "i915_drm.h"
#include "i915_drv.h"

static void intel_crt_dpms(struct drm_encoder *encoder, int mode)
{
	struct drm_device *dev = encoder->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 temp;

	temp = I915_READ(ADPA);
	temp &= ~(ADPA_HSYNC_CNTL_DISABLE | ADPA_VSYNC_CNTL_DISABLE);
	temp |= ADPA_DAC_ENABLE;

	switch(mode) {
	case DRM_MODE_DPMS_ON:
		temp |= ADPA_DAC_ENABLE;
		break;
	case DRM_MODE_DPMS_STANDBY:
		temp |= ADPA_DAC_ENABLE | ADPA_HSYNC_CNTL_DISABLE;
		break;
	case DRM_MODE_DPMS_SUSPEND:
		temp |= ADPA_DAC_ENABLE | ADPA_VSYNC_CNTL_DISABLE;
		break;
	case DRM_MODE_DPMS_OFF:
		temp |= ADPA_HSYNC_CNTL_DISABLE | ADPA_VSYNC_CNTL_DISABLE;
		break;
	}

	I915_WRITE(ADPA, temp);
}

static int intel_crt_mode_valid(struct drm_connector *connector,
				struct drm_display_mode *mode)
{
	struct drm_device *dev = connector->dev;

	int max_clock = 0;
	if (mode->flags & DRM_MODE_FLAG_DBLSCAN)
		return MODE_NO_DBLESCAN;

	if (mode->clock < 25000)
		return MODE_CLOCK_LOW;

	if (!IS_I9XX(dev))
		max_clock = 350000;
	else
		max_clock = 400000;
	if (mode->clock > max_clock)
		return MODE_CLOCK_HIGH;

	return MODE_OK;
}

static bool intel_crt_mode_fixup(struct drm_encoder *encoder,
				 struct drm_display_mode *mode,
				 struct drm_display_mode *adjusted_mode)
{
	return true;
}

static void intel_crt_mode_set(struct drm_encoder *encoder,
			       struct drm_display_mode *mode,
			       struct drm_display_mode *adjusted_mode)
{

	struct drm_device *dev = encoder->dev;
	struct drm_crtc *crtc = encoder->crtc;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct drm_i915_private *dev_priv = dev->dev_private;
	int dpll_md_reg;
	u32 adpa, dpll_md;

	if (intel_crtc->pipe == 0)
		dpll_md_reg = DPLL_A_MD;
	else
		dpll_md_reg = DPLL_B_MD;

	/*
	 * Disable separate mode multiplier used when cloning SDVO to CRT
	 * XXX this needs to be adjusted when we really are cloning
	 */
	if (IS_I965G(dev)) {
		dpll_md = I915_READ(dpll_md_reg);
		I915_WRITE(dpll_md_reg,
			   dpll_md & ~DPLL_MD_UDI_MULTIPLIER_MASK);
	}

	adpa = 0;
	if (adjusted_mode->flags & DRM_MODE_FLAG_PHSYNC)
		adpa |= ADPA_HSYNC_ACTIVE_HIGH;
	if (adjusted_mode->flags & DRM_MODE_FLAG_PVSYNC)
		adpa |= ADPA_VSYNC_ACTIVE_HIGH;

	if (intel_crtc->pipe == 0) {
		adpa |= ADPA_PIPE_A_SELECT;
		I915_WRITE(BCLRPAT_A, 0);
	} else {
		adpa |= ADPA_PIPE_B_SELECT;
		I915_WRITE(BCLRPAT_B, 0);
	}

	I915_WRITE(ADPA, adpa);
}

/**
 * Uses CRT_HOTPLUG_EN and CRT_HOTPLUG_STAT to detect CRT presence.
 *
 * Not for i915G/i915GM
 *
 * \return true if CRT is connected.
 * \return false if CRT is disconnected.
 */
static bool intel_crt_detect_hotplug(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 hotplug_en;
	int i, tries = 0;
	/*
	 * On 4 series desktop, CRT detect sequence need to be done twice
	 * to get a reliable result.
	 */

	if (IS_G4X(dev) && !IS_GM45(dev))
		tries = 2;
	else
		tries = 1;
	hotplug_en = I915_READ(PORT_HOTPLUG_EN);
	hotplug_en &= CRT_FORCE_HOTPLUG_MASK;
	hotplug_en |= CRT_HOTPLUG_FORCE_DETECT;

	if (IS_G4X(dev))
		hotplug_en |= CRT_HOTPLUG_ACTIVATION_PERIOD_64;

	hotplug_en |= CRT_HOTPLUG_VOLTAGE_COMPARE_50;

	for (i = 0; i < tries ; i++) {
		unsigned long timeout;
		/* turn on the FORCE_DETECT */
		I915_WRITE(PORT_HOTPLUG_EN, hotplug_en);
		timeout = jiffies + msecs_to_jiffies(1000);
		/* wait for FORCE_DETECT to go off */
		do {
			if (!(I915_READ(PORT_HOTPLUG_EN) &
					CRT_HOTPLUG_FORCE_DETECT))
				break;
			msleep(1);
		} while (time_after(timeout, jiffies));
	}

	if ((I915_READ(PORT_HOTPLUG_STAT) & CRT_HOTPLUG_MONITOR_MASK) ==
	    CRT_HOTPLUG_MONITOR_COLOR)
		return true;

	return false;
}

static bool intel_crt_detect_ddc(struct drm_connector *connector)
{
	struct intel_output *intel_output = to_intel_output(connector);

	/* CRT should always be at 0, but check anyway */
	if (intel_output->type != INTEL_OUTPUT_ANALOG)
		return false;

	return intel_ddc_probe(intel_output);
}

static enum drm_connector_status intel_crt_detect(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;

	if (IS_I9XX(dev) && !IS_I915G(dev) && !IS_I915GM(dev)) {
		if (intel_crt_detect_hotplug(connector))
			return connector_status_connected;
		else
			return connector_status_disconnected;
	}

	if (intel_crt_detect_ddc(connector))
		return connector_status_connected;

	/* TODO use load detect */
	return connector_status_unknown;
}

static void intel_crt_destroy(struct drm_connector *connector)
{
	struct intel_output *intel_output = to_intel_output(connector);

	intel_i2c_destroy(intel_output->ddc_bus);
	drm_sysfs_connector_remove(connector);
	drm_connector_cleanup(connector);
	kfree(connector);
}

static int intel_crt_get_modes(struct drm_connector *connector)
{
	struct intel_output *intel_output = to_intel_output(connector);
	return intel_ddc_get_modes(intel_output);
}

static int intel_crt_set_property(struct drm_connector *connector,
				  struct drm_property *property,
				  uint64_t value)
{
	struct drm_device *dev = connector->dev;

	if (property == dev->mode_config.dpms_property && connector->encoder)
		intel_crt_dpms(connector->encoder, (uint32_t)(value & 0xf));

	return 0;
}

/*
 * Routines for controlling stuff on the analog port
 */

static const struct drm_encoder_helper_funcs intel_crt_helper_funcs = {
	.dpms = intel_crt_dpms,
	.mode_fixup = intel_crt_mode_fixup,
	.prepare = intel_encoder_prepare,
	.commit = intel_encoder_commit,
	.mode_set = intel_crt_mode_set,
};

static const struct drm_connector_funcs intel_crt_connector_funcs = {
	.detect = intel_crt_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = intel_crt_destroy,
	.set_property = intel_crt_set_property,
};

static const struct drm_connector_helper_funcs intel_crt_connector_helper_funcs = {
	.mode_valid = intel_crt_mode_valid,
	.get_modes = intel_crt_get_modes,
	.best_encoder = intel_best_encoder,
};

static void intel_crt_enc_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
}

static const struct drm_encoder_funcs intel_crt_enc_funcs = {
	.destroy = intel_crt_enc_destroy,
};

void intel_crt_init(struct drm_device *dev)
{
	struct drm_connector *connector;
	struct intel_output *intel_output;

	intel_output = kzalloc(sizeof(struct intel_output), GFP_KERNEL);
	if (!intel_output)
		return;

	connector = &intel_output->base;
	drm_connector_init(dev, &intel_output->base,
			   &intel_crt_connector_funcs, DRM_MODE_CONNECTOR_VGA);

	drm_encoder_init(dev, &intel_output->enc, &intel_crt_enc_funcs,
			 DRM_MODE_ENCODER_DAC);

	drm_mode_connector_attach_encoder(&intel_output->base,
					  &intel_output->enc);

	/* Set up the DDC bus. */
	intel_output->ddc_bus = intel_i2c_create(dev, GPIOA, "CRTDDC_A");
	if (!intel_output->ddc_bus) {
		dev_printk(KERN_ERR, &dev->pdev->dev, "DDC bus registration "
			   "failed.\n");
		return;
	}

	intel_output->type = INTEL_OUTPUT_ANALOG;
	connector->interlace_allowed = 0;
	connector->doublescan_allowed = 0;

	drm_encoder_helper_add(&intel_output->enc, &intel_crt_helper_funcs);
	drm_connector_helper_add(connector, &intel_crt_connector_helper_funcs);

	drm_sysfs_connector_add(connector);
}
