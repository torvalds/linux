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
#include <drm/drmP.h>
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

struct intel_lvds_encoder {
	struct intel_encoder base;

	bool is_dual_link;
	u32 reg;

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
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_lvds_encoder *lvds_encoder = to_lvds_encoder(&encoder->base);
	u32 tmp;

	tmp = I915_READ(lvds_encoder->reg);

	if (!(tmp & LVDS_PORT_EN))
		return false;

	if (HAS_PCH_CPT(dev))
		*pipe = PORT_TO_PIPE_CPT(tmp);
	else
		*pipe = PORT_TO_PIPE(tmp);

	return true;
}

static void intel_lvds_get_config(struct intel_encoder *encoder,
				  struct intel_crtc_config *pipe_config)
{
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 lvds_reg, tmp, flags = 0;
	int dotclock;

	if (HAS_PCH_SPLIT(dev))
		lvds_reg = PCH_LVDS;
	else
		lvds_reg = LVDS;

	tmp = I915_READ(lvds_reg);
	if (tmp & LVDS_HSYNC_POLARITY)
		flags |= DRM_MODE_FLAG_NHSYNC;
	else
		flags |= DRM_MODE_FLAG_PHSYNC;
	if (tmp & LVDS_VSYNC_POLARITY)
		flags |= DRM_MODE_FLAG_NVSYNC;
	else
		flags |= DRM_MODE_FLAG_PVSYNC;

	pipe_config->adjusted_mode.flags |= flags;

	/* gen2/3 store dither state in pfit control, needs to match */
	if (INTEL_INFO(dev)->gen < 4) {
		tmp = I915_READ(PFIT_CONTROL);

		pipe_config->gmch_pfit.control |= tmp & PANEL_8TO6_DITHER_ENABLE;
	}

	dotclock = pipe_config->port_clock;

	if (HAS_PCH_SPLIT(dev_priv->dev))
		ironlake_check_encoder_dotclock(pipe_config, dotclock);

	pipe_config->adjusted_mode.crtc_clock = dotclock;
}

/* The LVDS pin pair needs to be on before the DPLLs are enabled.
 * This is an exception to the general rule that mode_set doesn't turn
 * things on.
 */
static void intel_pre_enable_lvds(struct intel_encoder *encoder)
{
	struct intel_lvds_encoder *lvds_encoder = to_lvds_encoder(&encoder->base);
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *crtc = to_intel_crtc(encoder->base.crtc);
	const struct drm_display_mode *adjusted_mode =
		&crtc->config.adjusted_mode;
	int pipe = crtc->pipe;
	u32 temp;

	if (HAS_PCH_SPLIT(dev)) {
		assert_fdi_rx_pll_disabled(dev_priv, pipe);
		assert_shared_dpll_disabled(dev_priv,
					    intel_crtc_to_shared_dpll(crtc));
	} else {
		assert_pll_disabled(dev_priv, pipe);
	}

	temp = I915_READ(lvds_encoder->reg);
	temp |= LVDS_PORT_EN | LVDS_A0A2_CLKA_POWER_UP;

	if (HAS_PCH_CPT(dev)) {
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
	temp |= crtc->config.gmch_pfit.lvds_border_bits;
	/* Set the B0-B3 data pairs corresponding to whether we're going to
	 * set the DPLLs for dual-channel mode or not.
	 */
	if (lvds_encoder->is_dual_link)
		temp |= LVDS_B0B3_POWER_UP | LVDS_CLKB_POWER_UP;
	else
		temp &= ~(LVDS_B0B3_POWER_UP | LVDS_CLKB_POWER_UP);

	/* It would be nice to set 24 vs 18-bit mode (LVDS_A3_POWER_UP)
	 * appropriately here, but we need to look more thoroughly into how
	 * panels behave in the two modes.
	 */

	/* Set the dithering flag on LVDS as needed, note that there is no
	 * special lvds dither control bit on pch-split platforms, dithering is
	 * only controlled through the PIPECONF reg. */
	if (INTEL_INFO(dev)->gen == 4) {
		/* Bspec wording suggests that LVDS port dithering only exists
		 * for 18bpp panels. */
		if (crtc->config.dither && crtc->config.pipe_bpp == 18)
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
static void intel_enable_lvds(struct intel_encoder *encoder)
{
	struct drm_device *dev = encoder->base.dev;
	struct intel_lvds_encoder *lvds_encoder = to_lvds_encoder(&encoder->base);
	struct intel_connector *intel_connector =
		&lvds_encoder->attached_connector->base;
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 ctl_reg, stat_reg;

	if (HAS_PCH_SPLIT(dev)) {
		ctl_reg = PCH_PP_CONTROL;
		stat_reg = PCH_PP_STATUS;
	} else {
		ctl_reg = PP_CONTROL;
		stat_reg = PP_STATUS;
	}

	I915_WRITE(lvds_encoder->reg, I915_READ(lvds_encoder->reg) | LVDS_PORT_EN);

	I915_WRITE(ctl_reg, I915_READ(ctl_reg) | POWER_TARGET_ON);
	POSTING_READ(lvds_encoder->reg);
	if (wait_for((I915_READ(stat_reg) & PP_ON) != 0, 1000))
		DRM_ERROR("timed out waiting for panel to power on\n");

	intel_panel_enable_backlight(intel_connector);
}

static void intel_disable_lvds(struct intel_encoder *encoder)
{
	struct drm_device *dev = encoder->base.dev;
	struct intel_lvds_encoder *lvds_encoder = to_lvds_encoder(&encoder->base);
	struct intel_connector *intel_connector =
		&lvds_encoder->attached_connector->base;
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 ctl_reg, stat_reg;

	if (HAS_PCH_SPLIT(dev)) {
		ctl_reg = PCH_PP_CONTROL;
		stat_reg = PCH_PP_STATUS;
	} else {
		ctl_reg = PP_CONTROL;
		stat_reg = PP_STATUS;
	}

	intel_panel_disable_backlight(intel_connector);

	I915_WRITE(ctl_reg, I915_READ(ctl_reg) & ~POWER_TARGET_ON);
	if (wait_for((I915_READ(stat_reg) & PP_ON) == 0, 1000))
		DRM_ERROR("timed out waiting for panel to power off\n");

	I915_WRITE(lvds_encoder->reg, I915_READ(lvds_encoder->reg) & ~LVDS_PORT_EN);
	POSTING_READ(lvds_encoder->reg);
}

static enum drm_mode_status
intel_lvds_mode_valid(struct drm_connector *connector,
		      struct drm_display_mode *mode)
{
	struct intel_connector *intel_connector = to_intel_connector(connector);
	struct drm_display_mode *fixed_mode = intel_connector->panel.fixed_mode;

	if (mode->hdisplay > fixed_mode->hdisplay)
		return MODE_PANEL;
	if (mode->vdisplay > fixed_mode->vdisplay)
		return MODE_PANEL;

	return MODE_OK;
}

static bool intel_lvds_compute_config(struct intel_encoder *intel_encoder,
				      struct intel_crtc_config *pipe_config)
{
	struct drm_device *dev = intel_encoder->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_lvds_encoder *lvds_encoder =
		to_lvds_encoder(&intel_encoder->base);
	struct intel_connector *intel_connector =
		&lvds_encoder->attached_connector->base;
	struct drm_display_mode *adjusted_mode = &pipe_config->adjusted_mode;
	struct intel_crtc *intel_crtc = lvds_encoder->base.new_crtc;
	unsigned int lvds_bpp;

	/* Should never happen!! */
	if (INTEL_INFO(dev)->gen < 4 && intel_crtc->pipe == 0) {
		DRM_ERROR("Can't support LVDS on pipe A\n");
		return false;
	}

	if ((I915_READ(lvds_encoder->reg) & LVDS_A3_POWER_MASK) ==
	    LVDS_A3_POWER_UP)
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

static void intel_lvds_mode_set(struct intel_encoder *encoder)
{
	/*
	 * We don't do anything here, the LVDS port is fully set up in the pre
	 * enable hook - the ordering constraints for enabling the lvds port vs.
	 * enabling the display pll are too strict.
	 */
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
		      connector->base.id, drm_get_connector_name(connector));

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
	struct drm_i915_private *dev_priv = dev->dev_private;

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
	if (!HAS_PCH_SPLIT(dev)) {
		drm_modeset_lock_all(dev);
		intel_modeset_setup_hw_state(dev, true);
		drm_modeset_unlock_all(dev);
	}

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
		if (crtc && crtc->enabled) {
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
	.best_encoder = intel_best_encoder,
};

static const struct drm_connector_funcs intel_lvds_connector_funcs = {
	.dpms = intel_connector_dpms,
	.detect = intel_lvds_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.set_property = intel_lvds_set_property,
	.destroy = intel_lvds_destroy,
};

static const struct drm_encoder_funcs intel_lvds_enc_funcs = {
	.destroy = intel_encoder_destroy,
};

static int __init intel_no_lvds_dmi_callback(const struct dmi_system_id *id)
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

/*
 * Enumerate the child dev array parsed from VBT to check whether
 * the LVDS is present.
 * If it is present, return 1.
 * If it is not present, return false.
 * If no child dev is parsed from VBT, it assumes that the LVDS is present.
 */
static bool lvds_is_present_in_vbt(struct drm_device *dev,
				   u8 *i2c_pin)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int i;

	if (!dev_priv->vbt.child_dev_num)
		return true;

	for (i = 0; i < dev_priv->vbt.child_dev_num; i++) {
		union child_device_config *uchild = dev_priv->vbt.child_dev + i;
		struct old_child_dev_config *child = &uchild->old;

		/* If the device type is not LFP, continue.
		 * We have to check both the new identifiers as well as the
		 * old for compatibility with some BIOSes.
		 */
		if (child->device_type != DEVICE_TYPE_INT_LFP &&
		    child->device_type != DEVICE_TYPE_LFP)
			continue;

		if (intel_gmbus_is_port_valid(child->i2c_pin))
			*i2c_pin = child->i2c_pin;

		/* However, we cannot trust the BIOS writers to populate
		 * the VBT correctly.  Since LVDS requires additional
		 * information from AIM blocks, a non-zero addin offset is
		 * a good indicator that the LVDS is actually present.
		 */
		if (child->addin_offset)
			return true;

		/* But even then some BIOS writers perform some black magic
		 * and instantiate the device without reference to any
		 * additional data.  Trust that if the VBT was written into
		 * the OpRegion then they have validated the LVDS's existence.
		 */
		if (dev_priv->opregion.vbt)
			return true;
	}

	return false;
}

static int intel_dual_link_lvds_callback(const struct dmi_system_id *id)
{
	DRM_INFO("Forcing lvds to dual link mode on %s\n", id->ident);
	return 1;
}

static const struct dmi_system_id intel_dual_link_lvds[] = {
	{
		.callback = intel_dual_link_lvds_callback,
		.ident = "Apple MacBook Pro (Core i5/i7 Series)",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Apple Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "MacBookPro8,2"),
		},
	},
	{ }	/* terminating entry */
};

bool intel_is_dual_link_lvds(struct drm_device *dev)
{
	struct intel_encoder *encoder;
	struct intel_lvds_encoder *lvds_encoder;

	list_for_each_entry(encoder, &dev->mode_config.encoder_list,
			    base.head) {
		if (encoder->type == INTEL_OUTPUT_LVDS) {
			lvds_encoder = to_lvds_encoder(&encoder->base);

			return lvds_encoder->is_dual_link;
		}
	}

	return false;
}

static bool compute_is_dual_link_lvds(struct intel_lvds_encoder *lvds_encoder)
{
	struct drm_device *dev = lvds_encoder->base.base.dev;
	unsigned int val;
	struct drm_i915_private *dev_priv = dev->dev_private;

	/* use the module option value if specified */
	if (i915.lvds_channel_mode > 0)
		return i915.lvds_channel_mode == 2;

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
	struct drm_i915_private *dev_priv = dev->dev_private;
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
	u32 lvds;
	int pipe;
	u8 pin;

	if (!intel_lvds_supported(dev))
		return;

	/* Skip init on machines we know falsely report LVDS */
	if (dmi_check_system(intel_no_lvds))
		return;

	pin = GMBUS_PORT_PANEL;
	if (!lvds_is_present_in_vbt(dev, &pin)) {
		DRM_DEBUG_KMS("LVDS is not present in VBT\n");
		return;
	}

	if (HAS_PCH_SPLIT(dev)) {
		if ((I915_READ(PCH_LVDS) & LVDS_DETECTED) == 0)
			return;
		if (dev_priv->vbt.edp_support) {
			DRM_DEBUG_KMS("disable LVDS for eDP support\n");
			return;
		}
	}

	lvds_encoder = kzalloc(sizeof(*lvds_encoder), GFP_KERNEL);
	if (!lvds_encoder)
		return;

	lvds_connector = kzalloc(sizeof(*lvds_connector), GFP_KERNEL);
	if (!lvds_connector) {
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
			 DRM_MODE_ENCODER_LVDS);

	intel_encoder->enable = intel_enable_lvds;
	intel_encoder->pre_enable = intel_pre_enable_lvds;
	intel_encoder->compute_config = intel_lvds_compute_config;
	intel_encoder->mode_set = intel_lvds_mode_set;
	intel_encoder->disable = intel_disable_lvds;
	intel_encoder->get_hw_state = intel_lvds_get_hw_state;
	intel_encoder->get_config = intel_lvds_get_config;
	intel_connector->get_hw_state = intel_connector_get_hw_state;
	intel_connector->unregister = intel_connector_unregister;

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

	if (HAS_PCH_SPLIT(dev)) {
		lvds_encoder->reg = PCH_LVDS;
	} else {
		lvds_encoder->reg = LVDS;
	}

	/* create the scaling mode property */
	drm_mode_create_scaling_mode_property(dev);
	drm_object_attach_property(&connector->base,
				      dev->mode_config.scaling_mode_property,
				      DRM_MODE_SCALE_ASPECT);
	intel_connector->panel.fitting_mode = DRM_MODE_SCALE_ASPECT;
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
	edid = drm_get_edid(connector, intel_gmbus_get_adapter(dev_priv, pin));
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

	if (IS_ERR_OR_NULL(edid)) {
		/* Didn't get an EDID, so
		 * Set wide sync ranges so we get all modes
		 * handed to valid_mode for checking
		 */
		connector->display_info.min_vfreq = 0;
		connector->display_info.max_vfreq = 200;
		connector->display_info.min_hfreq = 0;
		connector->display_info.max_hfreq = 200;
	}

	list_for_each_entry(scan, &connector->probed_modes, head) {
		if (scan->type & DRM_MODE_TYPE_PREFERRED) {
			DRM_DEBUG_KMS("using preferred mode from EDID: ");
			drm_mode_debug_printmodeline(scan);

			fixed_mode = drm_mode_duplicate(dev, scan);
			if (fixed_mode) {
				downclock_mode =
					intel_find_panel_downclock(dev,
					fixed_mode, connector);
				if (downclock_mode != NULL &&
					i915.lvds_downclock) {
					/* We found the downclock for LVDS. */
					dev_priv->lvds_downclock_avail = true;
					dev_priv->lvds_downclock =
						downclock_mode->clock;
					DRM_DEBUG_KMS("LVDS downclock is found"
					" in EDID. Normal clock %dKhz, "
					"downclock %dKhz\n",
					fixed_mode->clock,
					dev_priv->lvds_downclock);
				}
				goto out;
			}
		}
	}

	/* Failed to get EDID, what about VBT? */
	if (dev_priv->vbt.lfp_lvds_vbt_mode) {
		DRM_DEBUG_KMS("using mode from VBT: ");
		drm_mode_debug_printmodeline(dev_priv->vbt.lfp_lvds_vbt_mode);

		fixed_mode = drm_mode_duplicate(dev, dev_priv->vbt.lfp_lvds_vbt_mode);
		if (fixed_mode) {
			fixed_mode->type |= DRM_MODE_TYPE_PREFERRED;
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

	lvds = I915_READ(LVDS);
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

	lvds_encoder->is_dual_link = compute_is_dual_link_lvds(lvds_encoder);
	DRM_DEBUG_KMS("detected %s-link lvds configuration\n",
		      lvds_encoder->is_dual_link ? "dual" : "single");

	/*
	 * Unlock registers and just
	 * leave them unlocked
	 */
	if (HAS_PCH_SPLIT(dev)) {
		I915_WRITE(PCH_PP_CONTROL,
			   I915_READ(PCH_PP_CONTROL) | PANEL_UNLOCK_REGS);
	} else {
		I915_WRITE(PP_CONTROL,
			   I915_READ(PP_CONTROL) | PANEL_UNLOCK_REGS);
	}
	lvds_connector->lid_notifier.notifier_call = intel_lid_notify;
	if (acpi_lid_notifier_register(&lvds_connector->lid_notifier)) {
		DRM_DEBUG_KMS("lid notifier registration failed\n");
		lvds_connector->lid_notifier.notifier_call = NULL;
	}
	drm_sysfs_connector_add(connector);

	intel_panel_init(&intel_connector->panel, fixed_mode, downclock_mode);
	intel_panel_setup_backlight(connector);

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
