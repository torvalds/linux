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

/* Panel types */
enum {
	TPO_CMD,
	TPO_VID,
	TMD_CMD,
	TMD_VID,
	PYR_CMD,
	PYR_VID,
	TPO,
	TMD,
	PYR,
	HDMI,
	GCT_DETECT
};

/* Junk that belongs elsewhere */
#define TPO_PANEL_WIDTH		84
#define TPO_PANEL_HEIGHT	46
#define TMD_PANEL_WIDTH		39
#define TMD_PANEL_HEIGHT	71
#define PYR_PANEL_WIDTH		53
#define PYR_PANEL_HEIGHT	95

/* Panel interface */
struct panel_info {
	u32 width_mm;
	u32 height_mm;
};

struct mdfld_dsi_dbi_output;

struct panel_funcs {
	const struct drm_encoder_funcs *encoder_funcs;
	const struct drm_encoder_helper_funcs *encoder_helper_funcs;
	struct drm_display_mode *(*get_config_mode) (struct drm_device *);
	void (*update_fb) (struct mdfld_dsi_dbi_output *, int);
	int (*get_panel_info) (struct drm_device *, int, struct panel_info *);
};

void mdfld_output_init(struct drm_device *dev);
int mdfld_panel_dpi(struct drm_device *dev);
int mdfld_get_panel_type(struct drm_device *dev, int pipe);
void mdfld_disable_crtc (struct drm_device *dev, int pipe);

#endif
