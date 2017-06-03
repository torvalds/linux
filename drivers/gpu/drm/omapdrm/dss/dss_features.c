/*
 * linux/drivers/video/omap2/dss/dss_features.c
 *
 * Copyright (C) 2010 Texas Instruments
 * Author: Archit Taneja <archit@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <drm/drm_fourcc.h>

#include "omapdss.h"
#include "dss.h"
#include "dss_features.h"

/* Defines a generic omap register field */
struct dss_reg_field {
	u8 start, end;
};

struct dss_param_range {
	int min, max;
};

struct omap_dss_features {
	const struct dss_reg_field *reg_fields;
	const int num_reg_fields;

	const enum dss_feat_id *features;
	const int num_features;

	const int num_mgrs;
	const int num_ovls;
	const enum omap_display_type *supported_displays;
	const enum omap_dss_output_id *supported_outputs;
	const u32 **supported_color_modes;
	const enum omap_overlay_caps *overlay_caps;
	const struct dss_param_range *dss_params;

	const u32 buffer_size_unit;
	const u32 burst_size_unit;
};

/* This struct is assigned to one of the below during initialization */
static const struct omap_dss_features *omap_current_dss_features;

static const struct dss_reg_field omap2_dss_reg_fields[] = {
	[FEAT_REG_FIRHINC]			= { 11, 0 },
	[FEAT_REG_FIRVINC]			= { 27, 16 },
	[FEAT_REG_FIFOLOWTHRESHOLD]		= { 8, 0 },
	[FEAT_REG_FIFOHIGHTHRESHOLD]		= { 24, 16 },
	[FEAT_REG_FIFOSIZE]			= { 8, 0 },
	[FEAT_REG_HORIZONTALACCU]		= { 9, 0 },
	[FEAT_REG_VERTICALACCU]			= { 25, 16 },
	[FEAT_REG_DISPC_CLK_SWITCH]		= { 0, 0 },
};

static const struct dss_reg_field omap3_dss_reg_fields[] = {
	[FEAT_REG_FIRHINC]			= { 12, 0 },
	[FEAT_REG_FIRVINC]			= { 28, 16 },
	[FEAT_REG_FIFOLOWTHRESHOLD]		= { 11, 0 },
	[FEAT_REG_FIFOHIGHTHRESHOLD]		= { 27, 16 },
	[FEAT_REG_FIFOSIZE]			= { 10, 0 },
	[FEAT_REG_HORIZONTALACCU]		= { 9, 0 },
	[FEAT_REG_VERTICALACCU]			= { 25, 16 },
	[FEAT_REG_DISPC_CLK_SWITCH]		= { 0, 0 },
};

static const struct dss_reg_field am43xx_dss_reg_fields[] = {
	[FEAT_REG_FIRHINC]			= { 12, 0 },
	[FEAT_REG_FIRVINC]			= { 28, 16 },
	[FEAT_REG_FIFOLOWTHRESHOLD]	= { 11, 0 },
	[FEAT_REG_FIFOHIGHTHRESHOLD]		= { 27, 16 },
	[FEAT_REG_FIFOSIZE]		= { 10, 0 },
	[FEAT_REG_HORIZONTALACCU]		= { 9, 0 },
	[FEAT_REG_VERTICALACCU]			= { 25, 16 },
	[FEAT_REG_DISPC_CLK_SWITCH]		= { 0, 0 },
};

static const struct dss_reg_field omap4_dss_reg_fields[] = {
	[FEAT_REG_FIRHINC]			= { 12, 0 },
	[FEAT_REG_FIRVINC]			= { 28, 16 },
	[FEAT_REG_FIFOLOWTHRESHOLD]		= { 15, 0 },
	[FEAT_REG_FIFOHIGHTHRESHOLD]		= { 31, 16 },
	[FEAT_REG_FIFOSIZE]			= { 15, 0 },
	[FEAT_REG_HORIZONTALACCU]		= { 10, 0 },
	[FEAT_REG_VERTICALACCU]			= { 26, 16 },
	[FEAT_REG_DISPC_CLK_SWITCH]		= { 9, 8 },
};

static const struct dss_reg_field omap5_dss_reg_fields[] = {
	[FEAT_REG_FIRHINC]			= { 12, 0 },
	[FEAT_REG_FIRVINC]			= { 28, 16 },
	[FEAT_REG_FIFOLOWTHRESHOLD]		= { 15, 0 },
	[FEAT_REG_FIFOHIGHTHRESHOLD]		= { 31, 16 },
	[FEAT_REG_FIFOSIZE]			= { 15, 0 },
	[FEAT_REG_HORIZONTALACCU]		= { 10, 0 },
	[FEAT_REG_VERTICALACCU]			= { 26, 16 },
	[FEAT_REG_DISPC_CLK_SWITCH]		= { 9, 7 },
};

static const enum omap_display_type omap2_dss_supported_displays[] = {
	/* OMAP_DSS_CHANNEL_LCD */
	OMAP_DISPLAY_TYPE_DPI | OMAP_DISPLAY_TYPE_DBI,

	/* OMAP_DSS_CHANNEL_DIGIT */
	OMAP_DISPLAY_TYPE_VENC,
};

static const enum omap_display_type omap3430_dss_supported_displays[] = {
	/* OMAP_DSS_CHANNEL_LCD */
	OMAP_DISPLAY_TYPE_DPI | OMAP_DISPLAY_TYPE_DBI |
	OMAP_DISPLAY_TYPE_SDI | OMAP_DISPLAY_TYPE_DSI,

	/* OMAP_DSS_CHANNEL_DIGIT */
	OMAP_DISPLAY_TYPE_VENC,
};

static const enum omap_display_type omap3630_dss_supported_displays[] = {
	/* OMAP_DSS_CHANNEL_LCD */
	OMAP_DISPLAY_TYPE_DPI | OMAP_DISPLAY_TYPE_DBI |
	OMAP_DISPLAY_TYPE_DSI,

	/* OMAP_DSS_CHANNEL_DIGIT */
	OMAP_DISPLAY_TYPE_VENC,
};

static const enum omap_display_type am43xx_dss_supported_displays[] = {
	/* OMAP_DSS_CHANNEL_LCD */
	OMAP_DISPLAY_TYPE_DPI | OMAP_DISPLAY_TYPE_DBI,
};

static const enum omap_display_type omap4_dss_supported_displays[] = {
	/* OMAP_DSS_CHANNEL_LCD */
	OMAP_DISPLAY_TYPE_DBI | OMAP_DISPLAY_TYPE_DSI,

	/* OMAP_DSS_CHANNEL_DIGIT */
	OMAP_DISPLAY_TYPE_VENC | OMAP_DISPLAY_TYPE_HDMI,

	/* OMAP_DSS_CHANNEL_LCD2 */
	OMAP_DISPLAY_TYPE_DPI | OMAP_DISPLAY_TYPE_DBI |
	OMAP_DISPLAY_TYPE_DSI,
};

static const enum omap_display_type omap5_dss_supported_displays[] = {
	/* OMAP_DSS_CHANNEL_LCD */
	OMAP_DISPLAY_TYPE_DPI | OMAP_DISPLAY_TYPE_DBI |
	OMAP_DISPLAY_TYPE_DSI,

	/* OMAP_DSS_CHANNEL_DIGIT */
	OMAP_DISPLAY_TYPE_HDMI | OMAP_DISPLAY_TYPE_DPI,

	/* OMAP_DSS_CHANNEL_LCD2 */
	OMAP_DISPLAY_TYPE_DPI | OMAP_DISPLAY_TYPE_DBI |
	OMAP_DISPLAY_TYPE_DSI,
};

static const enum omap_dss_output_id omap2_dss_supported_outputs[] = {
	/* OMAP_DSS_CHANNEL_LCD */
	OMAP_DSS_OUTPUT_DPI | OMAP_DSS_OUTPUT_DBI,

	/* OMAP_DSS_CHANNEL_DIGIT */
	OMAP_DSS_OUTPUT_VENC,
};

static const enum omap_dss_output_id omap3430_dss_supported_outputs[] = {
	/* OMAP_DSS_CHANNEL_LCD */
	OMAP_DSS_OUTPUT_DPI | OMAP_DSS_OUTPUT_DBI |
	OMAP_DSS_OUTPUT_SDI | OMAP_DSS_OUTPUT_DSI1,

	/* OMAP_DSS_CHANNEL_DIGIT */
	OMAP_DSS_OUTPUT_VENC,
};

static const enum omap_dss_output_id omap3630_dss_supported_outputs[] = {
	/* OMAP_DSS_CHANNEL_LCD */
	OMAP_DSS_OUTPUT_DPI | OMAP_DSS_OUTPUT_DBI |
	OMAP_DSS_OUTPUT_DSI1,

	/* OMAP_DSS_CHANNEL_DIGIT */
	OMAP_DSS_OUTPUT_VENC,
};

static const enum omap_dss_output_id am43xx_dss_supported_outputs[] = {
	/* OMAP_DSS_CHANNEL_LCD */
	OMAP_DSS_OUTPUT_DPI | OMAP_DSS_OUTPUT_DBI,
};

static const enum omap_dss_output_id omap4_dss_supported_outputs[] = {
	/* OMAP_DSS_CHANNEL_LCD */
	OMAP_DSS_OUTPUT_DBI | OMAP_DSS_OUTPUT_DSI1,

	/* OMAP_DSS_CHANNEL_DIGIT */
	OMAP_DSS_OUTPUT_VENC | OMAP_DSS_OUTPUT_HDMI,

	/* OMAP_DSS_CHANNEL_LCD2 */
	OMAP_DSS_OUTPUT_DPI | OMAP_DSS_OUTPUT_DBI |
	OMAP_DSS_OUTPUT_DSI2,
};

static const enum omap_dss_output_id omap5_dss_supported_outputs[] = {
	/* OMAP_DSS_CHANNEL_LCD */
	OMAP_DSS_OUTPUT_DPI | OMAP_DSS_OUTPUT_DBI |
	OMAP_DSS_OUTPUT_DSI1 | OMAP_DSS_OUTPUT_DSI2,

	/* OMAP_DSS_CHANNEL_DIGIT */
	OMAP_DSS_OUTPUT_HDMI,

	/* OMAP_DSS_CHANNEL_LCD2 */
	OMAP_DSS_OUTPUT_DPI | OMAP_DSS_OUTPUT_DBI |
	OMAP_DSS_OUTPUT_DSI1,

	/* OMAP_DSS_CHANNEL_LCD3 */
	OMAP_DSS_OUTPUT_DPI | OMAP_DSS_OUTPUT_DBI |
	OMAP_DSS_OUTPUT_DSI2,
};

#define COLOR_ARRAY(arr...) (const u32[]) { arr, 0 }

static const u32 *omap2_dss_supported_color_modes[] = {

	/* OMAP_DSS_GFX */
	COLOR_ARRAY(
	DRM_FORMAT_RGBX4444, DRM_FORMAT_RGB565,
	DRM_FORMAT_XRGB8888, DRM_FORMAT_RGB888),

	/* OMAP_DSS_VIDEO1 */
	COLOR_ARRAY(
	DRM_FORMAT_RGB565, DRM_FORMAT_XRGB8888,
	DRM_FORMAT_RGB888, DRM_FORMAT_YUYV,
	DRM_FORMAT_UYVY),

	/* OMAP_DSS_VIDEO2 */
	COLOR_ARRAY(
	DRM_FORMAT_RGB565, DRM_FORMAT_XRGB8888,
	DRM_FORMAT_RGB888, DRM_FORMAT_YUYV,
	DRM_FORMAT_UYVY),
};

static const u32 *omap3_dss_supported_color_modes[] = {
	/* OMAP_DSS_GFX */
	COLOR_ARRAY(
	DRM_FORMAT_RGBX4444, DRM_FORMAT_ARGB4444,
	DRM_FORMAT_RGB565, DRM_FORMAT_XRGB8888,
	DRM_FORMAT_RGB888, DRM_FORMAT_ARGB8888,
	DRM_FORMAT_RGBA8888, DRM_FORMAT_RGBX8888),

	/* OMAP_DSS_VIDEO1 */
	COLOR_ARRAY(
	DRM_FORMAT_XRGB8888, DRM_FORMAT_RGB888,
	DRM_FORMAT_RGBX4444, DRM_FORMAT_RGB565,
	DRM_FORMAT_YUYV, DRM_FORMAT_UYVY),

	/* OMAP_DSS_VIDEO2 */
	COLOR_ARRAY(
	DRM_FORMAT_RGBX4444, DRM_FORMAT_ARGB4444,
	DRM_FORMAT_RGB565, DRM_FORMAT_XRGB8888,
	DRM_FORMAT_RGB888, DRM_FORMAT_YUYV,
	DRM_FORMAT_UYVY, DRM_FORMAT_ARGB8888,
	DRM_FORMAT_RGBA8888, DRM_FORMAT_RGBX8888),
};

static const u32 *omap4_dss_supported_color_modes[] = {
	/* OMAP_DSS_GFX */
	COLOR_ARRAY(
	DRM_FORMAT_RGBX4444, DRM_FORMAT_ARGB4444,
	DRM_FORMAT_RGB565, DRM_FORMAT_XRGB8888,
	DRM_FORMAT_RGB888, DRM_FORMAT_ARGB8888,
	DRM_FORMAT_RGBA8888, DRM_FORMAT_RGBX8888,
	DRM_FORMAT_ARGB1555, DRM_FORMAT_XRGB4444,
	DRM_FORMAT_RGBA4444, DRM_FORMAT_XRGB1555),

	/* OMAP_DSS_VIDEO1 */
	COLOR_ARRAY(
	DRM_FORMAT_RGB565, DRM_FORMAT_RGBX4444,
	DRM_FORMAT_YUYV, DRM_FORMAT_ARGB1555,
	DRM_FORMAT_RGBA8888, DRM_FORMAT_NV12,
	DRM_FORMAT_RGBA4444, DRM_FORMAT_XRGB8888,
	DRM_FORMAT_RGB888, DRM_FORMAT_UYVY,
	DRM_FORMAT_ARGB4444, DRM_FORMAT_XRGB1555,
	DRM_FORMAT_ARGB8888, DRM_FORMAT_XRGB4444,
	DRM_FORMAT_RGBX8888),

       /* OMAP_DSS_VIDEO2 */
	COLOR_ARRAY(
	DRM_FORMAT_RGB565, DRM_FORMAT_RGBX4444,
	DRM_FORMAT_YUYV, DRM_FORMAT_ARGB1555,
	DRM_FORMAT_RGBA8888, DRM_FORMAT_NV12,
	DRM_FORMAT_RGBA4444, DRM_FORMAT_XRGB8888,
	DRM_FORMAT_RGB888, DRM_FORMAT_UYVY,
	DRM_FORMAT_ARGB4444, DRM_FORMAT_XRGB1555,
	DRM_FORMAT_ARGB8888, DRM_FORMAT_XRGB4444,
	DRM_FORMAT_RGBX8888),

	/* OMAP_DSS_VIDEO3 */
	COLOR_ARRAY(
	DRM_FORMAT_RGB565, DRM_FORMAT_RGBX4444,
	DRM_FORMAT_YUYV, DRM_FORMAT_ARGB1555,
	DRM_FORMAT_RGBA8888, DRM_FORMAT_NV12,
	DRM_FORMAT_RGBA4444, DRM_FORMAT_XRGB8888,
	DRM_FORMAT_RGB888, DRM_FORMAT_UYVY,
	DRM_FORMAT_ARGB4444, DRM_FORMAT_XRGB1555,
	DRM_FORMAT_ARGB8888, DRM_FORMAT_XRGB4444,
	DRM_FORMAT_RGBX8888),

	/* OMAP_DSS_WB */
	COLOR_ARRAY(
	DRM_FORMAT_RGB565, DRM_FORMAT_RGBX4444,
	DRM_FORMAT_YUYV, DRM_FORMAT_ARGB1555,
	DRM_FORMAT_RGBA8888, DRM_FORMAT_NV12,
	DRM_FORMAT_RGBA4444, DRM_FORMAT_XRGB8888,
	DRM_FORMAT_RGB888, DRM_FORMAT_UYVY,
	DRM_FORMAT_ARGB4444, DRM_FORMAT_XRGB1555,
	DRM_FORMAT_ARGB8888, DRM_FORMAT_XRGB4444,
	DRM_FORMAT_RGBX8888),
};

static const enum omap_overlay_caps omap2_dss_overlay_caps[] = {
	/* OMAP_DSS_GFX */
	OMAP_DSS_OVL_CAP_POS | OMAP_DSS_OVL_CAP_REPLICATION,

	/* OMAP_DSS_VIDEO1 */
	OMAP_DSS_OVL_CAP_SCALE | OMAP_DSS_OVL_CAP_POS |
		OMAP_DSS_OVL_CAP_REPLICATION,

	/* OMAP_DSS_VIDEO2 */
	OMAP_DSS_OVL_CAP_SCALE | OMAP_DSS_OVL_CAP_POS |
		OMAP_DSS_OVL_CAP_REPLICATION,
};

static const enum omap_overlay_caps omap3430_dss_overlay_caps[] = {
	/* OMAP_DSS_GFX */
	OMAP_DSS_OVL_CAP_GLOBAL_ALPHA | OMAP_DSS_OVL_CAP_POS |
		OMAP_DSS_OVL_CAP_REPLICATION,

	/* OMAP_DSS_VIDEO1 */
	OMAP_DSS_OVL_CAP_SCALE | OMAP_DSS_OVL_CAP_POS |
		OMAP_DSS_OVL_CAP_REPLICATION,

	/* OMAP_DSS_VIDEO2 */
	OMAP_DSS_OVL_CAP_SCALE | OMAP_DSS_OVL_CAP_GLOBAL_ALPHA |
		OMAP_DSS_OVL_CAP_POS | OMAP_DSS_OVL_CAP_REPLICATION,
};

static const enum omap_overlay_caps omap3630_dss_overlay_caps[] = {
	/* OMAP_DSS_GFX */
	OMAP_DSS_OVL_CAP_GLOBAL_ALPHA | OMAP_DSS_OVL_CAP_PRE_MULT_ALPHA |
		OMAP_DSS_OVL_CAP_POS | OMAP_DSS_OVL_CAP_REPLICATION,

	/* OMAP_DSS_VIDEO1 */
	OMAP_DSS_OVL_CAP_SCALE | OMAP_DSS_OVL_CAP_POS |
		OMAP_DSS_OVL_CAP_REPLICATION,

	/* OMAP_DSS_VIDEO2 */
	OMAP_DSS_OVL_CAP_SCALE | OMAP_DSS_OVL_CAP_GLOBAL_ALPHA |
		OMAP_DSS_OVL_CAP_PRE_MULT_ALPHA | OMAP_DSS_OVL_CAP_POS |
		OMAP_DSS_OVL_CAP_REPLICATION,
};

static const enum omap_overlay_caps omap4_dss_overlay_caps[] = {
	/* OMAP_DSS_GFX */
	OMAP_DSS_OVL_CAP_GLOBAL_ALPHA | OMAP_DSS_OVL_CAP_PRE_MULT_ALPHA |
		OMAP_DSS_OVL_CAP_ZORDER | OMAP_DSS_OVL_CAP_POS |
		OMAP_DSS_OVL_CAP_REPLICATION,

	/* OMAP_DSS_VIDEO1 */
	OMAP_DSS_OVL_CAP_SCALE | OMAP_DSS_OVL_CAP_GLOBAL_ALPHA |
		OMAP_DSS_OVL_CAP_PRE_MULT_ALPHA | OMAP_DSS_OVL_CAP_ZORDER |
		OMAP_DSS_OVL_CAP_POS | OMAP_DSS_OVL_CAP_REPLICATION,

	/* OMAP_DSS_VIDEO2 */
	OMAP_DSS_OVL_CAP_SCALE | OMAP_DSS_OVL_CAP_GLOBAL_ALPHA |
		OMAP_DSS_OVL_CAP_PRE_MULT_ALPHA | OMAP_DSS_OVL_CAP_ZORDER |
		OMAP_DSS_OVL_CAP_POS | OMAP_DSS_OVL_CAP_REPLICATION,

	/* OMAP_DSS_VIDEO3 */
	OMAP_DSS_OVL_CAP_SCALE | OMAP_DSS_OVL_CAP_GLOBAL_ALPHA |
		OMAP_DSS_OVL_CAP_PRE_MULT_ALPHA | OMAP_DSS_OVL_CAP_ZORDER |
		OMAP_DSS_OVL_CAP_POS | OMAP_DSS_OVL_CAP_REPLICATION,
};

static const struct dss_param_range omap2_dss_param_range[] = {
	[FEAT_PARAM_DSS_FCK]			= { 0, 133000000 },
	[FEAT_PARAM_DSS_PCD]			= { 2, 255 },
	[FEAT_PARAM_DOWNSCALE]			= { 1, 2 },
	/*
	 * Assuming the line width buffer to be 768 pixels as OMAP2 DISPC
	 * scaler cannot scale a image with width more than 768.
	 */
	[FEAT_PARAM_LINEWIDTH]			= { 1, 768 },
};

static const struct dss_param_range omap3_dss_param_range[] = {
	[FEAT_PARAM_DSS_FCK]			= { 0, 173000000 },
	[FEAT_PARAM_DSS_PCD]			= { 1, 255 },
	[FEAT_PARAM_DSIPLL_LPDIV]		= { 1, (1 << 13) - 1},
	[FEAT_PARAM_DSI_FCK]			= { 0, 173000000 },
	[FEAT_PARAM_DOWNSCALE]			= { 1, 4 },
	[FEAT_PARAM_LINEWIDTH]			= { 1, 1024 },
};

static const struct dss_param_range am43xx_dss_param_range[] = {
	[FEAT_PARAM_DSS_FCK]			= { 0, 200000000 },
	[FEAT_PARAM_DSS_PCD]			= { 1, 255 },
	[FEAT_PARAM_DOWNSCALE]			= { 1, 4 },
	[FEAT_PARAM_LINEWIDTH]			= { 1, 1024 },
};

static const struct dss_param_range omap4_dss_param_range[] = {
	[FEAT_PARAM_DSS_FCK]			= { 0, 186000000 },
	[FEAT_PARAM_DSS_PCD]			= { 1, 255 },
	[FEAT_PARAM_DSIPLL_LPDIV]		= { 0, (1 << 13) - 1 },
	[FEAT_PARAM_DSI_FCK]			= { 0, 170000000 },
	[FEAT_PARAM_DOWNSCALE]			= { 1, 4 },
	[FEAT_PARAM_LINEWIDTH]			= { 1, 2048 },
};

static const struct dss_param_range omap5_dss_param_range[] = {
	[FEAT_PARAM_DSS_FCK]			= { 0, 209250000 },
	[FEAT_PARAM_DSS_PCD]			= { 1, 255 },
	[FEAT_PARAM_DSIPLL_LPDIV]		= { 0, (1 << 13) - 1 },
	[FEAT_PARAM_DSI_FCK]			= { 0, 209250000 },
	[FEAT_PARAM_DOWNSCALE]			= { 1, 4 },
	[FEAT_PARAM_LINEWIDTH]			= { 1, 2048 },
};

static const enum dss_feat_id omap2_dss_feat_list[] = {
	FEAT_LCDENABLEPOL,
	FEAT_LCDENABLESIGNAL,
	FEAT_PCKFREEENABLE,
	FEAT_FUNCGATED,
	FEAT_ROWREPEATENABLE,
	FEAT_RESIZECONF,
};

static const enum dss_feat_id omap3430_dss_feat_list[] = {
	FEAT_LCDENABLEPOL,
	FEAT_LCDENABLESIGNAL,
	FEAT_PCKFREEENABLE,
	FEAT_FUNCGATED,
	FEAT_LINEBUFFERSPLIT,
	FEAT_ROWREPEATENABLE,
	FEAT_RESIZECONF,
	FEAT_DSI_REVERSE_TXCLKESC,
	FEAT_VENC_REQUIRES_TV_DAC_CLK,
	FEAT_CPR,
	FEAT_PRELOAD,
	FEAT_FIR_COEF_V,
	FEAT_ALPHA_FIXED_ZORDER,
	FEAT_FIFO_MERGE,
	FEAT_OMAP3_DSI_FIFO_BUG,
	FEAT_DPI_USES_VDDS_DSI,
};

static const enum dss_feat_id am35xx_dss_feat_list[] = {
	FEAT_LCDENABLEPOL,
	FEAT_LCDENABLESIGNAL,
	FEAT_PCKFREEENABLE,
	FEAT_FUNCGATED,
	FEAT_LINEBUFFERSPLIT,
	FEAT_ROWREPEATENABLE,
	FEAT_RESIZECONF,
	FEAT_DSI_REVERSE_TXCLKESC,
	FEAT_VENC_REQUIRES_TV_DAC_CLK,
	FEAT_CPR,
	FEAT_PRELOAD,
	FEAT_FIR_COEF_V,
	FEAT_ALPHA_FIXED_ZORDER,
	FEAT_FIFO_MERGE,
	FEAT_OMAP3_DSI_FIFO_BUG,
};

static const enum dss_feat_id am43xx_dss_feat_list[] = {
	FEAT_LCDENABLEPOL,
	FEAT_LCDENABLESIGNAL,
	FEAT_PCKFREEENABLE,
	FEAT_FUNCGATED,
	FEAT_LINEBUFFERSPLIT,
	FEAT_ROWREPEATENABLE,
	FEAT_RESIZECONF,
	FEAT_CPR,
	FEAT_PRELOAD,
	FEAT_FIR_COEF_V,
	FEAT_ALPHA_FIXED_ZORDER,
	FEAT_FIFO_MERGE,
};

static const enum dss_feat_id omap3630_dss_feat_list[] = {
	FEAT_LCDENABLEPOL,
	FEAT_LCDENABLESIGNAL,
	FEAT_PCKFREEENABLE,
	FEAT_FUNCGATED,
	FEAT_LINEBUFFERSPLIT,
	FEAT_ROWREPEATENABLE,
	FEAT_RESIZECONF,
	FEAT_DSI_PLL_PWR_BUG,
	FEAT_CPR,
	FEAT_PRELOAD,
	FEAT_FIR_COEF_V,
	FEAT_ALPHA_FIXED_ZORDER,
	FEAT_FIFO_MERGE,
	FEAT_OMAP3_DSI_FIFO_BUG,
	FEAT_DPI_USES_VDDS_DSI,
};

static const enum dss_feat_id omap4430_es1_0_dss_feat_list[] = {
	FEAT_MGR_LCD2,
	FEAT_CORE_CLK_DIV,
	FEAT_LCD_CLK_SRC,
	FEAT_DSI_DCS_CMD_CONFIG_VC,
	FEAT_DSI_VC_OCP_WIDTH,
	FEAT_DSI_GNQ,
	FEAT_HANDLE_UV_SEPARATE,
	FEAT_ATTR2,
	FEAT_CPR,
	FEAT_PRELOAD,
	FEAT_FIR_COEF_V,
	FEAT_ALPHA_FREE_ZORDER,
	FEAT_FIFO_MERGE,
	FEAT_BURST_2D,
};

static const enum dss_feat_id omap4430_es2_0_1_2_dss_feat_list[] = {
	FEAT_MGR_LCD2,
	FEAT_CORE_CLK_DIV,
	FEAT_LCD_CLK_SRC,
	FEAT_DSI_DCS_CMD_CONFIG_VC,
	FEAT_DSI_VC_OCP_WIDTH,
	FEAT_DSI_GNQ,
	FEAT_HDMI_CTS_SWMODE,
	FEAT_HANDLE_UV_SEPARATE,
	FEAT_ATTR2,
	FEAT_CPR,
	FEAT_PRELOAD,
	FEAT_FIR_COEF_V,
	FEAT_ALPHA_FREE_ZORDER,
	FEAT_FIFO_MERGE,
	FEAT_BURST_2D,
};

static const enum dss_feat_id omap4_dss_feat_list[] = {
	FEAT_MGR_LCD2,
	FEAT_CORE_CLK_DIV,
	FEAT_LCD_CLK_SRC,
	FEAT_DSI_DCS_CMD_CONFIG_VC,
	FEAT_DSI_VC_OCP_WIDTH,
	FEAT_DSI_GNQ,
	FEAT_HDMI_CTS_SWMODE,
	FEAT_HDMI_AUDIO_USE_MCLK,
	FEAT_HANDLE_UV_SEPARATE,
	FEAT_ATTR2,
	FEAT_CPR,
	FEAT_PRELOAD,
	FEAT_FIR_COEF_V,
	FEAT_ALPHA_FREE_ZORDER,
	FEAT_FIFO_MERGE,
	FEAT_BURST_2D,
};

static const enum dss_feat_id omap5_dss_feat_list[] = {
	FEAT_MGR_LCD2,
	FEAT_MGR_LCD3,
	FEAT_CORE_CLK_DIV,
	FEAT_LCD_CLK_SRC,
	FEAT_DSI_DCS_CMD_CONFIG_VC,
	FEAT_DSI_VC_OCP_WIDTH,
	FEAT_DSI_GNQ,
	FEAT_HDMI_CTS_SWMODE,
	FEAT_HDMI_AUDIO_USE_MCLK,
	FEAT_HANDLE_UV_SEPARATE,
	FEAT_ATTR2,
	FEAT_CPR,
	FEAT_PRELOAD,
	FEAT_FIR_COEF_V,
	FEAT_ALPHA_FREE_ZORDER,
	FEAT_FIFO_MERGE,
	FEAT_BURST_2D,
	FEAT_DSI_PHY_DCC,
	FEAT_MFLAG,
};

/* OMAP2 DSS Features */
static const struct omap_dss_features omap2_dss_features = {
	.reg_fields = omap2_dss_reg_fields,
	.num_reg_fields = ARRAY_SIZE(omap2_dss_reg_fields),

	.features = omap2_dss_feat_list,
	.num_features = ARRAY_SIZE(omap2_dss_feat_list),

	.num_mgrs = 2,
	.num_ovls = 3,
	.supported_displays = omap2_dss_supported_displays,
	.supported_outputs = omap2_dss_supported_outputs,
	.supported_color_modes = omap2_dss_supported_color_modes,
	.overlay_caps = omap2_dss_overlay_caps,
	.dss_params = omap2_dss_param_range,
	.buffer_size_unit = 1,
	.burst_size_unit = 8,
};

/* OMAP3 DSS Features */
static const struct omap_dss_features omap3430_dss_features = {
	.reg_fields = omap3_dss_reg_fields,
	.num_reg_fields = ARRAY_SIZE(omap3_dss_reg_fields),

	.features = omap3430_dss_feat_list,
	.num_features = ARRAY_SIZE(omap3430_dss_feat_list),

	.num_mgrs = 2,
	.num_ovls = 3,
	.supported_displays = omap3430_dss_supported_displays,
	.supported_outputs = omap3430_dss_supported_outputs,
	.supported_color_modes = omap3_dss_supported_color_modes,
	.overlay_caps = omap3430_dss_overlay_caps,
	.dss_params = omap3_dss_param_range,
	.buffer_size_unit = 1,
	.burst_size_unit = 8,
};

/*
 * AM35xx DSS Features. This is basically OMAP3 DSS Features without the
 * vdds_dsi regulator.
 */
static const struct omap_dss_features am35xx_dss_features = {
	.reg_fields = omap3_dss_reg_fields,
	.num_reg_fields = ARRAY_SIZE(omap3_dss_reg_fields),

	.features = am35xx_dss_feat_list,
	.num_features = ARRAY_SIZE(am35xx_dss_feat_list),

	.num_mgrs = 2,
	.num_ovls = 3,
	.supported_displays = omap3430_dss_supported_displays,
	.supported_outputs = omap3430_dss_supported_outputs,
	.supported_color_modes = omap3_dss_supported_color_modes,
	.overlay_caps = omap3430_dss_overlay_caps,
	.dss_params = omap3_dss_param_range,
	.buffer_size_unit = 1,
	.burst_size_unit = 8,
};

static const struct omap_dss_features am43xx_dss_features = {
	.reg_fields = am43xx_dss_reg_fields,
	.num_reg_fields = ARRAY_SIZE(am43xx_dss_reg_fields),

	.features = am43xx_dss_feat_list,
	.num_features = ARRAY_SIZE(am43xx_dss_feat_list),

	.num_mgrs = 1,
	.num_ovls = 3,
	.supported_displays = am43xx_dss_supported_displays,
	.supported_outputs = am43xx_dss_supported_outputs,
	.supported_color_modes = omap3_dss_supported_color_modes,
	.overlay_caps = omap3430_dss_overlay_caps,
	.dss_params = am43xx_dss_param_range,
	.buffer_size_unit = 1,
	.burst_size_unit = 8,
};

static const struct omap_dss_features omap3630_dss_features = {
	.reg_fields = omap3_dss_reg_fields,
	.num_reg_fields = ARRAY_SIZE(omap3_dss_reg_fields),

	.features = omap3630_dss_feat_list,
	.num_features = ARRAY_SIZE(omap3630_dss_feat_list),

	.num_mgrs = 2,
	.num_ovls = 3,
	.supported_displays = omap3630_dss_supported_displays,
	.supported_outputs = omap3630_dss_supported_outputs,
	.supported_color_modes = omap3_dss_supported_color_modes,
	.overlay_caps = omap3630_dss_overlay_caps,
	.dss_params = omap3_dss_param_range,
	.buffer_size_unit = 1,
	.burst_size_unit = 8,
};

/* OMAP4 DSS Features */
/* For OMAP4430 ES 1.0 revision */
static const struct omap_dss_features omap4430_es1_0_dss_features  = {
	.reg_fields = omap4_dss_reg_fields,
	.num_reg_fields = ARRAY_SIZE(omap4_dss_reg_fields),

	.features = omap4430_es1_0_dss_feat_list,
	.num_features = ARRAY_SIZE(omap4430_es1_0_dss_feat_list),

	.num_mgrs = 3,
	.num_ovls = 4,
	.supported_displays = omap4_dss_supported_displays,
	.supported_outputs = omap4_dss_supported_outputs,
	.supported_color_modes = omap4_dss_supported_color_modes,
	.overlay_caps = omap4_dss_overlay_caps,
	.dss_params = omap4_dss_param_range,
	.buffer_size_unit = 16,
	.burst_size_unit = 16,
};

/* For OMAP4430 ES 2.0, 2.1 and 2.2 revisions */
static const struct omap_dss_features omap4430_es2_0_1_2_dss_features = {
	.reg_fields = omap4_dss_reg_fields,
	.num_reg_fields = ARRAY_SIZE(omap4_dss_reg_fields),

	.features = omap4430_es2_0_1_2_dss_feat_list,
	.num_features = ARRAY_SIZE(omap4430_es2_0_1_2_dss_feat_list),

	.num_mgrs = 3,
	.num_ovls = 4,
	.supported_displays = omap4_dss_supported_displays,
	.supported_outputs = omap4_dss_supported_outputs,
	.supported_color_modes = omap4_dss_supported_color_modes,
	.overlay_caps = omap4_dss_overlay_caps,
	.dss_params = omap4_dss_param_range,
	.buffer_size_unit = 16,
	.burst_size_unit = 16,
};

/* For all the other OMAP4 versions */
static const struct omap_dss_features omap4_dss_features = {
	.reg_fields = omap4_dss_reg_fields,
	.num_reg_fields = ARRAY_SIZE(omap4_dss_reg_fields),

	.features = omap4_dss_feat_list,
	.num_features = ARRAY_SIZE(omap4_dss_feat_list),

	.num_mgrs = 3,
	.num_ovls = 4,
	.supported_displays = omap4_dss_supported_displays,
	.supported_outputs = omap4_dss_supported_outputs,
	.supported_color_modes = omap4_dss_supported_color_modes,
	.overlay_caps = omap4_dss_overlay_caps,
	.dss_params = omap4_dss_param_range,
	.buffer_size_unit = 16,
	.burst_size_unit = 16,
};

/* OMAP5 DSS Features */
static const struct omap_dss_features omap5_dss_features = {
	.reg_fields = omap5_dss_reg_fields,
	.num_reg_fields = ARRAY_SIZE(omap5_dss_reg_fields),

	.features = omap5_dss_feat_list,
	.num_features = ARRAY_SIZE(omap5_dss_feat_list),

	.num_mgrs = 4,
	.num_ovls = 4,
	.supported_displays = omap5_dss_supported_displays,
	.supported_outputs = omap5_dss_supported_outputs,
	.supported_color_modes = omap4_dss_supported_color_modes,
	.overlay_caps = omap4_dss_overlay_caps,
	.dss_params = omap5_dss_param_range,
	.buffer_size_unit = 16,
	.burst_size_unit = 16,
};

/* Functions returning values related to a DSS feature */
int dss_feat_get_num_mgrs(void)
{
	return omap_current_dss_features->num_mgrs;
}

int dss_feat_get_num_ovls(void)
{
	return omap_current_dss_features->num_ovls;
}

unsigned long dss_feat_get_param_min(enum dss_range_param param)
{
	return omap_current_dss_features->dss_params[param].min;
}

unsigned long dss_feat_get_param_max(enum dss_range_param param)
{
	return omap_current_dss_features->dss_params[param].max;
}

enum omap_display_type dss_feat_get_supported_displays(enum omap_channel channel)
{
	return omap_current_dss_features->supported_displays[channel];
}

enum omap_dss_output_id dss_feat_get_supported_outputs(enum omap_channel channel)
{
	return omap_current_dss_features->supported_outputs[channel];
}

const u32 *dss_feat_get_supported_color_modes(enum omap_plane_id plane)
{
	return omap_current_dss_features->supported_color_modes[plane];
}

enum omap_overlay_caps dss_feat_get_overlay_caps(enum omap_plane_id plane)
{
	return omap_current_dss_features->overlay_caps[plane];
}

bool dss_feat_color_mode_supported(enum omap_plane_id plane, u32 fourcc)
{
	const u32 *modes;
	unsigned int i;

	modes = omap_current_dss_features->supported_color_modes[plane];

	for (i = 0; modes[i]; ++i) {
		if (modes[i] == fourcc)
			return true;
	}

	return false;
}

u32 dss_feat_get_buffer_size_unit(void)
{
	return omap_current_dss_features->buffer_size_unit;
}

u32 dss_feat_get_burst_size_unit(void)
{
	return omap_current_dss_features->burst_size_unit;
}

/* DSS has_feature check */
bool dss_has_feature(enum dss_feat_id id)
{
	int i;
	const enum dss_feat_id *features = omap_current_dss_features->features;
	const int num_features = omap_current_dss_features->num_features;

	for (i = 0; i < num_features; i++) {
		if (features[i] == id)
			return true;
	}

	return false;
}

void dss_feat_get_reg_field(enum dss_feat_reg_field id, u8 *start, u8 *end)
{
	if (id >= omap_current_dss_features->num_reg_fields)
		BUG();

	*start = omap_current_dss_features->reg_fields[id].start;
	*end = omap_current_dss_features->reg_fields[id].end;
}

void dss_features_init(enum omapdss_version version)
{
	switch (version) {
	case OMAPDSS_VER_OMAP24xx:
		omap_current_dss_features = &omap2_dss_features;
		break;

	case OMAPDSS_VER_OMAP34xx_ES1:
	case OMAPDSS_VER_OMAP34xx_ES3:
		omap_current_dss_features = &omap3430_dss_features;
		break;

	case OMAPDSS_VER_OMAP3630:
		omap_current_dss_features = &omap3630_dss_features;
		break;

	case OMAPDSS_VER_OMAP4430_ES1:
		omap_current_dss_features = &omap4430_es1_0_dss_features;
		break;

	case OMAPDSS_VER_OMAP4430_ES2:
		omap_current_dss_features = &omap4430_es2_0_1_2_dss_features;
		break;

	case OMAPDSS_VER_OMAP4:
		omap_current_dss_features = &omap4_dss_features;
		break;

	case OMAPDSS_VER_OMAP5:
	case OMAPDSS_VER_DRA7xx:
		omap_current_dss_features = &omap5_dss_features;
		break;

	case OMAPDSS_VER_AM35xx:
		omap_current_dss_features = &am35xx_dss_features;
		break;

	case OMAPDSS_VER_AM43xx:
		omap_current_dss_features = &am43xx_dss_features;
		break;

	default:
		DSSWARN("Unsupported OMAP version");
		break;
	}
}
