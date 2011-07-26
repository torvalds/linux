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
 * Jim Liu <jim.liu@intel.com>
 * Jackie Li<yaodong.li@intel.com>
 * Gideon Eaton <eaton.
 * Scott Rowe <scott.m.rowe@intel.com>
 */

#include "mdfld_dsi_dbi.h"
#include "mdfld_dsi_dpi.h"
#include "mdfld_dsi_output.h"
#include "mdfld_output.h"

#include "mdfld_dsi_pkg_sender.h"

#include "displays/tmd_vid.h"

/* FIXME: static ? */
struct drm_display_mode *tmd_vid_get_config_mode(struct drm_device *dev)
{
	struct drm_display_mode *mode;
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct mrst_timing_info *ti = &dev_priv->gct_data.DTD;
	bool use_gct = false; /*Disable GCT for now*/

	mode = kzalloc(sizeof(*mode), GFP_KERNEL);
	if (!mode) {
		dev_err(dev->dev, "Out of memory\n");
		return NULL;
	}

	if (use_gct) {
		dev_dbg(dev->dev, "gct find MIPI panel.\n");

		mode->hdisplay = (ti->hactive_hi << 8) | ti->hactive_lo;
		mode->vdisplay = (ti->vactive_hi << 8) | ti->vactive_lo;
		mode->hsync_start = mode->hdisplay +
				((ti->hsync_offset_hi << 8) |
				ti->hsync_offset_lo);
		mode->hsync_end = mode->hsync_start +
				((ti->hsync_pulse_width_hi << 8) |
				ti->hsync_pulse_width_lo);
		mode->htotal = mode->hdisplay + ((ti->hblank_hi << 8) |
								ti->hblank_lo);
		mode->vsync_start = \
			mode->vdisplay + ((ti->vsync_offset_hi << 8) |
						ti->vsync_offset_lo);
		mode->vsync_end = \
			mode->vsync_start + ((ti->vsync_pulse_width_hi << 8) | \
						ti->vsync_pulse_width_lo);
		mode->vtotal = mode->vdisplay +
				((ti->vblank_hi << 8) | ti->vblank_lo);
		mode->clock = ti->pixel_clock * 10;

		dev_dbg(dev->dev, "hdisplay is %d\n", mode->hdisplay);
		dev_dbg(dev->dev, "vdisplay is %d\n", mode->vdisplay);
		dev_dbg(dev->dev, "HSS is %d\n", mode->hsync_start);
		dev_dbg(dev->dev, "HSE is %d\n", mode->hsync_end);
		dev_dbg(dev->dev, "htotal is %d\n", mode->htotal);
		dev_dbg(dev->dev, "VSS is %d\n", mode->vsync_start);
		dev_dbg(dev->dev, "VSE is %d\n", mode->vsync_end);
		dev_dbg(dev->dev, "vtotal is %d\n", mode->vtotal);
		dev_dbg(dev->dev, "clock is %d\n", mode->clock);
	} else {
		mode->hdisplay = 480;
		mode->vdisplay = 854;
		mode->hsync_start = 487;
		mode->hsync_end = 490;
		mode->htotal = 499;
		mode->vsync_start = 861;
		mode->vsync_end = 865;
		mode->vtotal = 873;
		mode->clock = 33264;
	}
	drm_mode_set_name(mode);
	drm_mode_set_crtcinfo(mode, 0);

	mode->type |= DRM_MODE_TYPE_PREFERRED;

	return mode;
}

static int tmd_vid_get_panel_info(struct drm_device *dev,
				int pipe,
				struct panel_info *pi)
{
	if (!dev || !pi)
		return -EINVAL;

	pi->width_mm = TMD_PANEL_WIDTH;
	pi->height_mm = TMD_PANEL_HEIGHT;

	return 0;
}

/*
 *	mdfld_init_TMD_MIPI	-	initialise a TMD interface
 *	@dsi_config: configuration
 *	@pipe: pipe to configure
 *
 *	This function is called only by mrst_dsi_mode_set and
 *	restore_display_registers.  since this function does not
 *	acquire the mutex, it is important that the calling function
 *	does!
 */


static void mdfld_dsi_tmd_drv_ic_init(struct mdfld_dsi_config *dsi_config,
				      int pipe)
{
	static u32 tmd_cmd_mcap_off[] = {0x000000b2};
	static u32 tmd_cmd_enable_lane_switch[] = {0x000101ef};
	static u32 tmd_cmd_set_lane_num[] = {0x006360ef};
	static u32 tmd_cmd_pushing_clock0[] = {0x00cc2fef};
	static u32 tmd_cmd_pushing_clock1[] = {0x00dd6eef};
	static u32 tmd_cmd_set_mode[] = {0x000000b3};
	static u32 tmd_cmd_set_sync_pulse_mode[] = {0x000961ef};
	static u32 tmd_cmd_set_column[] = {0x0100002a, 0x000000df};
	static u32 tmd_cmd_set_page[] = {0x0300002b, 0x00000055};
	static u32 tmd_cmd_set_video_mode[] = {0x00000153};
	/*no auto_bl,need add in furture*/
	static u32 tmd_cmd_enable_backlight[] = {0x00005ab4};
	static u32 tmd_cmd_set_backlight_dimming[] = {0x00000ebd};

	struct mdfld_dsi_pkg_sender *sender
			= mdfld_dsi_get_pkg_sender(dsi_config);

	DRM_INFO("Enter mdfld init TMD MIPI display.\n");

	if (!sender) {
		DRM_ERROR("Cannot get sender\n");
		return;
	}

	if (dsi_config->dvr_ic_inited)
		return;

	msleep(3);

	mdfld_dsi_send_gen_long_lp(sender, tmd_cmd_mcap_off, 1, 0);
	mdfld_dsi_send_gen_long_lp(sender, tmd_cmd_enable_lane_switch, 1, 0);
	mdfld_dsi_send_gen_long_lp(sender, tmd_cmd_set_lane_num, 1, 0);
	mdfld_dsi_send_gen_long_lp(sender, tmd_cmd_pushing_clock0, 1, 0);
	mdfld_dsi_send_gen_long_lp(sender, tmd_cmd_pushing_clock1, 1, 0);
	mdfld_dsi_send_gen_long_lp(sender, tmd_cmd_set_mode, 1, 0);
	mdfld_dsi_send_gen_long_lp(sender, tmd_cmd_set_sync_pulse_mode, 1, 0);
	mdfld_dsi_send_mcs_long_lp(sender, tmd_cmd_set_column, 2, 0);
	mdfld_dsi_send_mcs_long_lp(sender, tmd_cmd_set_page, 2, 0);
	mdfld_dsi_send_gen_long_lp(sender, tmd_cmd_set_video_mode, 1, 0);
	mdfld_dsi_send_gen_long_lp(sender, tmd_cmd_enable_backlight, 1, 0);
	mdfld_dsi_send_gen_long_lp(sender, tmd_cmd_set_backlight_dimming, 1, 0);

	dsi_config->dvr_ic_inited = 1;
}

/* TMD DPI encoder helper funcs */
static const struct drm_encoder_helper_funcs
					mdfld_tpo_dpi_encoder_helper_funcs = {
	.dpms = mdfld_dsi_dpi_dpms,
	.mode_fixup = mdfld_dsi_dpi_mode_fixup,
	.prepare = mdfld_dsi_dpi_prepare,
	.mode_set = mdfld_dsi_dpi_mode_set,
	.commit = mdfld_dsi_dpi_commit,
};

/* TMD DPI encoder funcs */
static const struct drm_encoder_funcs mdfld_tpo_dpi_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

void tmd_vid_init(struct drm_device *dev, struct panel_funcs *p_funcs)
{
	if (!dev || !p_funcs) {
		dev_err(dev->dev, "Invalid parameters\n");
		return;
	}

	p_funcs->encoder_funcs = &mdfld_tpo_dpi_encoder_funcs;
	p_funcs->encoder_helper_funcs = &mdfld_tpo_dpi_encoder_helper_funcs;
	p_funcs->get_config_mode = &tmd_vid_get_config_mode;
	p_funcs->update_fb = NULL;
	p_funcs->get_panel_info = tmd_vid_get_panel_info;
	p_funcs->reset = mdfld_dsi_panel_reset;
	p_funcs->drv_ic_init = mdfld_dsi_tmd_drv_ic_init;
}
