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
#include "i915_reg.h"
#include "intel_atomic.h"
#include "intel_backlight.h"
#include "intel_connector.h"
#include "intel_de.h"
#include "intel_display_types.h"
#include "intel_dpll.h"
#include "intel_fdi.h"
#include "intel_gmbus.h"
#include "intel_lvds.h"
#include "intel_lvds_regs.h"
#include "intel_panel.h"
#include "intel_pps_regs.h"

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

static struct intel_lvds_encoder *to_lvds_encoder(struct intel_encoder *encoder)
{
	return container_of(encoder, struct intel_lvds_encoder, base);
}

bool intel_lvds_port_enabled(struct drm_i915_private *i915,
			     i915_reg_t lvds_reg, enum pipe *pipe)
{
	u32 val;

	val = intel_de_read(i915, lvds_reg);

	/* asserts want to know the pipe even if the port is disabled */
	if (HAS_PCH_CPT(i915))
		*pipe = REG_FIELD_GET(LVDS_PIPE_SEL_MASK_CPT, val);
	else
		*pipe = REG_FIELD_GET(LVDS_PIPE_SEL_MASK, val);

	return val & LVDS_PORT_EN;
}

static bool intel_lvds_get_hw_state(struct intel_encoder *encoder,
				    enum pipe *pipe)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	struct intel_lvds_encoder *lvds_encoder = to_lvds_encoder(encoder);
	intel_wakeref_t wakeref;
	bool ret;

	wakeref = intel_display_power_get_if_enabled(i915, encoder->power_domain);
	if (!wakeref)
		return false;

	ret = intel_lvds_port_enabled(i915, lvds_encoder->reg, pipe);

	intel_display_power_put(i915, encoder->power_domain, wakeref);

	return ret;
}

static void intel_lvds_get_config(struct intel_encoder *encoder,
				  struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_lvds_encoder *lvds_encoder = to_lvds_encoder(encoder);
	u32 tmp, flags = 0;

	crtc_state->output_types |= BIT(INTEL_OUTPUT_LVDS);

	tmp = intel_de_read(dev_priv, lvds_encoder->reg);
	if (tmp & LVDS_HSYNC_POLARITY)
		flags |= DRM_MODE_FLAG_NHSYNC;
	else
		flags |= DRM_MODE_FLAG_PHSYNC;
	if (tmp & LVDS_VSYNC_POLARITY)
		flags |= DRM_MODE_FLAG_NVSYNC;
	else
		flags |= DRM_MODE_FLAG_PVSYNC;

	crtc_state->hw.adjusted_mode.flags |= flags;

	if (DISPLAY_VER(dev_priv) < 5)
		crtc_state->gmch_pfit.lvds_border_bits =
			tmp & LVDS_BORDER_ENABLE;

	/* gen2/3 store dither state in pfit control, needs to match */
	if (DISPLAY_VER(dev_priv) < 4) {
		tmp = intel_de_read(dev_priv, PFIT_CONTROL);

		crtc_state->gmch_pfit.control |= tmp & PFIT_PANEL_8TO6_DITHER_ENABLE;
	}

	crtc_state->hw.adjusted_mode.crtc_clock = crtc_state->port_clock;
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

	if (DISPLAY_VER(dev_priv) < 5 &&
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
		       REG_FIELD_PREP(PANEL_PORT_SELECT_MASK, pps->port) |
		       REG_FIELD_PREP(PANEL_POWER_UP_DELAY_MASK, pps->t1_t2) |
		       REG_FIELD_PREP(PANEL_LIGHT_ON_DELAY_MASK, pps->t5));

	intel_de_write(dev_priv, PP_OFF_DELAYS(0),
		       REG_FIELD_PREP(PANEL_POWER_DOWN_DELAY_MASK, pps->t3) |
		       REG_FIELD_PREP(PANEL_LIGHT_OFF_DELAY_MASK, pps->tx));

	intel_de_write(dev_priv, PP_DIVISOR(0),
		       REG_FIELD_PREP(PP_REFERENCE_DIVIDER_MASK, pps->divider) |
		       REG_FIELD_PREP(PANEL_POWER_CYCLE_DELAY_MASK, DIV_ROUND_UP(pps->t4, 1000) + 1));
}

static void intel_pre_enable_lvds(struct intel_atomic_state *state,
				  struct intel_encoder *encoder,
				  const struct intel_crtc_state *crtc_state,
				  const struct drm_connector_state *conn_state)
{
	struct intel_lvds_encoder *lvds_encoder = to_lvds_encoder(encoder);
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	const struct drm_display_mode *adjusted_mode = &crtc_state->hw.adjusted_mode;
	enum pipe pipe = crtc->pipe;
	u32 temp;

	if (HAS_PCH_SPLIT(i915)) {
		assert_fdi_rx_pll_disabled(i915, pipe);
		assert_shared_dpll_disabled(i915, crtc_state->shared_dpll);
	} else {
		assert_pll_disabled(i915, pipe);
	}

	intel_lvds_pps_init_hw(i915, &lvds_encoder->init_pps);

	temp = lvds_encoder->init_lvds_val;
	temp |= LVDS_PORT_EN | LVDS_A0A2_CLKA_POWER_UP;

	if (HAS_PCH_CPT(i915)) {
		temp &= ~LVDS_PIPE_SEL_MASK_CPT;
		temp |= LVDS_PIPE_SEL_CPT(pipe);
	} else {
		temp &= ~LVDS_PIPE_SEL_MASK;
		temp |= LVDS_PIPE_SEL(pipe);
	}

	/* set the corresponsding LVDS_BORDER bit */
	temp &= ~LVDS_BORDER_ENABLE;
	temp |= crtc_state->gmch_pfit.lvds_border_bits;

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
	 * only controlled through the TRANSCONF reg.
	 */
	if (DISPLAY_VER(i915) == 4) {
		/*
		 * Bspec wording suggests that LVDS port dithering only exists
		 * for 18bpp panels.
		 */
		if (crtc_state->dither && crtc_state->pipe_bpp == 18)
			temp |= LVDS_ENABLE_DITHER;
		else
			temp &= ~LVDS_ENABLE_DITHER;
	}
	temp &= ~(LVDS_HSYNC_POLARITY | LVDS_VSYNC_POLARITY);
	if (adjusted_mode->flags & DRM_MODE_FLAG_NHSYNC)
		temp |= LVDS_HSYNC_POLARITY;
	if (adjusted_mode->flags & DRM_MODE_FLAG_NVSYNC)
		temp |= LVDS_VSYNC_POLARITY;

	intel_de_write(i915, lvds_encoder->reg, temp);
}

/*
 * Sets the power state for the panel.
 */
static void intel_enable_lvds(struct intel_atomic_state *state,
			      struct intel_encoder *encoder,
			      const struct intel_crtc_state *crtc_state,
			      const struct drm_connector_state *conn_state)
{
	struct intel_lvds_encoder *lvds_encoder = to_lvds_encoder(encoder);
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);

	intel_de_rmw(dev_priv, lvds_encoder->reg, 0, LVDS_PORT_EN);

	intel_de_rmw(dev_priv, PP_CONTROL(0), 0, PANEL_POWER_ON);
	intel_de_posting_read(dev_priv, lvds_encoder->reg);

	if (intel_de_wait_for_set(dev_priv, PP_STATUS(0), PP_ON, 5000))
		drm_err(&dev_priv->drm,
			"timed out waiting for panel to power on\n");

	intel_backlight_enable(crtc_state, conn_state);
}

static void intel_disable_lvds(struct intel_atomic_state *state,
			       struct intel_encoder *encoder,
			       const struct intel_crtc_state *old_crtc_state,
			       const struct drm_connector_state *old_conn_state)
{
	struct intel_lvds_encoder *lvds_encoder = to_lvds_encoder(encoder);
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);

	intel_de_rmw(dev_priv, PP_CONTROL(0), PANEL_POWER_ON, 0);
	if (intel_de_wait_for_clear(dev_priv, PP_STATUS(0), PP_ON, 1000))
		drm_err(&dev_priv->drm,
			"timed out waiting for panel to power off\n");

	intel_de_rmw(dev_priv, lvds_encoder->reg, LVDS_PORT_EN, 0);
	intel_de_posting_read(dev_priv, lvds_encoder->reg);
}

static void gmch_disable_lvds(struct intel_atomic_state *state,
			      struct intel_encoder *encoder,
			      const struct intel_crtc_state *old_crtc_state,
			      const struct drm_connector_state *old_conn_state)

{
	intel_backlight_disable(old_conn_state);

	intel_disable_lvds(state, encoder, old_crtc_state, old_conn_state);
}

static void pch_disable_lvds(struct intel_atomic_state *state,
			     struct intel_encoder *encoder,
			     const struct intel_crtc_state *old_crtc_state,
			     const struct drm_connector_state *old_conn_state)
{
	intel_backlight_disable(old_conn_state);
}

static void pch_post_disable_lvds(struct intel_atomic_state *state,
				  struct intel_encoder *encoder,
				  const struct intel_crtc_state *old_crtc_state,
				  const struct drm_connector_state *old_conn_state)
{
	intel_disable_lvds(state, encoder, old_crtc_state, old_conn_state);
}

static void intel_lvds_shutdown(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);

	if (intel_de_wait_for_clear(dev_priv, PP_STATUS(0), PP_CYCLE_DELAY_ACTIVE, 5000))
		drm_err(&dev_priv->drm,
			"timed out waiting for panel power cycle delay\n");
}

static enum drm_mode_status
intel_lvds_mode_valid(struct drm_connector *_connector,
		      struct drm_display_mode *mode)
{
	struct intel_connector *connector = to_intel_connector(_connector);
	struct drm_i915_private *i915 = to_i915(connector->base.dev);
	const struct drm_display_mode *fixed_mode =
		intel_panel_fixed_mode(connector, mode);
	int max_pixclk = to_i915(connector->base.dev)->display.cdclk.max_dotclk_freq;
	enum drm_mode_status status;

	status = intel_cpu_transcoder_mode_valid(i915, mode);
	if (status != MODE_OK)
		return status;

	status = intel_panel_mode_valid(connector, mode);
	if (status != MODE_OK)
		return status;

	if (fixed_mode->clock > max_pixclk)
		return MODE_CLOCK_HIGH;

	return MODE_OK;
}

static int intel_lvds_compute_config(struct intel_encoder *encoder,
				     struct intel_crtc_state *crtc_state,
				     struct drm_connector_state *conn_state)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	struct intel_lvds_encoder *lvds_encoder = to_lvds_encoder(encoder);
	struct intel_connector *connector = lvds_encoder->attached_connector;
	struct drm_display_mode *adjusted_mode = &crtc_state->hw.adjusted_mode;
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	unsigned int lvds_bpp;
	int ret;

	/* Should never happen!! */
	if (DISPLAY_VER(i915) < 4 && crtc->pipe == 0) {
		drm_err(&i915->drm, "Can't support LVDS on pipe A\n");
		return -EINVAL;
	}

	if (HAS_PCH_SPLIT(i915)) {
		crtc_state->has_pch_encoder = true;
		if (!intel_fdi_compute_pipe_bpp(crtc_state))
			return -EINVAL;
	}

	if (lvds_encoder->a3_power == LVDS_A3_POWER_UP)
		lvds_bpp = 8*3;
	else
		lvds_bpp = 6*3;

	/* TODO: Check crtc_state->max_link_bpp_x16 instead of bw_constrained */
	if (lvds_bpp != crtc_state->pipe_bpp && !crtc_state->bw_constrained) {
		drm_dbg_kms(&i915->drm,
			    "forcing display bpp (was %d) to LVDS (%d)\n",
			    crtc_state->pipe_bpp, lvds_bpp);
		crtc_state->pipe_bpp = lvds_bpp;
	}

	crtc_state->sink_format = INTEL_OUTPUT_FORMAT_RGB;
	crtc_state->output_format = INTEL_OUTPUT_FORMAT_RGB;

	/*
	 * We have timings from the BIOS for the panel, put them in
	 * to the adjusted mode.  The CRTC will be set up for this mode,
	 * with the panel scaling set up to source from the H/VDisplay
	 * of the original mode.
	 */
	ret = intel_panel_compute_config(connector, adjusted_mode);
	if (ret)
		return ret;

	if (adjusted_mode->flags & DRM_MODE_FLAG_DBLSCAN)
		return -EINVAL;

	ret = intel_panel_fitting(crtc_state, conn_state);
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
static int intel_lvds_get_modes(struct drm_connector *_connector)
{
	struct intel_connector *connector = to_intel_connector(_connector);
	const struct drm_edid *fixed_edid = connector->panel.fixed_edid;

	/* Use panel fixed edid if we have one */
	if (!IS_ERR_OR_NULL(fixed_edid)) {
		drm_edid_connector_update(&connector->base, fixed_edid);

		return drm_edid_connector_add_modes(&connector->base);
	}

	return intel_panel_get_modes(connector);
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

struct intel_encoder *intel_get_lvds_encoder(struct drm_i915_private *i915)
{
	struct intel_encoder *encoder;

	for_each_intel_encoder(&i915->drm, encoder) {
		if (encoder->type == INTEL_OUTPUT_LVDS)
			return encoder;
	}

	return NULL;
}

bool intel_is_dual_link_lvds(struct drm_i915_private *i915)
{
	struct intel_encoder *encoder = intel_get_lvds_encoder(i915);

	return encoder && to_lvds_encoder(encoder)->is_dual_link;
}

static bool compute_is_dual_link_lvds(struct intel_lvds_encoder *lvds_encoder)
{
	struct drm_i915_private *i915 = to_i915(lvds_encoder->base.base.dev);
	struct intel_connector *connector = lvds_encoder->attached_connector;
	const struct drm_display_mode *fixed_mode =
		intel_panel_preferred_fixed_mode(connector);
	unsigned int val;

	/* use the module option value if specified */
	if (i915->display.params.lvds_channel_mode > 0)
		return i915->display.params.lvds_channel_mode == 2;

	/* single channel LVDS is limited to 112 MHz */
	if (fixed_mode->clock > 112999)
		return true;

	if (dmi_check_system(intel_dual_link_lvds))
		return true;

	/*
	 * BIOS should set the proper LVDS register value at boot, but
	 * in reality, it doesn't set the value when the lid is closed;
	 * we need to check "the value to be set" in VBT when LVDS
	 * register is uninitialized.
	 */
	val = intel_de_read(i915, lvds_encoder->reg);
	if (HAS_PCH_CPT(i915))
		val &= ~(LVDS_DETECTED | LVDS_PIPE_SEL_MASK_CPT);
	else
		val &= ~(LVDS_DETECTED | LVDS_PIPE_SEL_MASK);
	if (val == 0)
		val = connector->panel.vbt.bios_lvds_val;

	return (val & LVDS_CLKB_POWER_MASK) == LVDS_CLKB_POWER_UP;
}

static void intel_lvds_add_properties(struct drm_connector *connector)
{
	intel_attach_scaling_mode_property(connector);
}

/**
 * intel_lvds_init - setup LVDS connectors on this device
 * @i915: i915 device
 *
 * Create the connector, register the LVDS DDC bus, and try to figure out what
 * modes we can display on the LVDS panel (if present).
 */
void intel_lvds_init(struct drm_i915_private *i915)
{
	struct intel_lvds_encoder *lvds_encoder;
	struct intel_connector *connector;
	const struct drm_edid *drm_edid;
	struct intel_encoder *encoder;
	i915_reg_t lvds_reg;
	u32 lvds;
	u8 ddc_pin;

	/* Skip init on machines we know falsely report LVDS */
	if (dmi_check_system(intel_no_lvds)) {
		drm_WARN(&i915->drm, !i915->display.vbt.int_lvds_support,
			 "Useless DMI match. Internal LVDS support disabled by VBT\n");
		return;
	}

	if (!i915->display.vbt.int_lvds_support) {
		drm_dbg_kms(&i915->drm,
			    "Internal LVDS support disabled by VBT\n");
		return;
	}

	if (HAS_PCH_SPLIT(i915))
		lvds_reg = PCH_LVDS;
	else
		lvds_reg = LVDS;

	lvds = intel_de_read(i915, lvds_reg);

	if (HAS_PCH_SPLIT(i915)) {
		if ((lvds & LVDS_DETECTED) == 0)
			return;
	}

	ddc_pin = GMBUS_PIN_PANEL;
	if (!intel_bios_is_lvds_present(i915, &ddc_pin)) {
		if ((lvds & LVDS_PORT_EN) == 0) {
			drm_dbg_kms(&i915->drm,
				    "LVDS is not present in VBT\n");
			return;
		}
		drm_dbg_kms(&i915->drm,
			    "LVDS is not present in VBT, but enabled anyway\n");
	}

	lvds_encoder = kzalloc(sizeof(*lvds_encoder), GFP_KERNEL);
	if (!lvds_encoder)
		return;

	connector = intel_connector_alloc();
	if (!connector) {
		kfree(lvds_encoder);
		return;
	}

	lvds_encoder->attached_connector = connector;
	encoder = &lvds_encoder->base;

	drm_connector_init_with_ddc(&i915->drm, &connector->base,
				    &intel_lvds_connector_funcs,
				    DRM_MODE_CONNECTOR_LVDS,
				    intel_gmbus_get_adapter(i915, ddc_pin));

	drm_encoder_init(&i915->drm, &encoder->base, &intel_lvds_enc_funcs,
			 DRM_MODE_ENCODER_LVDS, "LVDS");

	encoder->enable = intel_enable_lvds;
	encoder->pre_enable = intel_pre_enable_lvds;
	encoder->compute_config = intel_lvds_compute_config;
	if (HAS_PCH_SPLIT(i915)) {
		encoder->disable = pch_disable_lvds;
		encoder->post_disable = pch_post_disable_lvds;
	} else {
		encoder->disable = gmch_disable_lvds;
	}
	encoder->get_hw_state = intel_lvds_get_hw_state;
	encoder->get_config = intel_lvds_get_config;
	encoder->update_pipe = intel_backlight_update;
	encoder->shutdown = intel_lvds_shutdown;
	connector->get_hw_state = intel_connector_get_hw_state;

	intel_connector_attach_encoder(connector, encoder);

	encoder->type = INTEL_OUTPUT_LVDS;
	encoder->power_domain = POWER_DOMAIN_PORT_OTHER;
	encoder->port = PORT_NONE;
	encoder->cloneable = 0;
	if (DISPLAY_VER(i915) < 4)
		encoder->pipe_mask = BIT(PIPE_B);
	else
		encoder->pipe_mask = ~0;

	drm_connector_helper_add(&connector->base, &intel_lvds_connector_helper_funcs);
	connector->base.display_info.subpixel_order = SubPixelHorizontalRGB;

	lvds_encoder->reg = lvds_reg;

	intel_lvds_add_properties(&connector->base);

	intel_lvds_pps_get_hw_state(i915, &lvds_encoder->init_pps);
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
	mutex_lock(&i915->drm.mode_config.mutex);
	if (vga_switcheroo_handler_flags() & VGA_SWITCHEROO_CAN_SWITCH_DDC)
		drm_edid = drm_edid_read_switcheroo(&connector->base, connector->base.ddc);
	else
		drm_edid = drm_edid_read_ddc(&connector->base, connector->base.ddc);
	if (drm_edid) {
		if (drm_edid_connector_update(&connector->base, drm_edid) ||
		    !drm_edid_connector_add_modes(&connector->base)) {
			drm_edid_connector_update(&connector->base, NULL);
			drm_edid_free(drm_edid);
			drm_edid = ERR_PTR(-EINVAL);
		}
	} else {
		drm_edid = ERR_PTR(-ENOENT);
	}
	intel_bios_init_panel_late(i915, &connector->panel, NULL,
				   IS_ERR(drm_edid) ? NULL : drm_edid);

	/* Try EDID first */
	intel_panel_add_edid_fixed_modes(connector, true);

	/* Failed to get EDID, what about VBT? */
	if (!intel_panel_preferred_fixed_mode(connector))
		intel_panel_add_vbt_lfp_fixed_mode(connector);

	/*
	 * If we didn't get a fixed mode from EDID or VBT, try checking
	 * if the panel is already turned on.  If so, assume that
	 * whatever is currently programmed is the correct mode.
	 */
	if (!intel_panel_preferred_fixed_mode(connector))
		intel_panel_add_encoder_fixed_mode(connector, encoder);

	mutex_unlock(&i915->drm.mode_config.mutex);

	/* If we still don't have a mode after all that, give up. */
	if (!intel_panel_preferred_fixed_mode(connector))
		goto failed;

	intel_panel_init(connector, drm_edid);

	intel_backlight_setup(connector, INVALID_PIPE);

	lvds_encoder->is_dual_link = compute_is_dual_link_lvds(lvds_encoder);
	drm_dbg_kms(&i915->drm, "detected %s-link lvds configuration\n",
		    lvds_encoder->is_dual_link ? "dual" : "single");

	lvds_encoder->a3_power = lvds & LVDS_A3_POWER_MASK;

	return;

failed:
	drm_dbg_kms(&i915->drm, "No LVDS modes found, disabling.\n");
	drm_connector_cleanup(&connector->base);
	drm_encoder_cleanup(&encoder->base);
	kfree(lvds_encoder);
	intel_connector_free(connector);
	return;
}
