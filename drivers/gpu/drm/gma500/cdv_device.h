/*
 * Copyright © 2011 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */

extern const struct drm_crtc_helper_funcs cdv_intel_helper_funcs;
extern const struct drm_crtc_funcs cdv_intel_crtc_funcs;
extern const struct gma_clock_funcs cdv_clock_funcs;
extern void cdv_intel_crt_init(struct drm_device *dev,
			struct psb_intel_mode_device *mode_dev);
extern void cdv_intel_lvds_init(struct drm_device *dev,
			struct psb_intel_mode_device *mode_dev);
extern void cdv_hdmi_init(struct drm_device *dev, struct psb_intel_mode_device *mode_dev,
			int reg);
extern struct drm_display_mode *cdv_intel_crtc_mode_get(struct drm_device *dev,
					     struct drm_crtc *crtc);

static inline void cdv_intel_wait_for_vblank(struct drm_device *dev)
{
	/* Wait for 20ms, i.e. one cycle at 50hz. */
        /* FIXME: msleep ?? */
	mdelay(20);
}


