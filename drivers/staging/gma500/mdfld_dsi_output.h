/*
 * Copyright © 2010 Intel Corporation
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
 * jim liu <jim.liu@intel.com>
 * Jackie Li<yaodong.li@intel.com>
 */

#ifndef __MDFLD_DSI_OUTPUT_H__
#define __MDFLD_DSI_OUTPUT_H__

#include <linux/backlight.h>
#include <drm/drmP.h>
#include <drm/drm.h>
#include <drm/drm_crtc.h>
#include <drm/drm_edid.h>

#include "psb_drv.h"
#include "psb_intel_drv.h"
#include "psb_intel_reg.h"
#include "power.h"
#include "mdfld_output.h"

#include <asm/mrst.h>


static inline struct mdfld_dsi_config *
	mdfld_dsi_get_config(struct mdfld_dsi_connector *connector)
{
	if (!connector)
		return NULL;
	return (struct mdfld_dsi_config *)connector->private;
}

static inline void *mdfld_dsi_get_pkg_sender(struct mdfld_dsi_config *config)
{
	struct mdfld_dsi_connector *dsi_connector;

	if (!config)
		return NULL;

	dsi_connector = config->connector;

	if (!dsi_connector)
		return NULL;

	return dsi_connector->pkg_sender;
}

static inline struct mdfld_dsi_config *
	mdfld_dsi_encoder_get_config(struct mdfld_dsi_encoder *encoder)
{
	if (!encoder)
		return NULL;
	return (struct mdfld_dsi_config *)encoder->private;
}

static inline struct mdfld_dsi_connector *
	mdfld_dsi_encoder_get_connector(struct mdfld_dsi_encoder *encoder)
{
	struct mdfld_dsi_config *config;

	if (!encoder)
		return NULL;

	config = mdfld_dsi_encoder_get_config(encoder);
	if (!config)
		return NULL;

	return config->connector;
}

static inline void *mdfld_dsi_encoder_get_pkg_sender(
	struct mdfld_dsi_encoder *encoder)
{
	struct mdfld_dsi_config *dsi_config;

	dsi_config = mdfld_dsi_encoder_get_config(encoder);
	if (!dsi_config)
		return NULL;

	return mdfld_dsi_get_pkg_sender(dsi_config);
}

static inline int mdfld_dsi_encoder_get_pipe(struct mdfld_dsi_encoder *encoder)
{
	struct mdfld_dsi_connector *connector;

	if (!encoder)
		return -1;

	connector = mdfld_dsi_encoder_get_connector(encoder);
	if (!connector)
		return -1;

	return connector->pipe;
}

extern void mdfld_dsi_gen_fifo_ready(struct drm_device *dev,
				u32 gen_fifo_stat_reg, u32 fifo_stat);
extern void mdfld_dsi_brightness_init(struct mdfld_dsi_config *dsi_config,
				int pipe);
extern void mdfld_dsi_brightness_control(struct drm_device *dev, int pipe,
				int level);
extern void mdfld_dsi_output_init(struct drm_device *dev, int pipe,
				struct mdfld_dsi_config *config,
				struct panel_funcs *p_cmd_funcs,
				struct panel_funcs *p_vid_funcs);
extern void mdfld_dsi_controller_init(struct mdfld_dsi_config *dsi_config,
				int pipe);
extern int mdfld_dsi_get_power_mode(struct mdfld_dsi_config *dsi_config,
				u32 *mode,
				u8 transmission);
extern int mdfld_dsi_get_diagnostic_result(struct mdfld_dsi_config *dsi_config,
				u32 *result,
				u8 transmission);
extern int mdfld_dsi_panel_reset(int pipe);

#endif /*__MDFLD_DSI_OUTPUT_H__*/
