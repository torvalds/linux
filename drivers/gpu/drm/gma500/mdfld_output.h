/*
 * Copyright (c)  2010 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicensen
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
 * Thomas Eaton <thomas.g.eaton@intel.com>
 * Scott Rowe <scott.m.rowe@intel.com>
*/

#ifndef MDFLD_OUTPUT_H
#define MDFLD_OUTPUT_H

#include "psb_drv.h"

#define TPO_PANEL_WIDTH		84
#define TPO_PANEL_HEIGHT	46
#define TMD_PANEL_WIDTH		39
#define TMD_PANEL_HEIGHT	71

struct mdfld_dsi_config;

enum panel_type {
	TPO_VID,
	TMD_VID,
	HDMI,
	TC35876X,
};

struct panel_info {
	u32 width_mm;
	u32 height_mm;
	/* Other info */
};

struct panel_funcs {
	const struct drm_encoder_funcs *encoder_funcs;
	const struct drm_encoder_helper_funcs *encoder_helper_funcs;
	struct drm_display_mode * (*get_config_mode)(struct drm_device *);
	int (*get_panel_info)(struct drm_device *, int, struct panel_info *);
	int (*reset)(int pipe);
	void (*drv_ic_init)(struct mdfld_dsi_config *dsi_config, int pipe);
};

int mdfld_output_init(struct drm_device *dev);

struct backlight_device *mdfld_get_backlight_device(void);
int mdfld_set_brightness(struct backlight_device *bd);

int mdfld_get_panel_type(struct drm_device *dev, int pipe);

extern const struct drm_crtc_helper_funcs mdfld_helper_funcs;

extern const struct panel_funcs mdfld_tmd_vid_funcs;
extern const struct panel_funcs mdfld_tpo_vid_funcs;

extern void mdfld_disable_crtc(struct drm_device *dev, int pipe);
extern void mdfldWaitForPipeEnable(struct drm_device *dev, int pipe);
extern void mdfldWaitForPipeDisable(struct drm_device *dev, int pipe);
#endif
