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

struct dss_param_range {
	int min, max;
};

struct omap_dss_features {
	const enum dss_feat_id *features;
	const int num_features;

	const enum omap_dss_output_id *supported_outputs;
	const struct dss_param_range *dss_params;
};

/* This struct is assigned to one of the below during initialization */
static const struct omap_dss_features *omap_current_dss_features;

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
	.features = omap2_dss_feat_list,
	.num_features = ARRAY_SIZE(omap2_dss_feat_list),

	.supported_outputs = omap2_dss_supported_outputs,
	.dss_params = omap2_dss_param_range,
};

/* OMAP3 DSS Features */
static const struct omap_dss_features omap3430_dss_features = {
	.features = omap3430_dss_feat_list,
	.num_features = ARRAY_SIZE(omap3430_dss_feat_list),

	.supported_outputs = omap3430_dss_supported_outputs,
	.dss_params = omap3_dss_param_range,
};

/*
 * AM35xx DSS Features. This is basically OMAP3 DSS Features without the
 * vdds_dsi regulator.
 */
static const struct omap_dss_features am35xx_dss_features = {
	.features = am35xx_dss_feat_list,
	.num_features = ARRAY_SIZE(am35xx_dss_feat_list),

	.supported_outputs = omap3430_dss_supported_outputs,
	.dss_params = omap3_dss_param_range,
};

static const struct omap_dss_features am43xx_dss_features = {
	.features = am43xx_dss_feat_list,
	.num_features = ARRAY_SIZE(am43xx_dss_feat_list),

	.supported_outputs = am43xx_dss_supported_outputs,
	.dss_params = am43xx_dss_param_range,
};

static const struct omap_dss_features omap3630_dss_features = {
	.features = omap3630_dss_feat_list,
	.num_features = ARRAY_SIZE(omap3630_dss_feat_list),

	.supported_outputs = omap3630_dss_supported_outputs,
	.dss_params = omap3_dss_param_range,
};

/* OMAP4 DSS Features */
/* For OMAP4430 ES 1.0 revision */
static const struct omap_dss_features omap4430_es1_0_dss_features  = {
	.features = omap4430_es1_0_dss_feat_list,
	.num_features = ARRAY_SIZE(omap4430_es1_0_dss_feat_list),

	.supported_outputs = omap4_dss_supported_outputs,
	.dss_params = omap4_dss_param_range,
};

/* For OMAP4430 ES 2.0, 2.1 and 2.2 revisions */
static const struct omap_dss_features omap4430_es2_0_1_2_dss_features = {
	.features = omap4430_es2_0_1_2_dss_feat_list,
	.num_features = ARRAY_SIZE(omap4430_es2_0_1_2_dss_feat_list),

	.supported_outputs = omap4_dss_supported_outputs,
	.dss_params = omap4_dss_param_range,
};

/* For all the other OMAP4 versions */
static const struct omap_dss_features omap4_dss_features = {
	.features = omap4_dss_feat_list,
	.num_features = ARRAY_SIZE(omap4_dss_feat_list),

	.supported_outputs = omap4_dss_supported_outputs,
	.dss_params = omap4_dss_param_range,
};

/* OMAP5 DSS Features */
static const struct omap_dss_features omap5_dss_features = {
	.features = omap5_dss_feat_list,
	.num_features = ARRAY_SIZE(omap5_dss_feat_list),

	.supported_outputs = omap5_dss_supported_outputs,
	.dss_params = omap5_dss_param_range,
};

/* Functions returning values related to a DSS feature */
unsigned long dss_feat_get_param_min(enum dss_range_param param)
{
	return omap_current_dss_features->dss_params[param].min;
}

unsigned long dss_feat_get_param_max(enum dss_range_param param)
{
	return omap_current_dss_features->dss_params[param].max;
}

enum omap_dss_output_id dss_feat_get_supported_outputs(enum omap_channel channel)
{
	return omap_current_dss_features->supported_outputs[channel];
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
