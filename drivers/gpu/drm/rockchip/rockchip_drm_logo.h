/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Copyright (c) 2021 Rockchip Electronics Co., Ltd.
 * Author: Sandy Huang <hjc@rock-chips.com>
 */

#ifndef ROCKCHIP_DRM_LOGO_H
#define ROCKCHIP_DRM_LOGO_H

struct rockchip_drm_mode_set {
	struct list_head head;
	struct drm_framebuffer *fb;
	struct rockchip_drm_sub_dev *sub_dev;
	struct drm_crtc *crtc;
	struct drm_display_mode *mode;
	int clock;
	int hdisplay;
	int vdisplay;
	int vrefresh;
	int flags;
	int picture_aspect_ratio;
	int crtc_hsync_end;
	int crtc_vsync_end;

	int left_margin;
	int right_margin;
	int top_margin;
	int bottom_margin;

	unsigned int brightness;
	unsigned int contrast;
	unsigned int saturation;
	unsigned int hue;

	bool mode_changed;
	bool force_output;
	int ratio;
};

void rockchip_drm_show_logo(struct drm_device *drm_dev);
void rockchip_free_loader_memory(struct drm_device *drm);

#endif
