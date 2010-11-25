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
#include <linux/types.h>
#include <linux/err.h>
#include <linux/slab.h>

#include <plat/display.h>
#include <plat/cpu.h>

#include "dss_features.h"

/* Defines a generic omap register field */
struct dss_reg_field {
	enum dss_feat_reg_field id;
	u8 start, end;
};

struct omap_dss_features {
	const struct dss_reg_field *reg_fields;
	const int num_reg_fields;

	const u32 has_feature;

	const int num_mgrs;
	const int num_ovls;
	const enum omap_display_type *supported_displays;
	const enum omap_color_mode *supported_color_modes;
};

/* This struct is assigned to one of the below during initialization */
static struct omap_dss_features *omap_current_dss_features;

static const struct dss_reg_field omap2_dss_reg_fields[] = {
	{ FEAT_REG_FIRHINC, 11, 0 },
	{ FEAT_REG_FIRVINC, 27, 16 },
	{ FEAT_REG_FIFOLOWTHRESHOLD, 8, 0 },
	{ FEAT_REG_FIFOHIGHTHRESHOLD, 24, 16 },
	{ FEAT_REG_FIFOSIZE, 8, 0 },
};

static const struct dss_reg_field omap3_dss_reg_fields[] = {
	{ FEAT_REG_FIRHINC, 12, 0 },
	{ FEAT_REG_FIRVINC, 28, 16 },
	{ FEAT_REG_FIFOLOWTHRESHOLD, 11, 0 },
	{ FEAT_REG_FIFOHIGHTHRESHOLD, 27, 16 },
	{ FEAT_REG_FIFOSIZE, 10, 0 },
};

static const enum omap_display_type omap2_dss_supported_displays[] = {
	/* OMAP_DSS_CHANNEL_LCD */
	OMAP_DISPLAY_TYPE_DPI | OMAP_DISPLAY_TYPE_DBI |
	OMAP_DISPLAY_TYPE_SDI | OMAP_DISPLAY_TYPE_DSI,

	/* OMAP_DSS_CHANNEL_DIGIT */
	OMAP_DISPLAY_TYPE_VENC,
};

static const enum omap_display_type omap3_dss_supported_displays[] = {
	/* OMAP_DSS_CHANNEL_LCD */
	OMAP_DISPLAY_TYPE_DPI | OMAP_DISPLAY_TYPE_DBI |
	OMAP_DISPLAY_TYPE_SDI | OMAP_DISPLAY_TYPE_DSI,

	/* OMAP_DSS_CHANNEL_DIGIT */
	OMAP_DISPLAY_TYPE_VENC,
};

static const enum omap_color_mode omap2_dss_supported_color_modes[] = {
	/* OMAP_DSS_GFX */
	OMAP_DSS_COLOR_CLUT1 | OMAP_DSS_COLOR_CLUT2 |
	OMAP_DSS_COLOR_CLUT4 | OMAP_DSS_COLOR_CLUT8 |
	OMAP_DSS_COLOR_RGB12U | OMAP_DSS_COLOR_RGB16 |
	OMAP_DSS_COLOR_RGB24U | OMAP_DSS_COLOR_RGB24P,

	/* OMAP_DSS_VIDEO1 */
	OMAP_DSS_COLOR_RGB16 | OMAP_DSS_COLOR_RGB24U |
	OMAP_DSS_COLOR_RGB24P | OMAP_DSS_COLOR_YUV2 |
	OMAP_DSS_COLOR_UYVY,

	/* OMAP_DSS_VIDEO2 */
	OMAP_DSS_COLOR_RGB16 | OMAP_DSS_COLOR_RGB24U |
	OMAP_DSS_COLOR_RGB24P | OMAP_DSS_COLOR_YUV2 |
	OMAP_DSS_COLOR_UYVY,
};

static const enum omap_color_mode omap3_dss_supported_color_modes[] = {
	/* OMAP_DSS_GFX */
	OMAP_DSS_COLOR_CLUT1 | OMAP_DSS_COLOR_CLUT2 |
	OMAP_DSS_COLOR_CLUT4 | OMAP_DSS_COLOR_CLUT8 |
	OMAP_DSS_COLOR_RGB12U | OMAP_DSS_COLOR_ARGB16 |
	OMAP_DSS_COLOR_RGB16 | OMAP_DSS_COLOR_RGB24U |
	OMAP_DSS_COLOR_RGB24P | OMAP_DSS_COLOR_ARGB32 |
	OMAP_DSS_COLOR_RGBA32 | OMAP_DSS_COLOR_RGBX32,

	/* OMAP_DSS_VIDEO1 */
	OMAP_DSS_COLOR_RGB24U | OMAP_DSS_COLOR_RGB24P |
	OMAP_DSS_COLOR_RGB12U | OMAP_DSS_COLOR_RGB16 |
	OMAP_DSS_COLOR_YUV2 | OMAP_DSS_COLOR_UYVY,

	/* OMAP_DSS_VIDEO2 */
	OMAP_DSS_COLOR_RGB12U | OMAP_DSS_COLOR_ARGB16 |
	OMAP_DSS_COLOR_RGB16 | OMAP_DSS_COLOR_RGB24U |
	OMAP_DSS_COLOR_RGB24P | OMAP_DSS_COLOR_YUV2 |
	OMAP_DSS_COLOR_UYVY | OMAP_DSS_COLOR_ARGB32 |
	OMAP_DSS_COLOR_RGBA32 | OMAP_DSS_COLOR_RGBX32,
};

/* OMAP2 DSS Features */
static struct omap_dss_features omap2_dss_features = {
	.reg_fields = omap2_dss_reg_fields,
	.num_reg_fields = ARRAY_SIZE(omap2_dss_reg_fields),

	.num_mgrs = 2,
	.num_ovls = 3,
	.supported_displays = omap2_dss_supported_displays,
	.supported_color_modes = omap2_dss_supported_color_modes,
};

/* OMAP3 DSS Features */
static struct omap_dss_features omap3430_dss_features = {
	.reg_fields = omap3_dss_reg_fields,
	.num_reg_fields = ARRAY_SIZE(omap3_dss_reg_fields),

	.has_feature	= FEAT_GLOBAL_ALPHA,

	.num_mgrs = 2,
	.num_ovls = 3,
	.supported_displays = omap3_dss_supported_displays,
	.supported_color_modes = omap3_dss_supported_color_modes,
};

static struct omap_dss_features omap3630_dss_features = {
	.reg_fields = omap3_dss_reg_fields,
	.num_reg_fields = ARRAY_SIZE(omap3_dss_reg_fields),

	.has_feature    = FEAT_GLOBAL_ALPHA | FEAT_PRE_MULT_ALPHA,

	.num_mgrs = 2,
	.num_ovls = 3,
	.supported_displays = omap3_dss_supported_displays,
	.supported_color_modes = omap3_dss_supported_color_modes,
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

enum omap_display_type dss_feat_get_supported_displays(enum omap_channel channel)
{
	return omap_current_dss_features->supported_displays[channel];
}

enum omap_color_mode dss_feat_get_supported_color_modes(enum omap_plane plane)
{
	return omap_current_dss_features->supported_color_modes[plane];
}

bool dss_feat_color_mode_supported(enum omap_plane plane,
		enum omap_color_mode color_mode)
{
	return omap_current_dss_features->supported_color_modes[plane] &
			color_mode;
}

/* DSS has_feature check */
bool dss_has_feature(enum dss_feat_id id)
{
	return omap_current_dss_features->has_feature & id;
}

void dss_feat_get_reg_field(enum dss_feat_reg_field id, u8 *start, u8 *end)
{
	if (id >= omap_current_dss_features->num_reg_fields)
		BUG();

	*start = omap_current_dss_features->reg_fields[id].start;
	*end = omap_current_dss_features->reg_fields[id].end;
}

void dss_features_init(void)
{
	if (cpu_is_omap24xx())
		omap_current_dss_features = &omap2_dss_features;
	else if (cpu_is_omap3630())
		omap_current_dss_features = &omap3630_dss_features;
	else if (cpu_is_omap34xx())
		omap_current_dss_features = &omap3430_dss_features;
}
