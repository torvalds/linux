/*
 * Copyright 2016 Linaro Ltd.
 * Copyright 2016 ZTE Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef __ZX_VOU_H__
#define __ZX_VOU_H__

#define VOU_CRTC_MASK		0x3

/* VOU output interfaces */
enum vou_inf_id {
	VOU_HDMI	= 0,
	VOU_RGB_LCD	= 1,
	VOU_TV_ENC	= 2,
	VOU_MIPI_DSI	= 3,
	VOU_LVDS	= 4,
	VOU_VGA		= 5,
};

enum vou_inf_data_sel {
	VOU_YUV444	= 0,
	VOU_RGB_101010	= 1,
	VOU_RGB_888	= 2,
	VOU_RGB_666	= 3,
};

struct vou_inf {
	enum vou_inf_id id;
	enum vou_inf_data_sel data_sel;
	u32 clocks_en_bits;
	u32 clocks_sel_bits;
};

void vou_inf_enable(const struct vou_inf *inf, struct drm_crtc *crtc);
void vou_inf_disable(const struct vou_inf *inf, struct drm_crtc *crtc);

int zx_vou_enable_vblank(struct drm_device *drm, unsigned int pipe);
void zx_vou_disable_vblank(struct drm_device *drm, unsigned int pipe);

#endif /* __ZX_VOU_H__ */
