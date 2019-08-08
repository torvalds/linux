/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright © 2011 Intel Corporation
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
extern void cdv_update_wm(struct drm_device *dev, struct drm_crtc *crtc);
extern void cdv_disable_sr(struct drm_device *dev);
