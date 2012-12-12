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
#include "drmP.h"
#include "drm.h"
#include "drm_crtc.h"
#include "drm_edid.h"
#include "intel_drv.h"
#include "i915_drm.h"
#include "i915_drv.h"
#include <linux/acpi.h>

/* Private structure for the integrated LVDS support */
struct intel_lvds {
	struct intel_encoder base;

	struct edid *edid;

	int fitting_mode;
	u32 pfit_control;
	u32 pfit_pgm_ratios;
	bool pfit_dirty;

	struct drm_display_mode *fixed_mode;
};

static struct intel_lvds *to_intel_lvds(struct drm_encoder *encoder)
{
	return container_of(encoder, struct intel_lvds, base.base);
}

static struct intel_lvds *intel_attached_lvds(struct drm_connector *connector)
{
	return container_of(intel_attached_encoder(connector),
			    struct intel_lvds, base);
}

/**
 * Sets the power state for the panel.
 */
static void intel_lvds_enable(struct intel_lvds *intel_lvds)
{
	struct drm_device *dev = intel_lvds->base.base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 ctl_reg, lvds_reg;

	if (HAS_PCH_SPLIT(dev)) {
		ctl_reg = PCH_PP_CONTROL;
		lvds_reg = PCH_LVDS;
	} else {
		ctl_reg = PP_CONTROL;
		lvds_reg = LVDS;
	}

	I915_WRITE(lvds_reg, I915_READ(lvds_reg) | LVDS_PORT_EN);

	if (intel_lvds->pfit_dirty) {
		/*
		 * Enable automatic panel scaling so that non-native modes
		 * fill the screen.  The panel fitter should only be
		 * adjusted whilst the pipe is disabled, according to
		 * register description and PRM.
		 */
		DRM_DEBUG_KMS("applying panel-fitter: %x, %x\n",
			      intel_lvds->pfit_control,
			      intel_lvds->pfit_pgm_ratios);
		if (wait_for((I915_READ(PP_STATUS) & PP_ON) == 0, 1000)) {
			DRM_ERROR("timed out waiting for panel to power off\n");
		} else {
			I915_WRITE(PFIT_PGM_RATIOS, intel_lvds->pfit_pgm_ratios);
			I915_WRITE(PFIT_CONTROL, intel_lvds->pfit_control);
			intel_lvds->pfit_dirty = false;
		}
	}

	I915_WRITE(ctl_reg, I915_READ(ctl_reg) | POWER_TARGET_ON);
	POSTING_READ(lvds_reg);

	intel_panel_enable_backlight(dev);
}

static void intel_lvds_disable(struct intel_lvds *intel_lvds)
{
	struct drm_device *dev = intel_lvds->base.base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 ctl_reg, lvds_reg;

	if (HAS_PCH_SPLIT(dev)) {
		ctl_reg = PCH_PP_CONTROL;
		lvds_reg = PCH_LVDS;
	} else {
		ctl_reg = PP_CONTROL;
		lvds_reg = LVDS;
	}

	intel_panel_disable_backlight(dev);

	I915_WRITE(ctl_reg, I915_READ(ctl_reg) & ~POWER_TARGET_ON);

	if (intel_lvds->pfit_control) {
		if (wait_for((I915_READ(PP_STATUS) & PP_ON) == 0, 1000))
			DRM_ERROR("timed out waiting for panel to power off\n");

		I915_WRITE(PFIT_CONTROL, 0);
		intel_lvds->pfit_dirty = true;
	}

	I915_WRITE(lvds_reg, I915_READ(lvds_reg) & ~LVDS_PORT_EN);
	POSTING_READ(lvds_reg);
}

static void intel_lvds_dpms(struct drm_encoder *encoder, int mode)
{
	struct intel_lvds *intel_lvds = to_intel_lvds(encoder);

	if (mode == DRM_MODE_DPMS_ON)
		intel_lvds_enable(intel_lvds);
	else
		intel_lvds_disable(intel_lvds);

	/* XXX: We never power down the LVDS pairs. */
}

static int intel_lvds_mode_valid(struct drm_connector *connector,
				 struct drm_display_mode *mode)
{
	struct intel_lvds *intel_lvds = intel_attached_lvds(connector);
	struct drm_display_mode *fixed_mode = intel_lvds->fixed_mode;

	if (mode->hdisplay > fixed_mode->hdisplay)
		return MODE_PANEL;
	if (mode->vdisplay > fixed_mode->vdisplay)
		return MODE_PANEL;

	return MODE_OK;
}

static void
centre_horizontally(struct drm_display_mode *mode,
		    int width)
{
	u32 border, sync_pos, blank_width, sync_width;

	/* keep the hsync and hblank widths constant */
	sync_width = mode->crtc_hsync_end - mode->crtc_hsync_start;
	blank_width = mode->crtc_hblank_end - mode->crtc_hblank_start;
	sync_pos = (blank_width - sync_width + 1) / 2;

	border = (mode->hdisplay - width + 1) / 2;
	border += border & 1; /* make the border even */

	mode->crtc_hdisplay = width;
	mode->crtc_hblank_start = width + border;
	mode->crtc_hblank_end = mode->crtc_hblank_start + blank_width;

	mode->crtc_hsync_start = mode->crtc_hblank_start + sync_pos;
	mode->crtc_hsync_end = mode->crtc_hsync_start + sync_width;
}

static void
centre_vertically(struct drm_display_mode *mode,
		  int height)
{
	u32 border, sync_pos, blank_width, sync_width;

	/* keep the vsync and vblank widths constant */
	sync_width = mode->crtc_vsync_end - mode->crtc_vsync_start;
	blank_width = mode->crtc_vblank_end - mode->crtc_vblank_start;
	sync_pos = (blank_width - sync_width + 1) / 2;

	border = (mode->vdisplay - height + 1) / 2;

	mode->crtc_vdisplay = height;
	mode->crtc_vblank_start = height + border;
	mode->crtc_vblank_end = mode->crtc_vblank_start + blank_width;

	mode->crtc_vsync_start = mode->crtc_vblank_start + sync_pos;
	mode->crtc_vsync_end = mode->crtc_vsync_start + sync_width;
}

static inline u32 panel_fitter_scaling(u32 source, u32 target)
{
	/*
	 * Floating point operation is not supported. So the FACTOR
	 * is defined, which can avoid the floating point computation
	 * when calculating the panel ratio.
	 */
#define ACCURACY 12
#define FACTOR (1 << ACCURACY)
	u32 ratio = source * FACTOR / target;
	return (FACTOR * ratio + FACTOR/2) / FACTOR;
}

static bool intel_lvds_mode_fixup(struct drm_encoder *encoder,
				  struct drm_display_mode *mode,
				  struct drm_display_mode *adjusted_mode)
{
	struct drm_device *dev = encoder->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(encoder->crtc);
	struct intel_lvds *intel_lvds = to_intel_lvds(encoder);
	struct drm_encoder *tmp_encoder;
	u32 pfit_control = 0, pfit_pgm_ratios = 0, border = 0;
	int pipe;

	/* Should never happen!! */
	if (INTEL_INFO(dev)->gen < 4 && intel_crtc->pipe == 0) {
		DRM_ERROR("Can't support LVDS on pipe A\n");
		return false;
	}

	/* Should never happen!! */
	list_for_each_entry(tmp_encoder, &dev->mode_config.encoder_list, head) {
		if (tmp_encoder != encoder && tmp_encoder->crtc == encoder->crtc) {
			DRM_ERROR("Can't enable LVDS and another "
			       "encoder on the same pipe\n");
			return false;
		}
	}

	/*
	 * We have timings from the BIOS for the panel, put them in
	 * to the adjusted mode.  The CRTC will be set up for this mode,
	 * with the panel scaling set up to source from the H/VDisplay
	 * of the original mode.
	 */
	intel_fixed_panel_mode(intel_lvds->fixed_mode, adjusted_mode);

	if (HAS_PCH_SPLIT(dev)) {
		intel_pch_panel_fitting(dev, intel_lvds->fitting_mode,
					mode, adjusted_mode);
		return true;
	}

	/* Native modes don't need fitting */
	if (adjusted_mode->hdisplay == mode->hdisplay &&
	    adjusted_mode->vdisplay == mode->vdisplay)
		goto out;

	/* 965+ wants fuzzy fitting */
	if (INTEL_INFO(dev)->gen >= 4)
		pfit_control |= ((intel_crtc->pipe << PFIT_PIPE_SHIFT) |
				 PFIT_FILTER_FUZZY);

	/*
	 * Enable automatic panel scaling for non-native modes so that they fill
	 * the screen.  Should be enabled before the pipe is enabled, according
	 * to register description and PRM.
	 * Change the value here to see the borders for debugging
	 */
	for_each_pipe(pipe)
		I915_WRITE(BCLRPAT(pipe), 0);

	switch (intel_lvds->fitting_mode) {
	case DRM_MODE_SCALE_CENTER:
		/*
		 * For centered modes, we have to calculate border widths &
		 * heights and modify the values programmed into the CRTC.
		 */
		centre_horizontally(adjusted_mode, mode->hdisplay);
		centre_vertically(adjusted_mode, mode->vdisplay);
		border = LVDS_BORDER_ENABLE;
		break;

	case DRM_MODE_SCALE_ASPECT:
		/* Scale but preserve the aspect ratio */
		if (INTEL_INFO(dev)->gen >= 4) {
			u32 scaled_width = adjusted_mode->hdisplay * mode->vdisplay;
			u32 scaled_height = mode->hdisplay * adjusted_mode->vdisplay;

			/* 965+ is easy, it does everything in hw */
			if (scaled_width > scaled_height)
				pfit_control |= PFIT_ENABLE | PFIT_SCALING_PILLAR;
			else if (scaled_width < scaled_height)
				pfit_control |= PFIT_ENABLE | PFIT_SCALING_LETTER;
			else if (adjusted_mode->hdisplay != mode->hdisplay)
				pfit_control |= PFIT_ENABLE | PFIT_SCALING_AUTO;
		} else {
			u32 scaled_width = adjusted_mode->hdisplay * mode->vdisplay;
			u32 scaled_height = mode->hdisplay * adjusted_mode->vdisplay;
			/*
			 * For earlier chips we have to calculate the scaling
			 * ratio by hand and program it into the
			 * PFIT_PGM_RATIO register
			 */
			if (scaled_width > scaled_height) { /* pillar */
				centre_horizontally(adjusted_mode, scaled_height / mode->vdisplay);

				border = LVDS_BORDER_ENABLE;
				if (mode->vdisplay != adjusted_mode->vdisplay) {
					u32 bits = panel_fitter_scaling(mode->vdisplay, adjusted_mode->vdisplay);
					pfit_pgm_ratios |= (bits << PFIT_HORIZ_SCALE_SHIFT |
							    bits << PFIT_VERT_SCALE_SHIFT);
					pfit_control |= (PFIT_ENABLE |
							 VERT_INTERP_BILINEAR |
							 HORIZ_INTERP_BILINEAR);
				}
			} else if (scaled_width < scaled_height) { /* letter */
				centre_vertically(adjusted_mode, scaled_width / mode->hdisplay);

				border = LVDS_BORDER_ENABLE;
				if (mode->hdisplay != adjusted_mode->hdisplay) {
					u32 bits = panel_fitter_scaling(mode->hdisplay, adjusted_mode->hdisplay);
					pfit_pgm_ratios |= (bits << PFIT_HORIZ_SCALE_SHIFT |
							    bits << PFIT_VERT_SCALE_SHIFT);
					pfit_control |= (PFIT_ENABLE |
							 VERT_INTERP_BILINEAR |
							 HORIZ_INTERP_BILINEAR);
				}
			} else
				/* Aspects match, Let hw scale both directions */
				pfit_control |= (PFIT_ENABLE |
						 VERT_AUTO_SCALE | HORIZ_AUTO_SCALE |
						 VERT_INTERP_BILINEAR |
						 HORIZ_INTERP_BILINEAR);
		}
		break;

	case DRM_MODE_SCALE_FULLSCREEN:
		/*
		 * Full scaling, even if it changes the aspect ratio.
		 * Fortunately this is all done for us in hw.
		 */
		if (mode->vdisplay != adjusted_mode->vdisplay ||
		    mode->hdisplay != adjusted_mode->hdisplay) {
			pfit_control |= PFIT_ENABLE;
			if (INTEL_INFO(dev)->gen >= 4)
				pfit_control |= PFIT_SCALING_AUTO;
			else
				pfit_control |= (VERT_AUTO_SCALE |
						 VERT_INTERP_BILINEAR |
						 HORIZ_AUTO_SCALE |
						 HORIZ_INTERP_BILINEAR);
		}
		break;

	default:
		break;
	}

out:
	/* If not enabling scaling, be consistent and always use 0. */
	if ((pfit_control & PFIT_ENABLE) == 0) {
		pfit_control = 0;
		pfit_pgm_ratios = 0;
	}

	/* Make sure pre-965 set dither correctly */
	if (INTEL_INFO(dev)->gen < 4 && dev_priv->lvds_dither)
		pfit_control |= PANEL_8TO6_DITHER_ENABLE;

	if (pfit_control != intel_lvds->pfit_control ||
	    pfit_pgm_ratios != intel_lvds->pfit_pgm_ratios) {
		intel_lvds->pfit_control = pfit_control;
		intel_lvds->pfit_pgm_ratios = pfit_pgm_ratios;
		intel_lvds->pfit_dirty = true;
	}
	dev_priv->lvds_border_bits = border;

	/*
	 * XXX: It would be nice to support lower refresh rates on the
	 * panels to reduce power consumption, and perhaps match the
	 * user's requested refresh rate.
	 */

	return true;
}

static void intel_lvds_prepare(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_lvds *intel_lvds = to_intel_lvds(encoder);

	/* We try to do the minimum that is necessary in order to unlock
	 * the registers for mode setting.
	 *
	 * On Ironlake, this is quite simple as we just set the unlock key
	 * and ignore all subtleties. (This may cause some issues...)
	 *
	 * Prior to Ironlake, we must disable the pipe if we want to adjust
	 * the panel fitter. However at all other times we can just reset
	 * the registers regardless.
	 */

	if (HAS_PCH_SPLIT(dev)) {
		I915_WRITE(PCH_PP_CONTROL,
			   I915_READ(PCH_PP_CONTROL) | PANEL_UNLOCK_REGS);
	} else if (intel_lvds->pfit_dirty) {
		I915_WRITE(PP_CONTROL,
			   (I915_READ(PP_CONTROL) | PANEL_UNLOCK_REGS)
			   & ~POWER_TARGET_ON);
	} else {
		I915_WRITE(PP_CONTROL,
			   I915_READ(PP_CONTROL) | PANEL_UNLOCK_REGS);
	}
}

static void intel_lvds_commit(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_lvds *intel_lvds = to_intel_lvds(encoder);

	/* Undo any unlocking done in prepare to prevent accidental
	 * adjustment of the registers.
	 */
	if (HAS_PCH_SPLIT(dev)) {
		u32 val = I915_READ(PCH_PP_CONTROL);
		if ((val & PANEL_UNLOCK_REGS) == PANEL_UNLOCK_REGS)
			I915_WRITE(PCH_PP_CONTROL, val & 0x3);
	} else {
		u32 val = I915_READ(PP_CONTROL);
		if ((val & PANEL_UNLOCK_REGS) == PANEL_UNLOCK_REGS)
			I915_WRITE(PP_CONTROL, val & 0x3);
	}

	/* Always do a full power on as we do not know what state
	 * we were left in.
	 */
	intel_lvds_enable(intel_lvds);
}

static void intel_lvds_mode_set(struct drm_encoder *encoder,
				struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode)
{
	/*
	 * The LVDS pin pair will already have been turned on in the
	 * intel_crtc_mode_set since it has a large impact on the DPLL
	 * settings.
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
	struct intel_lvds *intel_lvds = intel_attached_lvds(connector);
	struct drm_device *dev = connector->dev;
	struct drm_display_mode *mode;

	if (intel_lvds->edid)
		return drm_add_edid_modes(connector, intel_lvds->edid);

	mode = drm_mode_duplicate(dev, intel_lvds->fixed_mode);
	if (mode == NULL)
		return 0;

	drm_mode_probed_add(connector, mode);
	return 1;
}

static int intel_no_modeset_on_lid_dmi_callback(const struct dmi_system_id *id)
{
	DRM_DEBUG_KMS("Skipping forced modeset for %s\n", id->ident);
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
 * Lid events. Note the use of 'modeset_on_lid':
 *  - we set it on lid close, and reset it on open
 *  - we use it as a "only once" bit (ie we ignore
 *    duplicate events where it was already properly
 *    set/reset)
 *  - the suspend/resume paths will also set it to
 *    zero, since they restore the mode ("lid open").
 */
static int intel_lid_notify(struct notifier_block *nb, unsigned long val,
			    void *unused)
{
	struct drm_i915_private *dev_priv =
		container_of(nb, struct drm_i915_private, lid_notifier);
	struct drm_device *dev = dev_priv->dev;
	struct drm_connector *connector = dev_priv->int_lvds_connector;

	if (dev->switch_power_state != DRM_SWITCH_POWER_ON)
		return NOTIFY_OK;

	/*
	 * check and update the status of LVDS connector after receiving
	 * the LID nofication event.
	 */
	if (connector)
		connector->status = connector->funcs->detect(connector,
							     false);

	/* Don't force modeset on machines where it causes a GPU lockup */
	if (dmi_check_system(intel_no_modeset_on_lid))
		return NOTIFY_OK;
	if (!acpi_lid_open()) {
		dev_priv->modeset_on_lid = 1;
		return NOTIFY_OK;
	}

	if (!dev_priv->modeset_on_lid)
		return NOTIFY_OK;

	dev_priv->modeset_on_lid = 0;

	mutex_lock(&dev->mode_config.mutex);
	drm_helper_resume_force_mode(dev);
	mutex_unlock(&dev->mode_config.mutex);

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
	struct drm_device *dev = connector->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (dev_priv->lid_notifier.notifier_call)
		acpi_lid_notifier_unregister(&dev_priv->lid_notifier);
	drm_sysfs_connector_remove(connector);
	drm_connector_cleanup(connector);
	kfree(connector);
}

static int intel_lvds_set_property(struct drm_connector *connector,
				   struct drm_property *property,
				   uint64_t value)
{
	struct intel_lvds *intel_lvds = intel_attached_lvds(connector);
	struct drm_device *dev = connector->dev;

	if (property == dev->mode_config.scaling_mode_property) {
		struct drm_crtc *crtc = intel_lvds->base.base.crtc;

		if (value == DRM_MODE_SCALE_NONE) {
			DRM_DEBUG_KMS("no scaling not supported\n");
			return -EINVAL;
		}

		if (intel_lvds->fitting_mode == value) {
			/* the LVDS scaling property is not changed */
			return 0;
		}
		intel_lvds->fitting_mode = value;
		if (crtc && crtc->enabled) {
			/*
			 * If the CRTC is enabled, the display will be changed
			 * according to the new panel fitting mode.
			 */
			drm_crtc_helper_set_mode(crtc, &crtc->mode,
				crtc->x, crtc->y, crtc->fb);
		}
	}

	return 0;
}

static const struct drm_encoder_helper_funcs intel_lvds_helper_funcs = {
	.dpms = intel_lvds_dpms,
	.mode_fixup = intel_lvds_mode_fixup,
	.prepare = intel_lvds_prepare,
	.mode_set = intel_lvds_mode_set,
	.commit = intel_lvds_commit,
};

static const struct drm_connector_helper_funcs intel_lvds_connector_helper_funcs = {
	.get_modes = intel_lvds_get_modes,
	.mode_valid = intel_lvds_mode_valid,
	.best_encoder = intel_best_encoder,
};

static const struct drm_connector_funcs intel_lvds_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
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
	DRM_DEBUG_KMS("Skipping LVDS initialization for %s\n", id->ident);
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
		.ident = "Asus EeeBox PC EB1007",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK Computer INC."),
			DMI_MATCH(DMI_PRODUCT_NAME, "EB1007"),
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
		.ident = "ZOTAC ZBOXSD-ID12/ID13",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "ZOTAC"),
			DMI_MATCH(DMI_BOARD_NAME, "ZBOXSD-ID12/ID13"),
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

	{ }	/* terminating entry */
};

/**
 * intel_find_lvds_downclock - find the reduced downclock for LVDS in EDID
 * @dev: drm device
 * @connector: LVDS connector
 *
 * Find the reduced downclock for LVDS in EDID.
 */
static void intel_find_lvds_downclock(struct drm_device *dev,
				      struct drm_display_mode *fixed_mode,
				      struct drm_connector *connector)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_display_mode *scan;
	int temp_downclock;

	temp_downclock = fixed_mode->clock;
	list_for_each_entry(scan, &connector->probed_modes, head) {
		/*
		 * If one mode has the same resolution with the fixed_panel
		 * mode while they have the different refresh rate, it means
		 * that the reduced downclock is found for the LVDS. In such
		 * case we can set the different FPx0/1 to dynamically select
		 * between low and high frequency.
		 */
		if (scan->hdisplay == fixed_mode->hdisplay &&
		    scan->hsync_start == fixed_mode->hsync_start &&
		    scan->hsync_end == fixed_mode->hsync_end &&
		    scan->htotal == fixed_mode->htotal &&
		    scan->vdisplay == fixed_mode->vdisplay &&
		    scan->vsync_start == fixed_mode->vsync_start &&
		    scan->vsync_end == fixed_mode->vsync_end &&
		    scan->vtotal == fixed_mode->vtotal) {
			if (scan->clock < temp_downclock) {
				/*
				 * The downclock is already found. But we
				 * expect to find the lower downclock.
				 */
				temp_downclock = scan->clock;
			}
		}
	}
	if (temp_downclock < fixed_mode->clock && i915_lvds_downclock) {
		/* We found the downclock for LVDS. */
		dev_priv->lvds_downclock_avail = 1;
		dev_priv->lvds_downclock = temp_downclock;
		DRM_DEBUG_KMS("LVDS downclock is found in EDID. "
			      "Normal clock %dKhz, downclock %dKhz\n",
			      fixed_mode->clock, temp_downclock);
	}
}

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

	if (!dev_priv->child_dev_num)
		return true;

	for (i = 0; i < dev_priv->child_dev_num; i++) {
		struct child_device_config *child = dev_priv->child_dev + i;

		/* If the device type is not LFP, continue.
		 * We have to check both the new identifiers as well as the
		 * old for compatibility with some BIOSes.
		 */
		if (child->device_type != DEVICE_TYPE_INT_LFP &&
		    child->device_type != DEVICE_TYPE_LFP)
			continue;

		if (child->i2c_pin)
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

/**
 * intel_lvds_init - setup LVDS connectors on this device
 * @dev: drm device
 *
 * Create the connector, register the LVDS DDC bus, and try to figure out what
 * modes we can display on the LVDS panel (if present).
 */
bool intel_lvds_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_lvds *intel_lvds;
	struct intel_encoder *intel_encoder;
	struct intel_connector *intel_connector;
	struct drm_connector *connector;
	struct drm_encoder *encoder;
	struct drm_display_mode *scan; /* *modes, *bios_mode; */
	struct drm_crtc *crtc;
	u32 lvds;
	int pipe;
	u8 pin;

	/* Skip init on machines we know falsely report LVDS */
	if (dmi_check_system(intel_no_lvds))
		return false;

	pin = GMBUS_PORT_PANEL;
	if (!lvds_is_present_in_vbt(dev, &pin)) {
		DRM_DEBUG_KMS("LVDS is not present in VBT\n");
		return false;
	}

	if (HAS_PCH_SPLIT(dev)) {
		if ((I915_READ(PCH_LVDS) & LVDS_DETECTED) == 0)
			return false;
		if (dev_priv->edp.support) {
			DRM_DEBUG_KMS("disable LVDS for eDP support\n");
			return false;
		}
	}

	intel_lvds = kzalloc(sizeof(struct intel_lvds), GFP_KERNEL);
	if (!intel_lvds) {
		return false;
	}

	intel_connector = kzalloc(sizeof(struct intel_connector), GFP_KERNEL);
	if (!intel_connector) {
		kfree(intel_lvds);
		return false;
	}

	if (!HAS_PCH_SPLIT(dev)) {
		intel_lvds->pfit_control = I915_READ(PFIT_CONTROL);
	}

	intel_encoder = &intel_lvds->base;
	encoder = &intel_encoder->base;
	connector = &intel_connector->base;
	drm_connector_init(dev, &intel_connector->base, &intel_lvds_connector_funcs,
			   DRM_MODE_CONNECTOR_LVDS);

	drm_encoder_init(dev, &intel_encoder->base, &intel_lvds_enc_funcs,
			 DRM_MODE_ENCODER_LVDS);

	intel_connector_attach_encoder(intel_connector, intel_encoder);
	intel_encoder->type = INTEL_OUTPUT_LVDS;

	intel_encoder->clone_mask = (1 << INTEL_LVDS_CLONE_BIT);
	intel_encoder->crtc_mask = (1 << 1);
	if (INTEL_INFO(dev)->gen >= 5)
		intel_encoder->crtc_mask |= (1 << 0);
	drm_encoder_helper_add(encoder, &intel_lvds_helper_funcs);
	drm_connector_helper_add(connector, &intel_lvds_connector_helper_funcs);
	connector->display_info.subpixel_order = SubPixelHorizontalRGB;
	connector->interlace_allowed = false;
	connector->doublescan_allowed = false;

	/* create the scaling mode property */
	drm_mode_create_scaling_mode_property(dev);
	/*
	 * the initial panel fitting mode will be FULL_SCREEN.
	 */

	drm_connector_attach_property(&intel_connector->base,
				      dev->mode_config.scaling_mode_property,
				      DRM_MODE_SCALE_ASPECT);
	intel_lvds->fitting_mode = DRM_MODE_SCALE_ASPECT;
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
	intel_lvds->edid = drm_get_edid(connector,
					&dev_priv->gmbus[pin].adapter);
	if (intel_lvds->edid) {
		if (drm_add_edid_modes(connector,
				       intel_lvds->edid)) {
			drm_mode_connector_update_edid_property(connector,
								intel_lvds->edid);
		} else {
			kfree(intel_lvds->edid);
			intel_lvds->edid = NULL;
		}
	}
	if (!intel_lvds->edid) {
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
			intel_lvds->fixed_mode =
				drm_mode_duplicate(dev, scan);
			intel_find_lvds_downclock(dev,
						  intel_lvds->fixed_mode,
						  connector);
			goto out;
		}
	}

	/* Failed to get EDID, what about VBT? */
	if (dev_priv->lfp_lvds_vbt_mode) {
		intel_lvds->fixed_mode =
			drm_mode_duplicate(dev, dev_priv->lfp_lvds_vbt_mode);
		if (intel_lvds->fixed_mode) {
			intel_lvds->fixed_mode->type |=
				DRM_MODE_TYPE_PREFERRED;
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
		intel_lvds->fixed_mode = intel_crtc_mode_get(dev, crtc);
		if (intel_lvds->fixed_mode) {
			intel_lvds->fixed_mode->type |=
				DRM_MODE_TYPE_PREFERRED;
			goto out;
		}
	}

	/* If we still don't have a mode after all that, give up. */
	if (!intel_lvds->fixed_mode)
		goto failed;

out:
	if (HAS_PCH_SPLIT(dev)) {
		u32 pwm;

		pipe = (I915_READ(PCH_LVDS) & LVDS_PIPEB_SELECT) ? 1 : 0;

		/* make sure PWM is enabled and locked to the LVDS pipe */
		pwm = I915_READ(BLC_PWM_CPU_CTL2);
		if (pipe == 0 && (pwm & PWM_PIPE_B))
			I915_WRITE(BLC_PWM_CPU_CTL2, pwm & ~PWM_ENABLE);
		if (pipe)
			pwm |= PWM_PIPE_B;
		else
			pwm &= ~PWM_PIPE_B;
		I915_WRITE(BLC_PWM_CPU_CTL2, pwm | PWM_ENABLE);

		pwm = I915_READ(BLC_PWM_PCH_CTL1);
		pwm |= PWM_PCH_ENABLE;
		I915_WRITE(BLC_PWM_PCH_CTL1, pwm);
	}
	dev_priv->lid_notifier.notifier_call = intel_lid_notify;
	if (acpi_lid_notifier_register(&dev_priv->lid_notifier)) {
		DRM_DEBUG_KMS("lid notifier registration failed\n");
		dev_priv->lid_notifier.notifier_call = NULL;
	}
	/* keep the LVDS connector */
	dev_priv->int_lvds_connector = connector;
	drm_sysfs_connector_add(connector);
	return true;

failed:
	DRM_DEBUG_KMS("No LVDS modes found, disabling.\n");
	drm_connector_cleanup(connector);
	drm_encoder_cleanup(encoder);
	kfree(intel_lvds);
	kfree(intel_connector);
	return false;
}
