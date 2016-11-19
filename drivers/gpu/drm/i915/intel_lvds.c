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
#include <linux/dmi.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/vga_switcheroo.h>
#include <drm/drmP.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_edid.h>
#include "intel_drv.h"
#include <drm/i915_drm.h>
#include "i915_drv.h"
#include <linux/acpi.h>

/* Private structure for the integrated LVDS support */
struct intel_lvds_connector {
	struct intel_connector base;

	struct notifier_block lid_notifier;
};

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

	struct intel_lvds_connector *attached_connector;
};

static struct intel_lvds_encoder *to_lvds_encoder(struct drm_encoder *encoder)
{
	return container_of(encoder, struct intel_lvds_encoder, base.base);
}

static struct intel_lvds_connector *to_lvds_connector(struct drm_connector *connector)
{
	return container_of(connector, struct intel_lvds_connector, base.base);
}

static bool intel_lvds_get_hw_state(struct intel_encoder *encoder,
				    enum pipe *pipe)
{
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_lvds_encoder *lvds_encoder = to_lvds_encoder(&encoder->base);
	enum intel_display_power_domain power_domain;
	u32 tmp;
	bool ret;

	power_domain = intel_display_port_power_domain(encoder);
	if (!intel_display_power_get_if_enabled(dev_priv, power_domain))
		return false;

	ret = false;

	tmp = I915_READ(lvds_encoder->reg);

	if (!(tmp & LVDS_PORT_EN))
		goto out;

	if (HAS_PCH_CPT(dev))
		*pipe = PORT_TO_PIPE_CPT(tmp);
	else
		*pipe = PORT_TO_PIPE(tmp);

	ret = true;

out:
	intel_display_power_put(dev_priv, power_domain);

	return ret;
}

static void intel_lvds_get_config(struct intel_encoder *encoder,
				  struct intel_crtc_state *pipe_config)
{
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_lvds_encoder *lvds_encoder = to_lvds_encoder(&encoder->base);
	u32 tmp, flags = 0;

	tmp = I915_READ(lvds_encoder->reg);
	if (tmp & LVDS_HSYNC_POLARITY)
		flags |= DRM_MODE_FLAG_NHSYNC;
	else
		flags |= DRM_MODE_FLAG_PHSYNC;
	if (tmp & LVDS_VSYNC_POLARITY)
		flags |= DRM_MODE_FLAG_NVSYNC;
	else
		flags |= DRM_MODE_FLAG_PVSYNC;

	pipe_config->base.adjusted_mode.flags |= flags;

	if (INTEL_INFO(dev)->gen < 5)
		pipe_config->gmch_pfit.lvds_border_bits =
			tmp & LVDS_BORDER_ENABLE;

	/* gen2/3 store dither state in pfit control, needs to match */
	if (INTEL_INFO(dev)->gen < 4) {
		tmp = I915_READ(PFIT_CONTROL);

		pipe_config->gmch_pfit.control |= tmp & PANEL_8TO6_DITHER_ENABLE;
	}

	pipe_config->base.adjusted_mode.crtc_clock = pipe_config->port_clock;
}

static void intel_lvds_pps_get_hw_state(struct drm_i915_private *dev_priv,
					struct intel_lvds_pps *pps)
{
	u32 val;

	pps->powerdown_on_reset = I915_READ(PP_CONTROL(0)) & PANEL_POWER_RESET;

	val = I915_READ(PP_ON_DELAYS(0));
	pps->port = (val & PANEL_PORT_SELECT_MASK) >>
		    PANEL_PORT_SELECT_SHIFT;
	pps->t1_t2 = (val & PANEL_POWER_UP_DELAY_MASK) >>
		     PANEL_POWER_UP_DELAY_SHIFT;
	pps->t5 = (val & PANEL_LIGHT_ON_DELAY_MASK) >>
		  PANEL_LIGHT_ON_DELAY_SHIFT;

	val = I915_READ(PP_OFF_DELAYS(0));
	pps->t3 = (val & PANEL_POWER_DOWN_DELAY_MASK) >>
		  PANEL_POWER_DOWN_DELAY_SHIFT;
	pps->tx = (val & PANEL_LIGHT_OFF_DELAY_MASK) >>
		  PANEL_LIGHT_OFF_DELAY_SHIFT;

	val = I915_READ(PP_DIVISOR(0));
	pps->divider = (val & PP_REFERENCE_DIVIDER_MASK) >>
		       PP_REFERENCE_DIVIDER_SHIFT;
	val = (val & PANEL_POWER_CYCLE_DELAY_MASK) >>
	      PANEL_POWER_CYCLE_DELAY_SHIFT;
	/*
	 * Remove the BSpec specified +1 (100ms) offset that accounts for a
	 * too short power-cycle delay due to the asynchronous programming of
	 * the register.
	 */
	if (val)
		val--;
	/* Convert from 100ms to 100us units */
	pps->t4 = val * 1000;

	if (INTEL_INFO(dev_priv)->gen <= 4 &&
	    pps->t1_t2 == 0 && pps->t5 == 0 && pps->t3 == 0 && pps->tx == 0) {
		DRM_DEBUG_KMS("Panel power timings uninitialized, "
			      "setting defaults\n");
		/* Set T2 to 40ms and T5 to 200ms in 100 usec units */
		pps->t1_t2 = 40 * 10;
		pps->t5 = 200 * 10;
		/* Set T3 to 35ms and Tx to 200ms in 100 usec units */
		pps->t3 = 35 * 10;
		pps->tx = 200 * 10;
	}

	DRM_DEBUG_DRIVER("LVDS PPS:t1+t2 %d t3 %d t4 %d t5 %d tx %d "
			 "divider %d port %d powerdown_on_reset %d\n",
			 pps->t1_t2, pps->t3, pps->t4, pps->t5, pps->tx,
			 pps->divider, pps->port, pps->powerdown_on_reset);
}

static void intel_lvds_pps_init_hw(struct drm_i915_private *dev_priv,
				   struct intel_lvds_pps *pps)
{
	u32 val;

	val = I915_READ(PP_CONTROL(0));
	WARN_ON((val & PANEL_UNLOCK_MASK) != PANEL_UNLOCK_REGS);
	if (pps->powerdown_on_reset)
		val |= PANEL_POWER_RESET;
	I915_WRITE(PP_CONTROL(0), val);

	I915_WRITE(PP_ON_DELAYS(0), (pps->port << PANEL_PORT_SELECT_SHIFT) |
				    (pps->t1_t2 << PANEL_POWER_UP_DELAY_SHIFT) |
				    (pps->t5 << PANEL_LIGHT_ON_DELAY_SHIFT));
	I915_WRITE(PP_OFF_DELAYS(0), (pps->t3 << PANEL_POWER_DOWN_DELAY_SHIFT) |
				     (pps->tx << PANEL_LIGHT_OFF_DELAY_SHIFT));

	val = pps->divider << PP_REFERENCE_DIVIDER_SHIFT;
	val |= (DIV_ROUND_UP(pps->t4, 1000) + 1) <<
	       PANEL_POWER_CYCLE_DELAY_SHIFT;
	I915_WRITE(PP_DIVISOR(0), val);
}

static void intel_pre_enable_lvds(struct intel_encoder *encoder,
				  struct intel_crtc_state *pipe_config,
				  struct drm_connector_state *conn_state)
{
	struct intel_lvds_encoder *lvds_encoder = to_lvds_encoder(&encoder->base);
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_crtc *crtc = to_intel_crtc(pipe_config->base.crtc);
	const struct drm_display_mode *adjusted_mode = &pipe_config->base.adjusted_mode;
	int pipe = crtc->pipe;
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
		temp &= ~PORT_TRANS_SEL_MASK;
		temp |= PORT_TRANS_SEL_CPT(pipe);
	} else {
		if (pipe == 1) {
			temp |= LVDS_PIPEB_SELECT;
		} else {
			temp &= ~LVDS_PIPEB_SELECT;
		}
	}

	/* set the corresponsding LVDS_BORDER bit */
	temp &= ~LVDS_BORDER_ENABLE;
	temp |= pipe_config->gmch_pfit.lvds_border_bits;
	/* Set the B0-B3 data pairs corresponding to whether we're going to
	 * set the DPLLs for dual-channel mode or not.
	 */
	if (lvds_encoder->is_dual_link)
		temp |= LVDS_B0B3_POWER_UP | LVDS_CLKB_POWER_UP;
	else
		temp &= ~(LVDS_B0B3_POWER_UP | LVDS_CLKB_POWER_UP);

	/* It would be nice to set 24 vs 18-bit mode (LVDS_A3_POWER_UP)
	 * appropriately here, but we need to look more thoroughly into how
	 * panels behave in the two modes. For now, let's just maintain the
	 * value we got from the BIOS.
	 */
	temp &= ~LVDS_A3_POWER_MASK;
	temp |= lvds_encoder->a3_power;

	/* Set the dithering flag on LVDS as needed, note that there is no
	 * special lvds dither control bit on pch-split platforms, dithering is
	 * only controlled through the PIPECONF reg. */
	if (IS_GEN4(dev_priv)) {
		/* Bspec wording suggests that LVDS port dithering only exists
		 * for 18bpp panels. */
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

	I915_WRITE(lvds_encoder->reg, temp);
}

/**
 * Sets the power state for the panel.
 */
static void intel_enable_lvds(struct intel_encoder *encoder,
			      struct intel_crtc_state *pipe_config,
			      struct drm_connector_state *conn_state)
{
	struct drm_device *dev = encoder->base.dev;
	struct intel_lvds_encoder *lvds_encoder = to_lvds_encoder(&encoder->base);
	struct intel_connector *intel_connector =
		&lvds_encoder->attached_connector->base;
	struct drm_i915_private *dev_priv = to_i915(dev);

	I915_WRITE(lvds_encoder->reg, I915_READ(lvds_encoder->reg) | LVDS_PORT_EN);

	I915_WRITE(PP_CONTROL(0), I915_READ(PP_CONTROL(0)) | PANEL_POWER_ON);
	POSTING_READ(lvds_encoder->reg);
	if (intel_wait_for_register(dev_priv, PP_STATUS(0), PP_ON, PP_ON, 1000))
		DRM_ERROR("timed out waiting for panel to power on\n");

	intel_panel_enable_backlight(intel_connector);
}

static void intel_disable_lvds(struct intel_encoder *encoder,
			       struct intel_crtc_state *old_crtc_state,
			       struct drm_connector_state *old_conn_state)
{
	struct intel_lvds_encoder *lvds_encoder = to_lvds_encoder(&encoder->base);
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);

	I915_WRITE(PP_CONTROL(0), I915_READ(PP_CONTROL(0)) & ~PANEL_POWER_ON);
	if (intel_wait_for_register(dev_priv, PP_STATUS(0), PP_ON, 0, 1000))
		DRM_ERROR("timed out waiting for panel to power off\n");

	I915_WRITE(lvds_encoder->reg, I915_READ(lvds_encoder->reg) & ~LVDS_PORT_EN);
	POSTING_READ(lvds_encoder->reg);
}

static void gmch_disable_lvds(struct intel_encoder *encoder,
			      struct intel_crtc_state *old_crtc_state,
			      struct drm_connector_state *old_conn_state)

{
	struct intel_lvds_encoder *lvds_encoder = to_lvds_encoder(&encoder->base);
	struct intel_connector *intel_connector =
		&lvds_encoder->attached_connector->base;

	intel_panel_disable_backlight(intel_connector);

	intel_disable_lvds(encoder, old_crtc_state, old_conn_state);
}

static void pch_disable_lvds(struct intel_encoder *encoder,
			     struct intel_crtc_state *old_crtc_state,
			     struct drm_connector_state *old_conn_state)
{
	struct intel_lvds_encoder *lvds_encoder = to_lvds_encoder(&encoder->base);
	struct intel_connector *intel_connector =
		&lvds_encoder->attached_connector->base;

	intel_panel_disable_backlight(intel_connector);
}

static void pch_post_disable_lvds(struct intel_encoder *encoder,
				  struct intel_crtc_state *old_crtc_state,
				  struct drm_connector_state *old_conn_state)
{
	intel_disable_lvds(encoder, old_crtc_state, old_conn_state);
}

static enum drm_mode_status
intel_lvds_mode_valid(struct drm_connector *connector,
		      struct drm_display_mode *mode)
{
	struct intel_connector *intel_connector = to_intel_connector(connector);
	struct drm_display_mode *fixed_mode = intel_connector->panel.fixed_mode;
	int max_pixclk = to_i915(connector->dev)->max_dotclk_freq;

	if (mode->hdisplay > fixed_mode->hdisplay)
		return MODE_PANEL;
	if (mode->vdisplay > fixed_mode->vdisplay)
		return MODE_PANEL;
	if (fixed_mode->clock > max_pixclk)
		return MODE_CLOCK_HIGH;

	return MODE_OK;
}

static bool intel_lvds_compute_config(struct intel_encoder *intel_encoder,
				      struct intel_crtc_state *pipe_config,
				      struct drm_connector_state *conn_state)
{
	struct drm_device *dev = intel_encoder->base.dev;
	struct intel_lvds_encoder *lvds_encoder =
		to_lvds_encoder(&intel_encoder->base);
	struct intel_connector *intel_connector =
		&lvds_encoder->attached_connector->base;
	struct drm_display_mode *adjusted_mode = &pipe_config->base.adjusted_mode;
	struct intel_crtc *intel_crtc = to_intel_crtc(pipe_config->base.crtc);
	unsigned int lvds_bpp;

	/* Should never happen!! */
	if (INTEL_INFO(dev)->gen < 4 && intel_crtc->pipe == 0) {
		DRM_ERROR("Can't support LVDS on pipe A\n");
		return false;
	}

	if (lvds_encoder->a3_power == LVDS_A3_POWER_UP)
		lvds_bpp = 8*3;
	else
		lvds_bpp = 6*3;

	if (lvds_bpp != pipe_config->pipe_bpp && !pipe_config->bw_constrained) {
		DRM_DEBUG_KMS("forcing display bpp (was %d) to LVDS (%d)\n",
			      pipe_config->pipe_bpp, lvds_bpp);
		pipe_config->pipe_bpp = lvds_bpp;
	}

	/*
	 * We have timings from the BIOS for the panel, put them in
	 * to the adjusted mode.  The CRTC will be set up for this mode,
	 * with the panel scaling set up to source from the H/VDisplay
	 * of the original mode.
	 */
	intel_fixed_panel_mode(intel_connector->panel.fixed_mode,
			       adjusted_mode);

	if (HAS_PCH_SPLIT(dev)) {
		pipe_config->has_pch_encoder = true;

		intel_pch_panel_fitting(intel_crtc, pipe_config,
					intel_connector->panel.fitting_mode);
	} else {
		intel_gmch_panel_fitting(intel_crtc, pipe_config,
					 intel_connector->panel.fitting_mode);

	}

	/*
	 * XXX: It would be nice to support lower refresh rates on the
	 * panels to reduce power consumption, and perhaps match the
	 * user's requested refresh rate.
	 */

	return true;
}

/**
 * Detect the LVDS connection.
 *
 * Since LVDS doesn't have hotlug, we use the lid as a proxy.  Open means
 * connected and closed means disconnected.  We also send hotplug events as
 * needed, using lid status notification from the input layer.
 */
static enum drm_connector_status
intel_lvds_detect(struct drm_connector *connector, bool force)
{
	struct drm_device *dev = connector->dev;
	enum drm_connector_status status;

	DRM_DEBUG_KMS("[CONNECTOR:%d:%s]\n",
		      connector->base.id, connector->name);

	status = intel_panel_detect(dev);
	if (status != connector_status_unknown)
		return status;

	return connector_status_connected;
}

/**
 * Return the list of DDC modes if available, or the BIOS fixed mode otherwise.
 */
static int intel_lvds_get_modes(struct drm_connector *connector)
{
	struct intel_lvds_connector *lvds_connector = to_lvds_connector(connector);
	struct drm_device *dev = connector->dev;
	struct drm_display_mode *mode;

	/* use cached edid if we have one */
	if (!IS_ERR_OR_NULL(lvds_connector->base.edid))
		return drm_add_edid_modes(connector, lvds_connector->base.edid);

	mode = drm_mode_duplicate(dev, lvds_connector->base.panel.fixed_mode);
	if (mode == NULL)
		return 0;

	drm_mode_probed_add(connector, mode);
	return 1;
}

static int intel_no_modeset_on_lid_dmi_callback(const struct dmi_system_id *id)
{
	DRM_INFO("Skipping forced modeset for %s\n", id->ident);
	return 1;
}

/* The GPU hangs up on these systems if modeset is performed on LID open */
static const struct dmi_system_id intel_no_modeset_on_lid[] = {
	{
		.callback = intel_no_modeset_on_lid_dmi_callback,
		.ident = "Toshiba Tecra A11",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "TECRA A11"),
		},
	},

	{ }	/* terminating entry */
};

/*
 * Lid events. Note the use of 'modeset':
 *  - we set it to MODESET_ON_LID_OPEN on lid close,
 *    and set it to MODESET_DONE on open
 *  - we use it as a "only once" bit (ie we ignore
 *    duplicate events where it was already properly set)
 *  - the suspend/resume paths will set it to
 *    MODESET_SUSPENDED and ignore the lid open event,
 *    because they restore the mode ("lid open").
 */
static int intel_lid_notify(struct notifier_block *nb, unsigned long val,
			    void *unused)
{
	struct intel_lvds_connector *lvds_connector =
		container_of(nb, struct intel_lvds_connector, lid_notifier);
	struct drm_connector *connector = &lvds_connector->base.base;
	struct drm_device *dev = connector->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);

	if (dev->switch_power_state != DRM_SWITCH_POWER_ON)
		return NOTIFY_OK;

	mutex_lock(&dev_priv->modeset_restore_lock);
	if (dev_priv->modeset_restore == MODESET_SUSPENDED)
		goto exit;
	/*
	 * check and update the status of LVDS connector after receiving
	 * the LID nofication event.
	 */
	connector->status = connector->funcs->detect(connector, false);

	/* Don't force modeset on machines where it causes a GPU lockup */
	if (dmi_check_system(intel_no_modeset_on_lid))
		goto exit;
	if (!acpi_lid_open()) {
		/* do modeset on next lid open event */
		dev_priv->modeset_restore = MODESET_ON_LID_OPEN;
		goto exit;
	}

	if (dev_priv->modeset_restore == MODESET_DONE)
		goto exit;

	/*
	 * Some old platform's BIOS love to wreak havoc while the lid is closed.
	 * We try to detect this here and undo any damage. The split for PCH
	 * platforms is rather conservative and a bit arbitrary expect that on
	 * those platforms VGA disabling requires actual legacy VGA I/O access,
	 * and as part of the cleanup in the hw state restore we also redisable
	 * the vga plane.
	 */
	if (!HAS_PCH_SPLIT(dev))
		intel_display_resume(dev);

	dev_priv->modeset_restore = MODESET_DONE;

exit:
	mutex_unlock(&dev_priv->modeset_restore_lock);
	return NOTIFY_OK;
}

/**
 * intel_lvds_destroy - unregister and free LVDS structures
 * @connector: connector to free
 *
 * Unregister the DDC bus for this connector then free the driver private
 * structure.
 */
static void intel_lvds_destroy(struct drm_connector *connector)
{
	struct intel_lvds_connector *lvds_connector =
		to_lvds_connector(connector);

	if (lvds_connector->lid_notifier.notifier_call)
		acpi_lid_notifier_unregister(&lvds_connector->lid_notifier);

	if (!IS_ERR_OR_NULL(lvds_connector->base.edid))
		kfree(lvds_connector->base.edid);

	intel_panel_fini(&lvds_connector->base.panel);

	drm_connector_cleanup(connector);
	kfree(connector);
}

static int intel_lvds_set_property(struct drm_connector *connector,
				   struct drm_property *property,
				   uint64_t value)
{
	struct intel_connector *intel_connector = to_intel_connector(connector);
	struct drm_device *dev = connector->dev;

	if (property == dev->mode_config.scaling_mode_property) {
		struct drm_crtc *crtc;

		if (value == DRM_MODE_SCALE_NONE) {
			DRM_DEBUG_KMS("no scaling not supported\n");
			return -EINVAL;
		}

		if (intel_connector->panel.fitting_mode == value) {
			/* the LVDS scaling property is not changed */
			return 0;
		}
		intel_connector->panel.fitting_mode = value;

		crtc = intel_attached_encoder(connector)->base.crtc;
		if (crtc && crtc->state->enable) {
			/*
			 * If the CRTC is enabled, the display will be changed
			 * according to the new panel fitting mode.
			 */
			intel_crtc_restore_mode(crtc);
		}
	}

	return 0;
}

static const struct drm_connector_helper_funcs intel_lvds_connector_helper_funcs = {
	.get_modes = intel_lvds_get_modes,
	.mode_valid = intel_lvds_mode_valid,
};

static const struct drm_connector_funcs intel_lvds_connector_funcs = {
	.dpms = drm_atomic_helper_connector_dpms,
	.detect = intel_lvds_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.set_property = intel_lvds_set_property,
	.atomic_get_property = intel_connector_atomic_get_property,
	.late_register = intel_connector_register,
	.early_unregister = intel_connector_unregister,
	.destroy = intel_lvds_destroy,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
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

struct intel_encoder *intel_get_lvds_encoder(struct drm_device *dev)
{
	struct intel_encoder *intel_encoder;

	for_each_intel_encoder(dev, intel_encoder)
		if (intel_encoder->type == INTEL_OUTPUT_LVDS)
			return intel_encoder;

	return NULL;
}

bool intel_is_dual_link_lvds(struct drm_device *dev)
{
	struct intel_encoder *encoder = intel_get_lvds_encoder(dev);

	return encoder && to_lvds_encoder(&encoder->base)->is_dual_link;
}

static bool compute_is_dual_link_lvds(struct intel_lvds_encoder *lvds_encoder)
{
	struct drm_device *dev = lvds_encoder->base.base.dev;
	unsigned int val;
	struct drm_i915_private *dev_priv = to_i915(dev);

	/* use the module option value if specified */
	if (i915.lvds_channel_mode > 0)
		return i915.lvds_channel_mode == 2;

	/* single channel LVDS is limited to 112 MHz */
	if (lvds_encoder->attached_connector->base.panel.fixed_mode->clock
	    > 112999)
		return true;

	if (dmi_check_system(intel_dual_link_lvds))
		return true;

	/* BIOS should set the proper LVDS register value at boot, but
	 * in reality, it doesn't set the value when the lid is closed;
	 * we need to check "the value to be set" in VBT when LVDS
	 * register is uninitialized.
	 */
	val = I915_READ(lvds_encoder->reg);
	if (!(val & ~(LVDS_PIPE_MASK | LVDS_DETECTED)))
		val = dev_priv->vbt.bios_lvds_val;

	return (val & LVDS_CLKB_POWER_MASK) == LVDS_CLKB_POWER_UP;
}

static bool intel_lvds_supported(struct drm_device *dev)
{
	/* With the introduction of the PCH we gained a dedicated
	 * LVDS presence pin, use it. */
	if (HAS_PCH_IBX(dev) || HAS_PCH_CPT(dev))
		return true;

	/* Otherwise LVDS was only attached to mobile products,
	 * except for the inglorious 830gm */
	if (INTEL_INFO(dev)->gen <= 4 && IS_MOBILE(dev) && !IS_I830(dev))
		return true;

	return false;
}

/**
 * intel_lvds_init - setup LVDS connectors on this device
 * @dev: drm device
 *
 * Create the connector, register the LVDS DDC bus, and try to figure out what
 * modes we can display on the LVDS panel (if present).
 */
void intel_lvds_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_lvds_encoder *lvds_encoder;
	struct intel_encoder *intel_encoder;
	struct intel_lvds_connector *lvds_connector;
	struct intel_connector *intel_connector;
	struct drm_connector *connector;
	struct drm_encoder *encoder;
	struct drm_display_mode *scan; /* *modes, *bios_mode; */
	struct drm_display_mode *fixed_mode = NULL;
	struct drm_display_mode *downclock_mode = NULL;
	struct edid *edid;
	struct drm_crtc *crtc;
	i915_reg_t lvds_reg;
	u32 lvds;
	int pipe;
	u8 pin;

	if (!intel_lvds_supported(dev))
		return;

	/* Skip init on machines we know falsely report LVDS */
	if (dmi_check_system(intel_no_lvds))
		return;

	if (HAS_PCH_SPLIT(dev))
		lvds_reg = PCH_LVDS;
	else
		lvds_reg = LVDS;

	lvds = I915_READ(lvds_reg);

	if (HAS_PCH_SPLIT(dev)) {
		if ((lvds & LVDS_DETECTED) == 0)
			return;
		if (dev_priv->vbt.edp.support) {
			DRM_DEBUG_KMS("disable LVDS for eDP support\n");
			return;
		}
	}

	pin = GMBUS_PIN_PANEL;
	if (!intel_bios_is_lvds_present(dev_priv, &pin)) {
		if ((lvds & LVDS_PORT_EN) == 0) {
			DRM_DEBUG_KMS("LVDS is not present in VBT\n");
			return;
		}
		DRM_DEBUG_KMS("LVDS is not present in VBT, but enabled anyway\n");
	}

	lvds_encoder = kzalloc(sizeof(*lvds_encoder), GFP_KERNEL);
	if (!lvds_encoder)
		return;

	lvds_connector = kzalloc(sizeof(*lvds_connector), GFP_KERNEL);
	if (!lvds_connector) {
		kfree(lvds_encoder);
		return;
	}

	if (intel_connector_init(&lvds_connector->base) < 0) {
		kfree(lvds_connector);
		kfree(lvds_encoder);
		return;
	}

	lvds_encoder->attached_connector = lvds_connector;

	intel_encoder = &lvds_encoder->base;
	encoder = &intel_encoder->base;
	intel_connector = &lvds_connector->base;
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
	intel_connector->get_hw_state = intel_connector_get_hw_state;

	intel_connector_attach_encoder(intel_connector, intel_encoder);
	intel_encoder->type = INTEL_OUTPUT_LVDS;

	intel_encoder->cloneable = 0;
	if (HAS_PCH_SPLIT(dev))
		intel_encoder->crtc_mask = (1 << 0) | (1 << 1) | (1 << 2);
	else if (IS_GEN4(dev))
		intel_encoder->crtc_mask = (1 << 0) | (1 << 1);
	else
		intel_encoder->crtc_mask = (1 << 1);

	drm_connector_helper_add(connector, &intel_lvds_connector_helper_funcs);
	connector->display_info.subpixel_order = SubPixelHorizontalRGB;
	connector->interlace_allowed = false;
	connector->doublescan_allowed = false;

	lvds_encoder->reg = lvds_reg;

	/* create the scaling mode property */
	drm_mode_create_scaling_mode_property(dev);
	drm_object_attach_property(&connector->base,
				      dev->mode_config.scaling_mode_property,
				      DRM_MODE_SCALE_ASPECT);
	intel_connector->panel.fitting_mode = DRM_MODE_SCALE_ASPECT;

	intel_lvds_pps_get_hw_state(dev_priv, &lvds_encoder->init_pps);
	lvds_encoder->init_lvds_val = lvds;

	/*
	 * LVDS discovery:
	 * 1) check for EDID on DDC
	 * 2) check for VBT data
	 * 3) check to see if LVDS is already on
	 *    if none of the above, no panel
	 * 4) make sure lid is open
	 *    if closed, act like it's not there for now
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
			drm_mode_connector_update_edid_property(connector,
								edid);
		} else {
			kfree(edid);
			edid = ERR_PTR(-EINVAL);
		}
	} else {
		edid = ERR_PTR(-ENOENT);
	}
	lvds_connector->base.edid = edid;

	list_for_each_entry(scan, &connector->probed_modes, head) {
		if (scan->type & DRM_MODE_TYPE_PREFERRED) {
			DRM_DEBUG_KMS("using preferred mode from EDID: ");
			drm_mode_debug_printmodeline(scan);

			fixed_mode = drm_mode_duplicate(dev, scan);
			if (fixed_mode)
				goto out;
		}
	}

	/* Failed to get EDID, what about VBT? */
	if (dev_priv->vbt.lfp_lvds_vbt_mode) {
		DRM_DEBUG_KMS("using mode from VBT: ");
		drm_mode_debug_printmodeline(dev_priv->vbt.lfp_lvds_vbt_mode);

		fixed_mode = drm_mode_duplicate(dev, dev_priv->vbt.lfp_lvds_vbt_mode);
		if (fixed_mode) {
			fixed_mode->type |= DRM_MODE_TYPE_PREFERRED;
			connector->display_info.width_mm = fixed_mode->width_mm;
			connector->display_info.height_mm = fixed_mode->height_mm;
			goto out;
		}
	}

	/*
	 * If we didn't get EDID, try checking if the panel is already turned
	 * on.  If so, assume that whatever is currently programmed is the
	 * correct mode.
	 */

	/* Ironlake: FIXME if still fail, not try pipe mode now */
	if (HAS_PCH_SPLIT(dev))
		goto failed;

	pipe = (lvds & LVDS_PIPEB_SELECT) ? 1 : 0;
	crtc = intel_get_crtc_for_pipe(dev, pipe);

	if (crtc && (lvds & LVDS_PORT_EN)) {
		fixed_mode = intel_crtc_mode_get(dev, crtc);
		if (fixed_mode) {
			DRM_DEBUG_KMS("using current (BIOS) mode: ");
			drm_mode_debug_printmodeline(fixed_mode);
			fixed_mode->type |= DRM_MODE_TYPE_PREFERRED;
			goto out;
		}
	}

	/* If we still don't have a mode after all that, give up. */
	if (!fixed_mode)
		goto failed;

out:
	mutex_unlock(&dev->mode_config.mutex);

	intel_panel_init(&intel_connector->panel, fixed_mode, downclock_mode);
	intel_panel_setup_backlight(connector, INVALID_PIPE);

	lvds_encoder->is_dual_link = compute_is_dual_link_lvds(lvds_encoder);
	DRM_DEBUG_KMS("detected %s-link lvds configuration\n",
		      lvds_encoder->is_dual_link ? "dual" : "single");

	lvds_encoder->a3_power = lvds & LVDS_A3_POWER_MASK;

	lvds_connector->lid_notifier.notifier_call = intel_lid_notify;
	if (acpi_lid_notifier_register(&lvds_connector->lid_notifier)) {
		DRM_DEBUG_KMS("lid notifier registration failed\n");
		lvds_connector->lid_notifier.notifier_call = NULL;
	}

	return;

failed:
	mutex_unlock(&dev->mode_config.mutex);

	DRM_DEBUG_KMS("No LVDS modes found, disabling.\n");
	drm_connector_cleanup(connector);
	drm_encoder_cleanup(encoder);
	kfree(lvds_encoder);
	kfree(lvds_connector);
	return;
}
