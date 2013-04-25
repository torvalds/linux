/*
 * Copyright © 2006-2010 Intel Corporation
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
 *      Chris Wilson <chris@chris-wilson.co.uk>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/moduleparam.h>
#include "intel_drv.h"

#define PCI_LBPC 0xf4 /* legacy/combination backlight modes */

void
intel_fixed_panel_mode(struct drm_display_mode *fixed_mode,
		       struct drm_display_mode *adjusted_mode)
{
	adjusted_mode->hdisplay = fixed_mode->hdisplay;
	adjusted_mode->hsync_start = fixed_mode->hsync_start;
	adjusted_mode->hsync_end = fixed_mode->hsync_end;
	adjusted_mode->htotal = fixed_mode->htotal;

	adjusted_mode->vdisplay = fixed_mode->vdisplay;
	adjusted_mode->vsync_start = fixed_mode->vsync_start;
	adjusted_mode->vsync_end = fixed_mode->vsync_end;
	adjusted_mode->vtotal = fixed_mode->vtotal;

	adjusted_mode->clock = fixed_mode->clock;
}

/* adjusted_mode has been preset to be the panel's fixed mode */
void
intel_pch_panel_fitting(struct intel_crtc *intel_crtc,
			struct intel_crtc_config *pipe_config,
			int fitting_mode)
{
	struct drm_i915_private *dev_priv = intel_crtc->base.dev->dev_private;
	struct drm_display_mode *mode, *adjusted_mode;
	int x, y, width, height;

	mode = &pipe_config->requested_mode;
	adjusted_mode = &pipe_config->adjusted_mode;

	x = y = width = height = 0;

	/* Native modes don't need fitting */
	if (adjusted_mode->hdisplay == mode->hdisplay &&
	    adjusted_mode->vdisplay == mode->vdisplay)
		goto done;

	switch (fitting_mode) {
	case DRM_MODE_SCALE_CENTER:
		width = mode->hdisplay;
		height = mode->vdisplay;
		x = (adjusted_mode->hdisplay - width + 1)/2;
		y = (adjusted_mode->vdisplay - height + 1)/2;
		break;

	case DRM_MODE_SCALE_ASPECT:
		/* Scale but preserve the aspect ratio */
		{
			u32 scaled_width = adjusted_mode->hdisplay * mode->vdisplay;
			u32 scaled_height = mode->hdisplay * adjusted_mode->vdisplay;
			if (scaled_width > scaled_height) { /* pillar */
				width = scaled_height / mode->vdisplay;
				if (width & 1)
					width++;
				x = (adjusted_mode->hdisplay - width + 1) / 2;
				y = 0;
				height = adjusted_mode->vdisplay;
			} else if (scaled_width < scaled_height) { /* letter */
				height = scaled_width / mode->hdisplay;
				if (height & 1)
				    height++;
				y = (adjusted_mode->vdisplay - height + 1) / 2;
				x = 0;
				width = adjusted_mode->hdisplay;
			} else {
				x = y = 0;
				width = adjusted_mode->hdisplay;
				height = adjusted_mode->vdisplay;
			}
		}
		break;

	default:
	case DRM_MODE_SCALE_FULLSCREEN:
		x = y = 0;
		width = adjusted_mode->hdisplay;
		height = adjusted_mode->vdisplay;
		break;
	}

done:
	pipe_config->pch_pfit.pos = (x << 16) | y;
	pipe_config->pch_pfit.size = (width << 16) | height;
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

void intel_gmch_panel_fitting(struct intel_crtc *intel_crtc,
			      struct intel_crtc_config *pipe_config,
			      int fitting_mode)
{
	struct drm_device *dev = intel_crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 pfit_control = 0, pfit_pgm_ratios = 0, border = 0;
	struct drm_display_mode *mode, *adjusted_mode;

	mode = &pipe_config->requested_mode;
	adjusted_mode = &pipe_config->adjusted_mode;

	/* Native modes don't need fitting */
	if (adjusted_mode->hdisplay == mode->hdisplay &&
	    adjusted_mode->vdisplay == mode->vdisplay)
		goto out;

	switch (fitting_mode) {
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
			u32 scaled_width = adjusted_mode->hdisplay *
				mode->vdisplay;
			u32 scaled_height = mode->hdisplay *
				adjusted_mode->vdisplay;

			/* 965+ is easy, it does everything in hw */
			if (scaled_width > scaled_height)
				pfit_control |= PFIT_ENABLE |
					PFIT_SCALING_PILLAR;
			else if (scaled_width < scaled_height)
				pfit_control |= PFIT_ENABLE |
					PFIT_SCALING_LETTER;
			else if (adjusted_mode->hdisplay != mode->hdisplay)
				pfit_control |= PFIT_ENABLE | PFIT_SCALING_AUTO;
		} else {
			u32 scaled_width = adjusted_mode->hdisplay *
				mode->vdisplay;
			u32 scaled_height = mode->hdisplay *
				adjusted_mode->vdisplay;
			/*
			 * For earlier chips we have to calculate the scaling
			 * ratio by hand and program it into the
			 * PFIT_PGM_RATIO register
			 */
			if (scaled_width > scaled_height) { /* pillar */
				centre_horizontally(adjusted_mode,
						    scaled_height /
						    mode->vdisplay);

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
				centre_vertically(adjusted_mode,
						  scaled_width /
						  mode->hdisplay);

				border = LVDS_BORDER_ENABLE;
				if (mode->hdisplay != adjusted_mode->hdisplay) {
					u32 bits = panel_fitter_scaling(mode->hdisplay, adjusted_mode->hdisplay);
					pfit_pgm_ratios |= (bits << PFIT_HORIZ_SCALE_SHIFT |
							    bits << PFIT_VERT_SCALE_SHIFT);
					pfit_control |= (PFIT_ENABLE |
							 VERT_INTERP_BILINEAR |
							 HORIZ_INTERP_BILINEAR);
				}
			} else {
				/* Aspects match, Let hw scale both directions */
				pfit_control |= (PFIT_ENABLE |
						 VERT_AUTO_SCALE | HORIZ_AUTO_SCALE |
						 VERT_INTERP_BILINEAR |
						 HORIZ_INTERP_BILINEAR);
			}
		}
		break;
	default:
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
	}

	/* 965+ wants fuzzy fitting */
	/* FIXME: handle multiple panels by failing gracefully */
	if (INTEL_INFO(dev)->gen >= 4)
		pfit_control |= ((intel_crtc->pipe << PFIT_PIPE_SHIFT) |
				 PFIT_FILTER_FUZZY);

out:
	if ((pfit_control & PFIT_ENABLE) == 0) {
		pfit_control = 0;
		pfit_pgm_ratios = 0;
	}

	/* Make sure pre-965 set dither correctly for 18bpp panels. */
	if (INTEL_INFO(dev)->gen < 4 && pipe_config->pipe_bpp == 18)
		pfit_control |= PANEL_8TO6_DITHER_ENABLE;

	if (pfit_control != pipe_config->gmch_pfit.control ||
	    pfit_pgm_ratios != pipe_config->gmch_pfit.pgm_ratios) {
		pipe_config->gmch_pfit.control = pfit_control;
		pipe_config->gmch_pfit.pgm_ratios = pfit_pgm_ratios;
	}
	dev_priv->lvds_border_bits = border;
}

static int is_backlight_combination_mode(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (INTEL_INFO(dev)->gen >= 4)
		return I915_READ(BLC_PWM_CTL2) & BLM_COMBINATION_MODE;

	if (IS_GEN2(dev))
		return I915_READ(BLC_PWM_CTL) & BLM_LEGACY_MODE;

	return 0;
}

/* XXX: query mode clock or hardware clock and program max PWM appropriately
 * when it's 0.
 */
static u32 i915_read_blc_pwm_ctl(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 val;

	WARN_ON(!spin_is_locked(&dev_priv->backlight.lock));

	/* Restore the CTL value if it lost, e.g. GPU reset */

	if (HAS_PCH_SPLIT(dev_priv->dev)) {
		val = I915_READ(BLC_PWM_PCH_CTL2);
		if (dev_priv->regfile.saveBLC_PWM_CTL2 == 0) {
			dev_priv->regfile.saveBLC_PWM_CTL2 = val;
		} else if (val == 0) {
			val = dev_priv->regfile.saveBLC_PWM_CTL2;
			I915_WRITE(BLC_PWM_PCH_CTL2, val);
		}
	} else {
		val = I915_READ(BLC_PWM_CTL);
		if (dev_priv->regfile.saveBLC_PWM_CTL == 0) {
			dev_priv->regfile.saveBLC_PWM_CTL = val;
			if (INTEL_INFO(dev)->gen >= 4)
				dev_priv->regfile.saveBLC_PWM_CTL2 =
					I915_READ(BLC_PWM_CTL2);
		} else if (val == 0) {
			val = dev_priv->regfile.saveBLC_PWM_CTL;
			I915_WRITE(BLC_PWM_CTL, val);
			if (INTEL_INFO(dev)->gen >= 4)
				I915_WRITE(BLC_PWM_CTL2,
					   dev_priv->regfile.saveBLC_PWM_CTL2);
		}
	}

	return val;
}

static u32 intel_panel_get_max_backlight(struct drm_device *dev)
{
	u32 max;

	max = i915_read_blc_pwm_ctl(dev);

	if (HAS_PCH_SPLIT(dev)) {
		max >>= 16;
	} else {
		if (INTEL_INFO(dev)->gen < 4)
			max >>= 17;
		else
			max >>= 16;

		if (is_backlight_combination_mode(dev))
			max *= 0xff;
	}

	DRM_DEBUG_DRIVER("max backlight PWM = %d\n", max);

	return max;
}

static int i915_panel_invert_brightness;
MODULE_PARM_DESC(invert_brightness, "Invert backlight brightness "
	"(-1 force normal, 0 machine defaults, 1 force inversion), please "
	"report PCI device ID, subsystem vendor and subsystem device ID "
	"to dri-devel@lists.freedesktop.org, if your machine needs it. "
	"It will then be included in an upcoming module version.");
module_param_named(invert_brightness, i915_panel_invert_brightness, int, 0600);
static u32 intel_panel_compute_brightness(struct drm_device *dev, u32 val)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (i915_panel_invert_brightness < 0)
		return val;

	if (i915_panel_invert_brightness > 0 ||
	    dev_priv->quirks & QUIRK_INVERT_BRIGHTNESS) {
		u32 max = intel_panel_get_max_backlight(dev);
		if (max)
			return max - val;
	}

	return val;
}

static u32 intel_panel_get_backlight(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 val;
	unsigned long flags;

	spin_lock_irqsave(&dev_priv->backlight.lock, flags);

	if (HAS_PCH_SPLIT(dev)) {
		val = I915_READ(BLC_PWM_CPU_CTL) & BACKLIGHT_DUTY_CYCLE_MASK;
	} else {
		val = I915_READ(BLC_PWM_CTL) & BACKLIGHT_DUTY_CYCLE_MASK;
		if (INTEL_INFO(dev)->gen < 4)
			val >>= 1;

		if (is_backlight_combination_mode(dev)) {
			u8 lbpc;

			pci_read_config_byte(dev->pdev, PCI_LBPC, &lbpc);
			val *= lbpc;
		}
	}

	val = intel_panel_compute_brightness(dev, val);

	spin_unlock_irqrestore(&dev_priv->backlight.lock, flags);

	DRM_DEBUG_DRIVER("get backlight PWM = %d\n", val);
	return val;
}

static void intel_pch_panel_set_backlight(struct drm_device *dev, u32 level)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 val = I915_READ(BLC_PWM_CPU_CTL) & ~BACKLIGHT_DUTY_CYCLE_MASK;
	I915_WRITE(BLC_PWM_CPU_CTL, val | level);
}

static void intel_panel_actually_set_backlight(struct drm_device *dev, u32 level)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 tmp;

	DRM_DEBUG_DRIVER("set backlight PWM = %d\n", level);
	level = intel_panel_compute_brightness(dev, level);

	if (HAS_PCH_SPLIT(dev))
		return intel_pch_panel_set_backlight(dev, level);

	if (is_backlight_combination_mode(dev)) {
		u32 max = intel_panel_get_max_backlight(dev);
		u8 lbpc;

		/* we're screwed, but keep behaviour backwards compatible */
		if (!max)
			max = 1;

		lbpc = level * 0xfe / max + 1;
		level /= lbpc;
		pci_write_config_byte(dev->pdev, PCI_LBPC, lbpc);
	}

	tmp = I915_READ(BLC_PWM_CTL);
	if (INTEL_INFO(dev)->gen < 4)
		level <<= 1;
	tmp &= ~BACKLIGHT_DUTY_CYCLE_MASK;
	I915_WRITE(BLC_PWM_CTL, tmp | level);
}

/* set backlight brightness to level in range [0..max] */
void intel_panel_set_backlight(struct drm_device *dev, u32 level, u32 max)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 freq;
	unsigned long flags;

	spin_lock_irqsave(&dev_priv->backlight.lock, flags);

	freq = intel_panel_get_max_backlight(dev);
	if (!freq) {
		/* we are screwed, bail out */
		goto out;
	}

	/* scale to hardware */
	level = level * freq / max;

	dev_priv->backlight.level = level;
	if (dev_priv->backlight.device)
		dev_priv->backlight.device->props.brightness = level;

	if (dev_priv->backlight.enabled)
		intel_panel_actually_set_backlight(dev, level);
out:
	spin_unlock_irqrestore(&dev_priv->backlight.lock, flags);
}

void intel_panel_disable_backlight(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	unsigned long flags;

	spin_lock_irqsave(&dev_priv->backlight.lock, flags);

	dev_priv->backlight.enabled = false;
	intel_panel_actually_set_backlight(dev, 0);

	if (INTEL_INFO(dev)->gen >= 4) {
		uint32_t reg, tmp;

		reg = HAS_PCH_SPLIT(dev) ? BLC_PWM_CPU_CTL2 : BLC_PWM_CTL2;

		I915_WRITE(reg, I915_READ(reg) & ~BLM_PWM_ENABLE);

		if (HAS_PCH_SPLIT(dev)) {
			tmp = I915_READ(BLC_PWM_PCH_CTL1);
			tmp &= ~BLM_PCH_PWM_ENABLE;
			I915_WRITE(BLC_PWM_PCH_CTL1, tmp);
		}
	}

	spin_unlock_irqrestore(&dev_priv->backlight.lock, flags);
}

void intel_panel_enable_backlight(struct drm_device *dev,
				  enum pipe pipe)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	enum transcoder cpu_transcoder =
		intel_pipe_to_cpu_transcoder(dev_priv, pipe);
	unsigned long flags;

	spin_lock_irqsave(&dev_priv->backlight.lock, flags);

	if (dev_priv->backlight.level == 0) {
		dev_priv->backlight.level = intel_panel_get_max_backlight(dev);
		if (dev_priv->backlight.device)
			dev_priv->backlight.device->props.brightness =
				dev_priv->backlight.level;
	}

	if (INTEL_INFO(dev)->gen >= 4) {
		uint32_t reg, tmp;

		reg = HAS_PCH_SPLIT(dev) ? BLC_PWM_CPU_CTL2 : BLC_PWM_CTL2;


		tmp = I915_READ(reg);

		/* Note that this can also get called through dpms changes. And
		 * we don't track the backlight dpms state, hence check whether
		 * we have to do anything first. */
		if (tmp & BLM_PWM_ENABLE)
			goto set_level;

		if (INTEL_INFO(dev)->num_pipes == 3)
			tmp &= ~BLM_PIPE_SELECT_IVB;
		else
			tmp &= ~BLM_PIPE_SELECT;

		if (cpu_transcoder == TRANSCODER_EDP)
			tmp |= BLM_TRANSCODER_EDP;
		else
			tmp |= BLM_PIPE(cpu_transcoder);
		tmp &= ~BLM_PWM_ENABLE;

		I915_WRITE(reg, tmp);
		POSTING_READ(reg);
		I915_WRITE(reg, tmp | BLM_PWM_ENABLE);

		if (HAS_PCH_SPLIT(dev) &&
		    !(dev_priv->quirks & QUIRK_NO_PCH_PWM_ENABLE)) {
			tmp = I915_READ(BLC_PWM_PCH_CTL1);
			tmp |= BLM_PCH_PWM_ENABLE;
			tmp &= ~BLM_PCH_OVERRIDE_ENABLE;
			I915_WRITE(BLC_PWM_PCH_CTL1, tmp);
		}
	}

set_level:
	/* Call below after setting BLC_PWM_CPU_CTL2 and BLC_PWM_PCH_CTL1.
	 * BLC_PWM_CPU_CTL may be cleared to zero automatically when these
	 * registers are set.
	 */
	dev_priv->backlight.enabled = true;
	intel_panel_actually_set_backlight(dev, dev_priv->backlight.level);

	spin_unlock_irqrestore(&dev_priv->backlight.lock, flags);
}

static void intel_panel_init_backlight(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	dev_priv->backlight.level = intel_panel_get_backlight(dev);
	dev_priv->backlight.enabled = dev_priv->backlight.level != 0;
}

enum drm_connector_status
intel_panel_detect(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	/* Assume that the BIOS does not lie through the OpRegion... */
	if (!i915_panel_ignore_lid && dev_priv->opregion.lid_state) {
		return ioread32(dev_priv->opregion.lid_state) & 0x1 ?
			connector_status_connected :
			connector_status_disconnected;
	}

	switch (i915_panel_ignore_lid) {
	case -2:
		return connector_status_connected;
	case -1:
		return connector_status_disconnected;
	default:
		return connector_status_unknown;
	}
}

#ifdef CONFIG_BACKLIGHT_CLASS_DEVICE
static int intel_panel_update_status(struct backlight_device *bd)
{
	struct drm_device *dev = bl_get_data(bd);
	intel_panel_set_backlight(dev, bd->props.brightness,
				  bd->props.max_brightness);
	return 0;
}

static int intel_panel_get_brightness(struct backlight_device *bd)
{
	struct drm_device *dev = bl_get_data(bd);
	return intel_panel_get_backlight(dev);
}

static const struct backlight_ops intel_panel_bl_ops = {
	.update_status = intel_panel_update_status,
	.get_brightness = intel_panel_get_brightness,
};

int intel_panel_setup_backlight(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct backlight_properties props;
	unsigned long flags;

	intel_panel_init_backlight(dev);

	if (WARN_ON(dev_priv->backlight.device))
		return -ENODEV;

	memset(&props, 0, sizeof(props));
	props.type = BACKLIGHT_RAW;
	props.brightness = dev_priv->backlight.level;

	spin_lock_irqsave(&dev_priv->backlight.lock, flags);
	props.max_brightness = intel_panel_get_max_backlight(dev);
	spin_unlock_irqrestore(&dev_priv->backlight.lock, flags);

	if (props.max_brightness == 0) {
		DRM_DEBUG_DRIVER("Failed to get maximum backlight value\n");
		return -ENODEV;
	}
	dev_priv->backlight.device =
		backlight_device_register("intel_backlight",
					  &connector->kdev, dev,
					  &intel_panel_bl_ops, &props);

	if (IS_ERR(dev_priv->backlight.device)) {
		DRM_ERROR("Failed to register backlight: %ld\n",
			  PTR_ERR(dev_priv->backlight.device));
		dev_priv->backlight.device = NULL;
		return -ENODEV;
	}
	return 0;
}

void intel_panel_destroy_backlight(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	if (dev_priv->backlight.device) {
		backlight_device_unregister(dev_priv->backlight.device);
		dev_priv->backlight.device = NULL;
	}
}
#else
int intel_panel_setup_backlight(struct drm_connector *connector)
{
	intel_panel_init_backlight(connector->dev);
	return 0;
}

void intel_panel_destroy_backlight(struct drm_device *dev)
{
	return;
}
#endif

int intel_panel_init(struct intel_panel *panel,
		     struct drm_display_mode *fixed_mode)
{
	panel->fixed_mode = fixed_mode;

	return 0;
}

void intel_panel_fini(struct intel_panel *panel)
{
	struct intel_connector *intel_connector =
		container_of(panel, struct intel_connector, panel);

	if (panel->fixed_mode)
		drm_mode_destroy(intel_connector->base.dev, panel->fixed_mode);
}
