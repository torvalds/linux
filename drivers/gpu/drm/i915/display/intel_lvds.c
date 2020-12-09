/*
 * Copyright © 2006-2007 Intel Corporation
 * Copyright (c) 2006 Dave Airlie <airlied@linux.ie>
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
 *      Dave Airlie <airlied@linux.ie>
 *      Jesse Barnes <jesse.barnes@intel.com>
 */

#include <acpi/button.h>
#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/vga_switcheroo.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_edid.h>

#include "i915_drv.h"
#include "intel_atomic.h"
#include "intel_connector.h"
#include "intel_display_types.h"
#include "intel_gmbus.h"
#include "intel_lvds.h"
#include "intel_panel.h"

/* Private structure for the integrated LVDS support */
struct intel_lvds_pps {
	/* 100us units */
	int t1_t2;
	int t3;
	int t4;
	int t5;
	int tx;

	int divider;

	int port;
	bool powerdown_on_reset;
};

struct intel_lvds_encoder {
	struct intel_encoder base;

	bool is_dual_link;
	i915_reg_t reg;
	u32 a3_power;

	struct intel_lvds_pps init_pps;
	u32 init_lvds_val;

	struct intel_connector *attached_connector;
};

static struct intel_lvds_encoder *to_lvds_encoder(struct drm_encoder *encoder)
{
	return container_of(encoder, struct intel_lvds_encoder, base.base);
}

bool intel_lvds_port_enabled(struct drm_i915_private *dev_priv,
			     i915_reg_t lvds_reg, enum pipe *pipe)
{
	u32 val;

	val = intel_de_read(dev_priv, lvds_reg);

	/* asserts want to know the pipe even if the port is disabled */
	if (HAS_PCH_CPT(dev_priv))
		*pipe = (val & LVDS_PIPE_SEL_MASK_CPT) >> LVDS_PIPE_SEL_SHIFT_CPT;
	else
		*pipe = (val & LVDS_PIPE_SEL_MASK) >> LVDS_PIPE_SEL_SHIFT;

	return val & LVDS_PORT_EN;
}

static bool intel_lvds_get_hw_state(struct intel_encoder *encoder,
				    enum pipe *pipe)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_lvds_encoder *lvds_encoder = to_lvds_encoder(&encoder->base);
	intel_wakeref_t wakeref;
	bool ret;

	wakeref = intel_display_power_get_if_enabled(dev_priv,
						     encoder->power_domain);
	if (!wakeref)
		return false;

	ret = intel_lvds_port_enabled(dev_priv, lvds_encoder->reg, pipe);

	intel_display_power_put(dev_priv, encoder->power_domain, wakeref);

	return ret;
}

static void intel_lvds_get_config(struct intel_encoder *encoder,
				  struct intel_crtc_state *pipe_config)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_lvds_encoder *lvds_encoder = to_lvds_encoder(&encoder->base);
	u32 tmp, flags = 0;

	pipe_config->output_types |= BIT(INTEL_OUTPUT_LVDS);

	tmp = intel_de_read(dev_priv, lvds_encoder->reg);
	if (tmp & LVDS_HSYNC_POLARITY)
		flags |= DRM_MODE_FLAG_NHSYNC;
	else
		flags |= DRM_MODE_FLAG_PHSYNC;
	if (tmp & LVDS_VSYNC_POLARITY)
		flags |= DRM_MODE_FLAG_NVSYNC;
	else
		flags |= DRM_MODE_FLAG_PVSYNC;

	pipe_config->hw.adjusted_mode.flags |= flags;

	if (INTEL_GEN(dev_priv) < 5)
		pipe_config->gmch_pfit.lvds_border_bits =
			tmp & LVDS_BORDER_ENABLE;

	/* gen2/3 store dither state in pfit control, needs to match */
	if (INTEL_GEN(dev_priv) < 4) {
		tmp = intel_de_read(dev_priv, PFIT_CONTROL);

		pipe_config->gmch_pfit.control |= tmp & PANEL_8TO6_DITHER_ENABLE;
	}

	pipe_config->hw.adjusted_mode.crtc_clock = pipe_config->port_clock;
}

static void intel_lvds_pps_get_hw_state(struct drm_i915_private *dev_priv,
					struct intel_lvds_pps *pps)
{
	u32 val;

	pps->powerdown_on_reset = intel_de_read(dev_priv, PP_CONTROL(0)) & PANEL_POWER_RESET;

	val = intel_de_read(dev_priv, PP_ON_DELAYS(0));
	pps->port = REG_FIELD_GET(PANEL_PORT_SELECT_MASK, val);
	pps->t1_t2 = REG_FIELD_GET(PANEL_POWER_UP_DELAY_MASK, val);
	pps->t5 = REG_FIELD_GET(PANEL_LIGHT_ON_DELAY_MASK, val);

	val = intel_de_read(dev_priv, PP_OFF_DELAYS(0));
	pps->t3 = REG_FIELD_GET(PANEL_POWER_DOWN_DELAY_MASK, val);
	pps->tx = REG_FIELD_GET(PANEL_LIGHT_OFF_DELAY_MASK, val);

	val = intel_de_read(dev_priv, PP_DIVISOR(0));
	pps->divider = REG_FIELD_GET(PP_REFERENCE_DIVIDER_MASK, val);
	val = REG_FIELD_GET(PANEL_POWER_CYCLE_DELAY_MASK, val);
	/*
	 * Remove the BSpec specified +1 (100ms) offset that accounts for a
	 * too short power-cycle delay due to the asynchronous programming of
	 * the register.
	 */
	if (val)
		val--;
	/* Convert from 100ms to 100us units */
	pps->t4 = val * 1000;

	if (INTEL_GEN(dev_priv) <= 4 &&
	    pps->t1_t2 == 0 && pps->t5 == 0 && pps->t3 == 0 && pps->tx == 0) {
		drm_dbg_kms(&dev_priv->drm,
			    "Panel power timings uninitialized, "
			    "setting defaults\n");
		/* Set T2 to 40ms and T5 to 200ms in 100 usec units */
		pps->t1_t2 = 40 * 10;
		pps->t5 = 200 * 10;
		/* Set T3 to 35ms and Tx to 200ms in 100 usec units */
		pps->t3 = 35 * 10;
		pps->tx = 200 * 10;
	}

	drm_dbg(&dev_priv->drm, "LVDS PPS:t1+t2 %d t3 %d t4 %d t5 %d tx %d "
		"divider %d port %d powerdown_on_reset %d\n",
		pps->t1_t2, pps->t3, pps->t4, pps->t5, pps->tx,
		pps->divider, pps->port, pps->powerdown_on_reset);
}

static void intel_lvds_pps_init_hw(struct drm_i915_private *dev_priv,
				   struct intel_lvds_pps *pps)
{
	u32 val;

	val = intel_de_read(dev_priv, PP_CONTROL(0));
	drm_WARN_ON(&dev_priv->drm,
		    (val & PANEL_UNLOCK_MASK) != PANEL_UNLOCK_REGS);
	if (pps->powerdown_on_reset)
		val |= PANEL_POWER_RESET;
	intel_de_write(dev_priv, PP_CONTROL(0), val);

	intel_de_write(dev_priv, PP_ON_DELAYS(0),
		       REG_FIELD_PREP(PANEL_PORT_SELECT_MASK, pps->port) | REG_FIELD_PREP(PANEL_POWER_UP_DELAY_MASK, pps->t1_t2) | REG_FIELD_PREP(PANEL_LIGHT_ON_DELAY_MASK, pps->t5));

	intel_de_write(dev_priv, PP_OFF_DELAYS(0),
		       REG_FIELD_PREP(PANEL_POWER_DOWN_DELAY_MASK, pps->t3) | REG_FIELD_PREP(PANEL_LIGHT_OFF_DELAY_MASK, pps->tx));

	intel_de_write(dev_priv, PP_DIVISOR(0),
		       REG_FIELD_PREP(PP_REFERENCE_DIVIDER_MASK, pps->divider) | REG_FIELD_PREP(PANEL_POWER_CYCLE_DELAY_MASK, DIV_ROUND_UP(pps->t4, 1000) + 1));
}

static void intel_pre_enable_lvds(struct intel_atomic_state *state,
				  struct intel_encoder *encoder,
				  const struct intel_crtc_state *pipe_config,
				  const struct drm_connector_state *conn_state)
{
	struct intel_lvds_encoder *lvds_encoder = to_lvds_encoder(&encoder->base);
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_crtc *crtc = to_intel_crtc(pipe_config->uapi.crtc);
	const struct drm_display_mode *adjusted_mode = &pipe_config->hw.adjusted_mode;
	enum pipe pipe = crtc->pipe;
	u32 temp;

	if (HAS_PCH_SPLIT(dev_priv)) {
		assert_fdi_rx_pll_disabled(dev_priv, pipe);
		assert_shared_dpll_disabled(dev_priv,
					    pipe_config->shared_dpll);
	} else {
		assert_pll_disabled(dev_priv, pipe);
	}

	intel_lvds_pps_init_hw(dev_priv, &lvds_encoder->init_pps);

	temp = lvds_encoder->init_lvds_val;
	temp |= LVDS_PORT_EN | LVDS_A0A2_CLKA_POWER_UP;

	if (HAS_PCH_CPT(dev_priv)) {
		temp &= ~LVDS_PIPE_SEL_MASK_CPT;
		temp |= LVDS_PIPE_SEL_CPT(pipe);
	} else {
		temp &= ~LVDS_PIPE_SEL_MASK;
		temp |= LVDS_PIPE_SEL(pipe);
	}

	/* set the corresponsding LVDS_BORDER bit */
	temp &= ~LVDS_BORDER_ENABLE;
	temp |= pipe_config->gmch_pfit.lvds_border_bits;

	/*
	 * Set the B0-B3 data pairs corresponding to whether we're going to
	 * set the DPLLs for dual-channel mode or not.
	 */
	if (lvds_encoder->is_dual_link)
		temp |= LVDS_B0B3_POWER_UP | LVDS_CLKB_POWER_UP;
	else
		temp &= ~(LVDS_B0B3_POWER_UP | LVDS_CLKB_POWER_UP);

	/*
	 * It would be nice to set 24 vs 18-bit mode (LVDS_A3_POWER_UP)
	 * appropriately here, but we need to look more thoroughly into how
	 * panels behave in the two modes. For now, let's just maintain the
	 * value we got from the BIOS.
	 */
	temp &= ~LVDS_A3_POWER_MASK;
	temp |= lvds_encoder->a3_power;

	/*
	 * Set the dithering flag on LVDS as needed, note that there is no
	 * special lvds dither control bit on pch-split platforms, dithering is
	 * only controlled through the PIPECONF reg.
	 */
	if (IS_GEN(dev_priv, 4)) {
		/*
		 * Bspec wording suggests that LVDS port dithering only exists
		 * for 18bpp panels.
		 */
		if (pipe_config->dither && pipe_config->pipe_bpp == 18)
			temp |= LVDS_ENABLE_DITHER;
		else
			temp &= ~LVDS_ENABLE_DITHER;
	}
	temp &= ~(LVDS_HSYNC_POLARITY | LVDS_VSYNC_POLARITY);
	if (adjusted_mode->flags & DRM_MODE_FLAG_NHSYNC)
		temp |= LVDS_HSYNC_POLARITY;
	if (adjusted_mode->flags & DRM_MODE_FLAG_NVSYNC)
		temp |= LVDS_VSYNC_POLARITY;

	intel_de_write(dev_priv, lvds_encoder->reg, temp);
}

/*
 * Sets the power state for the panel.
 */
static void intel_enable_lvds(struct intel_atomic_state *state,
			      struct intel_encoder *encoder,
			      const struct intel_crtc_state *pipe_config,
			      const struct drm_connector_state *conn_state)
{
	struct drm_device *dev = encoder->base.dev;
	struct intel_lvds_encoder *lvds_encoder = to_lvds_encoder(&encoder->base);
	struct drm_i915_private *dev_priv = to_i915(dev);

	intel_de_write(dev_priv, lvds_encoder->reg,
		       intel_de_read(dev_priv, lvds_encoder->reg) | LVDS_PORT_EN);

	intel_de_write(dev_priv, PP_CONTROL(0),
		       intel_de_read(dev_priv, PP_CONTROL(0)) | PANEL_POWER_ON);
	intel_de_posting_read(dev_priv, lvds_encoder->reg);

	if (intel_de_wait_for_set(dev_priv, PP_STATUS(0), PP_ON, 5000))
		drm_err(&dev_priv->drm,
			"timed out waiting for panel to power on\n");

	intel_panel_enable_backlight(pipe_config, conn_state);
}

static void intel_disable_lvds(struct intel_atomic_state *state,
			       struct intel_encoder *encoder,
			       const struct intel_crtc_state *old_crtc_state,
			       const struct drm_connector_state *old_conn_state)
{
	struct intel_lvds_encoder *lvds_encoder = to_lvds_encoder(&encoder->base);
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);

	intel_de_write(dev_priv, PP_CONTROL(0),
		       intel_de_read(dev_priv, PP_CONTROL(0)) & ~PANEL_POWER_ON);
	if (intel_de_wait_for_clear(dev_priv, PP_STATUS(0), PP_ON, 1000))
		drm_err(&dev_priv->drm,
			"timed out waiting for panel to power off\n");

	intel_de_write(dev_priv, lvds_encoder->reg,
		       intel_de_read(dev_priv, lvds_encoder->reg) & ~LVDS_PORT_EN);
	intel_de_posting_read(dev_priv, lvds_encoder->reg);
}

static void gmch_disable_lvds(struct intel_atomic_state *state,
			      struct intel_encoder *encoder,
			      const struct intel_crtc_state *old_crtc_state,
			      const struct drm_connector_state *old_conn_state)

{
	intel_panel_disable_backlight(old_conn_state);

	intel_disable_lvds(state, encoder, old_crtc_state, old_conn_state);
}

static void pch_disable_lvds(struct intel_atomic_state *state,
			     struct intel_encoder *encoder,
			     const struct intel_crtc_state *old_crtc_state,
			     const struct drm_connector_state *old_conn_state)
{
	intel_panel_disable_backlight(old_conn_state);
}

static void pch_post_disable_lvds(struct intel_atomic_state *state,
				  struct intel_encoder *encoder,
				  const struct intel_crtc_state *old_crtc_state,
				  const struct drm_connector_state *old_conn_state)
{
	intel_disable_lvds(state, encoder, old_crtc_state, old_conn_state);
}

static enum drm_mode_status
intel_lvds_mode_valid(struct drm_connector *connector,
		      struct drm_display_mode *mode)
{
	struct intel_connector *intel_connector = to_intel_connector(connector);
	struct drm_display_mode *fixed_mode = intel_connector->panel.fixed_mode;
	int max_pixclk = to_i915(connector->dev)->max_dotclk_freq;

	if (mode->flags & DRM_MODE_FLAG_DBLSCAN)
		return MODE_NO_DBLESCAN;
	if (mode->hdisplay > fixed_mode->hdisplay)
		return MODE_PANEL;
	if (mode->vdisplay > fixed_mode->vdisplay)
		return MODE_PANEL;
	if (fixed_mode->clock > max_pixclk)
		return MODE_CLOCK_HIGH;

	return MODE_OK;
}

static int intel_lvds_compute_config(struct intel_encoder *intel_encoder,
				     struct intel_crtc_state *pipe_config,
				     struct drm_connector_state *conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(intel_encoder->base.dev);
	struct intel_lvds_encoder *lvds_encoder =
		to_lvds_encoder(&intel_encoder->base);
	struct intel_connector *intel_connector =
		lvds_encoder->attached_connector;
	struct drm_display_mode *adjusted_mode = &pipe_config->hw.adjusted_mode;
	struct intel_crtc *intel_crtc = to_intel_crtc(pipe_config->uapi.crtc);
	unsigned int lvds_bpp;
	int ret;

	/* Should never happen!! */
	if (INTEL_GEN(dev_priv) < 4 && intel_crtc->pipe == 0) {
		drm_err(&dev_priv->drm, "Can't support LVDS on pipe A\n");
		return -EINVAL;
	}

	if (lvds_encoder->a3_power == LVDS_A3_POWER_UP)
		lvds_bpp = 8*3;
	else
		lvds_bpp = 6*3;

	if (lvds_bpp != pipe_config->pipe_bpp && !pipe_config->bw_constrained) {
		drm_dbg_kms(&dev_priv->drm,
			    "forcing display bpp (was %d) to LVDS (%d)\n",
			    pipe_config->pipe_bpp, lvds_bpp);
		pipe_config->pipe_bpp = lvds_bpp;
	}

	pipe_config->output_format = INTEL_OUTPUT_FORMAT_RGB;

	/*
	 * We have timings from the BIOS for the panel, put them in
	 * to the adjusted mode.  The CRTC will be set up for this mode,
	 * with the panel scaling set up to source from the H/VDisplay
	 * of the original mode.
	 */
	intel_fixed_panel_mode(intel_connector->panel.fixed_mode,
			       adjusted_mode);

	if (adjusted_mode->flags & DRM_MODE_FLAG_DBLSCAN)
		return -EINVAL;

	if (HAS_PCH_SPLIT(dev_priv))
		pipe_config->has_pch_encoder = true;

	if (HAS_GMCH(dev_priv))
		ret = intel_gmch_panel_fitting(pipe_config, conn_state);
	else
		ret = intel_pch_panel_fitting(pipe_config, conn_state);
	if (ret)
		return ret;

	/*
	 * XXX: It would be nice to support lower refresh rates on the
	 * panels to reduce power consumption, and perhaps match the
	 * user's requested refresh rate.
	 */

	return 0;
}

/*
 * Return the list of DDC modes if available, or the BIOS fixed mode otherwise.
 */
static int intel_lvds_get_modes(struct drm_connector *connector)
{
	struct intel_connector *intel_connector = to_intel_connector(connector);
	struct drm_device *dev = connector->dev;
	struct drm_display_mode *mode;

	/* use cached edid if we have one */
	if (!IS_ERR_OR_NULL(intel_connector->edid))
		return drm_add_edid_modes(connector, intel_connector->edid);

	mode = drm_mode_duplicate(dev, intel_connector->panel.fixed_mode);
	if (mode == NULL)
		return 0;

	drm_mode_probed_add(connector, mode);
	return 1;
}

static const struct drm_connector_helper_funcs intel_lvds_connector_helper_funcs = {
	.get_modes = intel_lvds_get_modes,
	.mode_valid = intel_lvds_mode_valid,
	.atomic_check = intel_digital_connector_atomic_check,
};

static const struct drm_connector_funcs intel_lvds_connector_funcs = {
	.detect = intel_panel_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.atomic_get_property = intel_digital_connector_atomic_get_property,
	.atomic_set_property = intel_digital_connector_atomic_set_property,
	.late_register = intel_connector_register,
	.early_unregister = intel_connector_unregister,
	.destroy = intel_connector_destroy,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
	.atomic_duplicate_state = intel_digital_connector_duplicate_state,
};

static const struct drm_encoder_funcs intel_lvds_enc_funcs = {
	.destroy = intel_encoder_destroy,
};

static int intel_no_lvds_dmi_callback(const struct dmi_system_id *id)
{
	DRM_INFO("Skipping LVDS initialization for %s\n", id->ident);
	return 1;
}

/* These systems claim to have LVDS, but really don't */
static const struct dmi_system_id intel_no_lvds[] = {
	{
		.callback = intel_no_lvds_dmi_callback,
		.ident = "Apple Mac Mini (Core series)",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Apple"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Macmini1,1"),
		},
	},
	{
		.callback = intel_no_lvds_dmi_callback,
		.ident = "Apple Mac Mini (Core 2 series)",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Apple"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Macmini2,1"),
		},
	},
	{
		.callback = intel_no_lvds_dmi_callback,
		.ident = "MSI IM-945GSE-A",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "MSI"),
			DMI_MATCH(DMI_PRODUCT_NAME, "A9830IMS"),
		},
	},
	{
		.callback = intel_no_lvds_dmi_callback,
		.ident = "Dell Studio Hybrid",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Studio Hybrid 140g"),
		},
	},
	{
		.callback = intel_no_lvds_dmi_callback,
		.ident = "Dell OptiPlex FX170",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "OptiPlex FX170"),
		},
	},
	{
		.callback = intel_no_lvds_dmi_callback,
		.ident = "AOpen Mini PC",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "AOpen"),
			DMI_MATCH(DMI_PRODUCT_NAME, "i965GMx-IF"),
		},
	},
	{
		.callback = intel_no_lvds_dmi_callback,
		.ident = "AOpen Mini PC MP915",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "AOpen"),
			DMI_MATCH(DMI_BOARD_NAME, "i915GMx-F"),
		},
	},
	{
		.callback = intel_no_lvds_dmi_callback,
		.ident = "AOpen i915GMm-HFS",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "AOpen"),
			DMI_MATCH(DMI_BOARD_NAME, "i915GMm-HFS"),
		},
	},
	{
		.callback = intel_no_lvds_dmi_callback,
                .ident = "AOpen i45GMx-I",
                .matches = {
                        DMI_MATCH(DMI_BOARD_VENDOR, "AOpen"),
                        DMI_MATCH(DMI_BOARD_NAME, "i45GMx-I"),
                },
        },
	{
		.callback = intel_no_lvds_dmi_callback,
		.ident = "Aopen i945GTt-VFA",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_VERSION, "AO00001JW"),
		},
	},
	{
		.callback = intel_no_lvds_dmi_callback,
		.ident = "Clientron U800",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Clientron"),
			DMI_MATCH(DMI_PRODUCT_NAME, "U800"),
		},
	},
	{
                .callback = intel_no_lvds_dmi_callback,
                .ident = "Clientron E830",
                .matches = {
                        DMI_MATCH(DMI_SYS_VENDOR, "Clientron"),
                        DMI_MATCH(DMI_PRODUCT_NAME, "E830"),
                },
        },
        {
		.callback = intel_no_lvds_dmi_callback,
		.ident = "Asus EeeBox PC EB1007",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK Computer INC."),
			DMI_MATCH(DMI_PRODUCT_NAME, "EB1007"),
		},
	},
	{
		.callback = intel_no_lvds_dmi_callback,
		.ident = "Asus AT5NM10T-I",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "ASUSTeK Computer INC."),
			DMI_MATCH(DMI_BOARD_NAME, "AT5NM10T-I"),
		},
	},
	{
		.callback = intel_no_lvds_dmi_callback,
		.ident = "Hewlett-Packard HP t5740",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Hewlett-Packard"),
			DMI_MATCH(DMI_PRODUCT_NAME, " t5740"),
		},
	},
	{
		.callback = intel_no_lvds_dmi_callback,
		.ident = "Hewlett-Packard t5745",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Hewlett-Packard"),
			DMI_MATCH(DMI_PRODUCT_NAME, "hp t5745"),
		},
	},
	{
		.callback = intel_no_lvds_dmi_callback,
		.ident = "Hewlett-Packard st5747",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Hewlett-Packard"),
			DMI_MATCH(DMI_PRODUCT_NAME, "hp st5747"),
		},
	},
	{
		.callback = intel_no_lvds_dmi_callback,
		.ident = "MSI Wind Box DC500",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "MICRO-STAR INTERNATIONAL CO., LTD"),
			DMI_MATCH(DMI_BOARD_NAME, "MS-7469"),
		},
	},
	{
		.callback = intel_no_lvds_dmi_callback,
		.ident = "Gigabyte GA-D525TUD",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Gigabyte Technology Co., Ltd."),
			DMI_MATCH(DMI_BOARD_NAME, "D525TUD"),
		},
	},
	{
		.callback = intel_no_lvds_dmi_callback,
		.ident = "Supermicro X7SPA-H",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Supermicro"),
			DMI_MATCH(DMI_PRODUCT_NAME, "X7SPA-H"),
		},
	},
	{
		.callback = intel_no_lvds_dmi_callback,
		.ident = "Fujitsu Esprimo Q900",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU"),
			DMI_MATCH(DMI_PRODUCT_NAME, "ESPRIMO Q900"),
		},
	},
	{
		.callback = intel_no_lvds_dmi_callback,
		.ident = "Intel D410PT",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Intel"),
			DMI_MATCH(DMI_BOARD_NAME, "D410PT"),
		},
	},
	{
		.callback = intel_no_lvds_dmi_callback,
		.ident = "Intel D425KT",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Intel"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "D425KT"),
		},
	},
	{
		.callback = intel_no_lvds_dmi_callback,
		.ident = "Intel D510MO",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Intel"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "D510MO"),
		},
	},
	{
		.callback = intel_no_lvds_dmi_callback,
		.ident = "Intel D525MW",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Intel"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "D525MW"),
		},
	},
	{
		.callback = intel_no_lvds_dmi_callback,
		.ident = "Radiant P845",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Radiant Systems Inc"),
			DMI_MATCH(DMI_PRODUCT_NAME, "P845"),
		},
	},

	{ }	/* terminating entry */
};

static int intel_dual_link_lvds_callback(const struct dmi_system_id *id)
{
	DRM_INFO("Forcing lvds to dual link mode on %s\n", id->ident);
	return 1;
}

static const struct dmi_system_id intel_dual_link_lvds[] = {
	{
		.callback = intel_dual_link_lvds_callback,
		.ident = "Apple MacBook Pro 15\" (2010)",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Apple Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "MacBookPro6,2"),
		},
	},
	{
		.callback = intel_dual_link_lvds_callback,
		.ident = "Apple MacBook Pro 15\" (2011)",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Apple Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "MacBookPro8,2"),
		},
	},
	{
		.callback = intel_dual_link_lvds_callback,
		.ident = "Apple MacBook Pro 15\" (2012)",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Apple Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "MacBookPro9,1"),
		},
	},
	{ }	/* terminating entry */
};

struct intel_encoder *intel_get_lvds_encoder(struct drm_i915_private *dev_priv)
{
	struct intel_encoder *encoder;

	for_each_intel_encoder(&dev_priv->drm, encoder) {
		if (encoder->type == INTEL_OUTPUT_LVDS)
			return encoder;
	}

	return NULL;
}

bool intel_is_dual_link_lvds(struct drm_i915_private *dev_priv)
{
	struct intel_encoder *encoder = intel_get_lvds_encoder(dev_priv);

	return encoder && to_lvds_encoder(&encoder->base)->is_dual_link;
}

static bool compute_is_dual_link_lvds(struct intel_lvds_encoder *lvds_encoder)
{
	struct drm_device *dev = lvds_encoder->base.base.dev;
	unsigned int val;
	struct drm_i915_private *dev_priv = to_i915(dev);

	/* use the module option value if specified */
	if (dev_priv->params.lvds_channel_mode > 0)
		return dev_priv->params.lvds_channel_mode == 2;

	/* single channel LVDS is limited to 112 MHz */
	if (lvds_encoder->attached_connector->panel.fixed_mode->clock > 112999)
		return true;

	if (dmi_check_system(intel_dual_link_lvds))
		return true;

	/*
	 * BIOS should set the proper LVDS register value at boot, but
	 * in reality, it doesn't set the value when the lid is closed;
	 * we need to check "the value to be set" in VBT when LVDS
	 * register is uninitialized.
	 */
	val = intel_de_read(dev_priv, lvds_encoder->reg);
	if (HAS_PCH_CPT(dev_priv))
		val &= ~(LVDS_DETECTED | LVDS_PIPE_SEL_MASK_CPT);
	else
		val &= ~(LVDS_DETECTED | LVDS_PIPE_SEL_MASK);
	if (val == 0)
		val = dev_priv->vbt.bios_lvds_val;

	return (val & LVDS_CLKB_POWER_MASK) == LVDS_CLKB_POWER_UP;
}

/**
 * intel_lvds_init - setup LVDS connectors on this device
 * @dev_priv: i915 device
 *
 * Create the connector, register the LVDS DDC bus, and try to figure out what
 * modes we can display on the LVDS panel (if present).
 */
void intel_lvds_init(struct drm_i915_private *dev_priv)
{
	struct drm_device *dev = &dev_priv->drm;
	struct intel_lvds_encoder *lvds_encoder;
	struct intel_encoder *intel_encoder;
	struct intel_connector *intel_connector;
	struct drm_connector *connector;
	struct drm_encoder *encoder;
	struct drm_display_mode *fixed_mode = NULL;
	struct drm_display_mode *downclock_mode = NULL;
	struct edid *edid;
	i915_reg_t lvds_reg;
	u32 lvds;
	u8 pin;
	u32 allowed_scalers;

	/* Skip init on machines we know falsely report LVDS */
	if (dmi_check_system(intel_no_lvds)) {
		drm_WARN(dev, !dev_priv->vbt.int_lvds_support,
			 "Useless DMI match. Internal LVDS support disabled by VBT\n");
		return;
	}

	if (!dev_priv->vbt.int_lvds_support) {
		drm_dbg_kms(&dev_priv->drm,
			    "Internal LVDS support disabled by VBT\n");
		return;
	}

	if (HAS_PCH_SPLIT(dev_priv))
		lvds_reg = PCH_LVDS;
	else
		lvds_reg = LVDS;

	lvds = intel_de_read(dev_priv, lvds_reg);

	if (HAS_PCH_SPLIT(dev_priv)) {
		if ((lvds & LVDS_DETECTED) == 0)
			return;
	}

	pin = GMBUS_PIN_PANEL;
	if (!intel_bios_is_lvds_present(dev_priv, &pin)) {
		if ((lvds & LVDS_PORT_EN) == 0) {
			drm_dbg_kms(&dev_priv->drm,
				    "LVDS is not present in VBT\n");
			return;
		}
		drm_dbg_kms(&dev_priv->drm,
			    "LVDS is not present in VBT, but enabled anyway\n");
	}

	lvds_encoder = kzalloc(sizeof(*lvds_encoder), GFP_KERNEL);
	if (!lvds_encoder)
		return;

	intel_connector = intel_connector_alloc();
	if (!intel_connector) {
		kfree(lvds_encoder);
		return;
	}

	lvds_encoder->attached_connector = intel_connector;

	intel_encoder = &lvds_encoder->base;
	encoder = &intel_encoder->base;
	connector = &intel_connector->base;
	drm_connector_init(dev, &intel_connector->base, &intel_lvds_connector_funcs,
			   DRM_MODE_CONNECTOR_LVDS);

	drm_encoder_init(dev, &intel_encoder->base, &intel_lvds_enc_funcs,
			 DRM_MODE_ENCODER_LVDS, "LVDS");

	intel_encoder->enable = intel_enable_lvds;
	intel_encoder->pre_enable = intel_pre_enable_lvds;
	intel_encoder->compute_config = intel_lvds_compute_config;
	if (HAS_PCH_SPLIT(dev_priv)) {
		intel_encoder->disable = pch_disable_lvds;
		intel_encoder->post_disable = pch_post_disable_lvds;
	} else {
		intel_encoder->disable = gmch_disable_lvds;
	}
	intel_encoder->get_hw_state = intel_lvds_get_hw_state;
	intel_encoder->get_config = intel_lvds_get_config;
	intel_encoder->update_pipe = intel_panel_update_backlight;
	intel_connector->get_hw_state = intel_connector_get_hw_state;

	intel_connector_attach_encoder(intel_connector, intel_encoder);

	intel_encoder->type = INTEL_OUTPUT_LVDS;
	intel_encoder->power_domain = POWER_DOMAIN_PORT_OTHER;
	intel_encoder->port = PORT_NONE;
	intel_encoder->cloneable = 0;
	if (INTEL_GEN(dev_priv) < 4)
		intel_encoder->pipe_mask = BIT(PIPE_B);
	else
		intel_encoder->pipe_mask = ~0;

	drm_connector_helper_add(connector, &intel_lvds_connector_helper_funcs);
	connector->display_info.subpixel_order = SubPixelHorizontalRGB;
	connector->interlace_allowed = false;
	connector->doublescan_allowed = false;

	lvds_encoder->reg = lvds_reg;

	/* create the scaling mode property */
	allowed_scalers = BIT(DRM_MODE_SCALE_ASPECT);
	allowed_scalers |= BIT(DRM_MODE_SCALE_FULLSCREEN);
	allowed_scalers |= BIT(DRM_MODE_SCALE_CENTER);
	drm_connector_attach_scaling_mode_property(connector, allowed_scalers);
	connector->state->scaling_mode = DRM_MODE_SCALE_ASPECT;

	intel_lvds_pps_get_hw_state(dev_priv, &lvds_encoder->init_pps);
	lvds_encoder->init_lvds_val = lvds;

	/*
	 * LVDS discovery:
	 * 1) check for EDID on DDC
	 * 2) check for VBT data
	 * 3) check to see if LVDS is already on
	 *    if none of the above, no panel
	 */

	/*
	 * Attempt to get the fixed panel mode from DDC.  Assume that the
	 * preferred mode is the right one.
	 */
	mutex_lock(&dev->mode_config.mutex);
	if (vga_switcheroo_handler_flags() & VGA_SWITCHEROO_CAN_SWITCH_DDC)
		edid = drm_get_edid_switcheroo(connector,
				    intel_gmbus_get_adapter(dev_priv, pin));
	else
		edid = drm_get_edid(connector,
				    intel_gmbus_get_adapter(dev_priv, pin));
	if (edid) {
		if (drm_add_edid_modes(connector, edid)) {
			drm_connector_update_edid_property(connector,
								edid);
		} else {
			kfree(edid);
			edid = ERR_PTR(-EINVAL);
		}
	} else {
		edid = ERR_PTR(-ENOENT);
	}
	intel_connector->edid = edid;

	fixed_mode = intel_panel_edid_fixed_mode(intel_connector);
	if (fixed_mode)
		goto out;

	/* Failed to get EDID, what about VBT? */
	fixed_mode = intel_panel_vbt_fixed_mode(intel_connector);
	if (fixed_mode)
		goto out;

	/*
	 * If we didn't get EDID, try checking if the panel is already turned
	 * on.  If so, assume that whatever is currently programmed is the
	 * correct mode.
	 */
	fixed_mode = intel_encoder_current_mode(intel_encoder);
	if (fixed_mode) {
		drm_dbg_kms(&dev_priv->drm, "using current (BIOS) mode: ");
		drm_mode_debug_printmodeline(fixed_mode);
		fixed_mode->type |= DRM_MODE_TYPE_PREFERRED;
	}

	/* If we still don't have a mode after all that, give up. */
	if (!fixed_mode)
		goto failed;

out:
	mutex_unlock(&dev->mode_config.mutex);

	intel_panel_init(&intel_connector->panel, fixed_mode, downclock_mode);
	intel_panel_setup_backlight(connector, INVALID_PIPE);

	lvds_encoder->is_dual_link = compute_is_dual_link_lvds(lvds_encoder);
	drm_dbg_kms(&dev_priv->drm, "detected %s-link lvds configuration\n",
		    lvds_encoder->is_dual_link ? "dual" : "single");

	lvds_encoder->a3_power = lvds & LVDS_A3_POWER_MASK;

	return;

failed:
	mutex_unlock(&dev->mode_config.mutex);

	drm_dbg_kms(&dev_priv->drm, "No LVDS modes found, disabling.\n");
	drm_connector_cleanup(connector);
	drm_encoder_cleanup(encoder);
	kfree(lvds_encoder);
	intel_connector_free(intel_connector);
	return;
}
