/*
 * Copyright 2006 Dave Airlie <airlied@linux.ie>
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
#include <linux/slab.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>

#include "i915_drv.h"
#include "i915_reg.h"
#include "intel_connector.h"
#include "intel_de.h"
#include "intel_display_types.h"
#include "intel_dvo.h"
#include "intel_dvo_dev.h"
#include "intel_dvo_regs.h"
#include "intel_gmbus.h"
#include "intel_panel.h"

#define INTEL_DVO_CHIP_NONE	0
#define INTEL_DVO_CHIP_LVDS	1
#define INTEL_DVO_CHIP_TMDS	2
#define INTEL_DVO_CHIP_TVOUT	4
#define INTEL_DVO_CHIP_LVDS_NO_FIXED	5

#define SIL164_ADDR	0x38
#define CH7xxx_ADDR	0x76
#define TFP410_ADDR	0x38
#define NS2501_ADDR     0x38

static const struct intel_dvo_device intel_dvo_devices[] = {
	{
		.type = INTEL_DVO_CHIP_TMDS,
		.name = "sil164",
		.port = PORT_C,
		.slave_addr = SIL164_ADDR,
		.dev_ops = &sil164_ops,
	},
	{
		.type = INTEL_DVO_CHIP_TMDS,
		.name = "ch7xxx",
		.port = PORT_C,
		.slave_addr = CH7xxx_ADDR,
		.dev_ops = &ch7xxx_ops,
	},
	{
		.type = INTEL_DVO_CHIP_TMDS,
		.name = "ch7xxx",
		.port = PORT_C,
		.slave_addr = 0x75, /* For some ch7010 */
		.dev_ops = &ch7xxx_ops,
	},
	{
		.type = INTEL_DVO_CHIP_LVDS,
		.name = "ivch",
		.port = PORT_A,
		.slave_addr = 0x02, /* Might also be 0x44, 0x84, 0xc4 */
		.dev_ops = &ivch_ops,
	},
	{
		.type = INTEL_DVO_CHIP_TMDS,
		.name = "tfp410",
		.port = PORT_C,
		.slave_addr = TFP410_ADDR,
		.dev_ops = &tfp410_ops,
	},
	{
		.type = INTEL_DVO_CHIP_LVDS,
		.name = "ch7017",
		.port = PORT_C,
		.slave_addr = 0x75,
		.gpio = GMBUS_PIN_DPB,
		.dev_ops = &ch7017_ops,
	},
	{
		.type = INTEL_DVO_CHIP_LVDS_NO_FIXED,
		.name = "ns2501",
		.port = PORT_B,
		.slave_addr = NS2501_ADDR,
		.dev_ops = &ns2501_ops,
	},
};

struct intel_dvo {
	struct intel_encoder base;

	struct intel_dvo_device dev;

	struct intel_connector *attached_connector;
};

static struct intel_dvo *enc_to_dvo(struct intel_encoder *encoder)
{
	return container_of(encoder, struct intel_dvo, base);
}

static struct intel_dvo *intel_attached_dvo(struct intel_connector *connector)
{
	return enc_to_dvo(intel_attached_encoder(connector));
}

static bool intel_dvo_connector_get_hw_state(struct intel_connector *connector)
{
	struct drm_i915_private *i915 = to_i915(connector->base.dev);
	struct intel_encoder *encoder = intel_attached_encoder(connector);
	struct intel_dvo *intel_dvo = enc_to_dvo(encoder);
	enum port port = encoder->port;
	u32 tmp;

	tmp = intel_de_read(i915, DVO(port));

	if (!(tmp & DVO_ENABLE))
		return false;

	return intel_dvo->dev.dev_ops->get_hw_state(&intel_dvo->dev);
}

static bool intel_dvo_get_hw_state(struct intel_encoder *encoder,
				   enum pipe *pipe)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	enum port port = encoder->port;
	u32 tmp;

	tmp = intel_de_read(i915, DVO(port));

	*pipe = REG_FIELD_GET(DVO_PIPE_SEL_MASK, tmp);

	return tmp & DVO_ENABLE;
}

static void intel_dvo_get_config(struct intel_encoder *encoder,
				 struct intel_crtc_state *pipe_config)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	enum port port = encoder->port;
	u32 tmp, flags = 0;

	pipe_config->output_types |= BIT(INTEL_OUTPUT_DVO);

	tmp = intel_de_read(i915, DVO(port));
	if (tmp & DVO_HSYNC_ACTIVE_HIGH)
		flags |= DRM_MODE_FLAG_PHSYNC;
	else
		flags |= DRM_MODE_FLAG_NHSYNC;
	if (tmp & DVO_VSYNC_ACTIVE_HIGH)
		flags |= DRM_MODE_FLAG_PVSYNC;
	else
		flags |= DRM_MODE_FLAG_NVSYNC;

	pipe_config->hw.adjusted_mode.flags |= flags;

	pipe_config->hw.adjusted_mode.crtc_clock = pipe_config->port_clock;
}

static void intel_disable_dvo(struct intel_atomic_state *state,
			      struct intel_encoder *encoder,
			      const struct intel_crtc_state *old_crtc_state,
			      const struct drm_connector_state *old_conn_state)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	struct intel_dvo *intel_dvo = enc_to_dvo(encoder);
	enum port port = encoder->port;

	intel_dvo->dev.dev_ops->dpms(&intel_dvo->dev, false);

	intel_de_rmw(i915, DVO(port), DVO_ENABLE, 0);
	intel_de_posting_read(i915, DVO(port));
}

static void intel_enable_dvo(struct intel_atomic_state *state,
			     struct intel_encoder *encoder,
			     const struct intel_crtc_state *pipe_config,
			     const struct drm_connector_state *conn_state)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	struct intel_dvo *intel_dvo = enc_to_dvo(encoder);
	enum port port = encoder->port;

	intel_dvo->dev.dev_ops->mode_set(&intel_dvo->dev,
					 &pipe_config->hw.mode,
					 &pipe_config->hw.adjusted_mode);

	intel_de_rmw(i915, DVO(port), 0, DVO_ENABLE);
	intel_de_posting_read(i915, DVO(port));

	intel_dvo->dev.dev_ops->dpms(&intel_dvo->dev, true);
}

static enum drm_mode_status
intel_dvo_mode_valid(struct drm_connector *_connector,
		     struct drm_display_mode *mode)
{
	struct intel_connector *connector = to_intel_connector(_connector);
	struct intel_dvo *intel_dvo = intel_attached_dvo(connector);
	const struct drm_display_mode *fixed_mode =
		intel_panel_fixed_mode(connector, mode);
	int max_dotclk = to_i915(connector->base.dev)->max_dotclk_freq;
	int target_clock = mode->clock;

	if (mode->flags & DRM_MODE_FLAG_DBLSCAN)
		return MODE_NO_DBLESCAN;

	/* XXX: Validate clock range */

	if (fixed_mode) {
		enum drm_mode_status status;

		status = intel_panel_mode_valid(connector, mode);
		if (status != MODE_OK)
			return status;

		target_clock = fixed_mode->clock;
	}

	if (target_clock > max_dotclk)
		return MODE_CLOCK_HIGH;

	return intel_dvo->dev.dev_ops->mode_valid(&intel_dvo->dev, mode);
}

static int intel_dvo_compute_config(struct intel_encoder *encoder,
				    struct intel_crtc_state *pipe_config,
				    struct drm_connector_state *conn_state)
{
	struct intel_dvo *intel_dvo = enc_to_dvo(encoder);
	struct intel_connector *connector = to_intel_connector(conn_state->connector);
	struct drm_display_mode *adjusted_mode = &pipe_config->hw.adjusted_mode;
	const struct drm_display_mode *fixed_mode =
		intel_panel_fixed_mode(intel_dvo->attached_connector, adjusted_mode);

	/*
	 * If we have timings from the BIOS for the panel, put them in
	 * to the adjusted mode.  The CRTC will be set up for this mode,
	 * with the panel scaling set up to source from the H/VDisplay
	 * of the original mode.
	 */
	if (fixed_mode) {
		int ret;

		ret = intel_panel_compute_config(connector, adjusted_mode);
		if (ret)
			return ret;
	}

	if (adjusted_mode->flags & DRM_MODE_FLAG_DBLSCAN)
		return -EINVAL;

	pipe_config->sink_format = INTEL_OUTPUT_FORMAT_RGB;
	pipe_config->output_format = INTEL_OUTPUT_FORMAT_RGB;

	return 0;
}

static void intel_dvo_pre_enable(struct intel_atomic_state *state,
				 struct intel_encoder *encoder,
				 const struct intel_crtc_state *pipe_config,
				 const struct drm_connector_state *conn_state)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	struct intel_crtc *crtc = to_intel_crtc(pipe_config->uapi.crtc);
	const struct drm_display_mode *adjusted_mode = &pipe_config->hw.adjusted_mode;
	enum port port = encoder->port;
	enum pipe pipe = crtc->pipe;
	u32 dvo_val;

	/* Save the active data order, since I don't know what it should be set to. */
	dvo_val = intel_de_read(i915, DVO(port)) &
		  (DVO_DEDICATED_INT_ENABLE |
		   DVO_PRESERVE_MASK | DVO_ACT_DATA_ORDER_MASK);
	dvo_val |= DVO_DATA_ORDER_FP | DVO_BORDER_ENABLE |
		   DVO_BLANK_ACTIVE_HIGH;

	dvo_val |= DVO_PIPE_SEL(pipe);
	dvo_val |= DVO_PIPE_STALL;
	if (adjusted_mode->flags & DRM_MODE_FLAG_PHSYNC)
		dvo_val |= DVO_HSYNC_ACTIVE_HIGH;
	if (adjusted_mode->flags & DRM_MODE_FLAG_PVSYNC)
		dvo_val |= DVO_VSYNC_ACTIVE_HIGH;

	intel_de_write(i915, DVO_SRCDIM(port),
		       DVO_SRCDIM_HORIZONTAL(adjusted_mode->crtc_hdisplay) |
		       DVO_SRCDIM_VERTICAL(adjusted_mode->crtc_vdisplay));
	intel_de_write(i915, DVO(port), dvo_val);
}

static enum drm_connector_status
intel_dvo_detect(struct drm_connector *_connector, bool force)
{
	struct intel_connector *connector = to_intel_connector(_connector);
	struct drm_i915_private *i915 = to_i915(connector->base.dev);
	struct intel_dvo *intel_dvo = intel_attached_dvo(connector);

	drm_dbg_kms(&i915->drm, "[CONNECTOR:%d:%s]\n",
		    connector->base.base.id, connector->base.name);

	if (!INTEL_DISPLAY_ENABLED(i915))
		return connector_status_disconnected;

	return intel_dvo->dev.dev_ops->detect(&intel_dvo->dev);
}

static int intel_dvo_get_modes(struct drm_connector *_connector)
{
	struct intel_connector *connector = to_intel_connector(_connector);
	struct drm_i915_private *i915 = to_i915(connector->base.dev);
	int num_modes;

	/*
	 * We should probably have an i2c driver get_modes function for those
	 * devices which will have a fixed set of modes determined by the chip
	 * (TV-out, for example), but for now with just TMDS and LVDS,
	 * that's not the case.
	 */
	num_modes = intel_ddc_get_modes(&connector->base,
					intel_gmbus_get_adapter(i915, GMBUS_PIN_DPC));
	if (num_modes)
		return num_modes;

	return intel_panel_get_modes(connector);
}

static const struct drm_connector_funcs intel_dvo_connector_funcs = {
	.detect = intel_dvo_detect,
	.late_register = intel_connector_register,
	.early_unregister = intel_connector_unregister,
	.destroy = intel_connector_destroy,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
};

static const struct drm_connector_helper_funcs intel_dvo_connector_helper_funcs = {
	.mode_valid = intel_dvo_mode_valid,
	.get_modes = intel_dvo_get_modes,
};

static void intel_dvo_enc_destroy(struct drm_encoder *encoder)
{
	struct intel_dvo *intel_dvo = enc_to_dvo(to_intel_encoder(encoder));

	if (intel_dvo->dev.dev_ops->destroy)
		intel_dvo->dev.dev_ops->destroy(&intel_dvo->dev);

	intel_encoder_destroy(encoder);
}

static const struct drm_encoder_funcs intel_dvo_enc_funcs = {
	.destroy = intel_dvo_enc_destroy,
};

static int intel_dvo_encoder_type(const struct intel_dvo_device *dvo)
{
	switch (dvo->type) {
	case INTEL_DVO_CHIP_TMDS:
		return DRM_MODE_ENCODER_TMDS;
	case INTEL_DVO_CHIP_LVDS_NO_FIXED:
	case INTEL_DVO_CHIP_LVDS:
		return DRM_MODE_ENCODER_LVDS;
	default:
		MISSING_CASE(dvo->type);
		return DRM_MODE_ENCODER_NONE;
	}
}

static int intel_dvo_connector_type(const struct intel_dvo_device *dvo)
{
	switch (dvo->type) {
	case INTEL_DVO_CHIP_TMDS:
		return DRM_MODE_CONNECTOR_DVII;
	case INTEL_DVO_CHIP_LVDS_NO_FIXED:
	case INTEL_DVO_CHIP_LVDS:
		return DRM_MODE_CONNECTOR_LVDS;
	default:
		MISSING_CASE(dvo->type);
		return DRM_MODE_CONNECTOR_Unknown;
	}
}

static bool intel_dvo_init_dev(struct drm_i915_private *dev_priv,
			       struct intel_dvo *intel_dvo,
			       const struct intel_dvo_device *dvo)
{
	struct i2c_adapter *i2c;
	u32 dpll[I915_MAX_PIPES];
	enum pipe pipe;
	int gpio;
	bool ret;

	/*
	 * Allow the I2C driver info to specify the GPIO to be used in
	 * special cases, but otherwise default to what's defined
	 * in the spec.
	 */
	if (intel_gmbus_is_valid_pin(dev_priv, dvo->gpio))
		gpio = dvo->gpio;
	else if (dvo->type == INTEL_DVO_CHIP_LVDS)
		gpio = GMBUS_PIN_SSC;
	else
		gpio = GMBUS_PIN_DPB;

	/*
	 * Set up the I2C bus necessary for the chip we're probing.
	 * It appears that everything is on GPIOE except for panels
	 * on i830 laptops, which are on GPIOB (DVOA).
	 */
	i2c = intel_gmbus_get_adapter(dev_priv, gpio);

	intel_dvo->dev = *dvo;

	/*
	 * GMBUS NAK handling seems to be unstable, hence let the
	 * transmitter detection run in bit banging mode for now.
	 */
	intel_gmbus_force_bit(i2c, true);

	/*
	 * ns2501 requires the DVO 2x clock before it will
	 * respond to i2c accesses, so make sure we have
	 * the clock enabled before we attempt to initialize
	 * the device.
	 */
	for_each_pipe(dev_priv, pipe)
		dpll[pipe] = intel_de_rmw(dev_priv, DPLL(pipe), 0, DPLL_DVO_2X_MODE);

	ret = dvo->dev_ops->init(&intel_dvo->dev, i2c);

	/* restore the DVO 2x clock state to original */
	for_each_pipe(dev_priv, pipe) {
		intel_de_write(dev_priv, DPLL(pipe), dpll[pipe]);
	}

	intel_gmbus_force_bit(i2c, false);

	return ret;
}

static bool intel_dvo_probe(struct drm_i915_private *i915,
			    struct intel_dvo *intel_dvo)
{
	int i;

	/* Now, try to find a controller */
	for (i = 0; i < ARRAY_SIZE(intel_dvo_devices); i++) {
		if (intel_dvo_init_dev(i915, intel_dvo,
				       &intel_dvo_devices[i]))
			return true;
	}

	return false;
}

void intel_dvo_init(struct drm_i915_private *i915)
{
	struct intel_connector *connector;
	struct intel_encoder *encoder;
	struct intel_dvo *intel_dvo;

	intel_dvo = kzalloc(sizeof(*intel_dvo), GFP_KERNEL);
	if (!intel_dvo)
		return;

	connector = intel_connector_alloc();
	if (!connector) {
		kfree(intel_dvo);
		return;
	}

	intel_dvo->attached_connector = connector;

	encoder = &intel_dvo->base;

	encoder->disable = intel_disable_dvo;
	encoder->enable = intel_enable_dvo;
	encoder->get_hw_state = intel_dvo_get_hw_state;
	encoder->get_config = intel_dvo_get_config;
	encoder->compute_config = intel_dvo_compute_config;
	encoder->pre_enable = intel_dvo_pre_enable;
	connector->get_hw_state = intel_dvo_connector_get_hw_state;

	if (!intel_dvo_probe(i915, intel_dvo)) {
		kfree(intel_dvo);
		intel_connector_free(connector);
		return;
	}

	encoder->type = INTEL_OUTPUT_DVO;
	encoder->power_domain = POWER_DOMAIN_PORT_OTHER;
	encoder->port = intel_dvo->dev.port;
	encoder->pipe_mask = ~0;

	if (intel_dvo->dev.type != INTEL_DVO_CHIP_LVDS)
		encoder->cloneable = BIT(INTEL_OUTPUT_ANALOG) |
			BIT(INTEL_OUTPUT_DVO);

	drm_encoder_init(&i915->drm, &encoder->base,
			 &intel_dvo_enc_funcs,
			 intel_dvo_encoder_type(&intel_dvo->dev),
			 "DVO %c", port_name(encoder->port));

	drm_dbg_kms(&i915->drm, "[ENCODER:%d:%s] detected %s\n",
		    encoder->base.base.id, encoder->base.name,
		    intel_dvo->dev.name);

	if (intel_dvo->dev.type == INTEL_DVO_CHIP_TMDS)
		connector->polled = DRM_CONNECTOR_POLL_CONNECT |
			DRM_CONNECTOR_POLL_DISCONNECT;

	drm_connector_init(&i915->drm, &connector->base,
			   &intel_dvo_connector_funcs,
			   intel_dvo_connector_type(&intel_dvo->dev));

	drm_connector_helper_add(&connector->base,
				 &intel_dvo_connector_helper_funcs);
	connector->base.display_info.subpixel_order = SubPixelHorizontalRGB;

	intel_connector_attach_encoder(connector, encoder);

	if (intel_dvo->dev.type == INTEL_DVO_CHIP_LVDS) {
		/*
		 * For our LVDS chipsets, we should hopefully be able
		 * to dig the fixed panel mode out of the BIOS data.
		 * However, it's in a different format from the BIOS
		 * data on chipsets with integrated LVDS (stored in AIM
		 * headers, likely), so for now, just get the current
		 * mode being output through DVO.
		 */
		intel_panel_add_encoder_fixed_mode(connector, encoder);

		intel_panel_init(connector, NULL);
	}
}
