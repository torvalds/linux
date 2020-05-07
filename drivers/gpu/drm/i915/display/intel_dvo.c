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
#include "intel_connector.h"
#include "intel_display_types.h"
#include "intel_dvo.h"
#include "intel_dvo_dev.h"
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
		.dvo_reg = DVOC,
		.dvo_srcdim_reg = DVOC_SRCDIM,
		.slave_addr = SIL164_ADDR,
		.dev_ops = &sil164_ops,
	},
	{
		.type = INTEL_DVO_CHIP_TMDS,
		.name = "ch7xxx",
		.dvo_reg = DVOC,
		.dvo_srcdim_reg = DVOC_SRCDIM,
		.slave_addr = CH7xxx_ADDR,
		.dev_ops = &ch7xxx_ops,
	},
	{
		.type = INTEL_DVO_CHIP_TMDS,
		.name = "ch7xxx",
		.dvo_reg = DVOC,
		.dvo_srcdim_reg = DVOC_SRCDIM,
		.slave_addr = 0x75, /* For some ch7010 */
		.dev_ops = &ch7xxx_ops,
	},
	{
		.type = INTEL_DVO_CHIP_LVDS,
		.name = "ivch",
		.dvo_reg = DVOA,
		.dvo_srcdim_reg = DVOA_SRCDIM,
		.slave_addr = 0x02, /* Might also be 0x44, 0x84, 0xc4 */
		.dev_ops = &ivch_ops,
	},
	{
		.type = INTEL_DVO_CHIP_TMDS,
		.name = "tfp410",
		.dvo_reg = DVOC,
		.dvo_srcdim_reg = DVOC_SRCDIM,
		.slave_addr = TFP410_ADDR,
		.dev_ops = &tfp410_ops,
	},
	{
		.type = INTEL_DVO_CHIP_LVDS,
		.name = "ch7017",
		.dvo_reg = DVOC,
		.dvo_srcdim_reg = DVOC_SRCDIM,
		.slave_addr = 0x75,
		.gpio = GMBUS_PIN_DPB,
		.dev_ops = &ch7017_ops,
	},
	{
		.type = INTEL_DVO_CHIP_LVDS_NO_FIXED,
		.name = "ns2501",
		.dvo_reg = DVOB,
		.dvo_srcdim_reg = DVOB_SRCDIM,
		.slave_addr = NS2501_ADDR,
		.dev_ops = &ns2501_ops,
	},
};

struct intel_dvo {
	struct intel_encoder base;

	struct intel_dvo_device dev;

	struct intel_connector *attached_connector;

	bool panel_wants_dither;
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
	struct drm_device *dev = connector->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_dvo *intel_dvo = intel_attached_dvo(connector);
	u32 tmp;

	tmp = intel_de_read(dev_priv, intel_dvo->dev.dvo_reg);

	if (!(tmp & DVO_ENABLE))
		return false;

	return intel_dvo->dev.dev_ops->get_hw_state(&intel_dvo->dev);
}

static bool intel_dvo_get_hw_state(struct intel_encoder *encoder,
				   enum pipe *pipe)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dvo *intel_dvo = enc_to_dvo(encoder);
	u32 tmp;

	tmp = intel_de_read(dev_priv, intel_dvo->dev.dvo_reg);

	*pipe = (tmp & DVO_PIPE_SEL_MASK) >> DVO_PIPE_SEL_SHIFT;

	return tmp & DVO_ENABLE;
}

static void intel_dvo_get_config(struct intel_encoder *encoder,
				 struct intel_crtc_state *pipe_config)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dvo *intel_dvo = enc_to_dvo(encoder);
	u32 tmp, flags = 0;

	pipe_config->output_types |= BIT(INTEL_OUTPUT_DVO);

	tmp = intel_de_read(dev_priv, intel_dvo->dev.dvo_reg);
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

static void intel_disable_dvo(struct intel_encoder *encoder,
			      const struct intel_crtc_state *old_crtc_state,
			      const struct drm_connector_state *old_conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dvo *intel_dvo = enc_to_dvo(encoder);
	i915_reg_t dvo_reg = intel_dvo->dev.dvo_reg;
	u32 temp = intel_de_read(dev_priv, dvo_reg);

	intel_dvo->dev.dev_ops->dpms(&intel_dvo->dev, false);
	intel_de_write(dev_priv, dvo_reg, temp & ~DVO_ENABLE);
	intel_de_read(dev_priv, dvo_reg);
}

static void intel_enable_dvo(struct intel_encoder *encoder,
			     const struct intel_crtc_state *pipe_config,
			     const struct drm_connector_state *conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dvo *intel_dvo = enc_to_dvo(encoder);
	i915_reg_t dvo_reg = intel_dvo->dev.dvo_reg;
	u32 temp = intel_de_read(dev_priv, dvo_reg);

	intel_dvo->dev.dev_ops->mode_set(&intel_dvo->dev,
					 &pipe_config->hw.mode,
					 &pipe_config->hw.adjusted_mode);

	intel_de_write(dev_priv, dvo_reg, temp | DVO_ENABLE);
	intel_de_read(dev_priv, dvo_reg);

	intel_dvo->dev.dev_ops->dpms(&intel_dvo->dev, true);
}

static enum drm_mode_status
intel_dvo_mode_valid(struct drm_connector *connector,
		     struct drm_display_mode *mode)
{
	struct intel_dvo *intel_dvo = intel_attached_dvo(to_intel_connector(connector));
	const struct drm_display_mode *fixed_mode =
		to_intel_connector(connector)->panel.fixed_mode;
	int max_dotclk = to_i915(connector->dev)->max_dotclk_freq;
	int target_clock = mode->clock;

	if (mode->flags & DRM_MODE_FLAG_DBLSCAN)
		return MODE_NO_DBLESCAN;

	/* XXX: Validate clock range */

	if (fixed_mode) {
		if (mode->hdisplay > fixed_mode->hdisplay)
			return MODE_PANEL;
		if (mode->vdisplay > fixed_mode->vdisplay)
			return MODE_PANEL;

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
	const struct drm_display_mode *fixed_mode =
		intel_dvo->attached_connector->panel.fixed_mode;
	struct drm_display_mode *adjusted_mode = &pipe_config->hw.adjusted_mode;

	/*
	 * If we have timings from the BIOS for the panel, put them in
	 * to the adjusted mode.  The CRTC will be set up for this mode,
	 * with the panel scaling set up to source from the H/VDisplay
	 * of the original mode.
	 */
	if (fixed_mode)
		intel_fixed_panel_mode(fixed_mode, adjusted_mode);

	if (adjusted_mode->flags & DRM_MODE_FLAG_DBLSCAN)
		return -EINVAL;

	pipe_config->output_format = INTEL_OUTPUT_FORMAT_RGB;

	return 0;
}

static void intel_dvo_pre_enable(struct intel_encoder *encoder,
				 const struct intel_crtc_state *pipe_config,
				 const struct drm_connector_state *conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_crtc *crtc = to_intel_crtc(pipe_config->uapi.crtc);
	const struct drm_display_mode *adjusted_mode = &pipe_config->hw.adjusted_mode;
	struct intel_dvo *intel_dvo = enc_to_dvo(encoder);
	enum pipe pipe = crtc->pipe;
	u32 dvo_val;
	i915_reg_t dvo_reg = intel_dvo->dev.dvo_reg;
	i915_reg_t dvo_srcdim_reg = intel_dvo->dev.dvo_srcdim_reg;

	/* Save the data order, since I don't know what it should be set to. */
	dvo_val = intel_de_read(dev_priv, dvo_reg) &
		  (DVO_PRESERVE_MASK | DVO_DATA_ORDER_GBRG);
	dvo_val |= DVO_DATA_ORDER_FP | DVO_BORDER_ENABLE |
		   DVO_BLANK_ACTIVE_HIGH;

	dvo_val |= DVO_PIPE_SEL(pipe);
	dvo_val |= DVO_PIPE_STALL;
	if (adjusted_mode->flags & DRM_MODE_FLAG_PHSYNC)
		dvo_val |= DVO_HSYNC_ACTIVE_HIGH;
	if (adjusted_mode->flags & DRM_MODE_FLAG_PVSYNC)
		dvo_val |= DVO_VSYNC_ACTIVE_HIGH;

	/*I915_WRITE(DVOB_SRCDIM,
	  (adjusted_mode->crtc_hdisplay << DVO_SRCDIM_HORIZONTAL_SHIFT) |
	  (adjusted_mode->crtc_vdisplay << DVO_SRCDIM_VERTICAL_SHIFT));*/
	intel_de_write(dev_priv, dvo_srcdim_reg,
		       (adjusted_mode->crtc_hdisplay << DVO_SRCDIM_HORIZONTAL_SHIFT) | (adjusted_mode->crtc_vdisplay << DVO_SRCDIM_VERTICAL_SHIFT));
	/*I915_WRITE(DVOB, dvo_val);*/
	intel_de_write(dev_priv, dvo_reg, dvo_val);
}

static enum drm_connector_status
intel_dvo_detect(struct drm_connector *connector, bool force)
{
	struct intel_dvo *intel_dvo = intel_attached_dvo(to_intel_connector(connector));
	DRM_DEBUG_KMS("[CONNECTOR:%d:%s]\n",
		      connector->base.id, connector->name);
	return intel_dvo->dev.dev_ops->detect(&intel_dvo->dev);
}

static int intel_dvo_get_modes(struct drm_connector *connector)
{
	struct drm_i915_private *dev_priv = to_i915(connector->dev);
	const struct drm_display_mode *fixed_mode =
		to_intel_connector(connector)->panel.fixed_mode;

	/*
	 * We should probably have an i2c driver get_modes function for those
	 * devices which will have a fixed set of modes determined by the chip
	 * (TV-out, for example), but for now with just TMDS and LVDS,
	 * that's not the case.
	 */
	intel_ddc_get_modes(connector,
			    intel_gmbus_get_adapter(dev_priv, GMBUS_PIN_DPC));
	if (!list_empty(&connector->probed_modes))
		return 1;

	if (fixed_mode) {
		struct drm_display_mode *mode;
		mode = drm_mode_duplicate(connector->dev, fixed_mode);
		if (mode) {
			drm_mode_probed_add(connector, mode);
			return 1;
		}
	}

	return 0;
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

/*
 * Attempts to get a fixed panel timing for LVDS (currently only the i830).
 *
 * Other chips with DVO LVDS will need to extend this to deal with the LVDS
 * chip being on DVOB/C and having multiple pipes.
 */
static struct drm_display_mode *
intel_dvo_get_current_mode(struct intel_encoder *encoder)
{
	struct drm_display_mode *mode;

	mode = intel_encoder_current_mode(encoder);
	if (mode) {
		DRM_DEBUG_KMS("using current (BIOS) mode: ");
		drm_mode_debug_printmodeline(mode);
		mode->type |= DRM_MODE_TYPE_PREFERRED;
	}

	return mode;
}

static enum port intel_dvo_port(i915_reg_t dvo_reg)
{
	if (i915_mmio_reg_equal(dvo_reg, DVOA))
		return PORT_A;
	else if (i915_mmio_reg_equal(dvo_reg, DVOB))
		return PORT_B;
	else
		return PORT_C;
}

void intel_dvo_init(struct drm_i915_private *dev_priv)
{
	struct intel_encoder *intel_encoder;
	struct intel_dvo *intel_dvo;
	struct intel_connector *intel_connector;
	int i;
	int encoder_type = DRM_MODE_ENCODER_NONE;

	intel_dvo = kzalloc(sizeof(*intel_dvo), GFP_KERNEL);
	if (!intel_dvo)
		return;

	intel_connector = intel_connector_alloc();
	if (!intel_connector) {
		kfree(intel_dvo);
		return;
	}

	intel_dvo->attached_connector = intel_connector;

	intel_encoder = &intel_dvo->base;

	intel_encoder->disable = intel_disable_dvo;
	intel_encoder->enable = intel_enable_dvo;
	intel_encoder->get_hw_state = intel_dvo_get_hw_state;
	intel_encoder->get_config = intel_dvo_get_config;
	intel_encoder->compute_config = intel_dvo_compute_config;
	intel_encoder->pre_enable = intel_dvo_pre_enable;
	intel_connector->get_hw_state = intel_dvo_connector_get_hw_state;

	/* Now, try to find a controller */
	for (i = 0; i < ARRAY_SIZE(intel_dvo_devices); i++) {
		struct drm_connector *connector = &intel_connector->base;
		const struct intel_dvo_device *dvo = &intel_dvo_devices[i];
		struct i2c_adapter *i2c;
		int gpio;
		bool dvoinit;
		enum pipe pipe;
		u32 dpll[I915_MAX_PIPES];
		enum port port;

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
		 * have the clock enabled before we attempt to
		 * initialize the device.
		 */
		for_each_pipe(dev_priv, pipe) {
			dpll[pipe] = intel_de_read(dev_priv, DPLL(pipe));
			intel_de_write(dev_priv, DPLL(pipe),
				       dpll[pipe] | DPLL_DVO_2X_MODE);
		}

		dvoinit = dvo->dev_ops->init(&intel_dvo->dev, i2c);

		/* restore the DVO 2x clock state to original */
		for_each_pipe(dev_priv, pipe) {
			intel_de_write(dev_priv, DPLL(pipe), dpll[pipe]);
		}

		intel_gmbus_force_bit(i2c, false);

		if (!dvoinit)
			continue;

		port = intel_dvo_port(dvo->dvo_reg);
		drm_encoder_init(&dev_priv->drm, &intel_encoder->base,
				 &intel_dvo_enc_funcs, encoder_type,
				 "DVO %c", port_name(port));

		intel_encoder->type = INTEL_OUTPUT_DVO;
		intel_encoder->power_domain = POWER_DOMAIN_PORT_OTHER;
		intel_encoder->port = port;
		intel_encoder->pipe_mask = ~0;

		if (dvo->type != INTEL_DVO_CHIP_LVDS)
			intel_encoder->cloneable = (1 << INTEL_OUTPUT_ANALOG) |
				(1 << INTEL_OUTPUT_DVO);

		switch (dvo->type) {
		case INTEL_DVO_CHIP_TMDS:
			intel_connector->polled = DRM_CONNECTOR_POLL_CONNECT |
				DRM_CONNECTOR_POLL_DISCONNECT;
			drm_connector_init(&dev_priv->drm, connector,
					   &intel_dvo_connector_funcs,
					   DRM_MODE_CONNECTOR_DVII);
			encoder_type = DRM_MODE_ENCODER_TMDS;
			break;
		case INTEL_DVO_CHIP_LVDS_NO_FIXED:
		case INTEL_DVO_CHIP_LVDS:
			drm_connector_init(&dev_priv->drm, connector,
					   &intel_dvo_connector_funcs,
					   DRM_MODE_CONNECTOR_LVDS);
			encoder_type = DRM_MODE_ENCODER_LVDS;
			break;
		}

		drm_connector_helper_add(connector,
					 &intel_dvo_connector_helper_funcs);
		connector->display_info.subpixel_order = SubPixelHorizontalRGB;
		connector->interlace_allowed = false;
		connector->doublescan_allowed = false;

		intel_connector_attach_encoder(intel_connector, intel_encoder);
		if (dvo->type == INTEL_DVO_CHIP_LVDS) {
			/*
			 * For our LVDS chipsets, we should hopefully be able
			 * to dig the fixed panel mode out of the BIOS data.
			 * However, it's in a different format from the BIOS
			 * data on chipsets with integrated LVDS (stored in AIM
			 * headers, likely), so for now, just get the current
			 * mode being output through DVO.
			 */
			intel_panel_init(&intel_connector->panel,
					 intel_dvo_get_current_mode(intel_encoder),
					 NULL);
			intel_dvo->panel_wants_dither = true;
		}

		return;
	}

	kfree(intel_dvo);
	kfree(intel_connector);
}
